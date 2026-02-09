#include <Arduino.h>
#include <M5Unified.h>

#include <vector>

static constexpr uint16_t kBgPalette16[] = {
  TFT_BLACK,
  TFT_NAVY,
  TFT_DARKGREEN,
  TFT_DARKCYAN,
  TFT_MAROON,
  TFT_PURPLE,
  TFT_OLIVE,
  TFT_LIGHTGREY,
  TFT_DARKGREY,
  TFT_BLUE,
  TFT_GREEN,
  TFT_CYAN,
  TFT_RED,
  TFT_MAGENTA,
  TFT_YELLOW,
  TFT_WHITE,
};
static constexpr size_t kBgPaletteCount = sizeof(kBgPalette16) / sizeof(kBgPalette16[0]);
static uint8_t bgIndex = 0;
static uint16_t bgColor = TFT_BLACK;

// One tone per color index; index 0 (black) is silent.
static constexpr float kToneHz16[] = {
  0.0f,       // black (silent)
  261.63f,    // C4
  293.66f,    // D4
  329.63f,    // E4
  349.23f,    // F4
  392.00f,    // G4
  440.00f,    // A4
  493.88f,    // B4
  523.25f,    // C5
  587.33f,    // D5
  659.25f,    // E5
  698.46f,    // F5
  783.99f,    // G5
  880.00f,    // A5
  987.77f,    // B5
  1046.50f,   // C6
};
static_assert(sizeof(kToneHz16) / sizeof(kToneHz16[0]) == kBgPaletteCount, "Tone palette must match color palette");

static lgfx::LGFX_Sprite frameSpritePortrait;
static lgfx::LGFX_Sprite frameSpriteLandscape;
static uint8_t gDisplayRotation = 255; // unknown; 0=portrait, 1=landscape
static bool gSkipNextBtnAClick = false;
static bool imuOk = false;
static constexpr uint8_t kPortraitRotation = 0;
static constexpr uint8_t kStatusRotation = 1;

static void setDisplayRotation(uint8_t rot) {
  if (gDisplayRotation == rot) {
    return;
  }
  gDisplayRotation = rot;
  M5.Display.setRotation(rot);
}

static void drawArrow2D(lgfx::LGFX_Sprite& s, int x0, int y0, int x1, int y1, uint16_t color) {
  s.drawLine(x0, y0, x1, y1, color);

  const float dx = (float)(x1 - x0);
  const float dy = (float)(y1 - y0);
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 6.0f) {
    return;
  }

  const float ux = dx / len;
  const float uy = dy / len;
  const float headLen = 10.0f;
  const float headW = 6.0f;

  const float bx = (float)x1 - ux * headLen;
  const float by = (float)y1 - uy * headLen;

  const float px = -uy;
  const float py = ux;

  const int lx = (int)lroundf(bx + px * headW);
  const int ly = (int)lroundf(by + py * headW);
  const int rx = (int)lroundf(bx - px * headW);
  const int ry = (int)lroundf(by - py * headW);

  s.fillTriangle(x1, y1, lx, ly, rx, ry, color);
}

