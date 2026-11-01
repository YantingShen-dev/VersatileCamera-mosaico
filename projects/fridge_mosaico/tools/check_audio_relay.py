"""Send a synthetic local WAV through the actual browser-facing ASR relay."""
import asyncio
import json
import time
import wave
import websockets
from service import DATA

async def main():
    with wave.open(str(DATA/'asr-check.wav'),'rb') as f:
        assert f.getframerate()==16000 and f.getsampwidth()==2 and f.getnchannels()==1
        pcm=f.readframes(f.getnframes()) + bytes(32000)
    async with websockets.connect('ws://127.0.0.1:3223/audio',proxy=None) as ws:
        while True:
            event=json.loads(await asyncio.wait_for(ws.recv(),20))
            if event['type']=='error':raise RuntimeError(event['text'])
            if event['type']=='ready':break
        async def send():
            for pos in range(0,len(pcm),3200):
                await ws.send(pcm[pos:pos+3200]);await asyncio.sleep(.1)
        task=asyncio.create_task(send());start=time.monotonic()
        try:
            while True:
                event=json.loads(await asyncio.wait_for(ws.recv(),20))
                if event['type']=='transcript':print('Relay transcript:',event['text'])
                if event['type']=='error':raise RuntimeError(event['text'])
                if event['type']=='corrected':
                    s=event['state'];assert s['items'][0]['quantity']==4,s
                    print(f'PASS: ASR relay -> Qwen command -> native quantity 4 ({time.monotonic()-start:.1f}s including audio playback)');break
        finally:
            task.cancel();await asyncio.gather(task,return_exceptions=True)

if __name__=='__main__':asyncio.run(main())
