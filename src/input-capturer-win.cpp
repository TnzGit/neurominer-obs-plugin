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

#include "input-capturer-win.h"
#include <obs-module.h>
#include <plugin-support.h>

std::unique_ptr<InputCapturer> InputCapturer::create()
{
	return std::make_unique<InputCapturerWin>();
}

bool InputCapturerWin::start()
{
	obs_log(LOG_WARNING, "Windows input capture not yet implemented.");
	running_.store(true);
	return true;
}

void InputCapturerWin::stop()
{
	running_.store(false);
}

bool InputCapturerWin::isRunning() const
{
	return running_.load();
}

InputState InputCapturerWin::snapshotAndResetDeltas()
{
	return InputState{};
}

InputState InputCapturerWin::peekState() const
{
	return InputState{};
}

void InputCapturerWin::setSessionStart(std::chrono::steady_clock::time_point /*t0*/,
				       uint64_t /*wallClockMs*/)
{
}

std::vector<InputEvent> InputCapturerWin::drainEvents()
{
	return {};
}
