"""Provision through the official mosaico.py entry point; never put a key on argv/logs."""
import json
import runpy
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROJECT = 'E:/Studio/mosaico-env/fridge-build-workspace/projects/fridge_mosaico'
DEVICE = '4553502d4952495301001c2904d087f6'

def main():
    cfg = dict(line.split('=', 1) for line in (ROOT / '.mosaico-state/fridge/.env').read_text('utf-8-sig').splitlines()
               if '=' in line and not line.startswith('#'))
    if cfg.get('QWEN_BASE_URL') != 'https://dashscope.aliyuncs.com/compatible-mode/v1':
        raise SystemExit('This firmware supports the DashScope Beijing endpoint only.')
    payload = json.dumps({'api_key': cfg['DASHSCOPE_API_KEY'], 'model': cfg['QWEN_VISION_MODEL'],
                          'unix_time': int(time.time())}, separators=(',', ':')).encode()
    # In-process argv is converted by invoke_rpc to base64 on stdin of the managed
    # Gateway client. Neither shell history nor subprocess arguments contain it.
    sys.argv = [str(ROOT / 'mosaico.py'), '--json', 'iris', 'rpc', '0x4652', '4',
                '--payload-hex', payload.hex(), '--project', PROJECT, '--device-id', DEVICE]
    try:
        runpy.run_path(str(ROOT / 'mosaico.py'), run_name='__main__')
    finally:
        sys.argv = [sys.argv[0]]

if __name__ == '__main__':
    main()
