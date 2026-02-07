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

#include "input-capturer.h"
#include <atomic>

// Stub implementation for Linux — to be implemented with /dev/input or XInput2
class InputCapturerLinux : public InputCapturer {
public:
	bool start() override;
	void stop() override;
	bool isRunning() const override;
	InputState snapshotAndResetDeltas() override;
	InputState peekState() const override;
	void setSessionStart(std::chrono::steady_clock::time_point t0,
			     uint64_t wallClockMs) override;
	std::vector<InputEvent> drainEvents() override;

private:
	std::atomic<bool> running_{false};
};
