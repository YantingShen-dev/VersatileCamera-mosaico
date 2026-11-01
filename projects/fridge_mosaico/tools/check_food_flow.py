"""Opt-in real photograph check. Requires empty inventory and test-tomato.jpg."""
import time
import httpx
from service import DATA

def main():
    with httpx.Client(base_url='http://127.0.0.1:3223',trust_env=False,timeout=55) as client:
        def post(path,**data):
            r=client.post('/api/'+path,json=data);r.raise_for_status();return r.json()
        s=client.get('/api/state').json();assert not s['items'] and s['page']==0
        r=client.post('/api/upload',content=(DATA/'test-tomato.jpg').read_bytes());r.raise_for_status()
        post('command',action='in');s=post('command',action='capture')
        post('recognize',session=s['session'],capture=s['capture'])
        for _ in range(100):
            s=client.get('/api/state').json()
            if s['page']!=2:break
            time.sleep(.2)
        assert s['page']==3 and len(s['items'])==1 and s['items'][0]['quantity']==3,s
        print('PASS: uploaded real photo -> Qwen3-VL -> native inventory: three tomatoes')
        s=post('correct',session=s['session'],text='西红柿有四个')
        assert s['items'][0]['quantity']==4,s
        print('PASS: correction updates current transaction to four; session',s['session'])

if __name__=='__main__':main()
