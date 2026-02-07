#!/usr/bin/env python3
"""
estimate_latency.py — NeuroMiner Input-to-Photon Latency Auto-Alignment

Estimates the input-to-photon latency for a recorded session and writes
the result back into metadata.json as `input_latency_bias_ms`.

Algorithm:
  1. Read events.jsonl → extract discrete input events (key_down, mouse_button_down)
  2. Decode recording.mp4 → compute per-frame Mean Absolute Difference (MAD)
  3. For each input event, find the first video frame after the event where MAD
     exceeds a dynamic threshold (sliding-window mean + N * std)
  4. Compute latency = change_frame_pts - event_session_offset
  5. Take the median of all samples as the final input_latency_bias_ms
  6. Write the result back to metadata.json

Usage:
    python scripts/estimate_latency.py /path/to/<UUID>/

Dependencies:
    pip install opencv-python numpy
"""

import argparse
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np


def load_events(events_path: str) -> list[dict]:
    """Load events.jsonl and return list of event dicts."""
    events = []
    with open(events_path, "r") as f:
        for line in f:
            line = line.strip()
            if line:
                events.append(json.loads(line))
    return events


def load_metadata(metadata_path: str) -> dict:
    """Load metadata.json."""
    with open(metadata_path, "r") as f:
        return json.load(f)


def save_metadata(metadata_path: str, metadata: dict):
    """Save metadata.json with updated fields."""
    with open(metadata_path, "w") as f:
        json.dump(metadata, f, indent=2)
        f.write("\n")


def compute_frame_diffs(video_path: str) -> tuple[list[float], float]:
    """
    Decode the video and compute Mean Absolute Difference (MAD) between
    consecutive frames (grayscale). Returns (mad_list, fps).

    mad_list[i] = MAD between frame i and frame i+1.
    mad_list has length (total_frames - 1).
    """
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    if fps <= 0:
        fps = 60.0

    ret, prev_frame = cap.read()
    if not ret:
        cap.release()
        raise RuntimeError("Video has no frames")

    prev_gray = cv2.cvtColor(prev_frame, cv2.COLOR_BGR2GRAY).astype(np.float32)

    mad_list = []
    frame_idx = 1

    while True:
        ret, frame = cap.read()
        if not ret:
            break

        curr_gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
        mad = np.mean(np.abs(curr_gray - prev_gray))
        mad_list.append(float(mad))

        prev_gray = curr_gray
        frame_idx += 1

        if frame_idx % 1000 == 0:
            print(f"  Processed {frame_idx} frames...")

    cap.release()
    print(f"  Total frames decoded: {frame_idx}, MAD samples: {len(mad_list)}")
    return mad_list, fps


def find_change_frames(
    mad_list: list[float],
    window_size: int = 30,
    threshold_sigma: float = 3.0,
    min_mad: float = 1.0,
) -> list[int]:
    """
    Find frame indices where MAD exceeds a dynamic threshold.
    Returns a list of frame indices (1-based, since MAD[i] = diff between frame i and i+1).

    A frame i is a "change frame" if:
      MAD[i] > max(min_mad, rolling_mean + threshold_sigma * rolling_std)
    """
    if len(mad_list) == 0:
        return []

    mad_arr = np.array(mad_list)
    change_frames = []

    for i in range(len(mad_arr)):
        # Compute rolling stats over preceding window
        start = max(0, i - window_size)
        window = mad_arr[start:i] if i > start else mad_arr[0:1]

        mean_val = np.mean(window)
        std_val = np.std(window) if len(window) > 1 else 0.0

        threshold = max(min_mad, mean_val + threshold_sigma * std_val)

        if mad_arr[i] > threshold:
            # Frame index i+1 is the frame that changed (0-based frame i+1)
            change_frames.append(i + 1)

    return change_frames


def estimate_latency(
    events: list[dict],
    change_frames: list[int],
    fps: float,
    session_start_ms: int,
    max_latency_ms: float = 200.0,
    min_latency_ms: float = 0.0,
) -> list[float]:
    """
    For each discrete input event (key_down, mouse_button_down), find the first
    change frame that occurs AFTER the event's session_offset, and compute latency.

    Returns a list of latency samples (in ms).
    """
    # Filter to discrete events only (state transitions that should cause visual change)
    discrete_types = {"key_down", "mouse_button_down"}
    discrete_events = [e for e in events if e.get("type") in discrete_types]

    if not discrete_events:
        print("  No discrete input events found (key_down / mouse_button_down).")
        return []

    # Convert change frame indices to PTS in microseconds
    change_pts_us = [(f * 1_000_000.0 / fps) for f in change_frames]
    change_pts_us_sorted = sorted(change_pts_us)

    latency_samples = []

    for event in discrete_events:
        event_offset_us = event.get("session_offset_us", 0)

        # Find the first change frame PTS >= event_offset_us using binary search
        idx = np.searchsorted(change_pts_us_sorted, event_offset_us)

        if idx < len(change_pts_us_sorted):
            change_us = change_pts_us_sorted[idx]
            latency_us = change_us - event_offset_us
            latency_ms = latency_us / 1000.0

            if min_latency_ms <= latency_ms <= max_latency_ms:
                latency_samples.append(latency_ms)

    return latency_samples


