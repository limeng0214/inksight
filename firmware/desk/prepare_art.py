# 直接使用已生成的 800 × 480 黑白位图。普通编译不需要安装字体或 Pillow。
Import("env")
from pathlib import Path
import lzma

source = Path(env.subst("$PROJECT_DIR")) / "desk" / "art.inc.xz"
folder = Path(env.subst("$BUILD_DIR")) / "desk_assets"
folder.mkdir(parents=True, exist_ok=True)
target = folder / "desk_art.inc"
data = lzma.decompress(source.read_bytes())
if not target.exists() or target.read_bytes() != data:
    target.write_bytes(data)
env.Append(CPPPATH=[str(folder)])
