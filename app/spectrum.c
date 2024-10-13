#include "spectrum.h"
#include "../driver/battery.h"
#include "../driver/bk4819.h"
#include "../driver/delay.h"
#include "../driver/key.h"
#include "../driver/st7735s.h"
#include "../helper/helper.h"
#include "../misc.h"
#include "../radio/channels.h"
#include "../radio/scheduler.h"
#include "../radio/settings.h"
#include "../ui/gfx.h"
#include "../ui/helper.h"
#include "../ui/main.h"
#include "../ui/spectrum.h"
#include "radio.h"
#include <stddef.h>

typedef enum {
  BB_FREQ,
  BB_CURSOR,
  BB_SET1,
  BB_WATCH,
} BottomBar;

static const uint16_t U16_MAX = 65535;
static const uint32_t NUM_TIMEOUT = 3000;

static bool running;
static uint32_t step;
static uint32_t bw;
static uint8_t stepIndex;
static Loot msm, msmCoursor;
static int8_t band = 0;

static uint16_t rssiO = U16_MAX;
static uint16_t noiseO = 0;

static uint32_t lastRender;
static uint8_t delayMs = 3;
static bool hard = true;
static uint16_t noiseOpenDiff = 14;

static Loot catch = {0};
static bool isListening = false;

static KEY_t LastKey;
static uint32_t keyHoldTime;
static bool keyHold = false;

static uint32_t lastBatRender;
static uint32_t lastActionTime;

static bool needRedrawNumbers = true;

static BottomBar bb = BB_FREQ;

#define RANGES_STACK_SIZE 4
static FRange rangesStack[RANGES_STACK_SIZE] = {0};
static int8_t rangesStackIndex = -1;

#define LOOT_MAX 64
static Loot lootList[LOOT_MAX] = {};
static uint8_t lootListSize = 0;
static Loot *gLastActiveLoot;

void LOOT_BlacklistLast(void) {
  if (gLastActiveLoot) {
    gLastActiveLoot->goodKnown = false;
    gLastActiveLoot->blacklist = true;
  }
}

void LOOT_GoodKnownLast(void) {
  if (gLastActiveLoot) {
    gLastActiveLoot->blacklist = false;
    gLastActiveLoot->goodKnown = true;
  }
}

Loot *LOOT_Get(uint32_t f) {
  for (uint8_t i = 0; i < lootListSize; ++i) {
    if (f == lootList[i].f) {
      return &lootList[i];
    }
  }
  return NULL;
}

void LOOT_Update(Loot *m) {
  Loot *item = LOOT_Get(m->f);

  if (!item && m->open && lootListSize < LOOT_MAX) {
    lootList[lootListSize] = *m;
    item = &lootList[lootListSize];
    lootListSize++;
  }

  if (!item) {
    return;
  }

  if (item->blacklist || item->goodKnown) {
    m->open = false;
  }

  item->rssi = m->rssi;

  if (item->open) {
    gLastActiveLoot = item;
  }
  item->open = m->open;
  m->ct = item->ct;
  m->cd = item->cd;

  if (m->blacklist) {
    item->blacklist = true;
  }
}

static void rangeClear() { rangesStackIndex = -1; }

static bool rangePush(FRange r) {
  if (rangesStackIndex < RANGES_STACK_SIZE - 1) {
    rangesStack[++rangesStackIndex] = r;
  } else {
    for (uint8_t i = 1; i < RANGES_STACK_SIZE; ++i) {
      rangesStack[i - 1] = rangesStack[i];
    }
    rangesStack[rangesStackIndex] = r;
  }
  return true;
}

static FRange rangePop(void) {
  if (rangesStackIndex > 0) {
    return rangesStack[rangesStackIndex--]; // Do not care about existing value
  }
  return rangesStack[rangesStackIndex];
}

static FRange *rangePeek(void) {
  if (rangesStackIndex >= 0) {
    return &rangesStack[rangesStackIndex];
  }
  return NULL;
}

static void updateStats() {
  const uint16_t noiseFloor = SP_GetNoiseFloor();
  const uint16_t noiseMax = SP_GetNoiseMax();
  rssiO = noiseFloor;
  noiseO = noiseMax - noiseOpenDiff;
}

static bool isSquelchOpen() { return msm.rssi >= rssiO && msm.noise <= noiseO; }

static inline void tuneTo(uint32_t f) {
  int8_t b = f >= 24000000 ? 1 : -1;
  if (b != band) {
    band = b;
    BK4819_SelectFilter(b > 0);
  }
  BK4819_WriteRegister(0x38, (f >> 0) & 0xFFFF);
  BK4819_WriteRegister(0x39, (f >> 16) & 0xFFFF);
  if (hard) {
    BK4819_WriteRegister(0x30, 0x0200);
  } else {
    BK4819_WriteRegister(0x30, 0xBFF1 & ~BK4819_REG_30_ENABLE_VCO_CALIB);
  }
  BK4819_WriteRegister(0x30, 0xBFF1);
  DELAY_WaitMS(delayMs);
}

