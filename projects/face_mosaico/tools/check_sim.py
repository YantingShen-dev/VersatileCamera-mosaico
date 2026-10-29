"""Exercise the running native UI via the simulator's documented pointer API."""

import asyncio
import json
import struct
from pathlib import Path

from PIL import Image
from websockets.asyncio.client import connect

EVIDENCE = Path(__file__).resolve().parents[3] / "evidence"
STATE = EVIDENCE / "face-sim-input.txt"


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

        async def tap(x, y):
            await rpc("feed_pointer", dict(x=x, y=y, pressed=True))
            await asyncio.sleep(0.08)
            await rpc("feed_pointer", dict(x=x, y=y, pressed=False))
            await asyncio.sleep(0.6)

        async def capture(name):
            await asyncio.sleep(0.25)
            image.save(EVIDENCE / f"face-sim-{name}.png")
            print(name, flush=True)

        caps = await rpc("capabilities")
        print("Native backend active, runtime", caps["runtime_version"], flush=True)
        STATE.write_text("unknown", encoding="ascii")
        await asyncio.sleep(0.5)
        await capture("initial")
        await tap(160, 438)
        await capture("enrolled")
        await tap(380, 438)
        await capture("manager")
        await tap(405, 132)
        await capture("confirm")
        await tap(110, 380)
        await capture("cancel-keeps-record")
        await tap(405, 132)
        await tap(350, 380)
        await capture("deleted")
        await tap(240, 438)
        STATE.write_text("no_face", encoding="ascii")
        await asyncio.sleep(2.5)
        await tap(160, 438)
        await capture("reject-no-face")
        STATE.write_text("multiple", encoding="ascii")
        await asyncio.sleep(2.5)
        await tap(160, 438)
        await capture("reject-multiple")
        STATE.write_text("unknown", encoding="ascii")
        reader.cancel()


if __name__ == "__main__":
    asyncio.run(main())
