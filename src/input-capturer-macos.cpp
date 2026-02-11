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

#include "input-capturer-macos.h"
#include "key-names.h"
#include <obs-module.h>
#include <plugin-support.h>
#include <ApplicationServices/ApplicationServices.h>

// ---------------------------------------------------------------------------
// Factory: create platform-specific InputCapturer
// ---------------------------------------------------------------------------
std::unique_ptr<InputCapturer> InputCapturer::create()
{
	return std::make_unique<InputCapturerMacOS>();
}

// ---------------------------------------------------------------------------
// InputCapturerMacOS implementation
// ---------------------------------------------------------------------------

InputCapturerMacOS::InputCapturerMacOS() {}

InputCapturerMacOS::~InputCapturerMacOS()
{
	stop();
}

bool InputCapturerMacOS::hasAccessibilityPermission()
{
	return AXIsProcessTrusted();
}

void InputCapturerMacOS::setSessionStart(std::chrono::steady_clock::time_point t0, uint64_t wallClockMs)
{
	std::lock_guard<std::mutex> lock(mutex_);
	sessionStart_ = t0;
	sessionStartWallMs_ = wallClockMs;
	sessionStartSet_ = true;
	eventQueue_.clear();
}

std::vector<InputEvent> InputCapturerMacOS::drainEvents()
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<InputEvent> out;
	out.swap(eventQueue_);
	return out;
}

bool InputCapturerMacOS::start()
{
	if (running_.load())
		return true;

	if (!hasAccessibilityPermission()) {
		obs_log(LOG_WARNING,
			"Accessibility permission not granted. Input capture disabled. "
			"Please grant permission in System Settings > Privacy & Security > Accessibility.");
		return false;
	}

	// Event mask: keyboard + mouse + scroll
	CGEventMask eventMask = CGEventMaskBit(kCGEventKeyDown) | CGEventMaskBit(kCGEventKeyUp) |
				CGEventMaskBit(kCGEventFlagsChanged) | CGEventMaskBit(kCGEventMouseMoved) |
				CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventLeftMouseUp) |
				CGEventMaskBit(kCGEventRightMouseDown) | CGEventMaskBit(kCGEventRightMouseUp) |
				CGEventMaskBit(kCGEventOtherMouseDown) | CGEventMaskBit(kCGEventOtherMouseUp) |
				CGEventMaskBit(kCGEventLeftMouseDragged) | CGEventMaskBit(kCGEventRightMouseDragged) |
				CGEventMaskBit(kCGEventOtherMouseDragged) | CGEventMaskBit(kCGEventScrollWheel);

	eventTap_ = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly, eventMask,
				     InputCapturerMacOS::eventCallback, this);

	if (!eventTap_) {
		obs_log(LOG_ERROR, "Failed to create CGEventTap. Input capture disabled.");
		return false;
	}

	runLoopSource_ = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, eventTap_, 0);
	if (!runLoopSource_) {
		obs_log(LOG_ERROR, "Failed to create CFRunLoopSource.");
		CFRelease(eventTap_);
		eventTap_ = nullptr;
		return false;
	}

	running_.store(true);
	thread_ = std::thread(&InputCapturerMacOS::runLoopThread, this);

	obs_log(LOG_INFO, "macOS input capturer started.");
	return true;
}

void InputCapturerMacOS::stop()
{
	if (!running_.load())
		return;

	running_.store(false);

	if (runLoop_) {
		CFRunLoopStop(runLoop_);
	}

	if (thread_.joinable()) {
		thread_.join();
	}

	if (runLoopSource_) {
		CFRelease(runLoopSource_);
		runLoopSource_ = nullptr;
	}

	if (eventTap_) {
		CFRelease(eventTap_);
		eventTap_ = nullptr;
	}

	runLoop_ = nullptr;

	{
		std::lock_guard<std::mutex> lock(mutex_);
		pressedKeys_.clear();
		pressedButtons_.clear();
		accDx_ = 0;
		accDy_ = 0;
		accScrollDy_ = 0;
	}

	obs_log(LOG_INFO, "macOS input capturer stopped.");
}

bool InputCapturerMacOS::isRunning() const
{
	return running_.load();
}

InputState InputCapturerMacOS::snapshotAndResetDeltas()
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

InputState InputCapturerMacOS::peekState() const
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

void InputCapturerMacOS::runLoopThread()
{
	runLoop_ = CFRunLoopGetCurrent();
	CFRunLoopAddSource(runLoop_, runLoopSource_, kCFRunLoopCommonModes);
	CGEventTapEnable(eventTap_, true);
	CFRunLoopRun();
}

// ---------------------------------------------------------------------------
// Timestamp helpers (must hold mutex_)
// ---------------------------------------------------------------------------
void InputCapturerMacOS::stampEvent(InputEvent &ev) const
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

