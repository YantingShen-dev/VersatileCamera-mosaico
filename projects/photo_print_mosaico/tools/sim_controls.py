"""Local controls around the official native simulator; never talks to hardware."""
import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

HTML = r"""<!doctype html><meta charset="utf-8"><title>CCD 相机模拟器</title>
<style>body{margin:0;background:#e7e7df;color:#222;font:16px system-ui}main{display:flex;gap:32px;max-width:1060px;margin:24px auto;align-items:flex-start}iframe{border:0;width:580px;height:780px;background:#fff;border-radius:16px}aside{width:300px;padding:24px;background:#f7f7f0;border-radius:16px}button{font:inherit;padding:12px;margin:5px 0;width:100%;border:1px solid #ccc;border-radius:8px;background:white;cursor:pointer}button:hover{background:#e1e3d9}p{line-height:1.7}small{color:#666}#state{white-space:pre-line}</style>
<main><iframe src="http://127.0.0.1:3222/" title="Mosaico 原生模拟器"></iframe><aside>
<h2>CCD 拍照打印</h2><p>直接操作左侧相机界面的两个按钮。下面可以模拟手掌和打印机状态。</p>
<button onclick="setState('online palm')">模拟张开五指</button><button onclick="setState('online')">移开手掌 / 蓝牙连接</button>
<button onclick="setState('offline')">模拟打印机断开</button><button onclick="setState('fail')">模拟打印发送失败</button>
<p id="state">读取状态中…</p><small>这是原生 UI 的本地模拟器。手势输入和打印结果为模拟；这里不会真实打印。实物声音、识别率及走纸效果需在板子上验证。</small>
</aside></main><script>
async function setState(value){await fetch('/state',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({value})});}
const modes=['启动','取景','照片预览','发送中','已发送','发送失败','摄像头错误','选择滤镜','倒计时','拍照中'];
setInterval(async()=>{try{const r=await fetch('/state');const d=await r.json();if(d.values.length===8){let v=d.values;document.querySelector('#state').textContent='当前：'+modes[v[0]]+'\n模拟打印次数：'+v[1]+'\n倒计时：'+v[5]+'\n提示音事件：'+v[6]+' / 快门事件：'+v[7];}}catch{}},500);
</script>"""

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--state',required=True,type=Path)
    parser.add_argument('--port',default=3223,type=int)
    args=parser.parse_args()
    class Handler(BaseHTTPRequestHandler):
        def send(self,body,kind='application/json',status=200):
            data=body.encode('utf-8');self.send_response(status)
            self.send_header('Content-Type',kind+'; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def do_GET(self):
            if self.path=='/':return self.send(HTML,'text/html')
            if self.path!='/state':return self.send('{}',status=404)
            try:values=list(map(int,Path(str(args.state)+'.result').read_text().split()))
            except (OSError,ValueError):values=[]
            self.send(json.dumps({'values':values}))
        def do_POST(self):
            if self.path!='/state':return self.send('{}',status=404)
            if self.headers.get('Origin') not in (None,f'http://127.0.0.1:{args.port}',f'http://localhost:{args.port}'):
                return self.send('{}',status=403)
            try:
                size=int(self.headers.get('Content-Length','0'))
                if not 0<size<128:raise ValueError()
                value=json.loads(self.rfile.read(size))['value']
                if value not in ('online','online palm','offline','fail'):raise ValueError()
                args.state.write_text(value,encoding='ascii')
            except (ValueError,KeyError,OSError):return self.send('{}',status=400)
            self.send('{"ok":true}')
        def log_message(self,*args):pass
    print(f'Camera simulator controls: http://127.0.0.1:{args.port}/',flush=True)
    ThreadingHTTPServer(('127.0.0.1',args.port),Handler).serve_forever()

if __name__=='__main__':main()
