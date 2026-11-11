Import("env")
import os
import subprocess

def merge_bin(source, target, env):
    firmware = str(target[0])
    board = env.BoardConfig()
    mcu = board.get("build.mcu", "esp32")
    flash_mode = env.GetProjectOption("board_build.flash_mode", "dio")
    flash_freq = board.get("build.f_flash", "40000000L").replace("L", "")
    flash_freq_m = str(int(int(flash_freq) / 1000000)) + "m"
    flash_size = board.get("upload.flash_size", "4MB")

    esptool = os.path.join(
        (env.PioPlatform().get_package_dir("tool-esptoolpy") or "") if "tool-esptoolpy" in env.PioPlatform().packages else "",
        "esptool.py",
    )
    output = os.path.join(env.subst("$BUILD_DIR"), "firmware_merged.bin")

    extra = env.get("FLASH_EXTRA_IMAGES", [])
    print("=== FLASH_EXTRA_IMAGES ===")
    for offset, path in extra:
        print(f"  {offset} -> {env.subst(path)}")
    print(f"  APP -> 0x10000 {firmware}")
    print("==========================")

    # 新版烧录工具是独立程序；按开发工具给出的地址合并各部分，生成从 0x0 写入的完整固件。
    modern = env.PioPlatform().get_package_dir("tool-esptool") if "tool-esptool" in env.PioPlatform().packages else None
    if modern and not os.path.isfile(esptool):
        exe = os.path.join(modern, "esptool.exe" if os.name == "nt" else "esptool")
        args = [exe, "--chip", mcu, "merge-bin", "-o", output]
        for offset, path in extra:
            args.extend([str(offset), env.subst(path)])
        args.extend(["0x10000", firmware])
        subprocess.run(args, check=True)
        return

    cmd = [
        "$PYTHONEXE", esptool,
        "--chip", mcu,
        "merge_bin",
        "--flash_mode", flash_mode,
        "--flash_freq", flash_freq_m,
        "--flash_size", flash_size,
        "-o", output,
    ]

    for offset, path in extra:
        cmd.extend([offset, env.subst(path)])

    cmd.extend(["0x10000", firmware])

    env.Execute(env.VerboseAction(" ".join(cmd), "Merging firmware into single binary"))

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_bin)
