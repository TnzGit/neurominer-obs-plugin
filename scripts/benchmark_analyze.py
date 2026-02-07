#!/usr/bin/env python3
"""
benchmark_analyze.py — NeuroMiner Ground Truth Latency Analyzer

Analyzes a benchmark recording session to measure the exact input-to-photon
latency of the recording pipeline. Designed to work with benchmark_app.py.

Algorithm:
  1. Read events.jsonl → find all SPACE key_down events (toggle triggers)
  2. Decode recording.mp4 → compute mean brightness per frame
  3. Detect brightness transitions (BLACK→WHITE and WHITE→BLACK)
  4. Match each SPACE event to the next brightness transition in the video
  5. Compute latency = transition_frame_capture_ns - event_session_offset_us
  6. Report statistics

The output tells you the EXACT pipeline latency:
  - CGEventTap capture → OBS compositor → video frame
  - This is the bias that should be compensated in training data.

Usage:
    python scripts/benchmark_analyze.py /path/to/<UUID>/

Dependencies:
    pip install opencv-python numpy
"""

import argparse
import json
import sys
from pathlib import Path

import cv2
import numpy as np


def load_events(path: str) -> list[dict]:
    """Load events.jsonl."""
    events = []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return events


def load_actions(path: str) -> list[dict]:
    """Load actions jsonl file."""
    actions = []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if line:
                actions.append(json.loads(line))
    return actions


def load_metadata(path: str) -> dict:
    """Load metadata.json."""
    with open(path, "r") as f:
        return json.load(f)


def compute_brightness(video_path: str) -> tuple[list[float], float, int]:
    """
    Decode the video and compute mean brightness (grayscale) per frame.
    Returns (brightness_list, fps, total_frames).
    """
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    if fps <= 0:
        fps = 60.0

    brightness_list = []
    frame_idx = 0

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        mean_brightness = float(np.mean(gray))
        brightness_list.append(mean_brightness)
        frame_idx += 1

        if frame_idx % 500 == 0:
            print(f"  Processed {frame_idx} frames...")

    cap.release()
    print(f"  Total frames: {frame_idx}")
    return brightness_list, fps, frame_idx


def detect_transitions(
    brightness: list[float],
    low_threshold: float = 64.0,
    high_threshold: float = 192.0,
) -> list[dict]:
    """
    Detect BLACK→WHITE and WHITE→BLACK transitions.

    A transition occurs when brightness crosses from below low_threshold
    to above high_threshold, or vice versa.

    Returns list of {"frame": int, "direction": "to_white"|"to_black"}.
    """
    if len(brightness) < 2:
        return []

    transitions = []
    prev_state = "black" if brightness[0] < 128 else "white"

    for i in range(1, len(brightness)):
        curr = brightness[i]

        if prev_state == "black" and curr > high_threshold:
            transitions.append({"frame": i, "direction": "to_white"})
            prev_state = "white"
        elif prev_state == "white" and curr < low_threshold:
            transitions.append({"frame": i, "direction": "to_black"})
            prev_state = "black"

    return transitions


def match_events_to_transitions(
    space_events: list[dict],
    transitions: list[dict],
    actions: list[dict],
    fps: float,
    session_start_ms: int,
    max_latency_ms: float = 500.0,
) -> list[dict]:
    """
    Match each SPACE key_down event to the next video brightness transition.

    Uses capture_ns from actions_resampled.jsonl (the OBS compositor timestamp) for
    precise frame timing. Falls back to frame_pts_ms if capture_ns is not
    available.

    Returns list of match results with latency info.
    """
    if not actions:
        print("  WARNING: No actions loaded, using frame index * 1000/fps for timing")

    # Build a map: frame_index -> capture_ns (from actions_resampled.jsonl)
    frame_capture_ns = {}
    frame_pts_ms_map = {}
    for a in actions:
        fidx = a.get("frame", -1)
        if fidx >= 0:
            frame_capture_ns[fidx] = a.get("capture_ns", 0)
            frame_pts_ms_map[fidx] = a.get("frame_pts_ms", fidx * 1000.0 / fps)

    transition_frames = [t["frame"] for t in transitions]
    results = []

    for event in space_events:
        event_offset_us = event.get("session_offset_us", 0)
        event_offset_ms = event_offset_us / 1000.0

        # Find the first transition frame AFTER this event's time
        best_transition = None
        best_latency_ms = None

        for t in transitions:
            t_frame = t["frame"]
            # Get the frame's time in session-relative ms
            if t_frame in frame_capture_ns and frame_capture_ns[t_frame] > 0:
                # Use capture_ns: convert to session-relative by subtracting first frame
                first_capture = frame_capture_ns.get(0, 0)
                frame_time_ms = (frame_capture_ns[t_frame] - first_capture) / 1_000_000.0
            elif t_frame in frame_pts_ms_map:
                frame_time_ms = frame_pts_ms_map[t_frame]
            else:
                frame_time_ms = t_frame * 1000.0 / fps

            latency_ms = frame_time_ms - event_offset_ms

            if 0 <= latency_ms <= max_latency_ms:
                if best_latency_ms is None or latency_ms < best_latency_ms:
                    best_transition = t
                    best_latency_ms = latency_ms
                    break  # Take the first one (smallest latency)

        if best_transition is not None:
            results.append({
                "event_offset_ms": event_offset_ms,
                "event_key": event.get("key", "Space"),
                "transition_frame": best_transition["frame"],
                "transition_direction": best_transition["direction"],
                "latency_ms": best_latency_ms,
            })

    return results