static void drawAxesScreen(float ax, float ay, float az) {
  // Normal UI uses portrait.
  setDisplayRotation(kPortraitRotation);

  auto& s = frameSpritePortrait;
  s.fillScreen(bgColor);

  const float norm = sqrtf(ax * ax + ay * ay + az * az);
  const float inv = (norm > 1e-6f) ? (1.0f / norm) : 1.0f;
  const float nx = ax * inv;
  const float ny = ay * inv;
  const float nz = az * inv;

  const float angXY = atan2f(ay, ax) * (180.0f / PI);
  const float angXZ = atan2f(az, ax) * (180.0f / PI);
  const float angYZ = atan2f(az, ay) * (180.0f / PI);

  // Pseudo-3D basis vectors on screen.
  // X: right, Y: up, Z: down-right (gives a simple 3D feel).
  const float bxX = 1.0f, byX = 0.0f;
  const float bxY = 0.0f, byY = -1.0f;
  const float bxZ = 0.70f, byZ = 0.70f;

  auto map3 = [&](float x, float y, float z, float scale, float& outX, float& outY) {
    outX = (x * bxX + y * bxY + z * bxZ) * scale;
    outY = (x * byX + y * byY + z * byZ) * scale;
  };

  const int cx = s.width() / 2;
  const int cy = s.height() / 2 + 10;
  const float axisLen = (float)std::min<int>(s.width(), s.height()) * 0.28f;

  // Axes
  float dx = 0.0f, dy = 0.0f;
  map3(1, 0, 0, axisLen, dx, dy);
  const int xEnd = cx + (int)lroundf(dx);
  const int yEnd = cy + (int)lroundf(dy);
  drawArrow2D(s, cx, cy, xEnd, yEnd, TFT_RED);
  s.setTextDatum(middle_left);
  s.setTextColor(TFT_RED, bgColor);
  s.drawString("X", xEnd + 6, yEnd);

  map3(0, 1, 0, axisLen, dx, dy);
  const int xEndY = cx + (int)lroundf(dx);
  const int yEndY = cy + (int)lroundf(dy);
  drawArrow2D(s, cx, cy, xEndY, yEndY, TFT_GREEN);
  s.setTextColor(TFT_GREEN, bgColor);
  s.drawString("Y", xEndY + 6, yEndY);

  map3(0, 0, 1, axisLen, dx, dy);
  const int xEndZ = cx + (int)lroundf(dx);
  const int yEndZ = cy + (int)lroundf(dy);
  drawArrow2D(s, cx, cy, xEndZ, yEndZ, TFT_BLUE);
  s.setTextColor(TFT_BLUE, bgColor);
  s.drawString("Z", xEndZ + 6, yEndZ);

  // Acceleration vector (normalized), drawn in the same pseudo-3D basis.
  map3(nx, ny, nz, axisLen, dx, dy);
  const int xAcc = cx + (int)lroundf(dx);
  const int yAcc = cy + (int)lroundf(dy);
  drawArrow2D(s, cx, cy, xAcc, yAcc, TFT_YELLOW);
  s.setTextColor(TFT_YELLOW, bgColor);
  s.setTextDatum(middle_center);
  s.drawString("a", xAcc, yAcc - 10);

  // Text readout
  s.setTextDatum(top_left);
  s.setTextSize(1);
  s.setTextColor(TFT_WHITE, bgColor);
  char line[96];
  snprintf(line, sizeof(line), "ax:% .3f  ay:% .3f  az:% .3f", ax, ay, az);
  s.drawString(line, 6, 6);
  snprintf(line, sizeof(line), "|a|:% .3f   nx:% .2f ny:% .2f nz:% .2f", norm, nx, ny, nz);
  s.drawString(line, 6, 20);
  snprintf(line, sizeof(line), "atan2(ay,ax):% .1f deg", angXY);
  s.drawString(line, 6, 34);
  snprintf(line, sizeof(line), "atan2(az,ax):% .1f deg", angXZ);
  s.drawString(line, 6, 48);
  snprintf(line, sizeof(line), "atan2(az,ay):% .1f deg", angYZ);
  s.drawString(line, 6, 62);

  s.setTextColor(TFT_LIGHTGREY, bgColor);

  // Status row: uptime (left) + battery (right).
  const uint32_t upSec = millis() / 1000u;
  const uint32_t upMin = upSec / 60u;
  const uint32_t upHr = upMin / 60u;
  const uint32_t upDispMin = upMin % 60u;
  const uint32_t upDispSec = upSec % 60u;
  char upLine[32];
  snprintf(upLine, sizeof(upLine), "up %lu:%02lu:%02lu", (unsigned long)upHr, (unsigned long)upDispMin, (unsigned long)upDispSec);

  int batt = (int)M5.Power.getBatteryLevel();
  const int16_t battMv = M5.Power.getBatteryVoltage();
  char battLine[24];
  if (batt < 0 || battMv <= 0) {
    snprintf(battLine, sizeof(battLine), "bat --%%");
  } else {
    if (batt > 100) batt = 100;
    if (batt < 0) batt = 0;
    snprintf(battLine, sizeof(battLine), "bat %d%%", batt);
  }

  s.setTextDatum(top_left);
  s.drawString(upLine, 6, s.height() - 38);
  s.setTextDatum(top_right);
  s.drawString(battLine, s.width() - 6, s.height() - 38);

  s.setTextDatum(top_left);
  s.drawString("KEY1: color   HOLD KEY1: PLAY", 6, s.height() - 26);
  s.drawString("KEY2: hold rec / release play", 6, s.height() - 14);

  M5.Display.startWrite();
  s.pushSprite(&M5.Display, 0, 0);
  M5.Display.endWrite();
}

// --- Audio record/playback (KEY2 = M5.BtnB) ---
static constexpr uint32_t kRecSampleRateHz = 16000;
static constexpr size_t kRecChunkSamples = 512;
static constexpr uint8_t kMasterVolume = static_cast<uint8_t>(255 * 0.70f);

// Recording buffer is sized at runtime (PSRAM/heap). These are the computed limits.
static uint32_t gRecMaxMs = 3000;
static size_t gRecMaxSamples = (kRecSampleRateHz * 3000) / 1000;

static int16_t* gRecPcm = nullptr;
static size_t gRecSamples = 0;
static bool gRecReadyWaitRelease = false;
static bool gRecActive = false;
static std::vector<uint8_t> gRecAdpcm;
static bool gRecStartRequested = false;
static bool gPlayActive = false;

static constexpr size_t kRecSpectrumBins = 16;
static uint8_t gRecSpectrum[kRecSpectrumBins] = {0};
static float gRecSpectrumSmooth[kRecSpectrumBins] = {0.0f};
static uint32_t gPlayStartMs = 0;

static bool gRecMetricsValid = false;
static float gRecRmsDbfs = -99.9f;
static float gRecPeakDbfs = -99.9f;
static float gRecClipPercent = 0.0f;

