#include "stdafx.h"
#include "WinKeyMapper.h"

Action WinKeyMapper::KeyToAction(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (wParam == VK_UP) return PITCH_UP;
    if (wParam == VK_DOWN) return PITCH_DOWN;
    if (wParam == VK_LEFT) return YAW_LEFT;
    if (wParam == VK_RIGHT) return YAW_RIGHT;

    const int keyboardScanCode = (lParam >> 16) & 0x00ff;
    const int virtualKey = wParam;

    BYTE keyboardState[256];
    GetKeyboardState(keyboardState);
    WORD ascii = 0;
    const int len = ToAscii(virtualKey, keyboardScanCode, keyboardState, &ascii, 0);
    if (len == 1) {
        if (ascii == 'f') {
            return MOVE_FORWARD;
        }
        if (ascii == 'b') {
            return MOVE_BACK;
        }
        if (ascii == 'a') {
            return ADVANCE_TIME;
        }
        if (ascii == 'd') {
            return UNADVANCE_TIME;
        }
        if (ascii == 'n') {
            return NEXT_CLOUD_LAYER;
        }
        if (ascii == 'p') {
            return NEXT_PRECIPITATION_TYPE;
        }
    }
    return UNKNOWN;
}