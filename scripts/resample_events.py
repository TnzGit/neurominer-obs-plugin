#!/usr/bin/env python3
"""
resample_events.py — NeuroMiner High-Precision Event-to-Frame Resampling

Rebuilds per-frame input states from the raw sub-frame event stream (events.jsonl)
with latency compensation applied. This is the HIGH-PRECISION alternative to
apply_offset.py (which uses simple frame-shift).

Algorithm:
  1. Load events.jsonl (sub-frame raw events with session_offset_us).
  2. Load metadata.json to get FPS and input_latency_bias_ms.
  3. For each video frame N, compute its time window:
       frame_start = N * frame_duration_us - bias_us
       frame_end   = (N + 1) * frame_duration_us - bias_us
  4. Replay all events up to frame_end to reconstruct the exact input state
     (keyboard held keys, mouse buttons, and accumulate dx/dy/scroll).
  5. Output actions_resampled.jsonl with the reconstructed per-frame state.

Key differences from apply_offset.py:
  - Sub-frame precision: events are replayed at microsecond resolution
  - Fractional-frame offsets: a 45ms bias at 60fps = 2.7 frames (not rounded to 3)
  - Mouse delta accumulation: dx/dy/scroll are accumulated across all events
    within each compensated frame window
  - Key hold states: correctly tracked even when key_down spans frame boundaries

Usage:
    python scripts/resample_events.py /path/to/<UUID>/
    python scripts/resample_events.py /path/to/<UUID>/ --bias-ms 50
    python scripts/resample_events.py /path/to/<UUID>/ --output custom.jsonl

Dependencies: None (standard library only).
"""

import argparse
import json
import sys
from pathlib import Path


def load_jsonl(path: str) -> list[dict]:
    """Load a .jsonl file and return list of dicts."""
    items = []
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if line:
                items.append(json.loads(line))
    return items


def load_metadata(path: str) -> dict:
    with open(path, "r") as f:
        return json.load(f)


class InputStateTracker:
    """
    Stateful tracker that replays raw events to reconstruct input state.

    Tracks:
    - keys_held: set of currently pressed keys
    - buttons_held: set of currently pressed mouse buttons
    - mouse_x, mouse_y: latest absolute position
    - dx, dy, scroll_dy: accumulated deltas within current frame window
    """

    def __init__(self):
        self.keys_held: set[str] = set()
        self.buttons_held: set[str] = set()
        self.mouse_x: int = 0
        self.mouse_y: int = 0
        # Accumulators (reset per frame window)
        self.dx: int = 0
        self.dy: int = 0
        self.scroll_dy: int = 0

    def reset_deltas(self):
        """Reset per-frame accumulations."""
        self.dx = 0
        self.dy = 0
        self.scroll_dy = 0

    def apply_event(self, event: dict):
        """Apply a single raw event to update the tracked state."""
        event_type = event.get("type", "")

        if event_type == "key_down":
            key = event.get("key", "")
            if key:
                self.keys_held.add(key)
        elif event_type == "key_up":
            key = event.get("key", "")
            self.keys_held.discard(key)
        elif event_type == "mouse_button_down":
            button = event.get("button", "")
            if button:
                self.buttons_held.add(button)
        elif event_type == "mouse_button_up":
            button = event.get("button", "")
            self.buttons_held.discard(button)
        elif event_type == "mouse_move":
            self.dx += event.get("dx", 0)
            self.dy += event.get("dy", 0)
            # Update absolute position if provided
            if "x" in event:
                self.mouse_x = event["x"]
            if "y" in event:
                self.mouse_y = event["y"]
        elif event_type == "scroll":
            self.scroll_dy += event.get("scroll_dy", 0)

    def snapshot(self) -> dict:
        """Return a snapshot of the current state for one frame."""
        return {
            "key": sorted(self.keys_held),
            "mouse": {
                "dx": self.dx,
                "dy": self.dy,
                "x": self.mouse_x,
                "y": self.mouse_y,
                "scroll_dy": self.scroll_dy,
                "button": sorted(self.buttons_held),
            },
        }


def resample(
    events: list[dict],
    total_frames: int,
    fps: float,
    bias_us: float,
    actions: list[dict] | None = None,
) -> list[dict]:
    """
    Resample raw events into per-frame input states with latency compensation.

    For frame N, we look at events whose session_offset_us falls in:
        [N * frame_us - bias_us, (N+1) * frame_us - bias_us)

    This effectively shifts the event timeline forward by bias_us, so that
    input events are matched to the video frame they actually affected.

    Args:
        events: List of raw event dicts, sorted by session_offset_us.
        total_frames: Number of video frames to generate.
        fps: Video frame rate.
        bias_us: Latency bias in microseconds (positive = events are early).
        actions: Optional original actions.jsonl entries to preserve frame metadata.

    Returns:
        List of dicts, one per frame, with aligned input state.
    """
    frame_us = 1_000_000.0 / fps
    tracker = InputStateTracker()

    # Sort events by offset (should already be sorted, but ensure)
    sorted_events = sorted(events, key=lambda e: e.get("session_offset_us", 0))
    event_idx = 0
    total_events = len(sorted_events)

    results = []

    for frame_n in range(total_frames):
        # Compensated time window for this frame
        win_start_us = frame_n * frame_us - bias_us
        win_end_us = (frame_n + 1) * frame_us - bias_us

        # Reset per-frame delta accumulators
        tracker.reset_deltas()

        # Replay all events that fall within [win_start, win_end)
        # Note: events before win_start of frame 0 (negative window) are still
        # applied to maintain correct key held state — they just won't have
        # their deltas counted for any frame.
        while event_idx < total_events:
            ev = sorted_events[event_idx]
            ev_time = ev.get("session_offset_us", 0)

            if ev_time < win_start_us:
                # Event is before this frame's window — apply for state tracking
                # but don't accumulate its deltas (they belong to no frame
                # or a frame that's already passed)
                tracker.apply_event(ev)
                tracker.reset_deltas()  # Discard deltas from pre-window events
                event_idx += 1
            elif ev_time < win_end_us:
                # Event is within this frame's window
                tracker.apply_event(ev)
                event_idx += 1
            else:
                # Event is beyond this frame's window — stop
                break

        # Build output entry
        snap = tracker.snapshot()
        entry = {
            "frame": frame_n,
            "frame_pts_ms": frame_n * 1000.0 / fps,
        }

        # Preserve original frame metadata if available
        if actions and frame_n < len(actions):
            orig = actions[frame_n]
            entry["timestamp_ms"] = orig.get("timestamp_ms", 0)
            if "capture_ns" in orig:
                entry["capture_ns"] = orig["capture_ns"]

        entry["key"] = snap["key"]
        entry["mouse"] = snap["mouse"]

        results.append(entry)

    return results


