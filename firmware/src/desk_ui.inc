// 微雪三向键：向上 GPIO4、中按 GPIO5、向下 GPIO6。低电平表示按下。
#include <Preferences.h>
#include <desk_art.inc>
namespace desk {
Preferences prefs;
int page=0, preview=-1, weekday=-1, reading=0, stage=0, dayKey=0, shown=-1;
bool hint=false, dirty=true, birthdayToday=false;
uint32_t remaining=360000, started=0;
uint8_t region[40*104];
uint32_t left(){uint32_t used=millis()-started;return stage==1?(used<remaining?remaining-used:0):remaining;}
void saveStep(){if(dayKey){prefs.putInt("day",dayKey);prefs.putInt("step",stage>=3?stage:0);}}
// 联网校准日期后选择当天课表，并恢复当天已确认的睡前步骤。
void calendar(){
 time_t now=time(nullptr);if(now<1700000000)return;
 tm d;localtime_r(&now,&d);int k=(1900+d.tm_year)*1000+d.tm_yday;
 if(k==dayKey)return;
 bool first=dayKey==0;dayKey=k;weekday=d.tm_wday;reading=d.tm_yday%7;preview=-1;hint=false;
 birthdayToday=d.tm_mon==10 && d.tm_mday==21;
 if(first && prefs.getInt("day",0)==k)stage=prefs.getInt("step",0);
 if(!first){stage=0;remaining=360000;}
 if(birthdayToday && first)page=3;
 dirty=true;
}
// 整页切换时合成数字；倒计时期间只重绘数字占用的矩形。
void clockDraw(int sec,bool full){
 int codes[]={sec/600,sec/60%10,10,sec%60/10,sec%10};
 for(int y=0;y<104;y++)for(int i=0;i<5;i++)memcpy(region+y*40+i*8,digits[codes[i]]+y*8,8);
 if(full){for(int y=0;y<104;y++)memcpy(imgBuf+(204+y)*100+30,region+y*40,40);}
 else epdPartialDisplay(region,240,204,560,308);
 shown=sec;
}
void draw(){
 int sec=(left()+999)/1000;
 if(dirty){
 int d=preview<0?weekday:preview;
 const uint8_t *art=page==0?(d<0?nosync:schoolArt[d]):page==1?readArt[reading*2+hint]:page==2?bedArt[stage]:birthday;
 memcpy(imgBuf,art,48000);if(page==2 && stage<3)clockDraw(sec,true);
 epdDisplayFast(imgBuf);dirty=false;
 }else if(page==2 && stage==1 && sec!=shown)clockDraw(sec,false);
}
// 两侧键循环翻页，中键根据当前页面执行查看、暂停或确认。
void press(int key){
 if(key!=5){int count=birthdayToday?4:3;page=(page+count+(key==4?-1:1))%count;preview=-1;}
 else if(page==0)preview=preview<0?(weekday<0?1:(weekday+1)%7):(preview+1)%7;
 else if(page==1)hint=!hint;
 else if(page==2){
 if(stage==0||stage==2){started=millis();stage=1;}
 else if(stage==1){remaining=left();stage=2;}
 else if(stage<6){++stage;saveStep();}
 }
 dirty=true;
}
// 屏幕刷新会短暂阻塞；独立采集按键，避免漏掉刷新期间的短按。
QueueHandle_t keyQueue;
void collectKeys(void*){
 for(;;){
 static int sample=-1,stable=-1;static uint32_t changed=0;int key=-1;
 for(int p=4;p<=6;p++)if(digitalRead(p)==LOW){key=p;break;}
 if(key!=sample){sample=key;changed=millis();}
 if(sample!=stable && millis()-changed>=30){stable=sample;if(stable>=0)xQueueSend(keyQueue,&stable,0);}
 vTaskDelay(pdMS_TO_TICKS(10));
 }
}
void begin(){
 for(int p=4;p<=6;p++)pinMode(p,INPUT_PULLUP);
 keyQueue=xQueueCreate(12,sizeof(int));
 configASSERT(keyQueue);
 xTaskCreate(collectKeys,"deskKeys",2048,nullptr,2,nullptr);
 prefs.begin("desk",false);WiFi.mode(WIFI_STA);WiFi.begin(cfgSSID.c_str(),cfgPass.c_str());
 configTime(8*3600,0,"ntp.aliyun.com","pool.ntp.org");
}
void poll(){
 calendar();if(stage==1 && left()==0){stage=3;page=2;saveStep();dirty=true;}
 int key;
 if(xQueueReceive(keyQueue,&key,0)==pdTRUE)press(key);
 draw();delay(5);
}
}
