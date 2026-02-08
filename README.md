# StickS3 Axes & Echo

StickS3 Axes & Echo is a small, reliable diagnostic firmware for the **M5Stack StickS3 (ESP32‑S3)**. It visualizes the **IMU accelerometer axes** on the LCD and lets you **record and play back audio** using the built-in mic/speaker.

StickS3 Axes & Echo lives in this repository because it is the “bring-up / proving-ground” firmware used to validate UI, IMU orientation, and the audio pipeline before building the actual auto-watering UI.

## What it does

- **IMU axes viewer (portrait UI):** draws X/Y/Z axes (RGB) plus the normalized acceleration vector `a` (yellow), with numeric readouts (`ax/ay/az`, magnitude, and `atan2` angles).
- **Audio push-to-record + playback:** hold **KEY2** to record, release to play back.
- **Flicker-free rendering:** draws into sprites (double-buffer style) and blits to the display.
- **Stable in practice:** designed to keep UI updates throttled and avoid audio/codec conflicts; in normal use it should run without hangs.

## Controls (on-device buttons)

- **KEY1 / BtnA**
  - Click: cycle through a 16-color background palette
  - Tone: plays a short tone per color (black is silent)
  - Hold (~650ms): calibrate IMU “zero/level” to the current pose

- **KEY2 / BtnB**
  - Press + hold: start recording (a short beep plays first)
  - Release: play back the recorded audio
  - If you reach the buffer limit while still holding, the UI asks you to release to play.

## UI modes

- **Normal (portrait):** IMU axes + vector + text readouts.
- **Status screens (landscape):** RECORD / HOLD / PLAY / ERROR screens with a small footer showing mic/speaker/buffer status.

## Audio implementation notes (important)

The StickS3 audio path uses a shared codec (ES8311). Switching between mic and speaker requires explicit ordering to avoid “no audio” states.

In [src/main.cpp](src/main.cpp), the helpers `ensureSpeakerOn()`, `ensureSpeakerOff()`, and `ensureMicOff()` are intentionally used whenever transitioning between recording and playback.

Recording details:
- Sample rate: **16 kHz**, mono
- Buffer: allocated at runtime, **PSRAM preferred** (`ps_malloc`), then heap fallback
- Codec: **IMA ADPCM** encode/decode stored in RAM (used to validate the pipeline; playback uses `M5.Speaker.playRaw()`)

## Build / Upload (VS Code PlatformIO)

This project is meant to be built with the **PlatformIO VS Code extension** (not the terminal CLI).

- Environment: `m5stack-sticks3` (see [platformio.ini](platformio.ini))
- Upload port: `/dev/ttyACM0` (configured in [platformio.ini](platformio.ini))

Use VS Code commands:
- `PlatformIO: Build`
- `PlatformIO: Upload`
- `PlatformIO: Monitor`
- If something gets stuck: `PlatformIO: Clean` then `PlatformIO: Upload`

## Repo map

- Main firmware: [src/main.cpp](src/main.cpp)
- PlatformIO config / deps: [platformio.ini](platformio.ini)

## Optional: secrets

There is a template for Wi‑Fi credentials at [include/secrets.example.h](include/secrets.example.h) (copy to `include/secrets.h`). The current demo firmware does not require Wi‑Fi, but the repo keeps this in place for future work.