enum class UiMode : uint8_t {
  Normal = 0,
  RecordBeep,
  Recording,
  HoldMaxRelease,
  Playing,
  Error,
};

static UiMode gUiMode = UiMode::Normal;
static uint32_t gRecStartMs = 0;
static uint32_t gUiLastDrawMs = 0;
static const char* gLastError = nullptr;

static void ensureSpeakerOn() {
  if (!M5.Speaker.isEnabled()) {
    return;
  }
  if (!M5.Speaker.isRunning()) {
    (void)M5.Speaker.begin();
  }
  M5.Speaker.setVolume(kMasterVolume);
}

static void ensureSpeakerOff() {
  if (!M5.Speaker.isEnabled()) {
    return;
  }
  if (M5.Speaker.isRunning()) {
    M5.Speaker.stop();
    // Give the speaker task a moment to drain/stop.
    uint32_t t0 = millis();
    while (M5.Speaker.isPlaying() && (millis() - t0) < 200) {
      M5.update();
      delay(1);
    }
    M5.Speaker.end();
  }
}

static void ensureMicOff() {
  if (M5.Mic.isRunning()) {
    M5.Mic.end();
  }
}

static void playToneIfEnabled(float hz, uint16_t ms, bool wait) {
  if (!M5.Speaker.isEnabled() || hz <= 0.0f) {
    return;
  }
  ensureSpeakerOn();
  (void)M5.Speaker.tone(hz, ms);
  if (!wait) {
    return;
  }
  const uint32_t t0 = millis();
  const uint32_t timeout = ms + 190;
  while (M5.Speaker.isPlaying() && (millis() - t0) < timeout) {
    M5.update();
    delay(1);
  }
}

static bool imaAdpcmEncodeBuffer(const int16_t* pcm, size_t samples, std::vector<uint8_t>& out);
static bool imaAdpcmDecodeToBuffer(const std::vector<uint8_t>& in, int16_t* pcmOut, size_t samples);

static bool startPlayback() {
  if (gRecSamples == 0 || !M5.Speaker.isEnabled()) {
    return false;
  }
  (void)imaAdpcmEncodeBuffer(gRecPcm, gRecSamples, gRecAdpcm);
  (void)imaAdpcmDecodeToBuffer(gRecAdpcm, gRecPcm, gRecSamples);
  (void)M5.Speaker.playRaw(gRecPcm, gRecSamples, kRecSampleRateHz, false, 1, -1, true);
  gPlayStartMs = millis();
  gPlayActive = true;
  gUiMode = UiMode::Playing;
  return true;
}

static bool shouldDrawStatus(uint32_t now, uint32_t intervalMs) {
  if (now - gUiLastDrawMs <= intervalMs) {
    return false;
  }
  gUiLastDrawMs = now;
  return true;
}

static void drawImuDisabledScreen() {
  setDisplayRotation(kPortraitRotation);
  frameSpritePortrait.fillScreen(bgColor);
  frameSpritePortrait.setTextDatum(middle_center);
  frameSpritePortrait.setTextColor(TFT_WHITE, bgColor);
  frameSpritePortrait.drawString("IMU disabled", frameSpritePortrait.width() / 2, frameSpritePortrait.height() / 2);
  M5.Display.startWrite();
  frameSpritePortrait.pushSprite(&M5.Display, 0, 0);
  M5.Display.endWrite();
}

static void drawSpectrumBarsVertical(lgfx::LGFX_Sprite& s, int x, int y, int w, int h, const uint8_t* bins, size_t binCount, uint16_t barColor, uint16_t bg) {
  if (bins == nullptr || binCount == 0 || w <= 0 || h <= 0) {
    return;
  }

  // Frame + clear area to current UI background.
  s.drawRect(x, y, w, h, TFT_DARKGREY);
  s.fillRect(x + 1, y + 1, w - 2, h - 2, bg);

  const int innerW = w - 2;
  const int innerH = h - 2;
  if (innerW <= 0 || innerH <= 0) {
    return;
  }

  // Bars fill the full available width (no gaps). Distribute remainder pixels.
  const int baseW = innerW / (int)binCount;
  const int rem = innerW % (int)binCount;
  if (baseW <= 0) {
    return;
  }

  int bx = x + 1;
  for (size_t i = 0; i < binCount; ++i) {
    const int bw = baseW + ((int)i < rem ? 1 : 0);
    const float v = std::min(100.0f, (float)bins[i]);
    const int bh = (int)lroundf((v / 100.0f) * (float)innerH);
    if (bh > 0) {
      const int by = (y + 1) + (innerH - bh);
      s.fillRect(bx, by, bw, bh, barColor);
    }
    bx += bw;
  }
}

