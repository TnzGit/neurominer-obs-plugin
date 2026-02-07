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

#include "input-state.h"
#include <chrono>
#include <memory>
#include <vector>

class InputCapturer {
public:
	virtual ~InputCapturer() = default;

	// Start capturing keyboard/mouse input globally
	virtual bool start() = 0;

	// Stop capturing input
	virtual void stop() = 0;

	// Returns true if currently capturing
	virtual bool isRunning() const = 0;

	// Thread-safe: snapshot current state and reset per-frame deltas (dx, dy, scroll_dy)
	virtual InputState snapshotAndResetDeltas() = 0;

	// Thread-safe: peek at current state without resetting deltas (for UI preview)
	virtual InputState peekState() const = 0;

	// Set the session start timepoint for computing session_offset_us on raw events.
	// Must be called before start().
	virtual void setSessionStart(std::chrono::steady_clock::time_point t0,
				     uint64_t wallClockMs) = 0;

	// Thread-safe: drain all raw InputEvents accumulated since the last call.
	// Returns the events and clears the internal queue.
	virtual std::vector<InputEvent> drainEvents() = 0;

	// Factory: create platform-specific implementation
	static std::unique_ptr<InputCapturer> create();
};
