#include "spectrum.h"
#include "../driver/st7735s.h"
#include "../helper/helper.h"
#include "../ui/gfx.h"
#include <string.h>

#define MAX_POINTS 160
#define WF_XN 80
#define WF_YN 45

typedef struct {
  uint8_t sx;
  uint8_t w;
} Bar;

static uint16_t rssiHistory[MAX_POINTS] = {0};
static uint16_t noiseHistory[MAX_POINTS] = {0};
static bool markers[MAX_POINTS] = {0};
static bool needRedraw[MAX_POINTS] = {0};

static uint8_t x = 255;
static uint8_t ox = 255;
static uint8_t filledPoints;

static uint32_t stepsCount;
static uint32_t currentStep;
static uint32_t step;
static uint32_t bw;

static FRange range;

static uint16_t vMin = 0, vMax = 512;

static uint8_t curX = MAX_POINTS / 2;
static uint8_t curSbWidth = 16;

static uint8_t oyA[MAX_POINTS] = {0};
static uint16_t wfPxBuf[MAX_POINTS] = {0};
static const uint8_t WF_CUR_Y = WF_YN + 11;

static const int16_t GRADIENT_PALETTE[] = {
    // 0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,    0x0,
    0x0,    0x0,    0x800,  0x1000, 0x1000, 0x1800, 0x2000, 0x2000, 0x2800,
    0x3000, 0x3000, 0x3800, 0x4000, 0x4800, 0x4800, 0x5000, 0x5000, 0x5800,
    0x6000, 0x6800, 0x6800, 0x7000, 0x7800, 0x7800, 0x8000, 0x8800, 0x8820,
    0x9061, 0x98a1, 0xa0e2, 0xa922, 0xb163, 0xb9a3, 0xc1e4, 0xca24, 0xd265,
    0xdaa5, 0xe2e6, 0xeb26, 0xf367, 0xfb87, 0xf3c8, 0xebe9, 0xe42a, 0xdc4b,
    0xd46c, 0xc48c, 0xbcad, 0xb4ee, 0xad0f, 0x9d30, 0x9551, 0x8d92, 0x85b3,
    0x75d4, 0x6df5, 0x6636, 0x5e57, 0x4e78, 0x4699, 0x3eda, 0x36fb, 0x271c,
    0x1f3d, 0x177e, 0xf9f,  0x79f,  0x77f,  0x75f,  0x71f,  0x6ff,  0x6df,
    0x6bf,  0x67f,  0x65f,  0x63f,  0x61f,  0x5ff,  0x5df,  0x59f,  0x57f,
    0x55f,  0x53f,  0x4ff,  0x4df,  0x4bf,  0x49f,  0x47f,  0x43f,  0x41f,
    0x3ff,  0x3df,  0x39f,  0x37f,  0x35f,  0x33f,  0x31f,  0x2ff,  0x2bf,
    0x29f,  0x27f,  0x25f,  0x21f,  0x1ff,  0x1df,  0x1bf,  0x19f,  0x15f,
    0x13f,  0x11f,  0xff,   0xbf,   0x9f,   0x7f,   0x5f,   0x3f,   0x319f,
    0x9c9f, 0xffbf};

static uint8_t rssi2palIndex(uint16_t rssi) {
  return ConvertDomain(rssi, vMin, vMax, 0, ARRAY_SIZE(GRADIENT_PALETTE) - 1);
}

static uint8_t rssi2y(uint16_t rssi, uint8_t sh) {
  return ConvertDomain(rssi, vMin, vMax, 0, sh);
}

static uint8_t f2x(uint32_t f) {
  return ConvertDomain(f, range.start, range.end + step, 0, MAX_POINTS - 1);
}

static uint32_t x2f(uint8_t x) {
  return ConvertDomain(x, 0, MAX_POINTS - 1, range.start, range.end);
}

static uint32_t roundToStep(uint32_t f, uint32_t step) {
  uint32_t sd = f % step;
  if (sd > step / 2) {
    f += step - sd;
  } else {
    f -= sd;
  }
  return f;
}

