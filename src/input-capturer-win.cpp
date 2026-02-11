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
#include "key-names.h"
#include <obs-module.h>
#include <plugin-support.h>

// ---------------------------------------------------------------------------
// Singleton instance pointer for hook callbacks
// ---------------------------------------------------------------------------
InputCapturerWin *InputCapturerWin::s_instance = nullptr;

// ---------------------------------------------------------------------------
// Factory: create platform-specific InputCapturer
// ---------------------------------------------------------------------------
std::unique_ptr<InputCapturer> InputCapturer::create()
{
	return std::make_unique<InputCapturerWin>();
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

InputCapturerWin::InputCapturerWin() {}

InputCapturerWin::~InputCapturerWin()
{
	stop();
}

// ---------------------------------------------------------------------------
// setSessionStart / drainEvents
// ---------------------------------------------------------------------------

void InputCapturerWin::setSessionStart(std::chrono::steady_clock::time_point t0, uint64_t wallClockMs)
{
	std::lock_guard<std::mutex> lock(mutex_);
	sessionStart_ = t0;
	sessionStartWallMs_ = wallClockMs;
	sessionStartSet_ = true;
	eventQueue_.clear();
}

std::vector<InputEvent> InputCapturerWin::drainEvents()
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<InputEvent> out;
	out.swap(eventQueue_);
	return out;
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------

bool InputCapturerWin::start()
{
	if (running_.load())
		return true;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		startupDone_ = false;
		hooksReady_ = false;
	}

	s_instance = this;
	running_.store(true);

	// Launch the hook thread. Hooks and the message pump live on this thread.
	thread_ = std::thread(&InputCapturerWin::hookThreadFunc, this);

	{
		std::unique_lock<std::mutex> lock(mutex_);
		// Wait for the hook thread to report startup result.
		if (!startupCv_.wait_for(lock, std::chrono::milliseconds(2000), [this]() { return startupDone_; })) {
			lock.unlock();
			running_.store(false);
			DWORD threadId = hookThreadId_.load();
			if (threadId != 0) {
				PostThreadMessageW(threadId, WM_QUIT, 0, 0);
			}
			if (thread_.joinable()) {
				thread_.join();
			}
			hookThreadId_.store(0);
			s_instance = nullptr;
			obs_log(LOG_ERROR, "Windows input capturer startup timed out.");
			return false;
		}

		if (!hooksReady_) {
			lock.unlock();
			running_.store(false);
			if (thread_.joinable()) {
				thread_.join();
			}
			hookThreadId_.store(0);
			s_instance = nullptr;
			obs_log(LOG_ERROR, "Windows input capturer failed to install hooks.");
			return false;
		}
	}

	obs_log(LOG_INFO, "Windows input capturer started.");
	return true;
}

void InputCapturerWin::stop()
{
	if (!running_.load() && !thread_.joinable())
		return;

	running_.store(false);

	// Tell the hook thread's message pump to quit
	DWORD threadId = 0;
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!startupDone_ && thread_.joinable()) {
			startupCv_.wait_for(lock, std::chrono::milliseconds(500), [this]() { return startupDone_; });
		}
		threadId = hookThreadId_.load();
	}

	if (threadId != 0) {
		PostThreadMessageW(threadId, WM_QUIT, 0, 0);
	}

	if (thread_.joinable()) {
		thread_.join();
	}

	hookThreadId_.store(0);
	s_instance = nullptr;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		pressedKeys_.clear();
		pressedButtons_.clear();
		accDx_ = 0;
		accDy_ = 0;
		accScrollDy_ = 0;
		hasLastMouse_ = false;
	}

	obs_log(LOG_INFO, "Windows input capturer stopped.");
}

bool InputCapturerWin::isRunning() const
{
	return running_.load();
}

// ---------------------------------------------------------------------------
// Snapshot / Peek
// ---------------------------------------------------------------------------

InputState InputCapturerWin::snapshotAndResetDeltas()
{
	std::lock_guard<std::mutex> lock(mutex_);

	InputState state;
	state.keys.assign(pressedKeys_.begin(), pressedKeys_.end());
	state.mouse.dx = accDx_;
	state.mouse.dy = accDy_;
	state.mouse.x = mouseX_;
	state.mouse.y = mouseY_;
	state.mouse.scroll_dy = accScrollDy_;
	state.mouse.buttons.assign(pressedButtons_.begin(), pressedButtons_.end());

	accDx_ = 0;
	accDy_ = 0;
	accScrollDy_ = 0;

	return state;
}

