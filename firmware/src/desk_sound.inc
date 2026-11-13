// 微雪音频示例的 ES8311、功放和 I2S 接线。
#include <Wire.h>
#include <ESP_I2S.h>
#include <HTTPClient.h>
#include <atomic>
#include "desk_codec/es8311.h"
namespace deskSound {
constexpr int rate=16000;
I2SClass bus;
es8311_handle_t codec=nullptr;
std::atomic<bool> busy(false), cancel(false), finish(false);
std::atomic<int> phase(0); // 0 待机，1 录音，2 等待，3 播放，4 失败。
String gateway;
bool ready(){
 if(codec)return true;
 pinMode(39,OUTPUT);digitalWrite(39,LOW);
 if(!Wire.begin(41,42))return false;
 bus.setPins(14,47,48,21,13);
 if(!bus.begin(I2S_MODE_STD,rate,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO,I2S_STD_SLOT_LEFT))return false;
 auto dev=es8311_create(I2C_NUM_0,ES8311_ADDRRES_0);
 es8311_clock_config_t clk={false,false,true,rate*256,rate};
 if(!dev||es8311_init(dev,&clk,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16)!=ESP_OK||
    es8311_voice_volume_set(dev,55,nullptr)!=ESP_OK||es8311_microphone_config(dev,false)!=ESP_OK){
  if(dev)es8311_delete(dev);bus.end();return false;
 }
 es8311_microphone_gain_set(dev,ES8311_MIC_GAIN_24DB);codec=dev;return true;
}
void bugle(){
 // 本地合成起床号风格短曲，即使 Wi-Fi 暂时中断也能演奏。
 const int notes[]={392,523,659,784,0,659,784,659,523,392,523,659,784,1047};
 const int beats[]={1,1,1,3,1,1,2,1,1,1,1,1,2,4};
 digitalWrite(39,HIGH);
 const uint32_t since=millis();
 auto active=[&](){return !cancel && millis()-since<300000UL;};
 while(active())for(int n=0;n<14&&active();n++){
  int len=rate*beats[n]/5;
  for(int at=0;at<len&&active();){int16_t pcm[160];int count=min(160,len-at);
   for(int k=0;k<count;k++){int t=at+k;float a=2*PI*notes[n]*t/rate;
    float env=min(1.f,min(t/150.f,(len-t)/280.f));pcm[k]=env*(4300*sin(a)+1300*sin(2*a));}
   bus.write((uint8_t*)pcm,count*2);at+=count;
  }
 }
}
void chat(){
 // 页面中按才启用麦克风，再按发送，最长 8 秒。
 const size_t cap=rate*2*8;auto *pcm=(uint8_t*)ps_malloc(cap);if(!pcm){phase=4;return;}
 phase=1;size_t n=0;
 while(!cancel&&!finish&&n<cap){size_t got=bus.readBytes((char*)pcm+n,min(size_t(640),cap-n));if(!got)break;n+=got;}
 if(cancel||n<640){free(pcm);phase=0;return;}
 phase=2;HTTPClient http;WiFiClient client;http.setTimeout(45000);http.setConnectTimeout(2500);
 if(!http.begin(client,gateway+"/turn")){free(pcm);phase=4;return;}
 http.addHeader("Content-Type","application/octet-stream");int code=http.POST(pcm,n);free(pcm);
 int bytes=http.getSize();
 if(!cancel&&code==200&&bytes>0&&bytes<=rate*2*120&&!(bytes%2)){
  phase=3;digitalWrite(39,HIGH);uint8_t block[640];int remain=bytes;
  while(remain&&!cancel){size_t got=http.getStream().readBytes(block,min((int)sizeof(block),remain));if(!got)break;
   bus.write(block,got);remain-=got;}
  phase=(remain&&!cancel)?4:0;
 }else phase=cancel?0:4;
 http.end();
}
void worker(void *arg){if(ready()){if(arg==(void*)2){digitalWrite(39,HIGH);for(size_t i=0;i<sizeof(praisePCM)&&!cancel;i+=640)bus.write(praisePCM+i,min(size_t(640),sizeof(praisePCM)-i));}else if(arg)bugle();else chat();}else phase=4;
 digitalWrite(39,LOW);busy=false;vTaskDelete(nullptr);}
bool start(int kind){if(busy.exchange(true))return false;cancel=false;finish=false;phase=0;
 if(xTaskCreate(worker,"deskAudio",8192,(void*)(intptr_t)kind,2,nullptr)!=pdPASS){busy=false;phase=4;return false;}return true;}
}
