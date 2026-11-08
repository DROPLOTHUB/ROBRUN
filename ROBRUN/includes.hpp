#pragma once
#include <windows.h>
#include <ctype.h>
#include <string>
#include <iostream>
#include <fstream>
#include <array>
#include <dwmapi.h>
#include <thread>
#include <map>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include <plog/Log.h>
#include <plog/Initializers/RollingFileInitializer.h>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/MessageOnlyFormatter.h>

#ifndef _WINDEF_
typedef unsigned long DWORD;
typedef unsigned __int64 ULONGLONG;
#endif

#include "utils/math.hpp"
#include "utils/xor.hpp"
#include "utils/settings.hpp"
#include "driver/driver.hpp"

extern uintptr_t game_base;
extern Aspect::Vector2 screen_size;
extern std::string g_settings_path; // path next to exe, set in real_main()

static bool invalidptr(std::uintptr_t address) noexcept
{
    if (address == 0 || address < 0x10000) return true;
    if (address == 0xFFFFFFFF || address == 0xFFFFFFFFFFFFFFFFULL) return true;
    if (address > 0x7FFFFFFFFFFFULL) return true;
    if (address >= 0xFFFF800000000000ULL) return true;
    return false;
}

template<typename T>
static bool invalidptr(T* ptr) noexcept
{
    return invalidptr(reinterpret_cast<std::uintptr_t>(ptr));
}

#pragma comment(lib,"user32.lib")

// ============================================================
//  Unified key name table (VK + gamepad codes 0x1001+)
// ============================================================

inline std::map<int, const char*> key_names = {
    {1, "Left Mouse"},
    {2, "Right Mouse"},
    {3, "Cancel"},
    {4, "Middle Mouse"},
    {5, "Side Button 1"},
    {6, "Side Button 2"},
    {8, "Backspace"},
    {9, "Tab"},
    {13, "Enter"},
    {27, "Escape"},
    {32, "Spacebar"},
    {33, "Page Up"},
    {34, "Page Down"},
    {35, "End"},
    {36, "Home"},
    {37, "Left Arrow"},
    {38, "Up Arrow"},
    {39, "Right Arrow"},
    {40, "Down Arrow"},
    {45, "Insert"},
    {46, "Delete"},
    {48, "0"}, {49, "1"}, {50, "2"}, {51, "3"}, {52, "4"},
    {53, "5"}, {54, "6"}, {55, "7"}, {56, "8"}, {57, "9"},
    {65, "A"}, {66, "B"}, {67, "C"}, {68, "D"}, {69, "E"},
    {70, "F"}, {71, "G"}, {72, "H"}, {73, "I"}, {74, "J"},
    {75, "K"}, {76, "L"}, {77, "M"}, {78, "N"}, {79, "O"},
    {80, "P"}, {81, "Q"}, {82, "R"}, {83, "S"}, {84, "T"},
    {85, "U"}, {86, "V"}, {87, "W"}, {88, "X"}, {89, "Y"},
    {90, "Z"},
    {112, "F1"}, {113, "F2"}, {114, "F3"}, {115, "F4"},
    {116, "F5"}, {117, "F6"}, {118, "F7"}, {119, "F8"},
    {120, "F9"}, {121, "F10"}, {122, "F11"}, {123, "F12"},
    {96, "Numpad 0"}, {97, "Numpad 1"}, {98, "Numpad 2"},
    {99, "Numpad 3"}, {100, "Numpad 4"}, {101, "Numpad 5"},
    {102, "Numpad 6"}, {103, "Numpad 7"}, {104, "Numpad 8"},
    {105, "Numpad 9"}, {106, "Numpad *"}, {107, "Numpad +"},
    {108, "Numpad Enter"}, {109, "Numpad -"}, {110, "Numpad ."},
    {111, "Numpad /"},
    {16, "Shift"}, {17, "Ctrl"}, {18, "Alt"},
    {160, "Left Shift"}, {161, "Right Shift"},
    {162, "Left Ctrl"}, {163, "Right Ctrl"},
    {164, "Left Alt"}, {165, "Right Alt"},
    {91, "Left Windows"}, {92, "Right Windows"},
    {93, "Menu"}, {144, "Num Lock"}, {145, "Scroll Lock"},
    {20, "Caps Lock"}, {19, "Pause"}, {44, "Print Screen"},
    // Gamepad virtual codes (safe range above 0xFF)
    {0x1001, "Gamepad A"},
    {0x1002, "Gamepad B"},
    {0x1003, "Gamepad X"},
    {0x1004, "Gamepad Y"},
    {0x1005, "Gamepad LB"},
    {0x1006, "Gamepad RB"},
    {0x1007, "Gamepad LT"},
    {0x1008, "Gamepad RT"},
    {0x1009, "Gamepad D-Up"},
    {0x100A, "Gamepad D-Down"},
    {0x100B, "Gamepad D-Left"},
    {0x100C, "Gamepad D-Right"},
    {0x100D, "Gamepad Start"},
    {0x100E, "Gamepad Back"},
};

