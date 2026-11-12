"""Generate the confirmed CCD scene and an OFL font subset."""
import json
import os
from pathlib import Path
from fontTools import subset
from PIL import Image
root=Path(__file__).resolve().parents[1]
ui=root/'components/app_photo/ui'
texts='CCD SNAP PHOTO CHECK FILTER READY BT -- 1:1 384px 0123456789 /%?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz正常黑白反色波点阈值版画拍照打印滤镜上一项下一项双击确认返回取景手掌五秒倒计时已识别取消立即拍照检查照片确认后出纸重拍正在发送请保持打印机开机已发送请检查出纸失败再试摄像头不可用检查左侧模块启动连接中未连接已连接手势准备中可用音效扩展板正在处理'
texts += ''.join(p.read_text(encoding='utf-8') for p in (root/'components/app_photo').glob('*.c'))
charset=''.join(sorted(set(texts)))
font=Path(os.environ['PHOTO_FONT_SOURCE'])
options=subset.Options();f=subset.load_font(str(font),options)
s=subset.Subsetter();s.populate(text=charset);s.subset(f)
subset.save_font(f,str(ui/'fonts/PhotoSans.ttf'),options)
(ui/'fonts/OFL.txt').write_text('\n'.join(line.rstrip() for line in font.with_name('OFL.txt').read_text().splitlines())+'\n',encoding='utf-8')
Image.new('RGB',(384,384),'#c8c8c2').save(ui/'assets/preview.png')
objects=[]
def obj(kind,x,y,w,h,parent=-1,**kw):
    objects.append(dict(type=kind,x=x,y=y,w=w,h=h,parent=parent,**kw));return len(objects)-1
def label(text,x,y,w,h,size=18,parent=-1,bind=None,color='#171917',align='left'):
    kw=dict(text=text,font_size=size,fg_color=color,text_align=align)
    if bind:kw.update(name=bind,bind=bind,font_charset="012345" if bind=="countdown" else charset)
    return obj('label',x,y,w,h,parent,**kw)
def visible(name,x,y,w,h,color='#222722'):
    return obj('container',x,y,w,h,name=name,bind=name,bind_target='visible',hidden=True,bg_color=color)
obj('container',27,14,8,8,radius=4,bg_color='#CF3727')
label('CCD SNAP',43,5,270,25,17,bind='title')
label('BT --',321,6,133,24,16,bind='connection',align='right')
obj('image',48,48,384,384,name='preview',bind='preview',image='assets/preview.png')
for x,y,sx,sy in [(25,48,1,1),(455,48,-1,1),(25,420,1,-1),(455,420,-1,-1)]:
    obj('container',x if sx>0 else x-39,y,40,3,bg_color='#141414')
    obj('container',x,y if sy>0 else y-27,3,28,bg_color='#141414')
focus=visible('focus',220,210,40,40,'#444844')
obj('container',10,19,20,2,focus,bg_color='#FFFFFF')
obj('container',19,10,2,20,focus,bg_color='#FFFFFF')
count=visible('countdown_box',175,150,130,140,'#222722')
label('5',0,0,130,130,100,count,'countdown','#FFFFFF','center')
panel=visible('filter_box',48,350,384,78)
label('双击 2 确认返回取景',0,8,384,32,19,panel,color='#F5F5EE',align='center')
label('启动摄像头',24,29,432,18,13,bind='status')
label('正常黑白 / 1:1',0,43,384,26,16,panel,'filter_name','#F5F5EE','center')
for x,name,bind,text in [(24,'button1','button1_text','1 拍照'),(250,'button2','button2_text','2 滤镜')]:
    b=obj('button',x,437,206,40,name=name,callback=name,radius=4,bg_color='#ECEDE5')
    label(text,0,0,206,40,21,b,bind,align='center')
scene=dict(screen='photo',w=480,h=480,screen_bg='#F4F4ED',font='fonts/PhotoSans.ttf',default_font_size=18,swipe=False,objects=objects)
(ui/'main.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
