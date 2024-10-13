/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "helper.h"
#include "../driver/pins.h"
#include "../misc.h"
#include "../radio/scheduler.h"
#include "../radio/settings.h"

char gShortString[10];

void Int2Ascii(uint32_t Number, uint8_t Size) {
  uint32_t Divider = 1;

  for (; Size != 0; Size--) {
    gShortString[Size - 1] =
        '0' + ((Number / Divider) - ((Number / Divider) / 10) * 10);
    Divider *= 10;
  }
}

uint16_t TIMER_Calculate(uint16_t Setting) {
  if (Setting < 4) {
    return Setting * 5;
  } else {
    return (Setting - 2) * 15;
  }
}

void SCREEN_TurnOn(void) {
  if (gSettings.bEnableDisplay) {
    gEnableBlink = false;
    STANDBY_Counter = 0;
    gpio_bits_set(GPIOA, BOARD_GPIOA_LCD_RESX);
  }
}

void STANDBY_BlinkGreen(void) {
  if (STANDBY_Counter > 5000) {
    STANDBY_Counter = 0;
    gpio_bits_set(GPIOA, BOARD_GPIOA_LED_GREEN);
    gBlinkGreen = 1;
    gGreenLedTimer = 0;
  }
  if (gBlinkGreen && gGreenLedTimer > 199) {
    if ((!gScannerMode || !gExtendedSettings.ScanBlink) &&
        gRadioMode != RADIO_MODE_RX) {
      gpio_bits_reset(GPIOA, BOARD_GPIOA_LED_GREEN);
    }
    gBlinkGreen = false;
    gGreenLedTimer = 0;
  }
}

int64_t Clamp(int64_t v, int64_t min, int64_t max) {
  return v <= min ? min : (v >= max ? max : v);
}

int64_t ConvertDomain(int64_t aValue, int64_t aMin, int64_t aMax, int64_t bMin,
                      int64_t bMax) {
  const int64_t aRange = aMax - aMin;
  const int64_t bRange = bMax - bMin;
  aValue = Clamp(aValue, aMin, aMax);
  return ((aValue - aMin) * bRange + aRange / 2) / aRange + bMin;
}

int Mid(uint16_t *array, uint8_t n) {
  int32_t sum = 0;
  for (uint8_t i = 0; i < n; ++i) {
    sum += array[i];
  }
  return sum / n;
}

int Min(uint16_t *array, uint8_t n) {
  uint8_t min = array[0];
  for (uint8_t i = 1; i < n; ++i) {
    if (array[i] < min) {
      min = array[i];
    }
  }
  return min;
}

int Max(uint16_t *array, uint8_t n) {
  uint8_t max = array[0];
  for (uint8_t i = 1; i < n; ++i) {
    if (array[i] > max) {
      max = array[i];
    }
  }
  return max;
}

uint16_t Mean(uint16_t *array, uint8_t n) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; ++i) {
    sum += array[i];
  }
  return sum / n;
}

uint16_t Sqrt(uint32_t v) {
  uint16_t res = 0;
  for (uint32_t i = 0; i < v; ++i) {
    if (i * i <= v) {
      res = i;
    } else {
      break;
    }
  }
  return res;
}

uint16_t Std(uint16_t *data, uint8_t n) {
  uint32_t sumDev = 0;

  for (uint8_t i = 0; i < n; ++i) {
    sumDev += data[i] * data[i];
  }
  return Sqrt(sumDev / n);
}

int16_t Rssi2DBm(uint16_t rssi) { return (rssi >> 1) - 177; }

void ShiftShortStringRight(uint8_t Start, uint8_t End) {
  for (int8_t i = End; i > Start; i--) {
    gShortString[i + 1] = gShortString[i];
  }
}
