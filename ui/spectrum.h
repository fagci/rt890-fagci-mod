#ifndef SPECTRUM_DRAW_H
#define SPECTRUM_DRAW_H

#include "../misc.h"
#include <stdbool.h>
#include <stdint.h>

void SP_AddPoint(Loot *msm);
void SP_ResetHistory();
void SP_ResetRender();
void SP_Init(FRange *r, uint32_t stepSize, uint32_t bw);
void SP_Begin();
void SP_Next();
void SP_Render(uint8_t sy, uint8_t sh);
void WF_Render(bool wfDown);
void CUR_Render(uint8_t y);
bool CUR_Move(bool up);
bool CUR_Size(bool up);

uint16_t SP_GetNoiseFloor();
uint16_t SP_GetNoiseMax();

FRange CUR_GetRange(uint32_t step);
uint32_t CUR_GetCenterF(uint32_t step);
void CUR_Reset();

#endif /* end of include guard: SPECTRUM_DRAW_H */
