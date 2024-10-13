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

#ifndef HELPER_H
#define HELPER_H

#include <stdint.h>

extern char gShortString[10];

void Int2Ascii(uint32_t Number, uint8_t Size);
uint16_t TIMER_Calculate(uint16_t Setting);
void SCREEN_TurnOn(void);
void STANDBY_BlinkGreen(void);

int64_t Clamp(int64_t v, int64_t min, int64_t max);
int64_t ConvertDomain(int64_t aValue, int64_t aMin, int64_t aMax, int64_t bMin,
                      int64_t bMax);

int Mid(uint16_t *array, uint8_t n);
int Min(uint16_t *array, uint8_t n);
int Max(uint16_t *array, uint8_t n);
uint16_t Mean(uint16_t *array, uint8_t n);
uint16_t Std(uint16_t *data, uint8_t n);

int16_t Rssi2DBm(uint16_t rssi);

void ShiftShortStringRight(uint8_t Start, uint8_t End);

#endif
