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

#include <cstdint>
#include <string>
#include <vector>

struct MouseState {
	int dx = 0;        // relative X movement since last frame
	int dy = 0;        // relative Y movement since last frame
	int x = 0;         // absolute X position
	int y = 0;         // absolute Y position
	int scroll_dy = 0; // scroll wheel delta

	std::vector<std::string> buttons; // currently pressed mouse buttons
};

struct InputState {
	std::vector<std::string> keys; // currently held keyboard keys
	MouseState mouse;
};

// Raw input event with sub-frame precision timestamps.
// Recorded independently of frame boundaries for post-processing alignment.
struct InputEvent {
	// Event type: "key_down", "key_up", "flags_changed",
	//   "mouse_move", "mouse_button_down", "mouse_button_up", "scroll"
	std::string type;
	// "hook" uses Windows keyboard/mouse callbacks; "poll" is a periodic keyboard-state fallback.
	std::string source = "hook";
	bool injected = false;

	uint64_t timestamp_ms = 0;     // Unix wall clock (ms) — absolute reference
	int64_t session_offset_us = 0; // microseconds since session start (steady_clock)

	// Keyboard fields
	std::string key; // e.g. "W", "LeftShift"

	// Mouse fields
	std::string button; // e.g. "LeftButton"
	int dx = 0, dy = 0;
	int x = 0, y = 0;
	int scroll_dy = 0;
};
