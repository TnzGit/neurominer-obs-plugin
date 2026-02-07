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

#include "recording-manager.h"
#include <plugin-support.h>

#include <obs.h>
#include <obs-frontend-api.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#include <sys/stat.h>
#define mkdir_p(path) mkdir(path, 0755)
#endif

// ---------------------------------------------------------------------------
// Helper: get current Unix timestamp in milliseconds
// ---------------------------------------------------------------------------
static uint64_t currentTimestampMs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// Helper: extract parent directory from a file path
// ---------------------------------------------------------------------------
static std::string parentDir(const std::string &path)
{
	size_t pos = path.find_last_of("/\\");
	if (pos == std::string::npos)
		return ".";
	return path.substr(0, pos);
}

// ---------------------------------------------------------------------------
// Helper: escape a string for JSON output
// ---------------------------------------------------------------------------
static std::string jsonEscape(const std::string &s)
{
	std::string out;
	out.reserve(s.size() + 8);
	for (char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		default:
			out += c;
		}
	}
	return out;
}

// ---------------------------------------------------------------------------
// RecordingManager
// ---------------------------------------------------------------------------

RecordingManager::RecordingManager()
{
	capturer_ = InputCapturer::create();
}

RecordingManager::~RecordingManager()
{
	if (sessionActive_) {
		obs_remove_raw_video_callback(RecordingManager::onRawVideo, this);
		if (capturer_)
			capturer_->stop();
	}
}

void RecordingManager::setGameName(const std::string &name)
{
	std::lock_guard<std::mutex> lock(mutex_);
	gameName_ = name;
}

std::string RecordingManager::gameName() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return gameName_;
}

void RecordingManager::setEnabled(bool enabled)
{
	std::lock_guard<std::mutex> lock(mutex_);
	enabled_ = enabled;
}

bool RecordingManager::isEnabled() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return enabled_;
}

bool RecordingManager::isSessionActive() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return sessionActive_;
}

InputCapturer *RecordingManager::inputCapturer() const
{
	return capturer_.get();
}

void RecordingManager::startSession()
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (!enabled_) {
		obs_log(LOG_INFO, "Input capture disabled, skipping session start.");
		return;
	}

	if (sessionActive_) {
		obs_log(LOG_WARNING, "Session already active, ignoring duplicate start.");
		return;
	}

	sessionUUID_ = generateUUID();
	startTimestampMs_ = currentTimestampMs();
	sessionStartWallMs_ = startTimestampMs_;
	frameCounter_ = 0;
	actions_.clear();
	actions_.reserve(60 * 60 * 10); // pre-allocate for ~10 minutes at 60fps
	events_.clear();
	events_.reserve(60 * 60 * 100); // generous pre-allocation for raw events

	// Get FPS from OBS and cache it for frame_pts_ms calculation
	struct obs_video_info ovi = {};
	if (obs_get_video_info(&ovi) && ovi.fps_den > 0) {
		fps_ = (double)ovi.fps_num / (double)ovi.fps_den;
	} else {
		fps_ = 60.0;
	}

	// Set session start on the capturer for sub-frame timestamps
	auto steadyNow = std::chrono::steady_clock::now();
	if (capturer_) {
		capturer_->setSessionStart(steadyNow, sessionStartWallMs_);

		if (!capturer_->start()) {
			obs_log(LOG_WARNING, "Failed to start input capturer. "
					     "Actions will be recorded with empty input states.");
		}
	}

	// Register raw video callback — fires for every composed frame with
	// the authoritative OBS compositor timestamp (video_data->timestamp).
	// Passing NULL for conversion uses the default output format.
	obs_add_raw_video_callback(NULL, RecordingManager::onRawVideo, this);

	sessionActive_ = true;
	obs_log(LOG_INFO, "Recording session started: %s (fps=%.1f)", sessionUUID_.c_str(), fps_);
}

