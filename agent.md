# Agent Notes

## Flashing

We flash/upload firmware using the **PlatformIO VS Code extension** (PlatformIO: Upload), or CLI:

- Build (release): `pio run -e m5stack-sticks3`
- Build (screenshots): `pio run -e screenshot`
- Upload/Flash: PlatformIO: Upload / `pio run -e <env> -t upload`
- Serial monitor: PlatformIO: Monitor (115200)

StickS3 native USB: hold **power** until green LED blinks → download mode (`303a:1001`), then flash with `--before no_reset` if auto-reset fails. Short-click power to leave bootloader.

## M5Burner prep

- `m5burner.json` + `firmware/` are required for the official catalog PR.
- Post-build script `scripts/gen_firmware_dir.py` refreshes `firmware/` after every successful build.
- Screenshot bridge is `#ifdef S3_SCREENSHOT_BRIDGE` only (env `screenshot`). Release env has it off.
