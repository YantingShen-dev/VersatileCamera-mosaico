"""Explicit small live API checks; never emits credentials or request headers."""
import asyncio
import base64
import io
import json
import wave
from pathlib import Path
from PIL import Image
import websockets
from service import Companion, DATA, VISION_PROMPT

async def main():
    c=Companion()
    try:
        buffer=io.BytesIO();Image.new('RGB',(320,240),'white').save(buffer,format='JPEG')
        result=await c.qwen(c.cfg['QWEN_VISION_MODEL'],[
            {'role':'system','content':VISION_PROMPT},
            {'role':'user','content':[{'type':'image_url','image_url':{'url':'data:image/jpeg;base64,'+base64.b64encode(buffer.getvalue()).decode()}}]}])
        assert result['items']==[],result
        print('VISION: blank image correctly rejected')
        async with websockets.connect(c.cfg['QWEN_ASR_URL']+'?model=qwen3-asr-flash-realtime',
                additional_headers={'Authorization':'Bearer '+c.cfg['DASHSCOPE_API_KEY']},proxy=None) as ws:
            await ws.send(json.dumps({'event_id':'check_config','type':'session.update','session':{
                'input_audio_format':'pcm','sample_rate':16000,'input_audio_transcription':{'language':'zh'},'turn_detection':None}}))
            while True:
                event=json.loads(await asyncio.wait_for(ws.recv(),15))
                if event['type']=='error': raise RuntimeError('ASR configuration rejected: '+event['error'].get('code','unknown'))
                if event['type']=='session.updated':break
            print('ASR: realtime connection and PCM configuration accepted')
            wav=DATA/'asr-check.wav'
            if wav.exists():
                with wave.open(str(wav),'rb') as f:
                    assert f.getframerate()==16000 and f.getnchannels()==1 and f.getsampwidth()==2
                    pcm=f.readframes(f.getnframes())
                for offset in range(0,len(pcm),6400):
                    await ws.send(json.dumps({'event_id':f'chunk_{offset}','type':'input_audio_buffer.append','audio':base64.b64encode(pcm[offset:offset+6400]).decode()}))
                await ws.send(json.dumps({'event_id':'commit','type':'input_audio_buffer.commit'}))
                while True:
                    event=json.loads(await asyncio.wait_for(ws.recv(),20))
                    if event['type']=='error':raise RuntimeError('ASR audio rejected')
                    if event['type']=='conversation.item.input_audio_transcription.completed':
                        print('ASR transcript:',event.get('transcript'));break
    finally:await c.http.aclose()

if __name__=='__main__':asyncio.run(main())
