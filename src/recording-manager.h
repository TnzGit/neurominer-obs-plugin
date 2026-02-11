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
#include "input-state.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declaration — full definition lives in OBS media-io headers
struct video_data;

// One frame's worth of recorded action data
struct FrameAction {
	uint64_t frame;
	uint64_t timestamp_ms; // wall clock (ms) for human reference
	double frame_pts_ms;   // deterministic PTS = frame * 1000.0 / fps
	uint64_t capture_ns;   // OBS compositor timestamp (ns) from video_data->timestamp
	InputState state;
};

class RecordingManager {
public:
	RecordingManager();
	~RecordingManager();

	// Set the game name (from UI)
	void setGameName(const std::string &name);
	std::string gameName() const;

	// Enable / disable the capture feature
	void setEnabled(bool enabled);
	bool isEnabled() const;

	// Called when OBS starts recording
	void startSession();

	// Called when OBS stops recording
	void stopSession();

	// Whether a session is currently active
	bool isSessionActive() const;

	// Get a reference to the input capturer (for UI preview)
	InputCapturer *inputCapturer() const;

private:
	// OBS raw video callback — called for every composed frame with exact timestamp
	static void onRawVideo(void *param, struct video_data *frame);

	// Generate a v4 UUID string
	static std::string generateUUID();

	// Write metadata.json to the given directory
	void writeMetadata(const std::string &dir, uint64_t totalFrames, uint64_t durationMs);

	// Write actions.jsonl to the given directory
	void writeActions(const std::string &dir);

	// Write events.jsonl (raw sub-frame input events) to the given directory
	void writeEvents(const std::string &dir);

	// Serialize a single FrameAction to a JSON line
	static std::string frameActionToJson(const FrameAction &fa);

	// Serialize a single InputEvent to a JSON line
	static std::string inputEventToJson(const InputEvent &ev);

	std::unique_ptr<InputCapturer> capturer_;

	mutable std::mutex mutex_;
	bool enabled_ = true;
	bool sessionActive_ = false;
	std::string gameName_ = "Unknown";
	std::string sessionUUID_;
	uint64_t startTimestampMs_ = 0;
	uint64_t sessionStartWallMs_ = 0;
	uint64_t frameCounter_ = 0;
	double fps_ = 60.0;

	std::vector<FrameAction> actions_;
	std::vector<InputEvent> events_; // raw sub-frame event stream
};
