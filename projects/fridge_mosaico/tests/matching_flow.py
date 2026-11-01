"""Native pointer + framebuffer test; isolated FRIDGE_DATA_DIR only."""
import asyncio, json, os, struct
from datetime import date
from pathlib import Path
from continuous_flow import Viewer, Companion, DATA
from service import observations
import websockets

def observed(category, brand='', quantity=1, kind='food', evidence='label', label=None):
    return observations({'items':[dict(category=category,brand=brand,quantity=quantity,kind=kind,
                                       evidence=evidence,label_text=label if label is not None else brand+category)]})[0]

async def main():
    assert DATA.name=='fridge-match-continuous-test'
    c=Companion();assert not c.state()['items'],'Use a fresh or restored isolated fixture'
    async with websockets.connect('ws://127.0.0.1:3222/ws',proxy=None,max_size=2000000,ping_interval=None) as ws:
        v=Viewer(ws);reader=asyncio.create_task(v.receive());await asyncio.sleep(.4)
        async def scan(direction,items):
            if c.state()['page']!=0:
                await v.tap(60,440);await settled(lambda s:s['page']==0)
            await v.tap(120 if direction==1 else 340,440)
            await settled(lambda s:s['page']==1 and s['direction']==direction)
            await v.tap(240,440);await settled(lambda s:s['page']==2);s=c.state()
            (DATA/'preview.rgb565').write_bytes(struct.pack('<H',0x4a49)*(480*480));await asyncio.sleep(.2)
            return await c.command(13,session=s['session'],capture=s['capture'],items=items)
        def quantity(name):return sum(i['quantity'] for i in c.state()['items'] if i['name']==name)
        async def settled(predicate):
            for _ in range(100):
                if predicate(c.state()):return
                await asyncio.sleep(.05)
            raise AssertionError(c.state())
        async def choose():
            await v.tap(240,219)
            await settled(lambda s:not s['pending'])
        async def undo():await c.command(7,session=c.state()['session'])
        await scan(1,[observed('薯片','乐事',2),observed('黑咖啡',kind='drink',label='冰美式')])
        assert quantity('乐事薯片')==2 and quantity('黑咖啡')==1
        s=await scan(2,[observed('薯片',evidence='appearance')]);assert not s['pending'] and quantity('乐事薯片')==1
        await undo()
        await scan(1,[observed('薯片','可比克')])
        s=await scan(2,[observed('薯片',evidence='appearance')]);assert s['pending'] and s['choice_count']==2
        assert quantity('乐事薯片')==2 and quantity('可比克薯片')==1
        await asyncio.sleep(.4);v.shot('ambiguous-chips')
        await choose();assert quantity('乐事薯片')==1
        await undo()
        # Even one stocked drink is not enough evidence to auto-deduct a blurred cup.
        s=await scan(2,[observed('气泡水',kind='drink',evidence='appearance',label='')])
        assert s['pending'] and s['choice_count']==1 and quantity('黑咖啡')==1
        await asyncio.sleep(.4);v.shot('unclear-drink')
        await choose();assert quantity('黑咖啡')==0;await undo()
        # A genuinely conflicting label has no candidate, even with coffee in stock.
        s=await scan(2,[observed('气泡水',kind='drink')]);assert s['pending'] and s['choice_count']==0
        assert quantity('黑咖啡')==1
        sid,cap=s['session'],s['capture'];await v.tap(240,440)
        await settled(lambda s:s['page']==2)
        assert c.state()['page']==2 and not c.state()['pending']
        s=await c.command(14,session=sid,capture=cap,value=2);assert s['page']==2 and quantity('黑咖啡')==1
        # Whole batch waits: the clear chips cannot commit before the drink selection.
        s=await scan(2,[observed('薯片','乐事'),observed('饮品',kind='drink',evidence='unclear',label='')])
        assert s['pending'] and quantity('乐事薯片')==2
        await choose();assert quantity('乐事薯片')==1 and quantity('黑咖啡')==0;await undo()
        await scan(1,[observed('薯片','多力'),observed('薯片','上好佳')])
        s=await scan(2,[observed('薯片',evidence='appearance')]);assert s['choice_count']==4
        await v.tap(240,374);await settled(lambda s:s['choice_offset']==3)
        await asyncio.sleep(.3);v.shot('more-candidates')
        await choose();assert quantity('上好佳薯片')==0;await undo()
        await scan(1,[observed('饼干',evidence='appearance')])
        s=await scan(1,[observed('夹心饼干','奥利奥')]);assert s['pending'] and s['choice_count']==2
        await choose();assert quantity('奥利奥饼干')==2 and quantity('饼干')==0
        # Cleanup fixture through the same transaction engine.
        await v.tap(60,440);await settled(lambda s:s['page']==0)
        await v.tap(340,440);await settled(lambda s:s['page']==1)
        await v.tap(240,440);await settled(lambda s:s['page']==2);s=c.state()
        items=[dict(name=i['name'],quantity=round(i['quantity']*1000),date=date.today().isoformat()) for i in s['items']]
        s=await c.command(4,session=s['session'],capture=s['capture'],items=items);assert not s['items']
        await v.tap(60,440);reader.cancel()
    await c.http.aclose()
    print('PASS: native matching, explicit ambiguity choice, unclear drink guard, conflicting label, stale tap, atomic batch, pagination, brand enrichment.')
if __name__=='__main__':asyncio.run(main())