static inline void measure() {
  msm.rssi = BK4819_GetRSSI();
  msm.noise = BK4819_GetNoise();
  if (isListening) {
    noiseO -= noiseOpenDiff;
    msm.open = isSquelchOpen();
    noiseO += noiseOpenDiff;
  } else {
    msm.open = isSquelchOpen();
  }
  LOOT_Update(&msm);
  if (CUR_GetCenterF(step) == msm.f) {
    msmCoursor = msm;
    if (bb == BB_WATCH) {
      needRedrawNumbers = true;
    }
  }
}

static void drawF(uint32_t f, uint8_t x, uint8_t y, uint16_t color) {
  Int2Ascii(f, 8);
  ShiftShortStringRight(2, 7);
  gShortString[3] = '.';
  gColorForeground = color;
  gColorBackground = COLOR_BACKGROUND;
  UI_DrawSmallString(x, y, gShortString, 8);
  gColorForeground = COLOR_FOREGROUND;
  gColorBackground = COLOR_BACKGROUND;
}

static void setBB(BottomBar v) {
  bb = v;
  needRedrawNumbers = true;
  lastActionTime = gTimeSinceBoot;
}

static void renderNumbers() {
  if (bb != BB_FREQ && bb != BB_WATCH &&
      gTimeSinceBoot - lastActionTime > NUM_TIMEOUT) {
    bb = BB_FREQ;
    needRedrawNumbers = true;
  }
  if (!needRedrawNumbers) {
    return;
  }
  needRedrawNumbers = false;
  switch (bb) {
  case BB_SET1:
    DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);
    Int2Ascii(delayMs, 2);
    UI_DrawSmallString(2, 2, gShortString, 2);

    Int2Ascii(noiseOpenDiff, 2);
    UI_DrawSmallString(160 - 11, 2, gShortString, 2);
    break;
  case BB_CURSOR:
    DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);

    FRange cursorBounds = CUR_GetRange(step);
    drawF(cursorBounds.start, 2, 2, COLOR_YELLOW);
    drawF(CUR_GetCenterF(step), 58, 2, COLOR_YELLOW);
    drawF(cursorBounds.end, 112, 2, COLOR_YELLOW);
    break;
  case BB_WATCH:
    DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);

    Int2Ascii(msmCoursor.rssi, 3);
    UI_DrawSmallString(2, 2, gShortString, 3);
    drawF(msmCoursor.f, 58, 2, COLOR_YELLOW);
    break;
  default:
    DISPLAY_Fill(0, 159, 0, 10, COLOR_BACKGROUND);
    if (catch.f) {
      drawF(catch.f, 58, 2, COLOR_GREEN);
    } else {
      drawF(step, 58, 2, COLOR_FOREGROUND);
    }

    drawF(rangePeek()->start, 2, 2, COLOR_FOREGROUND);
    drawF(rangePeek()->end, 112, 2, COLOR_FOREGROUND);
    break;
  }
}

static void render(bool wfDown) {
  SP_Render(62, 30);
  WF_Render(wfDown);

  renderNumbers();

  if (gTimeSinceBoot - lastBatRender > 2000) {
    lastBatRender = gTimeSinceBoot;
    gBatteryVoltage = BATTERY_GetVoltage();
    UI_DrawBatteryBar();
  }
  lastRender = gTimeSinceBoot;
}

static void nextFreq() {
  msm.f += step;
  SP_Next();

  if (msm.f > rangePeek()->end) {
    updateStats();
    msm.f = rangePeek()->start;
    render(true);
    SP_Begin();
    return;
  }
}

static void init() {
  DISPLAY_FillColor(COLOR_BACKGROUND);

  catch.f = 0;

  msm.f = rangePeek()->start;
  SP_Init(rangePeek(), step, bw);
  CUR_Reset();
  CUR_Render(56);

  running = true;

  render(false);
}

static inline void toggleListening() {
  if (isListening != msm.open) {
    isListening = msm.open;
    if (isListening) {
      catch = msm;
      needRedrawNumbers = true;

      BK4819_StartAudio();
    } else {
      RADIO_EndAudio();
    }
    render(true);
  }
}

void Spectrum_Loop(void) {
  if (!isListening) {
    tuneTo(msm.f);
  }

  measure();
  SP_AddPoint(&msm);
  renderNumbers();
  toggleListening();

  if (isListening) {
    render(false);
    return;
  }

  nextFreq();
}