static void drawStatusScreen(const char* title, const char* line1, const char* line2, uint16_t accent, const uint8_t* spectrumBins = nullptr, size_t spectrumCount = 0) {
  // Status UI is displayed in landscape.
  setDisplayRotation(kStatusRotation);
  auto& frameSprite = frameSpriteLandscape;

  frameSprite.fillScreen(bgColor);

  frameSprite.setTextDatum(top_left);
  frameSprite.setTextColor(TFT_WHITE, bgColor);
  frameSprite.setTextSize(2);
  frameSprite.drawString(title, 8, 8);

  // Accent line
  frameSprite.fillRect(0, 34, frameSprite.width(), 4, accent);

  frameSprite.setTextSize(1);
  frameSprite.setTextColor(TFT_LIGHTGREY, bgColor);
  frameSprite.drawString(line1 ? line1 : "", 8, 44);
  frameSprite.drawString(line2 ? line2 : "", 8, 60);

  // Optional: spectrum
  if (spectrumBins != nullptr && spectrumCount > 0) {
    const int barX = 8;
    const int barY = 80;
    const int barW = frameSprite.width() - 16;
    const int barH = std::max(16, frameSprite.height() - barY - 28);
    drawSpectrumBarsVertical(frameSprite, barX, barY, barW, barH, spectrumBins, spectrumCount, accent, bgColor);
  }

  // Footer: buffer/mic/speaker quick status
  char footer[96];
  snprintf(footer, sizeof(footer), "Mic:%s  Spk:%s  Buf:%s", M5.Mic.isEnabled() ? "ON" : "OFF", M5.Speaker.isEnabled() ? "ON" : "OFF", gRecPcm ? "OK" : "NO");
  frameSprite.setTextColor(TFT_DARKGREY, bgColor);
  frameSprite.drawString(footer, 8, frameSprite.height() - 14);

  M5.Display.startWrite();
  frameSprite.pushSprite(&M5.Display, 0, 0);
  M5.Display.endWrite();
}

static void computeSpectrumFromPcmWindow(const int16_t* pcm, size_t totalSamples, size_t windowEndSample) {
  if (pcm == nullptr || totalSamples == 0) {
    return;
  }
  if (windowEndSample > totalSamples) {
    windowEndSample = totalSamples;
  }

  // Voice-focused centers in Hz (approx. 200..4000 Hz).
  static constexpr float kCentersHz[kRecSpectrumBins] = {
    200.0f, 250.0f, 315.0f, 400.0f,
    500.0f, 630.0f, 800.0f, 1000.0f,
    1250.0f, 1600.0f, 2000.0f, 2500.0f,
    2800.0f, 3150.0f, 3550.0f, 4000.0f,
  };
  static constexpr size_t kN = 256;

  const size_t N = (windowEndSample >= kN) ? kN : windowEndSample;
  if (N < 32) {
    return;
  }
  const int16_t* x = pcm + (windowEndSample - N);

  auto window = [&](size_t n) -> float {
    const float a = 2.0f * PI * (float)n / (float)(N - 1);
    return 0.5f - 0.5f * cosf(a);
  };

  float raw[kRecSpectrumBins];
  for (size_t bi = 0; bi < kRecSpectrumBins; ++bi) {
    const float f = kCentersHz[bi];
    int k = (int)lroundf((f * (float)N) / (float)kRecSampleRateHz);
    if (k < 1) k = 1;
    if (k > (int)N / 2 - 1) k = (int)N / 2 - 1;
    const float w = 2.0f * PI * (float)k / (float)N;
    const float coeff = 2.0f * cosf(w);

    float q0 = 0.0f;
    float q1 = 0.0f;
    float q2 = 0.0f;
    for (size_t n = 0; n < N; ++n) {
      const float s = ((float)x[n] / 32768.0f) * window(n);
      q0 = coeff * q1 - q2 + s;
      q2 = q1;
      q1 = q0;
    }

    const float power = (q1 * q1 + q2 * q2 - coeff * q1 * q2);
    raw[bi] = power;
  }

  const float fullScaleMag = (float)N * 0.5f;
  for (size_t i = 0; i < kRecSpectrumBins; ++i) {
    const float mag = sqrtf(std::max(1e-12f, raw[i]));
    float a = mag / fullScaleMag;
    if (a > 1.0f) a = 1.0f;
    const float db = 20.0f * log10f(std::max(1e-6f, a));

    const float dbMin = -72.0f;
    const float dbMax = -12.0f;
    float v = (db - dbMin) / (dbMax - dbMin);
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    const float attack = 0.40f;
    const float decay = 0.92f;
    float cur = gRecSpectrumSmooth[i];
    if (v > cur) cur = cur + (v - cur) * attack;
    else cur = cur * decay;
    gRecSpectrumSmooth[i] = cur;

    gRecSpectrum[i] = (uint8_t)lroundf(cur * 100.0f);
  }
}

