# 直接使用已生成的 800 × 480 黑白位图。普通编译不需要安装字体或 Pillow。
Import("env")
from pathlib import Path
import lzma

folder = Path(env.subst("$BUILD_DIR")) / "desk_assets"
folder.mkdir(parents=True, exist_ok=True)
for name, target_name in [("art", "desk_art"), ("custom", "desk_custom")]:
    source = Path(env.subst("$PROJECT_DIR")) / "desk" / (name + ".inc.xz")
    target = folder / (target_name + ".inc")
    data = lzma.decompress(source.read_bytes())
    if not target.exists() or target.read_bytes() != data:
        target.write_bytes(data)
env.Append(CPPPATH=[str(folder)])