def space_events_from_actions(actions: list[dict], fps: float) -> list[dict]:
    """
    Derive SPACE key_down events from per-frame actions.
    Uses frame_pts_ms (preferred) or frame index as timing.
    """
    space_events = []
    prev_has_space = False

    for i, a in enumerate(actions):
        keys = a.get("key", [])
        has_space = "Space" in keys

        if has_space and not prev_has_space:
            frame_pts_ms = a.get("frame_pts_ms", i * 1000.0 / fps)
            space_events.append({
                "type": "key_down",
                "key": "Space",
                "session_offset_us": int(frame_pts_ms * 1000.0),
            })

        prev_has_space = has_space

    return space_events


def alignment_stats(results: list[dict], fps: float) -> dict:
    """
    Compute frame-level alignment stats between event time and transition frame.
    Returns dict with delta-frame histogram and summary counts.
    """
    frame_interval_ms = 1000.0 / fps
    deltas = []

    for r in results:
        event_ms = r["event_offset_ms"]
        transition_frame = r["transition_frame"]
        event_frame_est = int(round(event_ms / frame_interval_ms))
        deltas.append(transition_frame - event_frame_est)

    histogram = {}
    for d in deltas:
        histogram[d] = histogram.get(d, 0) + 1

    total = len(deltas)
    within_0 = histogram.get(0, 0)
    within_1 = sum(histogram.get(d, 0) for d in (-1, 0, 1))
    within_2 = sum(histogram.get(d, 0) for d in (-2, -1, 0, 1, 2))

    return {
        "total": total,
        "histogram": histogram,
        "within_0": within_0,
        "within_1": within_1,
        "within_2": within_2,
        "frame_interval_ms": frame_interval_ms,
    }

