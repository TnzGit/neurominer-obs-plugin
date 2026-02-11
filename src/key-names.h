/*
NeuroMiner OBS Plugin
Copyright (C) 2026 NeuroMiner <info@neurominer.ai>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#ifdef __APPLE__

#include <cstdint>
#include <string>
#include <unordered_map>

// macOS virtual keycode -> human-readable key name mapping
// Reference: Events.h / Carbon HIToolbox
static const std::unordered_map<uint16_t, std::string> kMacKeyNames = {
	// Letters
	{0x00, "A"},
	{0x01, "S"},
	{0x02, "D"},
	{0x03, "F"},
	{0x04, "H"},
	{0x05, "G"},
	{0x06, "Z"},
	{0x07, "X"},
	{0x08, "C"},
	{0x09, "V"},
	{0x0B, "B"},
	{0x0C, "Q"},
	{0x0D, "W"},
	{0x0E, "E"},
	{0x0F, "R"},
	{0x10, "Y"},
	{0x11, "T"},
	{0x12, "1"},
	{0x13, "2"},
	{0x14, "3"},
	{0x15, "4"},
	{0x16, "6"},
	{0x17, "5"},
	{0x18, "Equal"},
	{0x19, "9"},
	{0x1A, "7"},
	{0x1B, "Minus"},
	{0x1C, "8"},
	{0x1D, "0"},
	{0x1E, "RightBracket"},
	{0x1F, "O"},
	{0x20, "U"},
	{0x21, "LeftBracket"},
	{0x22, "I"},
	{0x23, "P"},
	{0x25, "L"},
	{0x26, "J"},
	{0x27, "Quote"},
	{0x28, "K"},
	{0x29, "Semicolon"},
	{0x2A, "Backslash"},
	{0x2B, "Comma"},
	{0x2C, "Slash"},
	{0x2D, "N"},
	{0x2E, "M"},
	{0x2F, "Period"},

	// Special keys
	{0x24, "Return"},
	{0x30, "Tab"},
	{0x31, "Space"},
	{0x33, "Delete"},
	{0x35, "Escape"},
	{0x36, "RightCommand"},
	{0x37, "LeftCommand"},
	{0x38, "LeftShift"},
	{0x39, "CapsLock"},
	{0x3A, "LeftOption"},
	{0x3B, "LeftControl"},
	{0x3C, "RightShift"},
	{0x3D, "RightOption"},
	{0x3E, "RightControl"},
	{0x3F, "Function"},

	// Function keys
	{0x7A, "F1"},
	{0x78, "F2"},
	{0x63, "F3"},
	{0x76, "F4"},
	{0x60, "F5"},
	{0x61, "F6"},
	{0x62, "F7"},
	{0x64, "F8"},
	{0x65, "F9"},
	{0x6D, "F10"},
	{0x67, "F11"},
	{0x6F, "F12"},
	{0x69, "F13"},
	{0x6B, "F14"},
	{0x71, "F15"},

	// Arrow keys
	{0x7B, "LeftArrow"},
	{0x7C, "RightArrow"},
	{0x7D, "DownArrow"},
	{0x7E, "UpArrow"},

	// Navigation
	{0x72, "Help"},
	{0x73, "Home"},
	{0x74, "PageUp"},
	{0x75, "ForwardDelete"},
	{0x77, "End"},
	{0x79, "PageDown"},

	// Numpad
	{0x41, "KeypadDecimal"},
	{0x43, "KeypadMultiply"},
	{0x45, "KeypadPlus"},
	{0x47, "KeypadClear"},
	{0x4B, "KeypadDivide"},
	{0x4C, "KeypadEnter"},
	{0x4E, "KeypadMinus"},
	{0x51, "KeypadEquals"},
	{0x52, "Keypad0"},
	{0x53, "Keypad1"},
	{0x54, "Keypad2"},
	{0x55, "Keypad3"},
	{0x56, "Keypad4"},
	{0x57, "Keypad5"},
	{0x58, "Keypad6"},
	{0x59, "Keypad7"},
	{0x5B, "Keypad8"},
	{0x5C, "Keypad9"},

	// Misc
	{0x0A, "SectionSign"},
	{0x32, "Grave"},
};

inline std::string macKeyCodeToName(uint16_t keyCode)
{
	auto it = kMacKeyNames.find(keyCode);
	if (it != kMacKeyNames.end()) {
		return it->second;
	}
	return "Key" + std::to_string(keyCode);
}

#endif // __APPLE__

// ===========================================================================
// Windows: VK virtual-key code -> human-readable key name mapping
// ===========================================================================
#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <unordered_map>

// Primary VK code -> name mapping.
// Key names are kept consistent with the macOS mapping where possible.
static const std::unordered_map<DWORD, std::string> kWinKeyNames = {
	// Letters (VK_A .. VK_Z)
	{0x41, "A"},
	{0x42, "B"},
	{0x43, "C"},
	{0x44, "D"},
	{0x45, "E"},
	{0x46, "F"},
	{0x47, "G"},
	{0x48, "H"},
	{0x49, "I"},
	{0x4A, "J"},
	{0x4B, "K"},
	{0x4C, "L"},
	{0x4D, "M"},
	{0x4E, "N"},
	{0x4F, "O"},
	{0x50, "P"},
	{0x51, "Q"},
	{0x52, "R"},
	{0x53, "S"},
	{0x54, "T"},
	{0x55, "U"},
	{0x56, "V"},
	{0x57, "W"},
	{0x58, "X"},
	{0x59, "Y"},
	{0x5A, "Z"},

	// Digits (top row)
	{0x30, "0"},
	{0x31, "1"},
	{0x32, "2"},
	{0x33, "3"},
	{0x34, "4"},
	{0x35, "5"},
	{0x36, "6"},
	{0x37, "7"},
	{0x38, "8"},
	{0x39, "9"},

	// Function keys
	{VK_F1, "F1"},
	{VK_F2, "F2"},
	{VK_F3, "F3"},
	{VK_F4, "F4"},
	{VK_F5, "F5"},
	{VK_F6, "F6"},
	{VK_F7, "F7"},
	{VK_F8, "F8"},
	{VK_F9, "F9"},
	{VK_F10, "F10"},
	{VK_F11, "F11"},
	{VK_F12, "F12"},
	{VK_F13, "F13"},
	{VK_F14, "F14"},
	{VK_F15, "F15"},
	{VK_F16, "F16"},
	{VK_F17, "F17"},
	{VK_F18, "F18"},
	{VK_F19, "F19"},
	{VK_F20, "F20"},
	{VK_F21, "F21"},
	{VK_F22, "F22"},
	{VK_F23, "F23"},
	{VK_F24, "F24"},

	// Special keys
	{VK_RETURN, "Return"},
	{VK_TAB, "Tab"},
	{VK_SPACE, "Space"},
	{VK_BACK, "Delete"},          // Backspace — matches macOS "Delete"
	{VK_DELETE, "ForwardDelete"}, // Del key — matches macOS "ForwardDelete"
	{VK_ESCAPE, "Escape"},
	{VK_CAPITAL, "CapsLock"},

	// Arrow keys
	{VK_LEFT, "LeftArrow"},
	{VK_RIGHT, "RightArrow"},
	{VK_UP, "UpArrow"},
	{VK_DOWN, "DownArrow"},

	// Navigation
	{VK_HOME, "Home"},
	{VK_END, "End"},
	{VK_PRIOR, "PageUp"},
	{VK_NEXT, "PageDown"},
	{VK_INSERT, "Insert"},
	{VK_HELP, "Help"},

	// Modifier keys (generic VK codes — left/right disambiguation handled separately)
	{VK_LSHIFT, "LeftShift"},
	{VK_RSHIFT, "RightShift"},
	{VK_LCONTROL, "LeftControl"},
	{VK_RCONTROL, "RightControl"},
	{VK_LMENU, "LeftAlt"}, // Alt — Windows equivalent of macOS Option
	{VK_RMENU, "RightAlt"},
	{VK_LWIN, "LeftCommand"}, // Win key — mapped to "Command" for consistency
	{VK_RWIN, "RightCommand"},

	// Numpad
	{VK_NUMPAD0, "Keypad0"},
	{VK_NUMPAD1, "Keypad1"},
	{VK_NUMPAD2, "Keypad2"},
	{VK_NUMPAD3, "Keypad3"},
	{VK_NUMPAD4, "Keypad4"},
	{VK_NUMPAD5, "Keypad5"},
	{VK_NUMPAD6, "Keypad6"},
	{VK_NUMPAD7, "Keypad7"},
	{VK_NUMPAD8, "Keypad8"},
	{VK_NUMPAD9, "Keypad9"},
	{VK_MULTIPLY, "KeypadMultiply"},
	{VK_ADD, "KeypadPlus"},
	{VK_SUBTRACT, "KeypadMinus"},
	{VK_DECIMAL, "KeypadDecimal"},
	{VK_DIVIDE, "KeypadDivide"},
	{VK_NUMLOCK, "NumLock"},

	// OEM keys (US layout — symbols vary by keyboard layout)
	{VK_OEM_1, "Semicolon"},    // ;:
	{VK_OEM_2, "Slash"},        // /?
	{VK_OEM_3, "Grave"},        // `~
	{VK_OEM_4, "LeftBracket"},  // [{
	{VK_OEM_5, "Backslash"},    // \|
	{VK_OEM_6, "RightBracket"}, // ]}
	{VK_OEM_7, "Quote"},        // '"
	{VK_OEM_PLUS, "Equal"},     // =+
	{VK_OEM_COMMA, "Comma"},    // ,<
	{VK_OEM_MINUS, "Minus"},    // -_
	{VK_OEM_PERIOD, "Period"},  // .>

	// Misc
	{VK_SNAPSHOT, "PrintScreen"},
	{VK_SCROLL, "ScrollLock"},
	{VK_PAUSE, "Pause"},
	{VK_APPS, "Menu"}, // Context menu key
};

// Resolve a Windows VK code to a human-readable key name.
// Uses the extended flag from KBDLLHOOKSTRUCT to disambiguate left/right
// variants of Shift, Control, and Alt (Menu) when the generic VK is used.
inline std::string winVkCodeToName(DWORD vkCode, DWORD scanCode, bool extended)
{
	// Disambiguate generic modifier VK codes
	switch (vkCode) {
	case VK_SHIFT:
		// MapVirtualKey can distinguish left/right shift by scan code
		vkCode = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
		if (vkCode == 0)
			vkCode = VK_LSHIFT;
		break;
	case VK_CONTROL:
		vkCode = extended ? (DWORD)VK_RCONTROL : (DWORD)VK_LCONTROL;
		break;
	case VK_MENU: // Alt
		vkCode = extended ? (DWORD)VK_RMENU : (DWORD)VK_LMENU;
		break;
	}

	auto it = kWinKeyNames.find(vkCode);
	if (it != kWinKeyNames.end()) {
		return it->second;
	}
	return "VK" + std::to_string(vkCode);
}

#endif // _WIN32