InputState InputCapturerWin::peekState() const
{
	std::lock_guard<std::mutex> lock(mutex_);

	InputState state;
	state.keys.assign(pressedKeys_.begin(), pressedKeys_.end());
	state.mouse.dx = accDx_;
	state.mouse.dy = accDy_;
	state.mouse.x = mouseX_;
	state.mouse.y = mouseY_;
	state.mouse.scroll_dy = accScrollDy_;
	state.mouse.buttons.assign(pressedButtons_.begin(), pressedButtons_.end());

	return state;
}

// ---------------------------------------------------------------------------
// Hook thread: installs hooks, runs message pump, removes hooks on exit
// ---------------------------------------------------------------------------

void InputCapturerWin::hookThreadFunc()
{
	const DWORD threadId = GetCurrentThreadId();
	MSG initMsg;
	PeekMessageW(&initMsg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

	// Install low-level keyboard hook
	keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, InputCapturerWin::keyboardProc, NULL, 0);
	if (!keyboardHook_) {
		obs_log(LOG_ERROR, "Failed to install keyboard hook (error %lu).", GetLastError());
	}

	// Install low-level mouse hook
	mouseHook_ = SetWindowsHookExW(WH_MOUSE_LL, InputCapturerWin::mouseProc, NULL, 0);
	if (!mouseHook_) {
		obs_log(LOG_ERROR, "Failed to install mouse hook (error %lu).", GetLastError());
	}

	{
		std::lock_guard<std::mutex> lock(mutex_);
		hookThreadId_.store(threadId);
		hooksReady_ = (keyboardHook_ != NULL) && (mouseHook_ != NULL);
		startupDone_ = true;
	}
	startupCv_.notify_all();

	if (!hooksReady_) {
		if (keyboardHook_) {
			UnhookWindowsHookEx(keyboardHook_);
			keyboardHook_ = NULL;
		}
		if (mouseHook_) {
			UnhookWindowsHookEx(mouseHook_);
			mouseHook_ = NULL;
		}
		hookThreadId_.store(0);
		obs_log(LOG_ERROR, "Windows hooks startup failed (kbd=%p, mouse=%p).", (void *)keyboardHook_,
			(void *)mouseHook_);
		return;
	}

	obs_log(LOG_INFO, "Windows hooks installed (kbd=%p, mouse=%p).", (void *)keyboardHook_, (void *)mouseHook_);

	// Message pump — required for low-level hooks to receive events.
	// Runs until WM_QUIT is posted (from stop()).
	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	// Clean up hooks
	if (keyboardHook_) {
		UnhookWindowsHookEx(keyboardHook_);
		keyboardHook_ = NULL;
	}
	if (mouseHook_) {
		UnhookWindowsHookEx(mouseHook_);
		mouseHook_ = NULL;
	}

	hookThreadId_.store(0);
	obs_log(LOG_INFO, "Windows hooks removed, hook thread exiting.");
}

// ---------------------------------------------------------------------------
// Timestamp helpers (must hold mutex_)
// ---------------------------------------------------------------------------

void InputCapturerWin::stampEvent(InputEvent &ev) const
{
	using namespace std::chrono;
	auto now = steady_clock::now();

	// Wall clock
	auto wallNow = system_clock::now();
	ev.timestamp_ms = (uint64_t)duration_cast<milliseconds>(wallNow.time_since_epoch()).count();

	// Session-relative offset
	if (sessionStartSet_) {
		ev.session_offset_us = (int64_t)duration_cast<microseconds>(now - sessionStart_).count();
	} else {
		ev.session_offset_us = 0;
	}
}

void InputCapturerWin::pushEvent(InputEvent &&ev)
{
	// Only record events if session has started
	if (sessionStartSet_) {
		eventQueue_.push_back(std::move(ev));
	}
}

// ---------------------------------------------------------------------------
// Static keyboard hook callback
// ---------------------------------------------------------------------------