def main():
    parser = argparse.ArgumentParser(
        description="Estimate input-to-photon latency for a NeuroMiner recording session."
    )
    parser.add_argument(
        "session_dir",
        type=str,
        help="Path to the UUID session directory (containing recording.mp4, events.jsonl, metadata.json)",
    )
    parser.add_argument(
        "--window-size",
        type=int,
        default=30,
        help="Rolling window size for MAD threshold (default: 30 frames)",
    )
    parser.add_argument(
        "--threshold-sigma",
        type=float,
        default=3.0,
        help="Number of std deviations above mean for change detection (default: 3.0)",
    )
    parser.add_argument(
        "--max-latency-ms",
        type=float,
        default=200.0,
        help="Maximum plausible latency in ms (default: 200.0)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print estimated latency without writing to metadata.json",
    )
    args = parser.parse_args()

    session_dir = Path(args.session_dir)

    # Verify required files
    video_path = session_dir / "recording.mp4"
    events_path = session_dir / "events.jsonl"
    metadata_path = session_dir / "metadata.json"

    for p in [video_path, events_path, metadata_path]:
        if not p.exists():
            print(f"Error: {p} not found.", file=sys.stderr)
            sys.exit(1)

    # Step 1: Load data
    print("[1/4] Loading events...")
    events = load_events(str(events_path))
    print(f"  Loaded {len(events)} events.")

    metadata = load_metadata(str(metadata_path))

    session_start_ms = metadata.get("session_start_timestamp_ms", 0)
    meta_fps = metadata.get("video_meta", {}).get("fps", 60.0)

    # Step 2: Compute frame differences
    print("[2/4] Decoding video and computing frame differences...")
    mad_list, video_fps = compute_frame_diffs(str(video_path))

    fps = video_fps if video_fps > 0 else meta_fps
    print(f"  Video FPS: {fps:.1f}")

    # Step 3: Detect visual change frames
    print("[3/4] Detecting visual change frames...")
    change_frames = find_change_frames(
        mad_list,
        window_size=args.window_size,
        threshold_sigma=args.threshold_sigma,
    )
    print(f"  Detected {len(change_frames)} change frames.")

    # Step 4: Estimate latency
    print("[4/4] Estimating latency...")
    latency_samples = estimate_latency(
        events,
        change_frames,
        fps,
        session_start_ms,
        max_latency_ms=args.max_latency_ms,
    )

    if len(latency_samples) == 0:
        print("\nNo valid latency samples found. Cannot estimate bias.")
        print("This may happen if:")
        print("  - The recording has very little visual change")
        print("  - Input events are sparse or don't cause visible changes")
        print("  - The threshold is too strict (try --threshold-sigma 2.0)")
        sys.exit(1)

    median_latency = float(np.median(latency_samples))
    mean_latency = float(np.mean(latency_samples))
    std_latency = float(np.std(latency_samples))

    print(f"\n{'='*50}")
    print(f"Latency estimation results:")
    print(f"  Samples:    {len(latency_samples)}")
    print(f"  Median:     {median_latency:.1f} ms")
    print(f"  Mean:       {mean_latency:.1f} ms")
    print(f"  Std:        {std_latency:.1f} ms")
    print(f"  Min:        {min(latency_samples):.1f} ms")
    print(f"  Max:        {max(latency_samples):.1f} ms")
    print(f"{'='*50}")

    # Use median as the bias (robust to outliers)
    bias_ms = round(median_latency, 1)

    if args.dry_run:
        print(f"\n[DRY RUN] Would write input_latency_bias_ms = {bias_ms} to metadata.json")
    else:
        metadata["input_latency_bias_ms"] = bias_ms
        save_metadata(str(metadata_path), metadata)
        print(f"\nWrote input_latency_bias_ms = {bias_ms} to {metadata_path}")


if __name__ == "__main__":
    main()