static void computeAudioMetricsFromPcmWindow(const int16_t* pcm, size_t totalSamples, size_t windowEndSample) {
  if (pcm == nullptr || totalSamples == 0) {
    gRecMetricsValid = false;
    return;
  }
  if (windowEndSample > totalSamples) {
    windowEndSample = totalSamples;
  }

  static constexpr size_t kN = 256;
  const size_t N = (windowEndSample >= kN) ? kN : windowEndSample;
  if (N < 32) {
    gRecMetricsValid = false;
    return;
  }

  const int16_t* x = pcm + (windowEndSample - N);

  static constexpr int kClipThreshold = 32760;
  uint32_t clipped = 0;
  int peak = 0;
  double sumsq = 0.0;

  for (size_t n = 0; n < N; ++n) {
    const int v = (int)x[n];
    const int a = (v < 0) ? -v : v;
    if (a > peak) {
      peak = a;
    }
    if (a >= kClipThreshold) {
      ++clipped;
    }
    sumsq += (double)v * (double)v;
  }

  const double rms = sqrt(sumsq / (double)N);
  const float rmsNorm = (float)(rms / 32768.0);
  const float peakNorm = (float)peak / 32768.0f;

  auto toDbfs = [](float norm) -> float {
    if (norm <= 0.0f) {
      return -99.9f;
    }
    return 20.0f * log10f(norm);
  };

  gRecRmsDbfs = toDbfs(rmsNorm);
  gRecPeakDbfs = toDbfs(peakNorm);
  gRecClipPercent = 100.0f * ((float)clipped / (float)N);
  gRecMetricsValid = true;
}

struct IMAAdpcmState {
  int predictor = 0;
  int index = 0;
};

