#pragma once
#include <stdint.h>

void uiBegin();
void uiLoop();              // call every main-loop iteration
void uiButton(int kind);    // 1 click, 2 double click, 3 long press (hardware button)
void uiRebuild();           // after a theme change
