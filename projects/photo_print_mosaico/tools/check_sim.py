"""Exercise the running native UI via the simulator's documented pointer API."""

import asyncio
import json
import os
import struct
import time
from pathlib import Path

from PIL import Image
from websockets.asyncio.client import connect

EVIDENCE = Path(__file__).resolve().parents[3] / "evidence"
STATE = Path(os.environ["PHOTO_SIM_STATE"])


async def main():
    EVIDENCE.mkdir(exist_ok=True)
    async with connect("ws://127.0.0.1:3222/ws", max_size=4000000) as ws:
        pending = {}
        image = Image.new("RGB", (480, 480))
        sequence = 0

        async def receive():
            async for msg in ws:
                if isinstance(msg, str):
                    obj = json.loads(msg)
                    if obj.get("id") in pending:
                        pending.pop(obj["id"]).set_result(obj)
                    continue
                if msg[:4] != b"GSPF":
                    continue
                fmt = msg[5]
                rect_count, payload_size = struct.unpack_from("<II", msg, 28)
                offset = 36 + rect_count * 16
                for i in range(rect_count):
                    x1, y1, x2, y2 = struct.unpack_from("<iiii", msg, 36 + i * 16)
                    w, h = x2 - x1, y2 - y1
                    size = w * h * (2 if fmt == 0 else 3)
                    pixels = msg[offset : offset + size]
                    offset += size
                    if fmt == 0:
                        rgb = bytearray(w * h * 3)
                        for n, (value,) in enumerate(struct.iter_unpack("<H", pixels)):
                            rgb[n * 3 : n * 3 + 3] = bytes(
                                (
                                    ((value >> 11) & 31) * 255 // 31,
                                    ((value >> 5) & 63) * 255 // 63,
                                    (value & 31) * 255 // 31,
                                )
                            )
                        region = Image.frombytes("RGB", (w, h), bytes(rgb))
                    else:
                        region = Image.frombytes("RGB", (w, h), pixels, "raw", "BGR")
                    image.paste(region, (x1, y1))

        reader = asyncio.create_task(receive())

        async def rpc(method, params=None):
            nonlocal sequence
            sequence += 1
            future = asyncio.get_running_loop().create_future()
            pending[sequence] = future
            await ws.send(json.dumps(dict(jsonrpc="2.0", id=sequence, method=method, params=params or {})))
            result = await asyncio.wait_for(future, 5)
            if "error" in result:
                raise RuntimeError(result)
            return result.get("result")

        async def tap(x, y, delay=0.6):
            await rpc("feed_pointer", dict(x=x, y=y, pressed=True))
            await asyncio.sleep(0.08)
            await rpc("feed_pointer", dict(x=x, y=y, pressed=False))
            await asyncio.sleep(delay)

        async def capture(name):
            await asyncio.sleep(0.25)
            image.save(EVIDENCE / f"photo-sim-{name}.png")
            print(name, flush=True)

        def expect(mode, prints=None, filter_id=None):
            values=[]
            for _ in range(20):
                try: values=list(map(int,Path(str(STATE)+".result").read_text().split()))
                except (OSError,ValueError): values=[]
                if len(values)==8 and values[0]==mode:break
                time.sleep(0.03)
            assert values[0]==mode,(mode,values)
            if prints is not None:assert values[1]==prints,(prints,values)
            if filter_id is not None:assert values[4]==filter_id,(filter_id,values)
            return values

        caps=await rpc("capabilities")
        print("Native backend active, runtime",caps["runtime_version"],flush=True)
        STATE.write_text("online",encoding="ascii")
        await asyncio.sleep(0.5)
        await capture("ccd-live");expect(1,0,0)
        await tap(350,457);expect(7,0,0)
        for index in range(5):
            await capture(f"filter-{index}");expect(7,0,index)
            if index<4:await tap(350,457)
        await tap(350,457,0.08);await tap(350,457,0.2)
        expect(1,0,4)
        await tap(110,457);await capture("ccd-review");expect(2,0,4)
        STATE.write_text("offline",encoding="ascii");await asyncio.sleep(0.2)
        await tap(110,457);expect(2,0)
        STATE.write_text("online",encoding="ascii");await asyncio.sleep(0.2)
        await tap(110,457,0.08);await tap(110,457,0.2);expect(3,1)
        await asyncio.sleep(1.8);expect(4,1)
        STATE.write_text("fail",encoding="ascii");await asyncio.sleep(0.2)
        await tap(110,457);await asyncio.sleep(1.9);expect(5,2)
        await capture("ccd-failed-photo-kept")
        await tap(350,457);expect(1,2)
        STATE.write_text("online palm",encoding="ascii")
        await asyncio.sleep(1.0);v=expect(8,2);base_beeps=v[6]-1
        await capture("ccd-countdown")
        await asyncio.sleep(4.9);v=expect(2,2);assert v[6]-base_beeps==5,v
        await tap(350,457);await asyncio.sleep(1.2);expect(1,2)
        STATE.write_text("online",encoding="ascii");await asyncio.sleep(1.0)
        STATE.write_text("online palm",encoding="ascii");await asyncio.sleep(1.0);expect(8,2)
        await tap(350,457);await asyncio.sleep(1.0);expect(1,2)
        STATE.write_text("online",encoding="ascii")
        await capture("ccd-ready")
        print("PASS: all filters, double click preserves selection, preview/print/retry, palm countdown/rearm/cancel",flush=True)
        reader.cancel()


if __name__ == "__main__":
    asyncio.run(main())