static constexpr int kImaStepTable[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
  34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
  157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
  3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
  15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static constexpr int8_t kImaIndexTable[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
};

static uint8_t imaAdpcmEncodeNibble(int16_t sample, IMAAdpcmState& st) {
  int predictor = st.predictor;
  int index = st.index;
  int step = kImaStepTable[index];

  int diff = (int)sample - predictor;
  uint8_t code = 0;
  if (diff < 0) {
    code |= 8;
    diff = -diff;
  }

  int delta = step >> 3;
  if (diff >= step) {
    code |= 4;
    diff -= step;
    delta += step;
  }
  step >>= 1;
  if (diff >= step) {
    code |= 2;
    diff -= step;
    delta += step;
  }
  step >>= 1;
  if (diff >= step) {
    code |= 1;
    delta += step;
  }

  if (code & 8) {
    predictor -= delta;
  } else {
    predictor += delta;
  }

  if (predictor > 32767) predictor = 32767;
  if (predictor < -32768) predictor = -32768;

  index += kImaIndexTable[code & 0x0F];
  if (index < 0) index = 0;
  if (index > 88) index = 88;

  st.predictor = predictor;
  st.index = index;
  return (uint8_t)(code & 0x0F);
}

static int16_t imaAdpcmDecodeNibble(uint8_t code, IMAAdpcmState& st) {
  int predictor = st.predictor;
  int index = st.index;
  int step = kImaStepTable[index];

  int diff = step >> 3;
  if (code & 4) diff += step;
  if (code & 2) diff += (step >> 1);
  if (code & 1) diff += (step >> 2);

  if (code & 8) predictor -= diff;
  else predictor += diff;

  if (predictor > 32767) predictor = 32767;
  if (predictor < -32768) predictor = -32768;

  index += kImaIndexTable[code & 0x0F];
  if (index < 0) index = 0;
  if (index > 88) index = 88;

  st.predictor = predictor;
  st.index = index;
  return (int16_t)predictor;
}

static bool imaAdpcmEncodeBuffer(const int16_t* pcm, size_t samples, std::vector<uint8_t>& out) {
  out.clear();
  if (samples == 0 || pcm == nullptr) {
    return false;
  }

  // Header: predictor (LE int16), index (uint8), reserved (uint8)
  IMAAdpcmState st;
  st.predictor = pcm[0];
  st.index = 0;

  const size_t payloadNibbles = (samples > 1) ? (samples - 1) : 0;
  const size_t payloadBytes = (payloadNibbles + 1) / 2;
  out.reserve(4 + payloadBytes);

  out.push_back((uint8_t)(st.predictor & 0xFF));
  out.push_back((uint8_t)((st.predictor >> 8) & 0xFF));
  out.push_back((uint8_t)(st.index & 0xFF));
  out.push_back(0);

  uint8_t packed = 0;
  bool highNibble = false;
  for (size_t i = 1; i < samples; ++i) {
    const uint8_t nib = imaAdpcmEncodeNibble(pcm[i], st);
    if (!highNibble) {
      packed = nib;
      highNibble = true;
    } else {
      packed |= (uint8_t)(nib << 4);
      out.push_back(packed);
      highNibble = false;
      packed = 0;
    }
  }
  if (highNibble) {
    out.push_back(packed);
  }
  return true;
}

static bool imaAdpcmDecodeToBuffer(const std::vector<uint8_t>& in, int16_t* pcmOut, size_t samples) {
  if (pcmOut == nullptr || samples == 0) {
    return false;
  }
  if (in.size() < 4) {
    return false;
  }

  IMAAdpcmState st;
  st.predictor = (int16_t)((int)in[0] | ((int)in[1] << 8));
  st.index = (int)in[2];
  if (st.index < 0) st.index = 0;
  if (st.index > 88) st.index = 88;

  pcmOut[0] = (int16_t)st.predictor;
  if (samples == 1) {
    return true;
  }

  size_t pcmIdx = 1;
  for (size_t i = 4; i < in.size() && pcmIdx < samples; ++i) {
    const uint8_t b = in[i];
    const uint8_t lo = b & 0x0F;
    const uint8_t hi = (b >> 4) & 0x0F;

    pcmOut[pcmIdx++] = imaAdpcmDecodeNibble(lo, st);
    if (pcmIdx < samples) {
      pcmOut[pcmIdx++] = imaAdpcmDecodeNibble(hi, st);
    }
  }
  return pcmIdx == samples;
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5.begin(cfg);

  bgIndex = 0;
  bgColor = kBgPalette16[bgIndex];

  // 70% volume (0..255).
  ensureSpeakerOn();

  // Allocate recording buffer (prefer PSRAM if available).
  // Goal: significantly more than 3 seconds, but keep headroom for graphics/sound.
  // We downscale until allocation succeeds.
  const uint32_t freePsram = (uint32_t)ESP.getFreePsram();
  const uint32_t freeHeap = (uint32_t)ESP.getFreeHeap();

  // Leave generous headroom for sprites + runtime allocations.
  const uint32_t psramBudget = (freePsram > (512u * 1024u)) ? (freePsram - (512u * 1024u)) : 0;
  const uint32_t heapBudget = (freeHeap > (192u * 1024u)) ? (freeHeap - (192u * 1024u)) : 0;

  // Target: up to 30 seconds if memory allows.
  uint32_t targetMs = 30000;
  size_t targetSamples = (size_t)((kRecSampleRateHz * targetMs) / 1000);
  size_t targetBytes = targetSamples * sizeof(int16_t);

  // Cap target bytes by a conservative budget.
  const uint32_t totalBudget = psramBudget + (heapBudget / 2); // prefer PSRAM; keep heap margin.
  if (totalBudget > 0 && targetBytes > totalBudget) {
    targetBytes = totalBudget;
    targetSamples = targetBytes / sizeof(int16_t);
  }

  // Ensure at least 3 seconds.
  const size_t minSamples = (size_t)((kRecSampleRateHz * 3000) / 1000);
  if (targetSamples < minSamples) {
    targetSamples = minSamples;
  }

  // Try allocate; if fails, halve until success (down to minimum).
  size_t trySamples = targetSamples;
  while (trySamples >= minSamples && gRecPcm == nullptr) {
    const size_t bytes = trySamples * sizeof(int16_t);
    gRecPcm = (int16_t*)ps_malloc(bytes);
    if (!gRecPcm) {
      gRecPcm = (int16_t*)malloc(bytes);
    }
    if (!gRecPcm) {
      trySamples /= 2;
      // Keep alignment sane.
      trySamples = (trySamples / kRecChunkSamples) * kRecChunkSamples;
    } else {
      gRecMaxSamples = trySamples;
      gRecMaxMs = (uint32_t)((gRecMaxSamples * 1000ull) / kRecSampleRateHz);
    }
  }

  Serial.println();
  Serial.println("[autogarden] StickS3 audio record/playback");
  Serial.printf("Mic enabled: %d\n", (int)M5.Mic.isEnabled());
  Serial.printf("Speaker enabled: %d\n", (int)M5.Speaker.isEnabled());
  Serial.printf("Rec buffer: %s (%u bytes)\n", gRecPcm ? "OK" : "FAILED", (unsigned)(gRecMaxSamples * sizeof(int16_t)));
  Serial.printf("Rec max: %lums (~%lus)\n", (unsigned long)gRecMaxMs, (unsigned long)(gRecMaxMs / 1000));
  Serial.printf("Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %u bytes\n", (unsigned)ESP.getFreePsram());

  // Baseline: "upright" (USB-C down, GPIO up) should read normally.
  // We'll rotate the *text* smoothly, so keep the screen in portrait.
  setDisplayRotation(kPortraitRotation);

  imuOk = M5.Imu.isEnabled();

  // Full-screen frame buffer (double buffering) to prevent flicker/tearing.
  frameSpritePortrait.setColorDepth(16);
  frameSpritePortrait.createSprite(M5.Display.width(), M5.Display.height());

  // Landscape buffer for REC/PLAY UI.
  setDisplayRotation(kStatusRotation);
  frameSpriteLandscape.setColorDepth(16);
  frameSpriteLandscape.createSprite(M5.Display.width(), M5.Display.height());
  setDisplayRotation(kPortraitRotation);

  M5.Display.fillScreen(bgColor);
  frameSpritePortrait.fillScreen(TFT_BLACK);

  if (imuOk) {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    (void)M5.Imu.update();
    (void)M5.Imu.getAccel(&ax, &ay, &az);
    drawAxesScreen(ax, ay, az);
  } else {
    drawImuDisabledScreen();
  }
}

void loop() {
  M5.update();

  static uint32_t lastDrawMs = 0;

  // KEY1 / BtnA:
  // - short click: cycle background color (existing behavior)
  // - long press (~650ms): replay last recording (PLAY)
  static bool btnAHoldHandled = false;
  if (gUiMode == UiMode::Normal) {
    if (M5.BtnA.isPressed()) {
      if (!btnAHoldHandled && M5.BtnA.pressedFor(650)) {
        btnAHoldHandled = true;
        gSkipNextBtnAClick = true;

        if (!gPlayActive && !gRecActive && !gRecReadyWaitRelease && gRecSamples > 0) {
          ensureMicOff();
          ensureSpeakerOn();
          (void)startPlayback();
        } else {
          // No recording available (or busy) -> subtle error tone.
          playToneIfEnabled(220.0f, 60, false);
        }

        // Force redraw immediately.
        lastDrawMs = 0;
      }
    } else {
      btnAHoldHandled = false;
    }
  }

  if (M5.BtnA.wasClicked()) {
    if (gSkipNextBtnAClick) {
      gSkipNextBtnAClick = false;
      // Swallow click generated by long-press release.
    } else {
    bgIndex = static_cast<uint8_t>((bgIndex + 1) % kBgPaletteCount);
    bgColor = kBgPalette16[bgIndex];

    // Play a short tone for each color except black.
    if (bgIndex != 0) {
      playToneIfEnabled(kToneHz16[bgIndex], 90, false);
    }

    // Force redraw immediately.
    lastDrawMs = 0;
    }
  }

  // If we're playing back, keep a simple status screen until playback finishes.
  if (gPlayActive) {
    if (!M5.Speaker.isPlaying()) {
      gPlayActive = false;
      gUiMode = UiMode::Normal;
      lastDrawMs = 0;
    } else {
      const uint32_t now = millis();
      if (shouldDrawStatus(now, 100)) {
        const uint32_t elapsedMs = now - gPlayStartMs;
        size_t pos = (size_t)(((uint64_t)elapsedMs * (uint64_t)kRecSampleRateHz) / 1000ull);
        if (pos > gRecSamples) {
          pos = gRecSamples;
        }
        computeSpectrumFromPcmWindow(gRecPcm, gRecSamples, pos);
        computeAudioMetricsFromPcmWindow(gRecPcm, gRecSamples, pos);
        char l1[64];
        char l2[64];
        snprintf(l1, sizeof(l1), "playing... pos:%u/%u", (unsigned)pos, (unsigned)gRecSamples);
        if (gRecMetricsValid) {
          snprintf(l2, sizeof(l2), "RMS % .1f dBFS  PEAK % .1f dBFS  CLIP %0.1f%%", gRecRmsDbfs, gRecPeakDbfs, gRecClipPercent);
        } else {
          snprintf(l2, sizeof(l2), "RMS -- dBFS  PEAK -- dBFS  CLIP --%%");
        }
        drawStatusScreen("PLAY", l1, l2, TFT_GREEN, gRecSpectrum, kRecSpectrumBins);
      }
      delay(1);
      return;
    }
  }

  // KEY2 / BtnB: press & hold to record up to 3 seconds, release to playback.
  // Beep once when recording starts so it's obvious.
  if (!gRecActive && !gRecReadyWaitRelease && gRecPcm != nullptr && M5.Mic.isEnabled()) {
    if (M5.BtnB.wasPressed()) {
      gRecStartRequested = true;
      gUiMode = UiMode::RecordBeep;
    }
  }

  if (gRecStartRequested) {
    gRecStartRequested = false;

    // Make sure speaker is usable for the beep.
    ensureMicOff();

    // Record-start beep (played before recording to avoid capturing the beep).
    playToneIfEnabled(1200.0f, 60, true);

    // IMPORTANT: enabling the mic reconfigures the ES8311 and will break audio output
    // until the speaker is re-initialized. Turn speaker off before enabling mic.
    ensureSpeakerOff();

    // Start recording only if the button is still held.
    if (M5.BtnB.isPressed()) {
      gRecSamples = 0;
      gRecActive = true;
      gRecReadyWaitRelease = false;
      gRecAdpcm.clear();
      gRecStartMs = millis();
      gUiMode = UiMode::Recording;

      Serial.println("[rec] START");
    }
  }

  // Recording loop (runs in small chunks, capped to 3 seconds).
  if (gRecActive) {
    // Record in chunks. We intentionally do not redraw the screen here to reduce CPU load.
    const bool pressed = M5.BtnB.isPressed();
    const bool atMax = (gRecSamples >= gRecMaxSamples);

    if (!pressed || atMax) {
      gRecActive = false;
      gRecReadyWaitRelease = pressed; // if user still holds, wait for release before playback.

      if (gRecReadyWaitRelease) {
        gUiMode = UiMode::HoldMaxRelease;
      } else {
        gUiMode = UiMode::Normal;
      }

      Serial.printf("[rec] STOP samples=%u\n", (unsigned)gRecSamples);

      // Stop mic and restore speaker right away so playback / beeps work again.
      ensureMicOff();
      ensureSpeakerOn();

      // If already released, playback immediately.
      if (!gRecReadyWaitRelease) {
        (void)startPlayback();
        lastDrawMs = 0;
      }
    } else {
      const size_t remain = gRecMaxSamples - gRecSamples;
      const size_t chunk = (remain < kRecChunkSamples) ? remain : kRecChunkSamples;

      // Enqueue a chunk, then wait until it's filled.
      if (chunk > 0) {
        const bool ok = M5.Mic.record(gRecPcm + gRecSamples, chunk, kRecSampleRateHz, false);
        if (!ok) {
          Serial.println("[rec] ERROR: M5.Mic.record failed");
          gRecActive = false;
          gRecReadyWaitRelease = false;
          gUiMode = UiMode::Error;
          gLastError = "Mic.record failed";
          ensureMicOff();
          ensureSpeakerOn();
          playToneIfEnabled(220.0f, 120, false);
          delay(1);
          return;
        }
        while (M5.Mic.isRecording()) {
          M5.update();

          // Update REC screen at low rate while we wait.
          const uint32_t now = millis();
          if (shouldDrawStatus(now, 120)) {
            const uint32_t elapsed = now - gRecStartMs;
            const uint32_t remainMs = (elapsed >= gRecMaxMs) ? 0 : (gRecMaxMs - elapsed);
            char l1[64];
            char l2[64];
            snprintf(l1, sizeof(l1), "REC  %lu.%02lus / %lus  samp:%u  left:%lums", (unsigned long)(elapsed / 1000), (unsigned long)((elapsed % 1000) / 10), (unsigned long)(gRecMaxMs / 1000), (unsigned)gRecSamples, (unsigned long)remainMs);
            if (gRecMetricsValid) {
              snprintf(l2, sizeof(l2), "RMS % .1f dBFS  PEAK % .1f dBFS  CLIP %0.1f%%", gRecRmsDbfs, gRecPeakDbfs, gRecClipPercent);
            } else {
              snprintf(l2, sizeof(l2), "RMS -- dBFS  PEAK -- dBFS  CLIP --%%");
            }
            drawStatusScreen("RECORDING", l1, l2, TFT_RED, gRecSpectrum, kRecSpectrumBins);
          }
          delay(1);
        }

        // Chunk has finished recording into gRecPcm[gRecSamples..gRecSamples+chunk).
        // Update spectrum from the recorded audio (avoid reading the buffer mid-write).
        computeSpectrumFromPcmWindow(gRecPcm + gRecSamples, chunk, chunk);
        computeAudioMetricsFromPcmWindow(gRecPcm + gRecSamples, chunk, chunk);
        gRecSamples += chunk;
      }
      delay(1);
    }
    return;
  }

  // If we hit max duration while still holding, wait for KEY2 release to playback.
  if (gRecReadyWaitRelease) {
    const uint32_t now = millis();
    if (shouldDrawStatus(now, 120)) {
      char l1[64];
      char l2[64];
      snprintf(l1, sizeof(l1), "MAX %lus reached", (unsigned long)(gRecMaxMs / 1000));
      snprintf(l2, sizeof(l2), "RELEASE KEY2 to play (%u samples)", (unsigned)gRecSamples);
      drawStatusScreen("HOLD", l1, l2, TFT_YELLOW);
    }
    if (M5.BtnB.wasReleased()) {
      gRecReadyWaitRelease = false;

      // Restore speaker before playback.
      ensureMicOff();
      ensureSpeakerOn();

      (void)startPlayback();
      lastDrawMs = 0;
    }

    // Stay on the HOLD/PLAY UI; don't fall through to normal rendering.
    delay(1);
    return;
  }

  if (gUiMode == UiMode::Error) {
    const uint32_t now = millis();
    if (shouldDrawStatus(now, 200)) {
      drawStatusScreen("ERROR", gLastError ? gLastError : "unknown", "Check MIC enable / wiring", TFT_RED);
    }
    delay(1);
    return;
  }

  // Normal UI uses portrait.
  static uint32_t lastFrameMs = 0;
  static float lastAx = 999.0f;
  static float lastAy = 999.0f;
  static float lastAz = 999.0f;
  const uint32_t now = millis();
  if (now - lastFrameMs < 33) {
    delay(1);
    return;
  }
  lastFrameMs = now;

  if (imuOk) {
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    (void)M5.Imu.update();
    (void)M5.Imu.getAccel(&ax, &ay, &az);

    const bool changed = (fabsf(ax - lastAx) + fabsf(ay - lastAy) + fabsf(az - lastAz)) > 0.02f;
    const bool timeRefresh = (now - lastDrawMs) > 200;
    if (changed || timeRefresh) {
      lastAx = ax;
      lastAy = ay;
      lastAz = az;
      lastDrawMs = now;
      drawAxesScreen(ax, ay, az);
    }
  } else {
    const bool timeRefresh = (now - lastDrawMs) > 500;
    if (timeRefresh) {
      lastDrawMs = now;
      drawImuDisabledScreen();
    }
  }

  delay(1);
}
