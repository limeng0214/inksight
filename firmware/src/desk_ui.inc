#include <Preferences.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <Adafruit_GFX.h>
#include <desk_art.inc>
#include "desk_chat_art.inc"
#include "desk_parent.inc"
#include "certs.h"
#include <WiFiClientSecure.h>
#include <desk_custom.inc>
#include "desk_sound.inc"
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <driver/gpio.h>

namespace desk {
enum Page { Schedule, Clock, Reading, Bed, Memo, Chat, PageCount };
Page page=Clock;
Preferences prefs;
WebServer parent(80);
QueueHandle_t keys;
bool dirty=true, note=false, hint=false, birthdayCard=false;
int offset=1, stage=0, lastSecond=-1;
uint32_t remaining=360000, started=0;
tm localDay{};
uint32_t touched=0, rotated=0; int footMinute=-1; bool praise=false;
bool ringing=false, pendingAlarm=false;
int paintedPhase=-1;
bool forceClean=false, memoChanged=false, retryMemo=true;
int batteryPercent=-1;
uint32_t nextNetwork=0;
bool networkEnabled=true;
float roomTemp=NAN;
char environmentText[64]="BAT --   -- C   --% RH";
String dateKey, lastClock, lastAddress, cloudState="Waiting";
constexpr int memoWidth=736, memoHeight=288, memoBytes=memoWidth*memoHeight/8;
uint8_t *previousFrame=nullptr;
uint8_t timerRegion[40*104];
GFXcanvas1 clockArea(320,104), dateArea(740,46);

String dateOf(const tm &value){
 char out[16];strftime(out,sizeof(out),"%Y-%m-%d",&value);return String(out);
}
uint32_t left(){
 if(stage!=1)return remaining;
 uint32_t elapsed=millis()-started;
 return elapsed>=remaining?0:remaining-elapsed;
}
void saveStep(){
 if(dateKey.length()){prefs.putString("date5",dateKey);prefs.putUChar("stage5",stage>=3?stage:0);}
}
// 每天的清单独立保存；跨日时按北京时间计算课表和诗文。
void calendar(){
 time_t now=time(nullptr);if(now<1700000000)return;
 tm d;localtime_r(&now,&d);localDay=d;String today=dateOf(d);
 if(today==dateKey)return;
 bool first=dateKey.isEmpty();dateKey=today;offset=1;hint=note=false;
 if(!first){forceClean=true;retryMemo=true;}
 if(stage!=1){remaining=360000;stage=prefs.getString("date5","")==today?prefs.getUChar("stage5",0):0;}
 if(stage>7)stage=0;
 birthdayCard=d.tm_mon==10&&d.tm_mday==21&&prefs.getString("birth5","")!=today;
 dirty=true;
}
void blit(const uint8_t *pixels,int stride,int x,int y,int rows){
 for(int i=0;i<rows;i++)memcpy(imgBuf+(y+i)*100+x/8,pixels+i*stride,stride);
}
void ascii(GFXcanvas1 &surface,const char *value,int size,int x,int y){
 surface.setTextWrap(false);surface.setTextColor(0);surface.setTextSize(size);surface.setCursor(x,y);surface.print(value);
}
void dateLabel(String text,int x,int y,int size){
 dateArea.fillScreen(1);ascii(dateArea,text.c_str(),size,0,0);
 int bytes=min((int)((text.length()*6*size+7)/8),(800-x)/8);
 for(int i=0;i<8*size;i++)memcpy(imgBuf+(y+i)*100+x/8,dateArea.getBuffer()+i*93,bytes);
}
void drawClock(bool full){
 char value[12]="--:--";if(dateKey.length())snprintf(value,sizeof(value),"%2d:%02d%s",localDay.tm_hour%12?localDay.tm_hour%12:12,localDay.tm_min,localDay.tm_hour<12?"AM":"PM");
 String text=value;if(!full&&text==lastClock)return;
 // Fixed 56-pixel cells keep the colon and minutes on a single baseline.
 clockArea.fillScreen(1);
 for(int i=0;i<5;i++){char c=text[i];if(c=='-')continue;
  int glyph=c==':'?10:c==' '?11:c-'0';
  for(int y=0;y<96;y++)memcpy(clockArea.getBuffer()+40*y+i*7,clockGlyphs[glyph]+y*7,7);
 }
 ascii(clockArea,text.substring(5).c_str(),3,280,55);
 blit(clockArea.getBuffer(),40,24,180,104);
 if(!full)epdPartialDisplay(clockArea.getBuffer(),24,180,344,284);
 lastClock=text;
}
void drawTimer(int sec,bool full){
 int codes[]={sec/600,sec/60%10,10,sec%60/10,sec%10};
 // 每个字形 64 × 104；通常只传秒个位，跨分钟时合并变化的数字。
 int first=0, end=5;
 if(!full && lastSecond>=0){
  int old[]={lastSecond/600,lastSecond/60%10,10,lastSecond%60/10,lastSecond%10};
  while(first<end && codes[first]==old[first])++first;
  while(end>first && codes[end-1]==old[end-1])--end;
  if(first==end)return;
 }
 int stride=(end-first)*8;
 for(int y=0;y<104;y++)for(int i=first;i<end;i++){
  memcpy(timerRegion+y*stride+(i-first)*8,digits[codes[i]]+y*8,8);
 }
 int x=32+first*64;
 // Commit the whole changed span in one display cycle, including on 06:00 -> 05:59.
 blit(timerRegion,stride,x,192,104);
 if(!full)epdPartialDisplay(timerRegion,x,192,32+end*64,296);
 lastSecond=sec;
}
// 一个文件保存原文和图像，临时文件写完再改名，避免更新中途断电造成半条留言。
String memoPath(const String &date){return "/desk-"+date;}
bool validDate(const String &s){
 if(s.length()!=10||s[4]!='-'||s[7]!='-')return false;
 for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isDigit(s[i]))return false;
 return true;
}
// A failed download leaves the last complete local copy intact.
bool fetchCloudNote(){
 // 家庭版直接读取本机留言，不要求家长绑定账号。
 cloudState="Token failed";
 if(!ensureDeviceToken())return false;
 WiFiClientSecure tls;tls.setCACert(ROOT_CA);HTTPClient http;
 http.setConnectTimeout(1500);http.setTimeout(1500);tls.setHandshakeTimeout(2);
 cloudState="Connect failed";
 if(!http.begin(tls,cfgServer+"/api/desk-notes/"+WiFi.macAddress()+"/"+dateKey+"?device=true"))return false;
 http.addHeader("X-Device-Token",cfgDeviceToken);
 int code=http.GET(),n=http.getSize();
 Serial.printf("[desk-note] %s HTTP %d, bytes %d\n",dateKey.c_str(),code,n);
 if(code<0){char msg[160]={};int err=tls.lastError(msg,sizeof(msg));Serial.printf("[desk-note] TLS %d %s\n",err,msg);}
 cloudState=code<0?String("Network ")+code:String("HTTP ")+code;
 if(code==200)cloudState="Format error";
 if(code==204){cloudState="No cloud note";http.end();return true;}
 if(code==401)clearDeviceToken();
 if(code!=200||n<memoBytes||n>memoBytes+2000){http.end();return false;}
 uint8_t *buf=(uint8_t*)ps_malloc(n);if(!buf)buf=(uint8_t*)malloc(n);
 if(!buf){cloudState="Memory low";http.end();return false;}
 // TLS can split the bitmap across records. Wait for each available block.
 int received=0;uint32_t progress=millis(),begin=progress;
 while(received<n && millis()-progress<1500 && millis()-begin<3000){
  if(uxQueueMessagesWaiting(keys)){free(buf);http.end();retryMemo=true;return false;}
  int available=tls.available();
  if(available>0){int got=tls.read(buf+received,min(available,n-received));if(got>0){received+=got;progress=millis();continue;}}
  if(!http.connected())break;
  delay(2);
 }
 cloudState="Download cut";
 if(received==n){
  String path=memoPath(dateKey);File old=LittleFS.open(path,"r");
  bool same=old&&old.size()==(size_t)n;
  uint8_t block[256];for(int off=0;same&&off<n;off+=sizeof(block)){int k=min((int)sizeof(block),n-off);
   same=old.read(block,k)==(size_t)k&&!memcmp(buf+off,block,k);}
  old.close();
  cloudState="Cloud OK";
  if(n==memoBytes){if(LittleFS.exists(path)&&LittleFS.remove(path)){memoChanged=true;if(page==Memo)dirty=true;}}
  else if(!same){File f=LittleFS.open("/cloud.tmp","w");bool ok=f&&f.write(buf,n)==(size_t)n;f.close();
   if(ok&&LittleFS.rename("/cloud.tmp",path)){memoChanged=true;if(page==Memo)dirty=true;}else cloudState="Save failed";}
 }
 Serial.printf("[desk-note] received %d/%d, %s\n",received,n,cloudState.c_str());
 free(buf);http.end();return cloudState=="Cloud OK";
}