LRESULT CALLBACK InputCapturerWin::keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && s_instance) {
		const auto &kbData = *reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
		s_instance->handleKeyboard(wParam, kbData);
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Static mouse hook callback
// ---------------------------------------------------------------------------

LRESULT CALLBACK InputCapturerWin::mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HC_ACTION && s_instance) {
		const auto &msData = *reinterpret_cast<MSLLHOOKSTRUCT *>(lParam);
		s_instance->handleMouse(wParam, msData);
	}
	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Instance-level keyboard handler
// ---------------------------------------------------------------------------

void InputCapturerWin::handleKeyboard(WPARAM wParam, const KBDLLHOOKSTRUCT &kbData)
{
	// Ignore synthetic/injected keyboard events to capture physical input only.
	if ((kbData.flags & LLKHF_INJECTED) != 0 || (kbData.flags & LLKHF_LOWER_IL_INJECTED) != 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);

	DWORD vk = kbData.vkCode;
	DWORD scan = kbData.scanCode;
	bool extended = (kbData.flags & LLKHF_EXTENDED) != 0;

	std::string keyName = winVkCodeToName(vk, scan, extended);

	switch (wParam) {
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN: {
		pressedKeys_.insert(keyName);

		InputEvent ev;
		ev.type = "key_down";
		ev.key = keyName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case WM_KEYUP:
	case WM_SYSKEYUP: {
		pressedKeys_.erase(keyName);

		InputEvent ev;
		ev.type = "key_up";
		ev.key = keyName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Instance-level mouse handler
// ---------------------------------------------------------------------------

void InputCapturerWin::handleMouse(WPARAM wParam, const MSLLHOOKSTRUCT &msData)
{
	// Ignore synthetic/injected mouse events to capture physical input only.
	if ((msData.flags & LLMHF_INJECTED) != 0 || (msData.flags & LLMHF_LOWER_IL_INJECTED) != 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(mutex_);

	switch (wParam) {
	// --- Mouse movement ---
	case WM_MOUSEMOVE: {
		int newX = (int)msData.pt.x;
		int newY = (int)msData.pt.y;

		int dx = 0, dy = 0;
		if (hasLastMouse_) {
			dx = newX - lastMouseX_;
			dy = newY - lastMouseY_;
		}

		lastMouseX_ = newX;
		lastMouseY_ = newY;
		hasLastMouse_ = true;

		mouseX_ = newX;
		mouseY_ = newY;
		accDx_ += dx;
		accDy_ += dy;

		InputEvent ev;
		ev.type = "mouse_move";
		ev.dx = dx;
		ev.dy = dy;
		ev.x = newX;
		ev.y = newY;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Left button ---
	case WM_LBUTTONDOWN: {
		pressedButtons_.insert("LeftButton");
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = "LeftButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case WM_LBUTTONUP: {
		pressedButtons_.erase("LeftButton");
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = "LeftButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Right button ---
	case WM_RBUTTONDOWN: {
		pressedButtons_.insert("RightButton");
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = "RightButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case WM_RBUTTONUP: {
		pressedButtons_.erase("RightButton");
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = "RightButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Middle button ---
	case WM_MBUTTONDOWN: {
		pressedButtons_.insert("MiddleButton");
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = "MiddleButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case WM_MBUTTONUP: {
		pressedButtons_.erase("MiddleButton");
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = "MiddleButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- X buttons (side buttons) ---
	case WM_XBUTTONDOWN: {
		WORD xButton = HIWORD(msData.mouseData);
		std::string btnName = (xButton == XBUTTON1) ? "XButton1" : "XButton2";
		pressedButtons_.insert(btnName);
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = btnName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case WM_XBUTTONUP: {
		WORD xButton = HIWORD(msData.mouseData);
		std::string btnName = (xButton == XBUTTON1) ? "XButton1" : "XButton2";
		pressedButtons_.erase(btnName);
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = btnName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Scroll wheel ---
	case WM_MOUSEWHEEL: {
		// mouseData contains the wheel delta in the high word (signed)
		short wheelDelta = (short)HIWORD(msData.mouseData);
		// Preserve raw wheel precision instead of normalizing to +/-1.
		int scrollDy = (int)wheelDelta;

		accScrollDy_ += scrollDy;

		InputEvent ev;
		ev.type = "scroll";
		ev.scroll_dy = scrollDy;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Horizontal scroll (rare but handle gracefully) ---
	case WM_MOUSEHWHEEL:
		// Ignore horizontal scroll for now — not in the data spec
		break;

	default:
		break;
	}
}
