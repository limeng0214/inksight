#include <Preferences.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <Adafruit_GFX.h>
#include <desk_art.inc>
#include "desk_parent.inc"

namespace desk {
enum Page { Schedule, Clock, Reading, Bed, Memo, PageCount };
Page page=Schedule;
Preferences prefs;
WebServer parent(80);
QueueHandle_t keys;
bool dirty=true, note=false, hint=false, birthdayCard=false;
int offset=1, stage=0, lastSecond=-1;
uint32_t remaining=360000, started=0;
tm localDay{};
String dateKey, lastClock, lastAddress;
constexpr int memoWidth=736, memoHeight=288, memoBytes=memoWidth*memoHeight/8;
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
 if(!first||stage!=1){remaining=360000;stage=prefs.getString("date5","")==today?prefs.getUChar("stage5",0):0;}
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
 clockArea.fillScreen(1);ascii(clockArea,text.substring(0,5).c_str(),8,0,18);ascii(clockArea,text.substring(5).c_str(),3,245,52);
 if(full)blit(clockArea.getBuffer(),40,24,180,104);
 else {
  int first=0;while(first<5&&text[first]==lastClock[first])first++;
  int end=5;while(end>first&&text[end-1]==lastClock[end-1])end--;
  int x=first*48,stop=end*48;if(text.substring(5)!=lastClock.substring(5))stop=288;
  int bytes=(stop-x)/8;
  if(bytes>0){for(int y=0;y<104;y++)memcpy(timerRegion+y*bytes,clockArea.getBuffer()+40*y+x/8,bytes);epdPartialDisplay(timerRegion,24+x,180,24+stop,284);}
 }
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
 if(full)blit(timerRegion,stride,x,192,104);
 else epdPartialDisplay(timerRegion,x,192,32+end*64,296);
 lastSecond=sec;
}
// 一个文件保存原文和图像，临时文件写完再改名，避免更新中途断电造成半条留言。
String memoPath(const String &date){return "/desk-"+date;}
bool validDate(const String &s){
 if(s.length()!=10||s[4]!='-'||s[7]!='-')return false;
 for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isDigit(s[i]))return false;
 return true;
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
  free(buf);if(ok&&page==Memo&&d==dateKey)dirty=true;parent.send(ok?200:500);
 });parent.begin();
}
void draw(){
 int sec=stage>=3?0:(left()+999)/1000;
 if(!dirty){if(!birthdayCard&&page==Clock)drawClock(false);if(!birthdayCard&&page==Bed&&stage==1&&sec!=lastSecond)drawTimer(sec,false);return;}
 const uint8_t *art=nullptr;
 if(birthdayCard)art=birthday;
 else switch(page){
 case Schedule:{int w=(localDay.tm_wday+offset)%7;art=dateKey.isEmpty()?nosync:(offset==1?tomorrowArt[w]:schoolArt[w]);break;}
 case Clock:art=poemArt[(localDay.tm_yday%2)*2+note];break;
 case Reading:art=readArt[(localDay.tm_yday%7)*2+hint];break;
 case Bed:art=bedArt[stage];break;
 case Memo:art=LittleFS.exists(memoPath(dateKey))?memoPage:memoEmpty;break;
 default:return;
 }
 memcpy(imgBuf,art,48000);
 if(!birthdayCard){
  if(page==Clock){char header[72]="Waiting for time";if(dateKey.length())strftime(header,sizeof(header),"%A, %B %d, %Y",&localDay);dateLabel(header,32,30,3);drawClock(true);}
  if(page==Bed)drawTimer(sec,true);
  if(page==Schedule&&dateKey.length()){tm d=localDay;d.tm_mday+=offset;mktime(&d);dateLabel(dateOf(d),568,41,3);}
  if(page==Memo){File f=LittleFS.open(memoPath(dateKey),"r");if(f&&f.size()>=memoBytes)for(int y=0;y<memoHeight;y++)f.read(imgBuf+(109+y)*100+4,92);
   lastAddress=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("offline");dateLabel(lastAddress,480,41,3);}
 }
 // 首屏全刷建立两份相同底图；一轮计时结束时全刷一次，清理累积残影。
 static bool haveBase=false;
 static int previousStage=-1;
 if(!haveBase || (page==Bed && stage==3 && previousStage!=3)){
  epdDisplay(imgBuf);
  haveBase=true;
 }else epdDisplayFast(imgBuf);
 previousStage=stage;
 dirty=false;
}
void press(int key){
 if(birthdayCard){birthdayCard=false;prefs.putString("birth5",dateKey);}
 else if(key!=5){page=Page((page+PageCount+(key==4?-1:1))%PageCount);offset=1;note=hint=false;}
 else switch(page){
 case Schedule:offset=offset%7+1;break;
 case Clock:note=!note;break;
 case Reading:hint=!hint;break;
 case Bed:if(stage==0||stage==2){started=millis();stage=1;}else if(stage==1){remaining=left();stage=remaining?2:3;}else if(stage<7)++stage;saveStep();break;
 default:return;
 }
 dirty=true;
}
// 刷屏期间独立采集按键，消抖后交给主循环。
void collectKeys(void*){
 int sample=-1,stable=-1;uint32_t changed=0;
 for(;;){int k=-1;for(int p=4;p<=6;p++)if(digitalRead(p)==LOW){k=p;break;}
 if(k!=sample){sample=k;changed=millis();}
 if(sample!=stable&&millis()-changed>=30){stable=sample;if(k>=0)xQueueSend(keys,&k,0);}
 vTaskDelay(pdMS_TO_TICKS(10));}
}
void begin(){
 for(int p=4;p<=6;p++)pinMode(p,INPUT_PULLUP);
 keys=xQueueCreate(12,sizeof(int));configASSERT(keys);
 xTaskCreate(collectKeys,"deskKeys",2048,nullptr,2,nullptr);
 prefs.begin("desk",false);WiFi.mode(WIFI_STA);WiFi.begin(cfgSSID.c_str(),cfgPass.c_str());
 configTime(8*3600,0,"ntp.aliyun.com","pool.ntp.org");setupParent();
}
void poll(){
 calendar();if(stage==1&&!left()){birthdayCard=false;stage=3;page=Bed;saveStep();dirty=true;}
 parent.handleClient();
 if(page==Memo&&(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("offline"))!=lastAddress)dirty=true;
 int key;if(xQueueReceive(keys,&key,0)==pdTRUE)press(key);
 draw();delay(5);
}
}
