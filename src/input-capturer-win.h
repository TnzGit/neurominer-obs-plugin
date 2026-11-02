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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class InputCapturerWin : public InputCapturer {
public:
	InputCapturerWin();
	~InputCapturerWin() override;

	bool start() override;
	void stop() override;
	bool isRunning() const override;
	InputState snapshotAndResetDeltas() override;
	InputState peekState() const override;

	void setSessionStart(std::chrono::steady_clock::time_point t0, uint64_t wallClockMs) override;
	std::vector<InputEvent> drainEvents() override;

private:
	// Low-level hook callbacks (static, delegate to singleton instance)
	static LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
	static LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam);

	// Instance-level event handlers
	void handleKeyboard(WPARAM wParam, const KBDLLHOOKSTRUCT &kbData);
	void handleMouse(WPARAM wParam, const MSLLHOOKSTRUCT &msData);

	// Push a raw InputEvent into the queue (must hold mutex_)
	void pushEvent(InputEvent &&ev);

	// Compute timestamps for a new event (must hold mutex_)
	void stampEvent(InputEvent &ev) const;

	// Hook thread function: installs hooks, runs message pump, cleans up
	void hookThreadFunc();
	void pollKeyboardLoop();

	mutable std::mutex mutex_;
	std::atomic<bool> running_{false};

	// Hook handles (only valid on hook thread)
	HHOOK keyboardHook_ = NULL;
	HHOOK mouseHook_ = NULL;

	// Hook thread
	std::thread thread_;
	std::thread keyboardPollThread_;
	std::atomic<bool> keyboardPollRunning_{false};
	std::atomic<DWORD> hookThreadId_{0};
	std::condition_variable startupCv_;
	bool startupDone_ = false;
	bool hooksReady_ = false;

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
	int lastMouseX_ = 0; // previous position for delta computation
	int lastMouseY_ = 0;
	bool hasLastMouse_ = false; // first move has no delta
	int accDx_ = 0;             // accumulated delta X since last snapshot
	int accDy_ = 0;             // accumulated delta Y since last snapshot
	int accScrollDy_ = 0;       // accumulated scroll delta since last snapshot

	// Singleton instance pointer (set during start, cleared during stop)
	// Needed because SetWindowsHookEx callbacks are plain C functions.
	static InputCapturerWin *s_instance;
};