void InputCapturerMacOS::pushEvent(InputEvent &&ev)
{
	// Only record events if session has started
	if (sessionStartSet_) {
		eventQueue_.push_back(std::move(ev));
	}
}

// ---------------------------------------------------------------------------
// Static CGEvent callback
// ---------------------------------------------------------------------------
CGEventRef InputCapturerMacOS::eventCallback(CGEventTapProxy /*proxy*/, CGEventType type, CGEventRef event,
					     void *userInfo)
{
	auto *self = static_cast<InputCapturerMacOS *>(userInfo);

	if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
		if (self->eventTap_) {
			CGEventTapEnable(self->eventTap_, true);
		}
		return event;
	}

	self->handleEvent(type, event);
	return event;
}

// ---------------------------------------------------------------------------
// Instance-level event handler
// ---------------------------------------------------------------------------
void InputCapturerMacOS::handleEvent(CGEventType type, CGEventRef event)
{
	std::lock_guard<std::mutex> lock(mutex_);

	switch (type) {
	// --- Keyboard ---
	case kCGEventKeyDown: {
		uint16_t keyCode = (uint16_t)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
		std::string keyName = macKeyCodeToName(keyCode);
		pressedKeys_.insert(keyName);

		InputEvent ev;
		ev.type = "key_down";
		ev.key = keyName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventKeyUp: {
		uint16_t keyCode = (uint16_t)CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode);
		std::string keyName = macKeyCodeToName(keyCode);
		pressedKeys_.erase(keyName);

		InputEvent ev;
		ev.type = "key_up";
		ev.key = keyName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventFlagsChanged: {
		CGEventFlags flags = CGEventGetFlags(event);
		updateModifiers(flags);

		InputEvent ev;
		ev.type = "flags_changed";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Mouse movement ---
	case kCGEventMouseMoved:
	case kCGEventLeftMouseDragged:
	case kCGEventRightMouseDragged:
	case kCGEventOtherMouseDragged: {
		CGPoint location = CGEventGetLocation(event);
		mouseX_ = (int)location.x;
		mouseY_ = (int)location.y;

		int64_t dx = CGEventGetIntegerValueField(event, kCGMouseEventDeltaX);
		int64_t dy = CGEventGetIntegerValueField(event, kCGMouseEventDeltaY);
		accDx_ += (int)dx;
		accDy_ += (int)dy;

		InputEvent ev;
		ev.type = "mouse_move";
		ev.dx = (int)dx;
		ev.dy = (int)dy;
		ev.x = mouseX_;
		ev.y = mouseY_;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Mouse buttons ---
	case kCGEventLeftMouseDown: {
		pressedButtons_.insert("LeftButton");
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = "LeftButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventLeftMouseUp: {
		pressedButtons_.erase("LeftButton");
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = "LeftButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventRightMouseDown: {
		pressedButtons_.insert("RightButton");
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = "RightButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventRightMouseUp: {
		pressedButtons_.erase("RightButton");
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = "RightButton";
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventOtherMouseDown: {
		int64_t btn = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
		std::string btnName = "Button" + std::to_string(btn);
		pressedButtons_.insert(btnName);
		InputEvent ev;
		ev.type = "mouse_button_down";
		ev.button = btnName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}
	case kCGEventOtherMouseUp: {
		int64_t btn = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber);
		std::string btnName = "Button" + std::to_string(btn);
		pressedButtons_.erase(btnName);
		InputEvent ev;
		ev.type = "mouse_button_up";
		ev.button = btnName;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	// --- Scroll wheel ---
	case kCGEventScrollWheel: {
		int64_t scrollDy = CGEventGetIntegerValueField(event, kCGScrollWheelEventDeltaAxis1);
		accScrollDy_ += (int)scrollDy;

		InputEvent ev;
		ev.type = "scroll";
		ev.scroll_dy = (int)scrollDy;
		stampEvent(ev);
		pushEvent(std::move(ev));
		break;
	}

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Update modifier key states based on current flags
// ---------------------------------------------------------------------------
void InputCapturerMacOS::updateModifiers(CGEventFlags flags)
{
	auto setMod = [this](bool pressed, const std::string &name) {
		if (pressed)
			pressedKeys_.insert(name);
		else
			pressedKeys_.erase(name);
	};

	setMod(flags & kCGEventFlagMaskShift, "LeftShift");
	setMod(flags & kCGEventFlagMaskControl, "LeftControl");
	setMod(flags & kCGEventFlagMaskAlternate, "LeftOption");
	setMod(flags & kCGEventFlagMaskCommand, "LeftCommand");
	setMod(flags & kCGEventFlagMaskAlphaShift, "CapsLock");
	setMod(flags & kCGEventFlagMaskSecondaryFn, "Function");
}
