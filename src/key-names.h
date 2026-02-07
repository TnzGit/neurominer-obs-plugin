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
