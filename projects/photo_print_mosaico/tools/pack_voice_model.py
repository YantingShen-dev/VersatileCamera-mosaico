"""Pack pinned ESP-SR Chinese MultiNet7 into app rodata; retain partition layout."""
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile

sr, output = Path(sys.argv[1]), Path(sys.argv[2])
spec = importlib.util.spec_from_file_location("sr_pack", sr / "model/pack_model.py")
module = importlib.util.module_from_spec(spec)
sys.dont_write_bytecode = True
spec.loader.exec_module(module)
output.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix="voice-model-", dir=output.parent) as tmp:
    root = Path(tmp)
    shutil.copytree(sr / "model/multinet_model/mn7_cn", root / "mn7_cn")
    (root / "fst").mkdir()
    (root / "fst/commands_cn.txt").write_text("1,zhun bei pai zhao\n", encoding="ascii")
    module.pack_models(str(root), str(output.resolve()))
print(f"Embedded Chinese command model: {output.stat().st_size} bytes")
