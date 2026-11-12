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
 title='课程表　星期'+'日一二三四五六'[i]
 page('school'+str(i),title,(['上午　'+'、'.join(c[:4]),'下午　'+'、'.join(c[4:7])]+(['晚上　晚自习'] if len(c)>7 else []) if c else ['今天没有校内课程。'])+([extra[i]] if extra[i] else []),'上下拨动翻页　·　中键查看下一天')
for i,r in enumerate(read):
 for a in range(2):page('read%d%d'%(i,a),'每日阅读　'+('找线索' if a else '读一读'),[r[0],r[2] if a else r[1]],'上下拨动翻页　·　中键切换问题与提示')
for i,t in enumerate(['开始','暂停','继续','滴眼药水','洗漱','戴 OK 镜','今晚已完成']):
 page('bed'+str(i),'睡前打卡',(['反转拍　6 分钟'] if i<3 else ['下一项　'+t] if i<6 else [t]),'上下拨动翻页　·　中键'+(t if i<3 else '确认完成' if i<6 else ''))
page('birthday','生日快乐！',['11 月 21 日','愿你一直对世界好奇。'],'上下拨动翻页')
page('nosync','课程表',['正在校对日期。','按中键可从星期一开始查看课表。'],'上下拨动翻页　·　中键查看课表')
for i,ch in enumerate('0123456789:'):
 im=Image.new('1',(64,104),1);ImageDraw.Draw(im).text((0,0),ch,font=font(90),fill=0);export('digit'+str(i),im)
def ptr(n,els):f.write('static const uint8_t* const '+n+'[]={'+','.join(els)+'};\n')
ptr('schoolArt',['school'+str(i) for i in range(7)]);ptr('readArt',['read%d%d'%(i,a) for i in range(7) for a in range(2)]);ptr('bedArt',['bed'+str(i) for i in range(7)]);ptr('digits',['digit'+str(i) for i in range(11)])

f.close()
import lzma
Path('desk/art.inc.xz').write_bytes(lzma.compress(Path('src/desk_art.inc').read_bytes()))
Path('src/desk_art.inc').unlink()
