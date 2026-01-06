#pragma once

#include "SampleDefines.h"
#include <windows.h>

class WinKeyMapper
{
public:
    Action KeyToAction(UINT message, WPARAM wParam, LPARAM lParam);
};