void RecordingManager::stopSession()
{
	std::string uuid;
	std::string game;
	uint64_t totalFrames;
	uint64_t startMs;
	uint64_t endMs = currentTimestampMs();

	{
		std::lock_guard<std::mutex> lock(mutex_);

		if (!sessionActive_) {
			obs_log(LOG_INFO, "No active session to stop.");
			return;
		}

		sessionActive_ = false;
		uuid = sessionUUID_;
		game = gameName_;
		totalFrames = frameCounter_;
		startMs = startTimestampMs_;
	}

	// Remove raw video callback first
	obs_remove_raw_video_callback(RecordingManager::onRawVideo, this);

	// Final drain of remaining events before stopping the capturer
	if (capturer_) {
		auto remaining = capturer_->drainEvents();
		{
			std::lock_guard<std::mutex> lock(mutex_);
			events_.insert(events_.end(),
				       std::make_move_iterator(remaining.begin()),
				       std::make_move_iterator(remaining.end()));
		}
		capturer_->stop();
	}

	uint64_t durationMs = endMs - startMs;

	obs_log(LOG_INFO, "Recording session stopped: %s (%llu frames, %llu ms, %zu events)",
		uuid.c_str(), (unsigned long long)totalFrames, (unsigned long long)durationMs,
		events_.size());

	// Get the recording file path from OBS
	char *lastRecording = obs_frontend_get_last_recording();
	if (!lastRecording || strlen(lastRecording) == 0) {
		obs_log(LOG_ERROR, "Could not get last recording path. Files will not be organized.");
		if (lastRecording)
			bfree(lastRecording);
		return;
	}

	std::string recordingPath(lastRecording);
	bfree(lastRecording);

	obs_log(LOG_INFO, "Last recording: %s", recordingPath.c_str());

	// Create UUID directory alongside the recording
	std::string baseDir = parentDir(recordingPath);
	std::string sessionDir = baseDir + "/" + uuid;

	if (mkdir_p(sessionDir.c_str()) != 0) {
		obs_log(LOG_WARNING, "mkdir returned non-zero for: %s (may already exist)",
			sessionDir.c_str());
	}

	// Move recording file to UUID/recording.mp4
	std::string destRecording = sessionDir + "/recording.mp4";
	if (std::rename(recordingPath.c_str(), destRecording.c_str()) != 0) {
		obs_log(LOG_ERROR, "Failed to move recording from '%s' to '%s'", recordingPath.c_str(),
			destRecording.c_str());
	} else {
		obs_log(LOG_INFO, "Moved recording to: %s", destRecording.c_str());
	}

	// Write all output files
	writeActions(sessionDir);
	writeEvents(sessionDir);
	writeMetadata(sessionDir, totalFrames, durationMs);

	obs_log(LOG_INFO, "Session files written to: %s", sessionDir.c_str());
}

// ---------------------------------------------------------------------------
// Raw video callback — called for every composed frame by the OBS video thread.
// video_data->timestamp is the authoritative compositor nanosecond timestamp.
// ---------------------------------------------------------------------------
void RecordingManager::onRawVideo(void *param, struct video_data *frame)
{
	auto *self = static_cast<RecordingManager *>(param);

	// Get the OBS compositor frame timestamp (nanoseconds)
	uint64_t captureNs = frame->timestamp;

	InputState state;
	std::vector<InputEvent> newEvents;

	if (self->capturer_ && self->capturer_->isRunning()) {
		state = self->capturer_->snapshotAndResetDeltas();
		newEvents = self->capturer_->drainEvents();
	}

	std::lock_guard<std::mutex> lock(self->mutex_);

	if (!self->sessionActive_)
		return;

	// Append raw events
	if (!newEvents.empty()) {
		self->events_.insert(self->events_.end(),
				     std::make_move_iterator(newEvents.begin()),
				     std::make_move_iterator(newEvents.end()));
	}

	// Record frame-level action snapshot
	FrameAction fa;
	fa.frame = self->frameCounter_;
	fa.timestamp_ms = currentTimestampMs();
	fa.frame_pts_ms = (double)self->frameCounter_ * 1000.0 / self->fps_;
	fa.capture_ns = captureNs;
	fa.state = std::move(state);
	self->actions_.push_back(std::move(fa));

	self->frameCounter_++;
}

// ---------------------------------------------------------------------------
// UUID v4 generation
// ---------------------------------------------------------------------------
std::string RecordingManager::generateUUID()
{
	static std::random_device rd;
	static std::mt19937_64 gen(rd());
	static std::uniform_int_distribution<uint64_t> dist;

	uint64_t a = dist(gen);
	uint64_t b = dist(gen);

	// Set version (4) and variant (RFC 4122)
	a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
	b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

	char buf[40];
	std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
		      (uint32_t)(a >> 32), (uint16_t)((a >> 16) & 0xFFFF),
		      (uint16_t)(a & 0xFFFF), (uint16_t)(b >> 48),
		      (unsigned long long)(b & 0x0000FFFFFFFFFFFFULL));
	return std::string(buf);
}

// ---------------------------------------------------------------------------
// Write metadata.json
// ---------------------------------------------------------------------------
void RecordingManager::writeMetadata(const std::string &dir, uint64_t totalFrames,
				     uint64_t durationMs)
{
	struct obs_video_info ovi = {};
	obs_get_video_info(&ovi);

	double fps = (ovi.fps_den > 0) ? (double)ovi.fps_num / (double)ovi.fps_den : 60.0;

	std::string platform;
#if defined(__APPLE__)
	platform = "mac";
#elif defined(_WIN32)
	platform = "windows";
#else
	platform = "linux";
#endif

	std::ostringstream ss;
	ss << "{\n";
	ss << "  \"stream_name\": \"" << jsonEscape(sessionUUID_) << "\",\n";
	ss << "  \"game_name\": \"" << jsonEscape(gameName_) << "\",\n";
	ss << "  \"recorder_version\": \"" << jsonEscape(PLUGIN_VERSION) << "\",\n";
	ss << "  \"platform\": \"" << platform << "\",\n";
	ss << "  \"video_meta\": {\n";
	ss << "    \"width\": " << ovi.base_width << ",\n";
	ss << "    \"height\": " << ovi.base_height << ",\n";
	ss << "    \"fps\": " << std::fixed << std::setprecision(1) << fps << ",\n";
	ss << "    \"total_frames\": " << totalFrames << ",\n";
	ss << "    \"duration_ms\": " << durationMs << "\n";
	ss << "  },\n";
	ss << "  \"session_start_timestamp_ms\": " << sessionStartWallMs_ << ",\n";
	ss << "  \"input_latency_bias_ms\": 0\n";
	ss << "}\n";

	std::string path = dir + "/metadata.json";
	std::ofstream ofs(path);
	if (ofs.is_open()) {
		ofs << ss.str();
		ofs.close();
		obs_log(LOG_INFO, "Wrote metadata.json (%llu frames)", (unsigned long long)totalFrames);
	} else {
		obs_log(LOG_ERROR, "Failed to write metadata.json at: %s", path.c_str());
	}
}