/* static void _drawTicks(uint8_t y, uint32_t fs, uint32_t fe, uint32_t div,
                       uint8_t h, uint16_t c) {
  for (uint32_t f = fs - (fs % div) + div; f < fe; f += div) {
    uint8_t x = f2x(f);
    ST7735S_SetAddrWindow(x, y, x, y + h - 1);
    for (uint8_t yp = y; yp < y + h; ++yp) {
      ST7735S_SendU16(yp / 2 % 2 ? c : COLOR_BACKGROUND);
    }
  }
  DISPLAY_ResetWindow();
}

static void drawTicks(uint8_t y, uint8_t h) {
  uint32_t fs = range.start;
  uint32_t fe = range.end;
  uint32_t bw = fe - fs;

  for (uint32_t p = 100000000; p >= 10; p /= 10) {
    if (p < bw) {
      _drawTicks(y, fs, fe, p / 2, h, COLOR_GREY_DARK);
      _drawTicks(y, fs, fe, p, h, COLOR_GREY);
      return;
    }
  }
} */

static Bar bar(const uint8_t i) {
  uint8_t sz = f2x(range.start + step) - f2x(range.start);
  const uint8_t szBw = f2x(range.start + bw) - f2x(range.start);

  if (szBw < sz) {
    sz = szBw;
  }

  if (sz < 2) {
    return (Bar){i, 1};
  }

  uint8_t w = sz % 2 == 0 ? sz + 1 : sz;

  int16_t sx = i - w / 2;
  int16_t ex = i + w / 2;

  if (sx < 0) {
    w += sx;
    sx = 0;
  }

  if (ex > MAX_POINTS) {
    w -= ex - MAX_POINTS;
  }
  return (Bar){sx, w};
}

void SP_ResetHistory(void) {
  for (uint8_t i = 0; i < MAX_POINTS; ++i) {
    oyA[i] = 0;
    rssiHistory[i] = 0;
    noiseHistory[i] = UINT16_MAX;
    markers[i] = false;
    needRedraw[i] = false;
  }
  filledPoints = 0;
  currentStep = 0;
}

void SP_ResetRender() { memset(needRedraw, true, MAX_POINTS); }

void SP_Begin(void) { currentStep = 0; }

void SP_Next(void) {
  if (currentStep < stepsCount - 1) {
    currentStep++;
  }
}

void SP_Init(FRange *r, uint32_t stepSize, uint32_t _bw) {
  filledPoints = 0;
  step = stepSize;
  bw = _bw;
  range = *r;
  stepsCount = (r->end - r->start) / stepSize + 1;
  SP_ResetHistory();
  SP_Begin();
}

void SP_AddPoint(Loot *msm) {
  x = f2x(msm->f);
  if (ox != x) {
    ox = x;
    rssiHistory[x] = markers[x] = 0;
    noiseHistory[x] = UINT16_MAX;
  }
  if (msm->rssi > rssiHistory[x]) {
    rssiHistory[x] = msm->rssi;
  }
  if (msm->noise < noiseHistory[x]) {
    noiseHistory[x] = msm->noise;
  }
  if (markers[x] == false && msm->open) {
    markers[x] = msm->open;
  }
  const uint8_t XL = f2x(msm->f + step);
  for (uint8_t nx = x + 1; nx < XL; ++nx) {
    rssiHistory[nx] = rssiHistory[x];
    noiseHistory[nx] = noiseHistory[x];
    markers[nx] = markers[x];
  }
  if (x > filledPoints && x < MAX_POINTS) {
    filledPoints = x + 1;
  }
}

static void updateMinMax() {
  const uint16_t rssiMin = Min(rssiHistory, filledPoints);
  const uint16_t rssiMax = Max(rssiHistory, filledPoints);
  const uint16_t noiseFloor = SP_GetNoiseFloor();

  vMin = rssiMin - 2;
  vMax = rssiMax + Clamp((rssiMax - noiseFloor), 35, rssiMax - noiseFloor);
}

