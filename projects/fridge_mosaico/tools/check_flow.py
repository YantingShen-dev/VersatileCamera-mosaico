"""Opt-in native/UI test fixture. Requires empty stock; finish with Undo to restore."""
import asyncio
import json
from datetime import date
from service import Companion, DATA

async def main():
    c=Companion()
    try:
        s=c.state()
        assert not s['items'], 'Test requires empty inventory; existing user records are never replaced'
        await c.command(6)
        await c.command(1)
        s=await c.command(3)
        names=['西红柿','黄瓜','鸡蛋','牛奶','土豆','胡萝卜','生菜','苹果','香蕉','洋葱']
        items=[{'name':name,'quantity':2000,'date':date.today().isoformat()} for name in names]
        s=await c.command(4,session=s['session'],capture=s['capture'],items=items)
        assert s['page']==1 and len(s['items'])==10,s
        s=await c.correct('西红柿有三个',s['session'])
        assert next(i for i in s['items'] if i['name']=='西红柿')['quantity']==3,s
        (DATA/'test-session.json').write_text(json.dumps({'session':s['session'],'revision':s['revision']}))
        print('PASS: native capture/apply + live Qwen Chinese correction (2 -> 3), 10 rows ready for scroll/persistence checks')
    finally:await c.http.aclose()

if __name__=='__main__':asyncio.run(main())