def main():
    parser = argparse.ArgumentParser(
        description="High-precision event-to-frame resampling with latency compensation."
    )
    parser.add_argument(
        "session_dir",
        type=str,
        help="Path to the UUID session directory",
    )
    parser.add_argument(
        "--bias-ms",
        type=float,
        default=None,
        help="Override input_latency_bias_ms from metadata.json",
    )
    parser.add_argument(
        "--output",
        type=str,
        default="actions_resampled.jsonl",
        help="Output filename (default: actions_resampled.jsonl)",
    )
    parser.add_argument(
        "--preserve-metadata",
        action="store_true",
        default=True,
        help="Preserve frame timestamps from original actions.jsonl (default: true)",
    )
    args = parser.parse_args()

    session_dir = Path(args.session_dir)
    events_path = session_dir / "events.jsonl"
    metadata_path = session_dir / "metadata.json"
    actions_path = session_dir / "actions.jsonl"

    if not events_path.exists():
        print(f"Error: {events_path} not found.", file=sys.stderr)
        sys.exit(1)
    if not metadata_path.exists():
        print(f"Error: {metadata_path} not found.", file=sys.stderr)
        sys.exit(1)

    # Load data
    print("[1/3] Loading data...")
    events = load_jsonl(str(events_path))
    metadata = load_metadata(str(metadata_path))

    actions = None
    if args.preserve_metadata and actions_path.exists():
        actions = load_jsonl(str(actions_path))
        print(f"  Actions:  {len(actions)} frames (for metadata)")

    fps = metadata.get("video_meta", {}).get("fps", 60.0)
    total_frames = metadata.get("video_meta", {}).get("total_frames", 0)
    bias_ms = args.bias_ms if args.bias_ms is not None else metadata.get("input_latency_bias_ms", 0)
    bias_us = bias_ms * 1000.0

    # If total_frames is not in metadata, try from actions length
    if total_frames == 0 and actions:
        total_frames = len(actions)
    if total_frames == 0:
        print("Error: Cannot determine total_frames. Check metadata.json.", file=sys.stderr)
        sys.exit(1)

    frame_duration_ms = 1000.0 / fps

    print(f"  Events:       {len(events)}")
    print(f"  Total frames: {total_frames}")
    print(f"  FPS:          {fps:.1f}")
    print(f"  Bias:         {bias_ms:.1f} ms ({bias_us:.0f} µs)")
    print(f"  Frame dur:    {frame_duration_ms:.2f} ms")
    print(f"  Effective shift: {bias_ms / frame_duration_ms:.2f} frames (sub-frame precision)")

    if bias_ms == 0:
        print("\n  WARNING: input_latency_bias_ms is 0. No compensation applied.")
        print("  Run estimate_latency.py or benchmark_analyze.py first, or use --bias-ms.\n")

    # Count event types for summary
    type_counts: dict[str, int] = {}
    for ev in events:
        t = ev.get("type", "unknown")
        type_counts[t] = type_counts.get(t, 0) + 1
    print(f"\n  Event types:")
    for t, c in sorted(type_counts.items()):
        print(f"    {t:25s} {c:>8d}")

    # Resample
    print(f"\n[2/3] Resampling events to {total_frames} frames with {bias_ms:.1f} ms compensation...")
    results = resample(events, total_frames, fps, bias_us, actions)

    # Stats
    frames_with_keys = sum(1 for r in results if r.get("key"))
    frames_with_mouse_move = sum(
        1 for r in results if r.get("mouse", {}).get("dx", 0) != 0 or r.get("mouse", {}).get("dy", 0) != 0
    )
    frames_with_buttons = sum(1 for r in results if r.get("mouse", {}).get("button"))
    print(f"\n  Output stats:")
    print(f"    Frames with key input:    {frames_with_keys:>6d} / {total_frames}")
    print(f"    Frames with mouse move:   {frames_with_mouse_move:>6d} / {total_frames}")
    print(f"    Frames with mouse button: {frames_with_buttons:>6d} / {total_frames}")

    # Write output
    output_path = session_dir / args.output
    print(f"\n[3/3] Writing {output_path}...")
    with open(output_path, "w") as f:
        for entry in results:
            f.write(json.dumps(entry, separators=(",", ":")) + "\n")

    print(f"  Done. {len(results)} entries written.")
    print(f"\n  Input events:   {events_path}  ({len(events)} events)")
    print(f"  Output:         {output_path}  ({len(results)} frames)")
    print(f"  Compensation:   {bias_ms:.1f} ms = {bias_ms / frame_duration_ms:.2f} frames")


if __name__ == "__main__":
    main()