inline const char* get_key_name(int key_code)
{
    if (key_code == 0) return "...";
    auto it = key_names.find(key_code);
    if (it != key_names.end() && it->second != nullptr)
        return it->second;
    return "...";
}

// Unified keybind check: VK codes (1-255) via DMA keyboard (gaming PC) or
// GetAsyncKeyState (DMA PC fallback), gamepad codes (0x1001+) via XInput.
// Using the DMA keyboard helper is critical — GetAsyncKeyState only sees the
// DMA PC's keyboard, not the gaming PC where the user is actually playing.
inline bool is_keybind_down(int key)
{
    if (key <= 0) return false;
    if (key <= 0xFF)
    {
        c_keys* kb = mem.GetKeyboard();
        if (kb && kb->IsKeyboardWorking())
            return kb->IsKeyDown(static_cast<uint32_t>(key));
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    XINPUT_STATE xi{};
    if (XInputGetState(0, &xi) != ERROR_SUCCESS) return false;
    switch (key)
    {
    case 0x1001: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
    case 0x1002: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
    case 0x1003: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
    case 0x1004: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;
    case 0x1005: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
    case 0x1006: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
    case 0x1007: return xi.Gamepad.bLeftTrigger  > 30;
    case 0x1008: return xi.Gamepad.bRightTrigger > 30;
    case 0x1009: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP)    != 0;
    case 0x100A: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN)  != 0;
    case 0x100B: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT)  != 0;
    case 0x100C: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
    case 0x100D: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
    case 0x100E: return (xi.Gamepad.wButtons & XINPUT_GAMEPAD_BACK)  != 0;
    default:     return false;
    }
}

// Poll for first gamepad button press (for keybind capture UI)
inline bool poll_ctrl_keybind(int& out_key)
{
    XINPUT_STATE xi{};
    if (XInputGetState(0, &xi) != ERROR_SUCCESS) return false;
    static const struct { int code; WORD mask; } btns[] = {
        {0x1001, XINPUT_GAMEPAD_A},
        {0x1002, XINPUT_GAMEPAD_B},
        {0x1003, XINPUT_GAMEPAD_X},
        {0x1004, XINPUT_GAMEPAD_Y},
        {0x1005, XINPUT_GAMEPAD_LEFT_SHOULDER},
        {0x1006, XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {0x1009, XINPUT_GAMEPAD_DPAD_UP},
        {0x100A, XINPUT_GAMEPAD_DPAD_DOWN},
        {0x100B, XINPUT_GAMEPAD_DPAD_LEFT},
        {0x100C, XINPUT_GAMEPAD_DPAD_RIGHT},
        {0x100D, XINPUT_GAMEPAD_START},
        {0x100E, XINPUT_GAMEPAD_BACK},
    };
    for (auto& b : btns)
        if (xi.Gamepad.wButtons & b.mask) { out_key = b.code; return true; }
    if (xi.Gamepad.bLeftTrigger  > 30) { out_key = 0x1007; return true; }
    if (xi.Gamepad.bRightTrigger > 30) { out_key = 0x1008; return true; }
    return false;
}