// Periods are measured from completion, so a slow or failed fetch cannot cause a tight loop.
void syncCloudNote(){
 static uint32_t attempted=0;
 static unsigned failures=0;
 static String lastSuccess="--:--";
 static bool wasOnMemo=false;
 bool onMemo=page==Memo&&!ringing&&!birthdayCard;
 if(onMemo&&!wasOnMemo)retryMemo=true;
 wasOnMemo=onMemo;
 if(uxQueueMessagesWaiting(keys)||stage==1||deskSound::busy||ringing||dateKey.isEmpty()||WiFi.status()!=WL_CONNECTED||
    cfgServer!="https://ink.h-iot.com"||(batteryPercent>=0&&batteryPercent<5))return;
 uint32_t interval=onMemo?300000UL:900000UL;
 if(failures){const uint32_t delays[]={30000,120000,300000,900000};interval=delays[min(failures-1,3u)];}
 if(batteryPercent>=0&&batteryPercent<15)interval=max(interval,uint32_t(1800000));
 if(attempted&&millis()-attempted<interval&&(!retryMemo||failures))return;
 retryMemo=false;
 if(fetchCloudNote()){
  failures=0;char stamp[8];strftime(stamp,sizeof(stamp),"%H:%M",&localDay);lastSuccess=stamp;
 }else{
  failures=min(failures+1,6u);
  cloudState=failures>=6?"Last sync "+lastSuccess:"Retry later";
 }
 attempted=millis();
 uint32_t nextInterval=onMemo?300000UL:900000UL;
 if(failures){const uint32_t waits[]={30000,120000,300000,900000};nextInterval=waits[min(failures-1,3u)];}
 if(batteryPercent>=0&&batteryPercent<15)nextInterval=max(nextInterval,uint32_t(1800000));
 nextNetwork=attempted+nextInterval;
}
void setupParent(){
 parent.on("/",HTTP_GET,[]{parent.send_P(200,"text/html;charset=utf-8",parentHTML);});
 parent.on("/notes",HTTP_GET,[]{
  String d=parent.arg("date");if(!validDate(d)){parent.send(400);return;}
  File f=LittleFS.open(memoPath(d),"r");String text;
  if(f&&f.size()>=memoBytes){f.seek(memoBytes);text=f.readString();}parent.send(200,"text/plain;charset=utf-8",text);
 });
 parent.on("/notes",HTTP_POST,[]{
  String d=parent.arg("date"),text=parent.arg("text"),encoded=parent.arg("pixels");
  if(!validDate(d)||text.length()>2000||encoded.length()>4*((2*memoBytes+2)/3)||encoded.isEmpty()){parent.send(400);return;}
  auto *buf=(uint8_t*)malloc(memoBytes);if(!buf){parent.send(503);return;}
  size_t packedSize=encoded.length()/4*3;
  auto *packed=(uint8_t*)malloc(packedSize);if(!packed){free(buf);parent.send(503);return;}
  size_t count=0,n=0;int err=mbedtls_base64_decode(packed,packedSize,&count,(const uint8_t*)encoded.c_str(),encoded.length());
  if(count%2)err=-1;
  for(size_t i=0;!err&&i<count;i+=2){size_t run=packed[i];if(!run||run>memoBytes-n){err=-1;break;}memset(buf+n,packed[i+1],run);n+=run;}
  free(packed);
  bool ok=false;
  if(!err&&n==memoBytes){
   String path=memoPath(d);
   if(text.isEmpty())ok=!LittleFS.exists(path)||LittleFS.remove(path);
   else{File f=LittleFS.open("/desk.tmp","w");ok=f&&f.write(buf,n)==n&&f.print(text)==text.length();f.close();if(ok)ok=LittleFS.rename("/desk.tmp",path);}
  }
  free(buf);if(ok&&page==Memo&&d==dateKey){memoChanged=true;dirty=true;}parent.send(ok?200:500);
 });
 parent.on("/gateway",HTTP_POST,[]{
  IPAddress address;
  if(!address.fromString(parent.arg("ip")) || !(address[0]==10 || (address[0]==192&&address[1]==168) || (address[0]==172&&address[1]>=16&&address[1]<=31))){parent.send(400);return;}
  if(deskSound::busy){parent.send(409);return;}
  deskSound::gateway="http://"+address.toString()+":8765";prefs.putString("gate",deskSound::gateway);
  dirty=true;parent.send(200,"text/plain;charset=utf-8","已保存，返回小智页即可对话。");
 });parent.begin();
}
int reg8(uint8_t reg){
 Wire.beginTransmission(0x34);Wire.write(reg);if(Wire.endTransmission(false)||Wire.requestFrom(uint8_t(0x34),uint8_t(1))!=1)return -1;return Wire.read();
}
bool shtCommand(uint16_t command){Wire.beginTransmission(0x70);Wire.write(command>>8);Wire.write(command&255);return Wire.endTransmission()==0;}
bool checked(const uint8_t *p){uint8_t c=255;for(int i=0;i<2;i++){c^=p[i];for(int j=0;j<8;j++)c=c&128?(c<<1)^0x31:c<<1;}return c==p[2];}
void sampleEnvironment(){
 char info[64],battery[16]="BAT --",ambient[24]="-- C   --% RH";
 batteryPercent=-1;int status=reg8(0);if(status>=0){if(status&8){int pct=reg8(0xa4);if(pct>=0&&pct<=100){batteryPercent=pct;snprintf(battery,sizeof(battery),"BAT %d%%",pct);}}else strcpy(battery,"NO BAT");}
 if(shtCommand(0x3517)){delay(2);if(shtCommand(0x7866)){delay(14);if(Wire.requestFrom(uint8_t(0x70),uint8_t(6))==6){uint8_t v[6];Wire.readBytes(v,6);if(checked(v)&&checked(v+3)){roomTemp=-45+175.*((v[0]<<8)|v[1])/65536;snprintf(ambient,sizeof(ambient),"%.1f C   %.0f%% RH",-45+175.*((v[0]<<8)|v[1])/65536,100.*((v[3]<<8)|v[4])/65536);}}}shtCommand(0xb098);}
 snprintf(environmentText,sizeof(environmentText),"%s   %s",battery,ambient);
}
void footerTime(bool full){
 char t[8]="--:--";if(dateKey.length())strftime(t,sizeof(t),"%H:%M",&localDay);
 dateArea.fillScreen(1);ascii(dateArea,t,2,0,0);
 uint8_t patch[8*16];for(int y=0;y<16;y++)memcpy(patch+8*y,dateArea.getBuffer()+93*y,8);
 blit(patch,8,720,455,16);if(!full) epdPartialDisplay(patch,720,455,784,471);footMinute=localDay.tm_min;
}
bool panelSleeping=false;
uint32_t lastClean=0;
void draw(){
 static int lastMinute=-1, viewBefore=-1, previousStage=-1;
 static unsigned partials=0, fasts=0;
 static uint32_t sampled=0;
 const bool environmentDue=!sampled||millis()-sampled>=600000;
 if(environmentDue){sampleEnvironment();sampled=millis();}
 const int view=ringing?PageCount+1:birthdayCard?PageCount:int(page);
 const bool timer=view==Bed&&stage==1;
 int sec=stage>=3?0:(left()+999)/1000;
 if(timer&&sec>30)sec=min(360,2*((sec+1)/2));
 if(panelSleeping&&!dirty&&!forceClean&&!ringing)return;
 bool full=forceClean||panelSleeping||viewBefore<0||millis()-lastClean>=86400000UL;
 bool fast=view!=viewBefore||(dirty&&view!=Chat&&view!=Memo)||(view==Memo&&memoChanged);
 bool minuteDue=localDay.tm_min!=lastMinute;
 if(timer&&!dirty&&lastSecond==sec)return;
 if(timer&&!dirty&&!(roomTemp<10)){drawTimer(sec,false);return;}
 if(!timer&&!dirty&&!minuteDue&&!full&&!(environmentDue&&view==Clock))return;
 if(timer&&previousStage!=1)fast=true;
 if(stage==3&&previousStage==1)full=true;
 const uint8_t *art=nullptr;
 if(ringing)art=wake;
 else if(birthdayCard)art=birthday;
 else switch(page){
 case Schedule:{int w=(localDay.tm_wday+offset)%7;art=dateKey.isEmpty()?nosync:(offset==1?tomorrowArt[w]:schoolArt[w]);break;}
 case Clock:art=poemArt[(localDay.tm_yday%2)*2+note];break;
 case Reading:art=readArt[(localDay.tm_yday%7)*2+hint];break;
 case Bed:art=bedArt[stage];break;
 case Chat:paintedPhase=deskSound::gateway.isEmpty()?5:deskSound::phase.load();art=chatArt[0];break;
 case Memo:art=LittleFS.exists(memoPath(dateKey))?memoPage:memoEmpty;break;
 default:return;
 }
 if(previousFrame)memcpy(previousFrame,imgBuf,48000);
 memcpy(imgBuf,art,48000);
 if(!birthdayCard&&!ringing){
  if(page==Clock){char header[72]="Waiting for time";if(dateKey.length())strftime(header,sizeof(header),"%A, %B %d, %Y",&localDay);blit(deskLogo,28,24,21,40);dateLabel(header,256,30,3);dateLabel(environmentText,24,365,2);drawClock(true);dateLabel("6:45 AM  Mon-Fri",24,312,2);}
  if(page==Chat){for(int y=96;y<395;y++)memset(imgBuf+y*100,255,100);blit(chatBubble,88,40,135,184);blit(chatStatus[paintedPhase],50,248,198,56);}
  if(page==Bed)drawTimer(sec,true);
  if(page==Schedule&&dateKey.length()){tm d=localDay;d.tm_mday+=offset;mktime(&d);dateLabel(dateOf(d),568,41,3);}
  if(page==Memo){File f=LittleFS.open(memoPath(dateKey),"r");if(f&&f.size()>=memoBytes)for(int y=0;y<memoHeight;y++)f.read(imgBuf+(109+y)*100+4,92);
   lastAddress=WiFi.status()==WL_CONNECTED?cloudState:String("Offline");dateLabel("ink.h-iot.com/jiayi",480,41,2);
   for(int y=425;y<455;y++)memset(imgBuf+y*100,255,100);dateLabel(lastAddress,32,432,2);}
 }
 // The large memo rectangle is intentionally exempt from ordinary page-change quotas.
 if(view==Memo&&viewBefore==Memo&&memoChanged&&!full&&previousFrame&&!(roomTemp<10)){
  const int first=109,last=first+memoHeight;
  // The screen outside the message window still contains the previous frame.
  memcpy(imgBuf,previousFrame,first*100);
  memcpy(imgBuf+last*100,previousFrame+last*100,(480-last)*100);
  for(int y=first;y<last;y++)memcpy(previousFrame+(y-first)*92,imgBuf+y*100+4,92);
  epdPartialDisplay(previousFrame,32,first,32+memoWidth,last);
  memoChanged=false;dirty=false;lastMinute=-1;
  // Leave the footer for the next update; the first patch contains only the note itself.
  lastAddress="";return;
 }
 // On the clock, the small footer shares the ten-minute environment update.
 static int clockFooter=-1;
 if(view==Clock){
  if(viewBefore!=Clock||environmentDue||clockFooter<0)clockFooter=localDay.tm_hour*60+localDay.tm_min;
  char t[8];snprintf(t,sizeof(t),"%02d:%02d",clockFooter/60,clockFooter%60);dateLabel(t,720,455,2);
 }else if(view!=Bed||stage>=3)footerTime(true);
 int x0=100,x1=0,y0=480,y1=0,changed=0;
 if(previousFrame)for(int y=0;y<480;y++)for(int x=0;x<100;x++){
  int diff=previousFrame[y*100+x]^imgBuf[y*100+x];if(!diff)continue;
  changed+=__builtin_popcount(diff);x0=min(x0,x);x1=max(x1,x+1);y0=min(y0,y);y1=max(y1,y+1);
 }
 if(!previousFrame||changed>800*480*4/10)full=true;
 if(changed||fast||full){
  if(roomTemp<5)full=true;
  else if(roomTemp<10)fast=true;
  int stride=x1-x0,rows=y1-y0;
  if(stride>80||rows>240||(!timer&&partials>=23))fast=true;
  // 六次快刷后，第七次整页更新全刷并清零。
  if(fast&&fasts>=6)full=true;
  if(!fast&&!full&&changed){
   // Reuse the old frame as scratch only after the bounding box is known.
   for(int y=0;y<rows;y++)memcpy(previousFrame+y*stride,imgBuf+(y0+y)*100+x0,stride);
   epdPartialDisplay(previousFrame,x0*8,y0,x1*8,y1);
   ++partials;
  }else if(full){epdDisplay(imgBuf);partials=fasts=0;lastClean=millis();}
  else{epdDisplayFast(imgBuf);partials=0;++fasts;}
 }
 panelSleeping=false;forceClean=false;memoChanged=false;
 viewBefore=view;previousStage=stage;lastMinute=localDay.tm_min;
 dirty=false;
}
void press(int key){
 if(panelSleeping)forceClean=true;
 touched=rotated=millis();
 if(key==0){deskSound::cancel=true;pendingAlarm=ringing=false;birthdayCard=false;page=Clock;dirty=true;return;}
 if(ringing){deskSound::cancel=true;pendingAlarm=false;ringing=false;dirty=true;return;}
 if(birthdayCard){birthdayCard=false;prefs.putString("birth5",dateKey);}
 else if(key!=5){if(page==Chat)deskSound::cancel=true;page=Page((page+PageCount+(key==4?-1:1))%PageCount);offset=1;note=hint=false;}
 else switch(page){
 case Memo:retryMemo=true;break;
 case Schedule:offset=offset%7+1;break;
 case Clock:note=!note;break;
 case Reading:hint=!hint;break;
 case Chat:if(deskSound::phase==1)deskSound::finish=true;else if(deskSound::busy)deskSound::cancel=true;else if(deskSound::gateway.length())deskSound::start(false);break;
 case Bed:if(stage==0||stage==2){started=millis();stage=1;}else if(stage==1){remaining=left();stage=remaining?2:3;}else if(stage<7)++stage;saveStep();break;
 default:return;
 }
 dirty=true;
}
// 刷屏期间独立采集按键，消抖后交给主循环。
void collectKeys(void*){
 int sample=-1,stable=-1;uint32_t changed=0;
 for(;;){int k=-1;for(int p=4;p<=6;p++)if(digitalRead(p)==LOW){k=p;break;}
 if(digitalRead(0)==LOW)k=0;
 if(k!=sample){sample=k;changed=millis();}
 if(sample!=stable&&millis()-changed>=30){stable=sample;if(k>=0)xQueueSend(keys,&k,0);}
 vTaskDelay(pdMS_TO_TICKS(10));}
}
void begin(){
 previousFrame=(uint8_t*)ps_malloc(48000);
 if(!previousFrame)previousFrame=(uint8_t*)malloc(48000);
 for(int p=4;p<=6;p++)pinMode(p,INPUT_PULLUP);
 pinMode(0,INPUT_PULLUP);Wire.begin(41,42);touched=rotated=millis();
 for(int p:{0,4,5,6})gpio_wakeup_enable((gpio_num_t)p,GPIO_INTR_LOW_LEVEL);esp_sleep_enable_gpio_wakeup();
 keys=xQueueCreate(12,sizeof(int));configASSERT(keys);
 xTaskCreate(collectKeys,"deskKeys",2048,nullptr,2,nullptr);
 prefs.begin("desk",false);deskSound::gateway=prefs.getString("gate","");WiFi.mode(WIFI_STA);WiFi.begin(cfgSSID.c_str(),cfgPass.c_str());
 configTime(8*3600,0,"ntp.aliyun.com","pool.ntp.org");setupParent();
}
void poll(){
 calendar();if(stage==1&&!left()){birthdayCard=false;stage=3;forceClean=true;retryMemo=true;praise=true;rotated=touched=millis();page=Bed;saveStep();dirty=true;}
 if(dateKey.length()&&localDay.tm_wday>=1&&localDay.tm_wday<=5&&localDay.tm_hour==6&&localDay.tm_min==45&&prefs.getString("alarmDay","")!=dateKey){
  prefs.putString("alarmDay",dateKey);deskSound::cancel=true;pendingAlarm=true;ringing=true;birthdayCard=false;page=Clock;dirty=true;
 }
 if(pendingAlarm&&!deskSound::busy){pendingAlarm=false;deskSound::start(true);}
 else if(ringing&&!pendingAlarm&&!deskSound::busy){ringing=false;dirty=true;}
 if(!birthdayCard&&!ringing&&page==Chat&&paintedPhase!=(deskSound::gateway.isEmpty()?5:deskSound::phase.load()))dirty=true;
 if(praise&&!pendingAlarm&&!ringing&&!deskSound::busy){praise=!deskSound::start(2);}
 bool engaged=stage==1||deskSound::busy||ringing||birthdayCard;
 if(engaged)touched=rotated=millis();
 else if(!panelSleeping&&millis()-touched<600000&&millis()-rotated>=300000){uint32_t lastInput=touched;press(6);touched=lastInput;}
 // Apply queued navigation before drawing, avoiding a slow redraw for every old scroll.
 int key;for(int count=0;count<12&&xQueueReceive(keys,&key,0)==pdTRUE;count++)press(key);
 parent.handleClient();
 if(page==Memo&&(WiFi.status()==WL_CONNECTED?cloudState:String("Offline"))!=lastAddress)dirty=true;
 draw();
 if(stage!=1&&millis()-rotated>=150&&!uxQueueMessagesWaiting(keys))syncCloudNote();
 delay(5);
 // The network may remain online on the notes page while the panel sleeps independently.
 if(!panelSleeping && !engaged && page!=Memo && millis()-touched>=600000){epdSleep();panelSleeping=true;}
 // Keep the local editor reachable on its page. Sleep between minute boundaries elsewhere.
 // 已校时后即使家庭网络短暂断开，也按分钟唤醒；预留重连时间。
 static uint32_t wifiStarted=0;
 bool networkReady=!networkEnabled || WiFi.status()==WL_CONNECTED || millis()-wifiStarted>=10000;
 if(stage!=1 && !engaged && !dirty && page!=Memo && dateKey.length() && millis()-touched>=60000 && networkReady && !uxQueueMessagesWaiting(keys)){
  timeval tv;gettimeofday(&tv,nullptr);int64_t us=60000000LL-(tv.tv_sec%60)*1000000LL-tv.tv_usec;
  if(!panelSleeping&&millis()-touched<600000)us=min(us,(int64_t)max(1L,300000L-(long)(millis()-rotated))*1000);

  if(networkEnabled&&esp_wifi_stop()!=ESP_OK)return;
  networkEnabled=false;
  if(esp_sleep_enable_timer_wakeup(us)==ESP_OK)esp_light_sleep_start();
  if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO)touched=millis();
  bool keyWake=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_GPIO;
  time_t now=time(nullptr);tm wakeTime;localtime_r(&now,&wakeTime);
  bool midnight=wakeTime.tm_hour==0&&wakeTime.tm_min==0;
  bool alarm=wakeTime.tm_wday>=1&&wakeTime.tm_wday<=5&&wakeTime.tm_hour==6&&wakeTime.tm_min==45;
  if(keyWake||midnight||alarm||int32_t(millis()-nextNetwork)>=0){
   esp_wifi_start();WiFi.reconnect();networkEnabled=true;wifiStarted=millis();
   if(keyWake||midnight||alarm)retryMemo=true;
  }
  delay(40);
 }
}
}
