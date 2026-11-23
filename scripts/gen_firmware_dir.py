"""Post-build script: emit M5Burner-ready `firmware/` dir + refresh version.

Wired as: extra_scripts = post:scripts/gen_firmware_dir.py
Also available as: pio run -t firmware

M5Burner rule (m5stack/M5Stack-Firmware README):
  filename = <name>_<flash address>.bin
"""

import json
import os
import shutil
import subprocess

Import("env")  # noqa: F821

PROJECT_DIR = env.Dir("$PROJECT_DIR").abspath  # noqa: F821
BUILD_DIR = env.Dir("$BUILD_DIR").abspath  # noqa: F821
FW_DIR = os.path.join(PROJECT_DIR, "firmware")
MANIFEST = os.path.join(PROJECT_DIR, "m5burner.json")


def boot_app0_path():
    try:
        framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")  # noqa: F821
        if framework_dir:
            candidate = os.path.join(framework_dir, "tools", "partitions", "boot_app0.bin")
            if os.path.isfile(candidate):
                return candidate
    except Exception:
        pass
    fallback = os.path.expanduser(
        "~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
    )
    return fallback


# Offsets match the esptool invocation for esp32-s3-devkitc-1 / default_8MB.
IMAGES = [
    ("bootloader", os.path.join(BUILD_DIR, "bootloader.bin"), "0x0"),
    ("partitions", os.path.join(BUILD_DIR, "partitions.bin"), "0x8000"),
    ("boot_app0", boot_app0_path(), "0xe000"),
    ("firmware", os.path.join(BUILD_DIR, "firmware.bin"), "0x10000"),
]


def git_version():
    try:
        tag = (
            subprocess.check_output(
                ["git", "describe", "--tags", "--abbrev=0"], cwd=PROJECT_DIR
            )
            .decode()
            .strip()
        )
        return tag.lstrip("v") or "0.0.0"
    except Exception:
        return None


def emit_firmware_dir(*_args, **_kwargs):
    # Only the release env may refresh firmware/ (M5Burner catalog bins).
    # Screenshot / debug builds carry S3_SCREENSHOT_BRIDGE and must not overwrite it.
    pioenv = env.get("PIOENV", "")  # noqa: F821
    if pioenv and pioenv != "m5stack-sticks3":
        print("[gen_firmware_dir] skip (env=%s, only m5stack-sticks3 writes firmware/)" % pioenv)
        return

    missing = [src for _, src, _ in IMAGES if not os.path.isfile(src)]
    if missing:
        print("[gen_firmware_dir] missing images, run a build first:")
        for m in missing:
            print("   ", m)
        return

    os.makedirs(FW_DIR, exist_ok=True)
    for name, src, addr in IMAGES:
        dst = os.path.join(FW_DIR, "%s_%s.bin" % (name, addr))
        shutil.copyfile(src, dst)
        print(
            "[gen_firmware_dir] %s (%d bytes)"
            % (os.path.basename(dst), os.path.getsize(dst))
        )

    if os.path.isfile(MANIFEST):
        with open(MANIFEST) as fh:
            manifest = json.load(fh)
        version = git_version()
        if version and manifest.get("version") != version:
            manifest["version"] = version
            with open(MANIFEST, "w") as fh:
                json.dump(manifest, fh, indent=2, ensure_ascii=False)
                fh.write("\n")
            print("[gen_firmware_dir] m5burner.json version ->", version)


# Custom target: pio run -t firmware
env.AddCustomTarget(  # noqa: F821
    name="firmware",
    dependencies="$BUILD_DIR/${PROGNAME}.bin",
    actions=[emit_firmware_dir],
    title="Generate M5Burner firmware/",
    description="Copy flash images into firmware/ with address suffixes",
)

# Also run after every successful build of this env.
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", env.VerboseAction(emit_firmware_dir, "Generating firmware/"))  # noqa: F821
