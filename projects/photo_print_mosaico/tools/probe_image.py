"""Send a local image to the device's bounded RAM probe, only via mosaico.py.

Requires Pillow. Does not persist the source image or transmit it to a cloud.
The device runs its normal detector/classifier/countdown on this frame for 10s.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

from PIL import Image, ImageOps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--python", default=sys.executable, help="Python for mosaico.py")
    parser.add_argument("--device-id", required=True)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[3]
    with Image.open(args.image) as source:
        frame = ImageOps.pad(ImageOps.exif_transpose(source).convert("L"),
                             (384, 384), method=Image.Resampling.LANCZOS, color=255)
        pixels = frame.tobytes()

    def rpc(payload):
        command = [args.python, "mosaico.py", "iris", "rpc", "0x5048", "2",
                   "--project", "projects/photo_print_mosaico", "--device-id", args.device_id,
                   "--payload-hex", payload.hex(), "--json"]
        result = subprocess.run(command, cwd=workspace, capture_output=True,
                                text=True, encoding="utf-8", timeout=60)
        if result.returncode:
            raise RuntimeError(result.stderr[-1000:] or result.stdout[-1000:])
        obj = json.loads(result.stdout)
        if obj.get("ok") is False:
            raise RuntimeError(str(obj)[-1000:])
        return obj

    print("Uploading padded grayscale frame: 384x384, 147456 bytes", flush=True)
    print("Begin:", json.dumps(rpc(b"\0")), flush=True)
    try:
        # The public RPC envelope is limited to 1024 bytes including our header.
        for offset in range(0, len(pixels), 1000):
            rpc(b"\1" + struct.pack("<I", offset) + pixels[offset:offset + 1000])
            if offset % 10000 == 0:
                print(f"Uploaded {min(offset + 1000, len(pixels))}/{len(pixels)}", flush=True)
        print("Run:", json.dumps(rpc(b"\2")), flush=True)
    except BaseException:
        try:
            rpc(b"\3")
        except Exception:
            pass
        raise


if __name__ == "__main__":
    main()