bool CheckKeys(void) {
  KEY_t key = KEY_GetButton();
  if (key != LastKey && key != KEY_NONE) {
    keyHoldTime = gTimeSinceBoot;
  }

  keyHold = key == LastKey && gTimeSinceBoot - keyHoldTime >= 500;
  bool isNewKey = key != LastKey;

  LastKey = key;

  if (isNewKey || keyHold) {
    switch (key) {
    case KEY_UP:
      return CUR_Move(true);
    case KEY_DOWN:
      return CUR_Move(false);
    case KEY_2:
      return CUR_Size(true);
    case KEY_8:
      return CUR_Size(false);
    case KEY_1:
      if (delayMs < 20) {
        delayMs++;
        return true;
      }
      return false;
    case KEY_7:
      if (delayMs > 1) {
        delayMs--;
        return true;
      }
      return false;
    case KEY_3:
      if (noiseOpenDiff < 40) {
        noiseOpenDiff++;
        return true;
      }
      return false;
    case KEY_9:
      if (noiseOpenDiff > 1) {
        noiseOpenDiff--;
        return true;
      }
      return false;
    default:
      break;
    }
  }
  if (isNewKey) {
    switch (key) {
    case KEY_MENU:
      if (rangesStackIndex < RANGES_STACK_SIZE - 1) {
        rangePush(CUR_GetRange(step));
        init();
        return true;
      }
      break;
    case KEY_4:
      hard ^= 1;
      return true;
    case KEY_EXIT:
      if (rangesStackIndex <= 0) {
        running = false;
      } else {
        rangePop();
        init();
      }
      return true;
    case KEY_HASH:
      LOOT_BlacklistLast();
      RADIO_EndAudio();
      isListening = false;
      catch.f = 0;
      nextFreq();
      return true;
    case KEY_STAR:
      if (bb == BB_WATCH) {
        setBB(BB_FREQ);
      } else {
        setBB(BB_WATCH);
      }
      return true;
    case KEY_6:
      if (stepIndex < 13) {
        stepIndex++;
        step = FREQUENCY_GetStep(stepIndex);
        if (step > rangePeek()->end - rangePeek()->start) {
          stepIndex = 0;
        }
      } else {
        stepIndex = 0;
      }
      step = FREQUENCY_GetStep(stepIndex);
      init();
      return true;
    default:
      break;
    }
  }
  return false;
}

void StopSpectrum(void) {
  SCREEN_TurnOn();
  ST7735S_Init();

  if (gSettings.WorkMode) {
    CHANNELS_LoadChannel(gSettings.VfoChNo[!gSettings.CurrentVfo],
                         !gSettings.CurrentVfo);
    CHANNELS_LoadChannel(gSettings.VfoChNo[gSettings.CurrentVfo],
                         gSettings.CurrentVfo);
  } else {
    CHANNELS_LoadChannel(gSettings.CurrentVfo ? 999 : 1000,
                         !gSettings.CurrentVfo);
    CHANNELS_LoadChannel(gSettings.CurrentVfo ? 1000 : 999,
                         gSettings.CurrentVfo);
  }

  RADIO_Tune(gSettings.CurrentVfo);
  UI_SetColors(gExtendedSettings.DarkMode);
  UI_DrawMain(false);
}

void APP_Spectrum(void) {
  RADIO_EndAudio(); // Just in case audio is open when spectrum starts
  RADIO_Tune(gSettings.CurrentVfo);
  uint32_t f1 = gVfoState[0].RX.Frequency;
  uint32_t f2 = gVfoState[1].RX.Frequency;
  bw = gVfoState[gSettings.CurrentVfo].bIsNarrow ? 1250 : 2500;

  stepIndex = gSettings.FrequencyStep;
  step = FREQUENCY_GetStep(stepIndex);
  rangeClear();

  if (f1 < f2) {
    rangePush((FRange){f1, f2});
  } else {
    rangePush((FRange){f2, f1});
  }

  init();

  while (running) {
    Spectrum_Loop();
    while (CheckKeys()) {
      needRedrawNumbers = true;
      if (LastKey == KEY_UP || LastKey == KEY_DOWN || LastKey == KEY_2 ||
          LastKey == KEY_8) {
        CUR_Render(56);
        setBB(BB_CURSOR);
      } else if (LastKey == KEY_1 || LastKey == KEY_7 || LastKey == KEY_3 ||
                 LastKey == KEY_9) {
        setBB(BB_SET1);
      } else {
        render(false);
      }
      renderNumbers();
      DELAY_WaitMS(keyHold ? 5 : 300);
    }
  }
  StopSpectrum();
}