// ---------------------------------------------------------------------------
// Write actions.jsonl
// ---------------------------------------------------------------------------
void RecordingManager::writeActions(const std::string &dir)
{
	std::string path = dir + "/actions.jsonl";
	std::ofstream ofs(path);
	if (!ofs.is_open()) {
		obs_log(LOG_ERROR, "Failed to write actions.jsonl at: %s", path.c_str());
		return;
	}

	for (const auto &fa : actions_) {
		ofs << frameActionToJson(fa) << "\n";
	}

	ofs.close();
	obs_log(LOG_INFO, "Wrote actions.jsonl (%zu entries)", actions_.size());
}

// ---------------------------------------------------------------------------
// Write events.jsonl (raw sub-frame input events)
// ---------------------------------------------------------------------------
void RecordingManager::writeEvents(const std::string &dir)
{
	std::string path = dir + "/events.jsonl";
	std::ofstream ofs(path);
	if (!ofs.is_open()) {
		obs_log(LOG_ERROR, "Failed to write events.jsonl at: %s", path.c_str());
		return;
	}

	for (const auto &ev : events_) {
		ofs << inputEventToJson(ev) << "\n";
	}

	ofs.close();
	obs_log(LOG_INFO, "Wrote events.jsonl (%zu entries)", events_.size());
}

// ---------------------------------------------------------------------------
// Serialize FrameAction to JSON line
// ---------------------------------------------------------------------------
std::string RecordingManager::frameActionToJson(const FrameAction &fa)
{
	std::ostringstream ss;
	ss << "{\"frame\":" << fa.frame;
	ss << ",\"timestamp_ms\":" << fa.timestamp_ms;
	ss << ",\"frame_pts_ms\":" << std::fixed << std::setprecision(1) << fa.frame_pts_ms;
	ss << ",\"capture_ns\":" << fa.capture_ns;

	// Keys array
	ss << ",\"key\":[";
	for (size_t i = 0; i < fa.state.keys.size(); i++) {
		if (i > 0)
			ss << ",";
		ss << "\"" << jsonEscape(fa.state.keys[i]) << "\"";
	}
	ss << "]";

	// Mouse object
	ss << ",\"mouse\":{";
	ss << "\"dx\":" << fa.state.mouse.dx;
	ss << ",\"dy\":" << fa.state.mouse.dy;
	ss << ",\"x\":" << fa.state.mouse.x;
	ss << ",\"y\":" << fa.state.mouse.y;
	ss << ",\"scroll_dy\":" << fa.state.mouse.scroll_dy;
	ss << ",\"button\":[";
	for (size_t i = 0; i < fa.state.mouse.buttons.size(); i++) {
		if (i > 0)
			ss << ",";
		ss << "\"" << jsonEscape(fa.state.mouse.buttons[i]) << "\"";
	}
	ss << "]";
	ss << "}";

	ss << "}";
	return ss.str();
}

// ---------------------------------------------------------------------------
// Serialize InputEvent to JSON line
// ---------------------------------------------------------------------------
std::string RecordingManager::inputEventToJson(const InputEvent &ev)
{
	std::ostringstream ss;
	ss << "{\"type\":\"" << jsonEscape(ev.type) << "\"";
	ss << ",\"timestamp_ms\":" << ev.timestamp_ms;
	ss << ",\"session_offset_us\":" << ev.session_offset_us;

	if (!ev.key.empty()) {
		ss << ",\"key\":\"" << jsonEscape(ev.key) << "\"";
	}
	if (!ev.button.empty()) {
		ss << ",\"button\":\"" << jsonEscape(ev.button) << "\"";
	}
	if (ev.type == "mouse_move") {
		ss << ",\"dx\":" << ev.dx;
		ss << ",\"dy\":" << ev.dy;
		ss << ",\"x\":" << ev.x;
		ss << ",\"y\":" << ev.y;
	}
	if (ev.type == "scroll") {
		ss << ",\"scroll_dy\":" << ev.scroll_dy;
	}

	ss << "}";
	return ss.str();
}
