"""Author the native GSP UI and subset the OFL Noto font (not a screen bitmap)."""
import json
import copy
from pathlib import Path
from fontTools import subset
from fontTools.varLib.instancer import instantiateVariableFont
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parents[1]
UI = ROOT / 'components/app_fridge/ui'
BG, INK, MUTED, GREEN = '#F6F5F0', '#24392F', '#7B857C', '#357457'

def main():
    (UI / 'fonts').mkdir(exist_ok=True)
    (UI / 'assets').mkdir(exist_ok=True)
    common = ''.join(bytes([a,b]).decode('gb2312', errors='ignore') for a in range(176,248) for b in range(161,255))
    source_text = ''.join(p.read_text(encoding='utf-8') for p in (ROOT / 'components/app_fridge').rglob('*.c'))
    chars = ''.join(sorted(set(common + source_text + ''.join(chr(i) for i in range(32,127)) + '、：，。…昨日今天入库出库我的冰箱把新鲜留给每一天无库存食材拍照识别返回完成取消撤销本次听到修改请稍后')))
    font = subset.load_font(str(WORKSPACE / '.tools/NotoSansSC-VF.ttf'), subset.Options())
    font = instantiateVariableFont(font, {'wght':450}, inplace=True)
    sub = subset.Subsetter(); sub.populate(text=chars); sub.subset(font)
    subset.save_font(font, str(UI / 'fonts/FridgeSans.ttf'), subset.Options())
    (UI / 'fonts/OFL.txt').write_bytes((WORKSPACE / '.tools/Noto-OFL.txt').read_bytes())
    objects = []
    def obj(kind,x,y,w,h,parent=-1,**kw):
        objects.append(dict(type=kind,parent=parent,x=x,y=y,w=w,h=h,**kw)); return len(objects)-1
    def label(text,x,y,w,h,size=18,color=INK,parent=-1,name=None,**kw):
        if name:
            kw.update(name=name,bind=name)
            kw.setdefault('font_charset',chars if size==18 else '0123456789.- /入库出库本次正在识别')
        return obj('label',x,y,w,h,parent,text=text,font_size=size,fg_color=color,**kw)
    def button(name,text,x,y,w,h,parent=-1,primary=False):
        b=obj('button',x,y,w,h,parent,name=name,callback=name,radius=14,bg_color=GREEN if primary else '#E6EAE1')
        label(text,0,0,w,h,18,'#FFFFFF' if primary else INK,b,text_align='center')
    def layer(name):
        return obj('container',0,0,480,480,bg_color=BG,name=name,bind=name,bind_target='visible',hidden=True)
    obj('container',0,0,480,480,bg_color=BG,name='backdrop',bind='backdrop',bind_target='visible')
    label('FRIDGE / 日常有鲜',16,10,330,22,12,MUTED)
    label('我的冰箱',16,34,330,42,28)
    obj('container',16,88,448,1,bg_color='#DDE2D8')
    label('库存食材',16,98,210,26,18)
    label('0',368,94,96,32,24,GREEN,name='count',text_align='right')
    row=obj('container',0,0,448,76,template='stock_row',max_instances=12,bg_color=BG)
    obj('container',0,0,448,70,row,bg_color='#FFFFFF',radius=12)
    obj('container',14,17,4,34,row,bg_color='#B9CBB4',radius=2)
    label('',30,9,296,28,18,parent=row,name='row_name')
    label('',30,39,250,20,13,MUTED,row,name='row_date')
    label('',326,14,102,38,26,GREEN,row,name='row_quantity',text_align='right')
    obj('list',16,136,448,264,name='stock',row_template='stock_row',item_count=0,item_height=76,scroll_snapshot=False)
    empty=obj('container',16,204,448,150,name='empty',bind='empty',bind_target='visible')
    # Small code-drawn refrigerator icon, all UI text remains native.
    obj('container',198,14,52,64,empty,border_color='#BCCABD',border_width=2,radius=9)
    obj('container',198,38,52,2,empty,bg_color='#BCCABD')
    obj('container',208,25,2,7,empty,bg_color='#BCCABD')
    obj('container',208,49,2,12,empty,bg_color='#BCCABD')
    label('无',0,90,448,38,26,MUTED,empty,text_align='center')
    actions=obj('container',16,416,448,48,name='home_actions',bind='home_actions',bind_target='visible')
    button('enter_in','入库',0,0,216,48,actions,True)
    button('enter_out','出库',232,0,216,48,actions)
    button('open_settings','设置',376,28,88,44)
    operation=layer('operation')
    image=Image.new('RGB',(480,480),'#17211D')
    image.save(UI/'assets/preview.png')
    obj('image',0,0,480,480,operation,name='preview',bind='preview',image='assets/preview.png')
    # GSP 1.5.1 has dynamic positions, but label opacity is authored. Eight
    # alpha variants provide fades without rasterizing UI text in the adapter.
    for lane in range(3):
        for step in range(8):
            alpha = round(255 * (step + 1) / 8)
            name = f'toast_{lane}_{step}'
            bubble = obj('container',dict(default=24,min=0,max=64),
                         dict(default=286-lane*72,min=30,max=320),432,54,operation,
                         name=name,bind=name,bind_target='visible',hidden=True,
                         bg_color='#173629',opacity=alpha,radius=16)
            label('',12,7,408,40,24,'#F7FFE9',bubble,
                  name=f'toast_text_{lane}_{step}',opacity=alpha,text_align='center',
                  font_charset=chars)
    status=obj('container',0,366,480,42,operation,bg_color='#17211D',name='camera_notice',bind='camera_notice',bind_target='visible',hidden=True)
    label('',12,6,456,30,18,'#FFFFFF',status,name='camera_status')
    match=obj('container',16,100,448,302,operation,name='match_panel',bind='match_panel',bind_target='visible',hidden=True,bg_color=BG,radius=16)
    label('',16,12,416,46,18,INK,match,name='match_title')
    label('选一项后记账，或按拍照重拍',16,60,416,28,18,MUTED,match)
    for i in range(3):
        slot=obj('container',16,96+i*52,416,46,match,name=f'match_slot_{i}',bind=f'match_slot_{i}',bind_target='visible',hidden=True)
        b=obj('button',0,0,416,46,slot,name=f'match_pick_{i}',callback=f'match_pick_{i}',radius=12,bg_color='#E6EAE1')
        label('',12,8,392,30,18,INK,b,name=f'match_name_{i}',text_align='center')
    more=obj('container',116,254,216,40,match,name='match_more_slot',bind='match_more_slot',bind_target='visible',hidden=True)
    button('match_more','更多候选',0,0,216,40,more)
    button('camera_back','返回',16,416,88,48,operation)
    shutter=obj('container',168,416,144,48,operation,name='shutter',bind='shutter',bind_target='visible')
    button('take_photo','拍照',0,0,144,48,shutter,True)
    review=layer('review')
    label('本次入库',26,24,320,46,28,parent=review,name='review_title')
    label('已更新库存，可以直接说出纠正',26,79,428,28,18,MUTED,review)
    obj('list',16,125,448,220,review,name='batch',row_template='stock_row',item_count=0,item_height=76,scroll_snapshot=False)
    obj('container',26,358,428,36,review,bg_color='#E7EDE3',radius=10)
    label('正在准备播报',38,361,404,30,18,GREEN,review,name='speech')
    button('undo','撤销本次',26,410,160,48,review)
    button('done','完成',200,410,254,48,review,True)
    settings=layer('settings')
    label('设置',16,22,280,40,28,parent=settings)
    button('settings_back','返回',376,20,88,44,settings)
    label('Wi-Fi',16,90,220,30,22,parent=settings)
    label('未连接',16,130,448,30,parent=settings,name='network_status')
    label('',16,165,448,28,parent=settings,name='network_address',color=MUTED)
    button('choose_network','选择网络',16,208,448,48,settings)
    label('播报音量',16,282,240,30,22,parent=settings)
    label('45%',320,282,144,30,parent=settings,name='volume_text',text_align='right')
    button('volume_down','-',16,330,88,52,settings)
    button('volume_mute','静音',120,330,240,52,settings)
    button('volume_up','+',376,330,88,52,settings)
    button('volume_test','试听',16,408,448,48,settings,True)
    networks=layer('networks')
    label('选择 Wi-Fi',16,22,300,40,28,parent=networks)
    button('networks_back','返回',376,20,88,44,networks)
    label('',16,78,448,50,parent=networks,name='scan_status')
    for i in range(4):
        slot=obj('container',16,140+i*60,448,52,networks,name=f'ap_slot_{i}',bind=f'ap_slot_{i}',bind_target='visible',hidden=True)
        b=obj('button',0,0,448,52,slot,name=f'ap_{i}',callback=f'ap_{i}',radius=12,bg_color='#E6EAE1')
        label('',12,10,424,32,parent=b,name=f'ap_text_{i}')
    button('net_previous','上一页',16,408,136,48,networks)
    button('net_scan','刷新',172,408,136,48,networks,True)
    button('net_next','下一页',328,408,136,48,networks)
    password=layer('password')
    label('输入 Wi-Fi 密码',16,18,320,40,24,parent=password)
    button('password_back','取消',376,16,88,44,password)
    label('',16,68,448,30,parent=password,name='selected_network')
    label('',16,106,448,30,parent=password,name='password_mask')
    label('输入后按连接，密码不会显示',16,146,448,45,parent=password,name='password_hint',color=MUTED)
    obj('keyboard',0,208,480,272,password,name='wifi_keyboard',font_size=18,bg_color=BG,fg_color=INK,
        key_color='#E6EAE1',function_color='#CDD9C9',ok_color=GREEN,ok_text_color='#FFFFFF',key_radius=6,ok_label='连接')
    scene=dict(screen='fridge',w=480,h=480,screen_bg=BG,font='fonts/FridgeSans.ttf',default_font_size=18,swipe=False,objects=objects)
    (UI/'main.json').write_text(json.dumps(scene,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Device: keep static UI glyphs in the 1 MiB ui_apps partition; an embedded
    # TTF in main_app supplies arbitrary ingredient names through FreeType.
    device = copy.deepcopy(scene)
    runtime_chars = ''.join(sorted(set(source_text + ''.join(chr(i) for i in range(32,127)))))
    for element in device['objects']:
        if element.get('text') == '已更新库存，可以直接说出纠正':
            element['text'] = '库存已保存，可核对或撤销本次'
        if (element.get('font_size') == 18 or element.get('name','').startswith('toast_text_')) and 'font_charset' in element:
            element['font_charset'] = runtime_chars
    (UI/'main-device.json').write_text(json.dumps(device,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__': main()