void SP_Render(uint8_t sy, uint8_t sh) {
  updateMinMax();

  for (uint8_t i = 0; i < MAX_POINTS;) {
    const Bar b = bar(i);
    const uint8_t ny = rssi2y(rssiHistory[i], sh);
    if (ny < oyA[i]) {
      DISPLAY_DrawRectangle1Nr(b.sx, sy + ny, oyA[i] - ny, b.w,
                               COLOR_BACKGROUND);
    }
    DISPLAY_DrawRectangle1Nr(b.sx, sy, ny, b.w, COLOR_FOREGROUND);
    i += b.w;
  }

  for (uint8_t i = 0; i < MAX_POINTS; ++i) {
    oyA[i] = rssi2y(rssiHistory[i], sh);
  }

  DISPLAY_ResetWindow();
}

void WF_Render(bool wfDown) {
  if (wfDown) {
    for (uint8_t y = 11; y < WF_CUR_Y; ++y) {
      ST7735S_ReadPixels(0, y + 1, wfPxBuf, MAX_POINTS, 1);
      ST7735S_SetAddrWindow(0, y, MAX_POINTS - 1, y);
      for (uint8_t i = 0; i < MAX_POINTS; ++i) {
        ST7735S_SendU16(wfPxBuf[i]);
      }
    }
  }

  for (uint8_t i = 0; i < MAX_POINTS;) {
    const Bar b = bar(i);
    const uint16_t color = GRADIENT_PALETTE[rssi2palIndex(rssiHistory[i])];
    DISPLAY_DrawRectangle1Nr(b.sx, WF_CUR_Y - 1, 1, b.w, color);
    i += b.w;
  }
  DISPLAY_ResetWindow();
}

uint16_t SP_GetNoiseFloor() { return Std(rssiHistory, filledPoints); }
uint16_t SP_GetNoiseMax() { return Max(noiseHistory, filledPoints); }

void CUR_Render(uint8_t y) {
  DISPLAY_Fill(0, MAX_POINTS - 1, y, y + 6 - 1, COLOR_BACKGROUND);

  y++;
  DISPLAY_Fill(curX - curSbWidth + 1, curX + curSbWidth - 1, y, y + 4 - 1,
               COLOR_RGB(8, 16, 0));
  DISPLAY_DrawRectangle1Nr(curX - curSbWidth, y, 4, 1, COLOR_YELLOW);
  DISPLAY_DrawRectangle1Nr(curX + curSbWidth, y, 4, 1, COLOR_YELLOW);

  DISPLAY_DrawRectangle1Nr(curX, y, 4, 1, COLOR_YELLOW);
  for (uint8_t d = 1; d < 4; ++d) {
    DISPLAY_DrawRectangle1Nr(curX - d, y + d, 4 - d, 1, COLOR_YELLOW);
    DISPLAY_DrawRectangle1Nr(curX + d, y + d, 4 - d, 1, COLOR_YELLOW);
  }
  DISPLAY_ResetWindow();
}

bool CUR_Move(bool up) {
  if (up) {
    if (curX + curSbWidth < MAX_POINTS - 1) {
      curX++;
      return true;
    }
  } else {
    if (curX - curSbWidth > 0) {
      curX--;
      return true;
    }
  }
  return false;
}

bool CUR_Size(bool up) {
  if (up) {
    if (curX + curSbWidth < MAX_POINTS - 1 && curX - curSbWidth > 0) {
      curSbWidth++;
      return true;
    }
  } else {
    if (curSbWidth > 1) {
      curSbWidth--;
      return true;
    }
  }
  return false;
}

FRange CUR_GetRange(uint32_t step) {
  FRange range = {
      .start = x2f(curX - curSbWidth),
      .end = x2f(curX + curSbWidth),
  };
  range.start = roundToStep(range.start, step);
  range.end = roundToStep(range.end, step);
  return range;
}

uint32_t CUR_GetCenterF(uint32_t step) { return roundToStep(x2f(curX), step); }

void CUR_Reset() {
  curX = 80;
  curSbWidth = 16;
}
