"""Integration check against an already running native simulator with an isolated store.
Run with FRIDGE_DATA_DIR=evidence/fridge-continuous-test, never with user stock.
"""
import asyncio, json, os, struct, sys
from pathlib import Path
from datetime import date
import websockets
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import service
from service import Companion
DATA=Path(os.environ.get("FRIDGE_DATA_DIR", ""))
assert "continuous-test" in str(DATA)
service.DATA=DATA
service.config=lambda: {}

class Viewer:
    def __init__(self, ws):
        self.ws=ws; self.image=Image.new('RGB',(480,480)); self.futures={}; self.ident=0
    async def receive(self):
        async for packet in self.ws:
            if isinstance(packet,str):
                m=json.loads(packet)
                if m.get('id') in self.futures:self.futures.pop(m['id']).set_result(m)
                continue
            if packet[:4]!=b'GSPF':continue
            fmt=packet[5];width,height=struct.unpack_from('<II',packet,16)
            rectangles,payload=struct.unpack_from('<II',packet,28)
            pos=36+rectangles*16
            for i in range(rectangles):
                x1,y1,x2,y2=struct.unpack_from('<iiii',packet,36+i*16)
                size=(x2-x1)*(y2-y1)
                if fmt==0:
                    tile=Image.frombytes('RGB',(x2-x1,y2-y1),packet[pos:pos+size*2],'raw','BGR;16')
                    pos+=size*2
                else:raise AssertionError(f'Unexpected frame format {fmt}')
                self.image.paste(tile,(x1,y1))
    async def rpc(self,method,params):
        self.ident+=1; future=asyncio.get_running_loop().create_future();self.futures[self.ident]=future
        await self.ws.send(json.dumps({'jsonrpc':'2.0','id':self.ident,'method':method,'params':params}))
        result=await asyncio.wait_for(future,5);assert 'error' not in result,result
        return result['result']
    async def tap(self,x,y):
        await self.rpc('feed_pointer',{'x':x,'y':y,'pressed':True});await asyncio.sleep(.1)
        await self.rpc('feed_pointer',{'x':x,'y':y,'pressed':False});await asyncio.sleep(.5)
    def shot(self,name): self.image.save(DATA/(name+'.png'))

async def main():
    assert 'continuous-test' in str(DATA), 'Use the isolated fixture directory'
    c=Companion();s=c.state();assert not s['items'],'Fixture must start empty'
    async with websockets.connect('ws://127.0.0.1:3222/ws',proxy=None,max_size=2000000,ping_interval=None) as ws:
        viewer=Viewer(ws);reader=asyncio.create_task(viewer.receive());await asyncio.sleep(.4)
        await viewer.tap(120,440);s=c.state();assert s['page']==1,s
        operation=s['operation'];viewer.shot('entry')
        await viewer.tap(240,440);s=c.state();assert s['page']==2,s
        sid,cap=s['session'],s['capture'];viewer.shot('recognizing')
        # Continue pushing different camera frames while vision is outstanding.
        for color in [0x7bef,0xad55]:
            (DATA/'preview.rgb565').write_bytes(struct.pack('<H',color)*(480*480));await asyncio.sleep(.2)
            assert c.state()['page']==2
        viewer.shot('live-busy')
        items=[{'name':'苹果','quantity':1000,'date':date.today().isoformat()}, {'name':'娃哈哈AD钙奶','quantity':2000,'date':date.today().isoformat()}]
        s=await c.command(4,session=sid,capture=cap,items=items)
        assert s['page']==1 and s['feedback']==1 and s['undo'] and s['operation']==operation,s
        shots=[]
        for delay,name in [(.08,'toast-in'),(.32,'toast-rise'),(.65,'toast-hold'),(1.2,'toast-gone')]:
            await asyncio.sleep(delay);viewer.shot(name);shots.append(viewer.image.tobytes())
        assert len(set(shots))>=3,'Animation frames must change'
        s=await c.command(9,session=sid,capture=cap);assert s['interrupted']
        s=await c.command(5,session=sid,capture=cap,text='苹果',value=3000)
        assert s['page']==1 and not s['interrupted'] and s['feedback']==2
        assert next(i['quantity'] for i in s['items'] if i['name']=='苹果')==3
        await asyncio.sleep(.3);viewer.shot('correction')
        await viewer.tap(240,440);s=c.state();assert s['page']==2 and s['session']!=sid
        newer=s['session'],s['capture']
        s=await c.command(4,session=sid,capture=cap,items=items);assert s['page']==2,'Old result rejected'
        s=await c.command(5,session=sid,capture=cap,text='苹果',value=9000);assert s['page']==2,'Old correction rejected'
        s=await c.command(4,session=newer[0],capture=newer[1],items=[items[0]])
        assert next(i['quantity'] for i in s['items'] if i['name']=='苹果')==4
        s=await c.command(7,session=newer[0]);assert s['page']==1
        assert next(i['quantity'] for i in s['items'] if i['name']=='苹果')==3,'Undo must preserve previous batch'
        await viewer.tap(60,440);s=c.state();assert s['page']==0
        await viewer.tap(340,440);await viewer.tap(240,440);s=c.state();assert s['page']==2
        s=await c.command(4,session=s['session'],capture=s['capture'],items=[{'name':'苹果','quantity':3000,'date':date.today().isoformat()},items[1]])
        assert s['page']==1 and not s['items'],'OUT auto commits without confirmation'
        await viewer.tap(60,440)
        viewer.shot('restored-empty')
        # Force a durable-write failure only inside this isolated fixture.
        await viewer.tap(120,440);await viewer.tap(240,440);s=c.state()
        previous_feedback=s['feedback_id'];blocker=DATA/'inventory.tmp';blocker.mkdir()
        try:
            s=await c.command(4,session=s['session'],capture=s['capture'],items=[items[0]])
            assert not s['items'] and s['feedback_id']==previous_feedback and '保存失败' in s['message']
        finally:blocker.rmdir()
        await viewer.tap(240,440);s=c.state()
        await c.command(4,session=s['session'],capture=s['capture'],items=[items[0]])
        s=await c.command(9,session=s['session']);assert s['interrupted']
        await viewer.tap(240,440);s=c.state()
        assert s['page']==2 and not s['interrupted'],'A new shutter press must cancel pending correction'
        await c.command(8,session=s['session'],capture=s['capture'],text='test retry')
        await viewer.tap(60,440);await viewer.tap(340,440);await viewer.tap(240,440);s=c.state()
        s=await c.command(4,session=s['session'],capture=s['capture'],items=[items[0]])
        assert not s['items'];await viewer.tap(60,440)
        reader.cancel()
    await c.http.aclose()
    print('PASS: native touch entry/capture, live preview during recognition, animated auto-commit, correction, consecutive capture, stale-result rejection, latest-batch undo, OUT and return.')
if __name__=='__main__': asyncio.run(main())
