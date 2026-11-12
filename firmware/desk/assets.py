# 在 firmware 目录运行。重制位图需安装 Pillow 和 Node.js。
from PIL import Image,ImageDraw,ImageFont
from pathlib import Path
import json,subprocess,re
p=Path('desk/content.html').read_text()
j=p[p.index('const school='):p.index('const $=')]
school,extra,read=json.loads(subprocess.check_output(['node','-e',j+'console.log(JSON.stringify([school,extra,readings]))']))
f=open('src/desk_art.inc','w')
def font(n):return ImageFont.truetype('/System/Library/Fonts/STHeiti Medium.ttc',n)
def export(name,img):
 b=img.tobytes(); f.write('static const uint8_t '+name+'[] PROGMEM={\n');f.writelines(','.join(str(x) for x in b[i:i+96])+',\n' for i in range(0,len(b),96));f.write('};\n')
def page(name,title,lines,footer):
 im=Image.new('1',(800,480),1);d=ImageDraw.Draw(im)
 d.text((28,20),title,font=font(42),fill=0);d.line((28,87,772,87),0,2); y=105
 for line in lines:
  acc=''
  for ch in line:
   if d.textlength(acc+ch,font=font(30))>738:
    d.text((30,y),acc,font=font(30),fill=0);y+=42;acc=''
   acc+=ch
  d.text((30,y),acc,font=font(30),fill=0);y+=48
 assert y<428,(name,y)
 d.line((28,420,772,420),0,1);d.text((28,433),footer,font=font(23),fill=0)
 export(name,im)
for i,c in enumerate(school):
 title='课程预览　星期'+'日一二三四五六'[i]
 page('school'+str(i),title,(['上午　'+'、'.join(c[:4]),'下午　'+'、'.join(c[4:7])]+(['晚上　晚自习'] if len(c)>7 else []) if c else ['今天没有校内课程。'])+([extra[i]] if extra[i] else []),'上下拨动翻页　·　中键查看下一天')
 page('tomorrow'+str(i),'明日课程　星期'+'日一二三四五六'[i],(['上午　'+'、'.join(c[:4]),'下午　'+'、'.join(c[4:7])]+(['晚上　晚自习'] if len(c)>7 else []) if c else ['明天没有校内课程。'])+([extra[i]] if extra[i] else []),'上下拨动翻页　·　中键查看后一天')
for i,r in enumerate(read):
 for a in range(2):page('read%d%d'%(i,a),'每日阅读　'+('找线索' if a else '读一读'),[r[0],r[2] if a else r[1]],'上下拨动翻页　·　中键切换问题与提示')
# 每张静态底图包含右侧复选框。秒数单独局刷，不擦除清单。
for step in range(8):
 im=Image.new('1',(800,480),1); d=ImageDraw.Draw(im)
 def label(x,y,t,n):d.text((x,y),t,font=font(n),fill=0)
 label(30,20,'睡前打卡',42); d.line((30,86,770,86),0,2); d.line((410,112,410,390),0,2)
 label(32,123,'反转拍',36);label(32,320,['准备开始','计时中','已暂停'][step] if step<3 else '计时完成',27)
 label(450,119,'睡前待办',31)
 for i,t in enumerate(['滴眼药水','刷牙','洗脚','戴 OK 镜']):
  y=179+52*i;d.rectangle((453,y,478,y+25),outline=0,width=2);label(496,y-5,t,29)
  if step>i+3:d.line([(457,y+12),(463,y+20),(475,y+5)],0,3)
  if step==i+3:label(700,y,'当前',21)
 label(30,432,(['中键开始','中键暂停','中键继续'][step] if step<3 else '今晚已完成' if step==7 else '中键勾选当前事项'),23)
 export('bed'+str(step),im)
poems=[['山居秋暝','王维','空山新雨后，天气晚来秋。','明月松间照，清泉石上流。','竹喧归浣女，莲动下渔舟。','随意春芳歇，王孙自可留。','雨后山中，有月光、清泉，','也有归来的浣女和渔舟。'],['枫桥夜泊','张继','月落乌啼霜满天，','江枫渔火对愁眠。','姑苏城外寒山寺，','夜半钟声到客船。','诗人夜泊枫桥，从船上望见','渔火，听到远处钟声。']]
for i,p in enumerate(poems):
 for note in range(2):
  im=Image.new('1',(800,480),1);d=ImageDraw.Draw(im);d.line((30,87,770,87),0,2);d.line((350,120,350,382),0)
  for y,t,n in [(119,p[0],34),(166,p[1],23)]+[(214+40*k,t,25) for k,t in enumerate(p[6:] if note else p[2:6])]:
   d.text((380,y),t,font=font(n),fill=0)
  d.text((30,434),'SCROLL: Turn page    PRESS: '+('Read poem' if note else 'Show note'),font=font(21),fill=0)
  export('poem%d%d'%(i,note),im)
page('memoEmpty','爸妈留言',['今天没有留言。'],'家长手机连接同一 Wi-Fi，打开上方网址填写。')
page('memoPage','爸妈留言',[],'家长手机连接同一 Wi-Fi，打开上方网址填写。')
page('birthday','生日快乐！',['11 月 21 日','愿你一直对世界好奇。'],'上下拨动翻页')
page('nosync','课程表',['正在校对日期。','按中键可从星期一开始查看课表。'],'上下拨动翻页　·　中键查看课表')
for i,ch in enumerate('0123456789:'):
 im=Image.new('1',(64,104),1);ImageDraw.Draw(im).text((0,0),ch,font=font(90),fill=0);export('digit'+str(i),im)
def ptr(n,els):f.write('static const uint8_t* const '+n+'[]={'+','.join(els)+'};\n')
ptr('schoolArt',['school'+str(i) for i in range(7)]);ptr('readArt',['read%d%d'%(i,a) for i in range(7) for a in range(2)]);ptr('bedArt',['bed'+str(i) for i in range(8)]);ptr('tomorrowArt',['tomorrow'+str(i) for i in range(7)]);ptr('poemArt',['poem%d%d'%(i,n) for i in range(2) for n in range(2)]);ptr('digits',['digit'+str(i) for i in range(11)])

f.close()
import lzma
Path('desk/art.inc.xz').write_bytes(lzma.compress(Path('src/desk_art.inc').read_bytes()))
Path('src/desk_art.inc').unlink()