def main():
    parser = argparse.ArgumentParser(
        description="Analyze a benchmark recording to measure input-to-photon latency."
    )
    parser.add_argument(
        "session_dir",
        type=str,
        help="Path to the UUID session directory",
    )
    parser.add_argument(
        "--space-from-actions",
        action="store_true",
        help="Derive SPACE key_down events from actions file instead of events.jsonl",
    )
    parser.add_argument(
        "--max-latency-ms",
        type=float,
        default=500.0,
        help="Maximum plausible latency in ms (default: 500)",
    )
    args = parser.parse_args()

    session_dir = Path(args.session_dir)

    video_path = session_dir / "recording.mp4"
    events_path = session_dir / "events.jsonl"
    actions_path = session_dir / "actions_resampled.jsonl"
    metadata_path = session_dir / "metadata.json"

    required = [video_path, metadata_path]
    for p in required:
        if not p.exists():
            print(f"Error: {p} not found.", file=sys.stderr)
            sys.exit(1)

    # Step 1: Load data
    print("[1/5] Loading session data...")
    metadata = load_metadata(str(metadata_path))
    actions = load_actions(str(actions_path)) if actions_path.exists() else []

    session_start_ms = metadata.get("session_start_timestamp_ms", 0)
    meta_fps = metadata.get("video_meta", {}).get("fps", 60.0)
    fps_hint = meta_fps if meta_fps > 0 else 60.0

    events = load_events(str(events_path)) if events_path.exists() else []
    if events:
        space_events_events = [
            e for e in events
            if e.get("type") == "key_down" and e.get("key") == "Space"
        ]
        print(f"  Found {len(space_events_events)} SPACE key_down events out of {len(events)} total events.")
    else:
        space_events_events = []
        print("  events.jsonl not found; skipping raw-event analysis.")

    if actions:
        space_events_actions = space_events_from_actions(actions, fps_hint)
        print(f"  Derived {len(space_events_actions)} SPACE key_down events from actions.")
    else:
        space_events_actions = []
        print("  actions_resampled.jsonl not found; skipping resampled analysis.")

    print(f"  Loaded {len(actions)} frame action records.")

    if not space_events_events and not space_events_actions:
        print("\nError: No SPACE key_down events found from events or actions.")
        print("Make sure you pressed SPACE while recording with benchmark_app.py.")
        sys.exit(1)

    # Step 2: Compute per-frame brightness
    print("\n[2/5] Decoding video and computing frame brightness...")
    brightness, video_fps, total_frames = compute_brightness(str(video_path))
    fps = video_fps if video_fps > 0 else meta_fps
    print(f"  Video FPS: {fps:.1f}, Total frames: {total_frames}")

    # Step 3: Detect brightness transitions
    print("\n[3/5] Detecting brightness transitions...")
    transitions = detect_transitions(brightness)
    print(f"  Detected {len(transitions)} transitions:")
    for t in transitions:
        frame_time_ms = t["frame"] * 1000.0 / fps
        print(f"    Frame {t['frame']} ({frame_time_ms:.0f}ms): {t['direction']}")

    if not transitions:
        print("\nError: No brightness transitions detected in the video.")
        print("Make sure the benchmark_app window was captured by OBS.")
        sys.exit(1)

    # Step 4: Match events to transitions
    print("\n[4/5] Matching input events to video transitions...")
    results_events = []
    if space_events_events:
        results_events = match_events_to_transitions(
            space_events_events, transitions, actions, fps, session_start_ms,
            max_latency_ms=args.max_latency_ms,
        )

    results_actions = []
    if space_events_actions:
        results_actions = match_events_to_transitions(
            space_events_actions, transitions, actions, fps, session_start_ms,
            max_latency_ms=args.max_latency_ms,
        )

    if not results_events and not results_actions:
        print("\nError: Could not match any SPACE events to brightness transitions.")
        print("Possible causes:")
        print("  - Benchmark app window was not properly captured")
        print("  - SPACE presses did not trigger visible changes in the recording")
        sys.exit(1)

    # Step 5: Report results
    print(f"\n[5/5] Analysis complete!")
    print(f"\n{'='*60}")
    print(f" NeuroMiner Ground Truth Latency Report")
    print(f"{'='*60}")

    def report_section(title: str, results: list[dict]):
        if not results:
            print(f"\n {title}: no matches")
            return

        latencies = [r["latency_ms"] for r in results]

        print(f"\n {title}")
        print(f" Individual measurements:")
        for i, r in enumerate(results):
            print(
                f"  #{i+1:2d}  SPACE @ {r['event_offset_ms']:8.1f}ms  →  "
                f"Frame {r['transition_frame']:5d} ({r['transition_direction']:8s})  "
                f"= {r['latency_ms']:6.1f}ms"
            )

        print(f"\n{'─'*60}")
        print(f" Summary ({len(latencies)} samples):")
        print(f"   Median latency:  {np.median(latencies):6.1f} ms")
        print(f"   Mean latency:    {np.mean(latencies):6.1f} ms")
        print(f"   Std deviation:   {np.std(latencies):6.1f} ms")
        print(f"   Min latency:     {np.min(latencies):6.1f} ms")
        print(f"   Max latency:     {np.max(latencies):6.1f} ms")
        print(f"   Frame interval:  {1000.0/fps:6.1f} ms ({fps:.1f} fps)")
        print(f"   Latency range:   {np.min(latencies)/1000*fps:.1f} ~ "
              f"{np.max(latencies)/1000*fps:.1f} frames")
        print(f"{'─'*60}")

        # Alignment quality (frame-level)
        stats = alignment_stats(results, fps)
        if stats["total"] > 0:
            total = stats["total"]
            print("\n Alignment quality (frame-level):")
            print(f"   Within 0 frames: {stats['within_0']:>4d} / {total} "
                  f"({stats['within_0'] / total * 100:5.1f}%)")
            print(f"   Within ±1 frame: {stats['within_1']:>4d} / {total} "
                  f"({stats['within_1'] / total * 100:5.1f}%)")
            print(f"   Within ±2 frames: {stats['within_2']:>3d} / {total} "
                  f"({stats['within_2'] / total * 100:5.1f}%)")
            print("   Delta-frame histogram:", dict(sorted(stats["histogram"].items())))

        median_ms = float(np.median(latencies))
        frame_interval_ms = 1000.0 / fps
        frames_of_latency = median_ms / frame_interval_ms

        print(f"\n Interpretation:")
        if frames_of_latency < 1.5:
            print(f"   EXCELLENT: {median_ms:.0f}ms median ({frames_of_latency:.1f} frames)")
            print(f"   The pipeline adds ~1 frame of latency. This is the theoretical")
            print(f"   minimum for screen-capture based recording.")
        elif frames_of_latency < 3.0:
            print(f"   GOOD: {median_ms:.0f}ms median ({frames_of_latency:.1f} frames)")
            print(f"   Typical for most setups. The latency is compensatable.")
        elif frames_of_latency < 5.0:
            print(f"   ACCEPTABLE: {median_ms:.0f}ms median ({frames_of_latency:.1f} frames)")
            print(f"   Higher than ideal. Check OBS encoding settings.")
        else:
            print(f"   HIGH: {median_ms:.0f}ms median ({frames_of_latency:.1f} frames)")
            print(f"   Something may be wrong with the setup.")

        print(f"\n Recommended input_latency_bias_ms = {round(median_ms)}")


    if results_events:
        report_section("Raw events (events.jsonl)", results_events)
        print(f"\n{'='*60}")
    if results_actions:
        report_section("Resampled actions (actions_resampled.jsonl)", results_actions)
        print(f"\n{'='*60}\n")


if __name__ == "__main__":
    main()
