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

#include <CoreGraphics/CoreGraphics.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class InputCapturerMacOS : public InputCapturer {
public:
	InputCapturerMacOS();
	~InputCapturerMacOS() override;

	bool start() override;
	void stop() override;
	bool isRunning() const override;
	InputState snapshotAndResetDeltas() override;
	InputState peekState() const override;

	void setSessionStart(std::chrono::steady_clock::time_point t0,
			     uint64_t wallClockMs) override;
	std::vector<InputEvent> drainEvents() override;

	// Check if accessibility permission is granted
	static bool hasAccessibilityPermission();

private:
	// CGEvent callback (static, delegates to instance)
	static CGEventRef eventCallback(CGEventTapProxy proxy, CGEventType type,
					CGEventRef event, void *userInfo);

	// Instance-level event handler
	void handleEvent(CGEventType type, CGEventRef event);

	// Update modifier key states based on flags
	void updateModifiers(CGEventFlags flags);

	// Push a raw InputEvent into the queue (must hold mutex_)
	void pushEvent(InputEvent &&ev);

	// Compute timestamps for a new event (must hold mutex_)
	void stampEvent(InputEvent &ev) const;

	// RunLoop thread function
	void runLoopThread();

	mutable std::mutex mutex_;
	std::atomic<bool> running_{false};

	// Event tap and run loop
	CFMachPortRef eventTap_ = nullptr;
	CFRunLoopSourceRef runLoopSource_ = nullptr;
	CFRunLoopRef runLoop_ = nullptr;
	std::thread thread_;

	// Session start references (protected by mutex_)
	std::chrono::steady_clock::time_point sessionStart_{};
	uint64_t sessionStartWallMs_ = 0;
	bool sessionStartSet_ = false;

	// Raw event queue (protected by mutex_)
	std::vector<InputEvent> eventQueue_;

	// Aggregated state (protected by mutex_)
	std::set<std::string> pressedKeys_;
	std::set<std::string> pressedButtons_;
	int mouseX_ = 0;
	int mouseY_ = 0;
	int accDx_ = 0; // accumulated delta X since last snapshot
	int accDy_ = 0; // accumulated delta Y since last snapshot
	int accScrollDy_ = 0; // accumulated scroll delta since last snapshot
};
