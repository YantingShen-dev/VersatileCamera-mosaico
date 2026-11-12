"""Generate the authored scene and a subset of the supplied open-source font."""
import json
import os
from pathlib import Path
from fontTools import subset
from PIL import Image
root=Path(__file__).resolve().parents[1]
ui=root/'components/app_photo/ui'
(ui/'fonts').mkdir(parents=True,exist_ok=True)
(ui/'assets').mkdir(exist_ok=True)
texts='拍照打印确认照片实时取景点击拍照重拍确认打印黑白打印预览确认后出纸RPP02N已连接连接中正在发送请保持打印机开机已发送请检查出纸发送失败检查出纸后再重试摄像头不可用请检查左侧模块正在启动摄像头正在处理再打印一张正在打印0123456789 /%'
charset=''.join(sorted(set(texts+'?')))
font=Path(os.environ['PHOTO_FONT_SOURCE'])
options=subset.Options();f=subset.load_font(str(font),options)
s=subset.Subsetter();s.populate(text=charset);s.subset(f)
subset.save_font(f,str(ui/'fonts/PhotoSans.ttf'),options)
(ui/'fonts/OFL.txt').write_text('\n'.join(line.rstrip() for line in font.with_name('OFL.txt').read_text().splitlines())+'\n',encoding='utf-8')
Image.new('RGB',(320,320),'#263943').save(ui/'assets/preview.png')
objects=[]
def obj(kind,x,y,w,h,parent=-1,**kw):
    objects.append(dict(type=kind,x=x,y=y,w=w,h=h,parent=parent,**kw));return len(objects)-1
def label(text,x,y,w,h,size=18,parent=-1,bind=None,color='#E5EBEB'):
    kw=dict(text=text,font_size=size,fg_color=color)
    if bind:kw.update(name=bind,bind=bind,font_charset=charset)
    return obj('label',x,y,w,h,parent,**kw)
def button(name,text,x,w,parent,primary=False,bind=None):
    b=obj('button',x,0,w,48,parent,name=name,callback=name,radius=10,bg_color='#FF9D31' if primary else '#304654')
    kw=dict(text=text,font_size=22,text_align='center',fg_color='#11202B' if primary else '#E5EBEB')
    if bind:kw.update(name=bind,bind=bind,font_charset=charset)
    obj('label',0,0,w,48,b,**kw)
label('拍照打印',24,14,260,38,26,bind='title')
label('RPP02N / 连接中',280,24,180,25,16,bind='connection',color='#78D7AA')
obj('image',80,64,320,320,name='preview',bind='preview',image='assets/preview.png')
label('正在启动摄像头',24,393,440,30,17,bind='status',color='#B5C6D0')
live=obj('container',24,426,432,48,name='live_controls',bind='live_controls',bind_target='visible',bg_color='#11202B')
button('capture','拍照',0,432,live,True)
review=obj('container',24,426,432,48,name='review_controls',bind='review_controls',bind_target='visible',hidden=True,bg_color='#11202B')
button('retake','重拍',0,178,review)
button('print','确认打印',192,240,review,True,'print_text')
scene=dict(screen='photo',w=480,h=480,screen_bg='#11202B',font='fonts/PhotoSans.ttf',default_font_size=18,swipe=False,objects=objects)
(ui/'main.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
