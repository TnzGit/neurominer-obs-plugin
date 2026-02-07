#!/usr/bin/env python3
"""
apply_offset.py — NeuroMiner Simple Frame-Shift Alignment

Reads actions.jsonl and metadata.json, shifts the input state by
`input_latency_bias_ms` frames, and writes actions_aligned.jsonl.

Logic:
  For video frame N, the displayed image was CAUSED by input that happened
  `input_latency_bias_ms` earlier. So the correct association is:

      aligned[frame=N].input = original[frame=N - offset_frames].input

  Where offset_frames = round(input_latency_bias_ms * fps / 1000).

  - Frames at the beginning (N < offset_frames) are padded with empty input.
  - Frames at the end are truncated (no corresponding future input).
  - Frame metadata (frame number, timestamps, capture_ns) are kept from the
    ORIGINAL frame N — only the input state is shifted.

This is the "quick & simple" version. For sub-frame precision, use
resample_events.py instead.

Usage:
    python scripts/apply_offset.py /path/to/<UUID>/
    python scripts/apply_offset.py /path/to/<UUID>/ --bias-ms 50
    python scripts/apply_offset.py /path/to/<UUID>/ --output custom_name.jsonl

Dependencies: None (standard library only).
"""

import argparse
import json
import math
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


def empty_input() -> dict:
    """Return an empty input state (no keys pressed, mouse idle)."""
    return {
        "key": [],
        "mouse": {
            "dx": 0,
            "dy": 0,
            "x": 0,
            "y": 0,
            "scroll_dy": 0,
            "button": [],
        },
    }


def main():
    parser = argparse.ArgumentParser(
        description="Apply latency offset to actions.jsonl by shifting input states."
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
        default="actions_aligned.jsonl",
        help="Output filename (default: actions_aligned.jsonl)",
    )
    args = parser.parse_args()

    session_dir = Path(args.session_dir)
    actions_path = session_dir / "actions.jsonl"
    metadata_path = session_dir / "metadata.json"

    if not actions_path.exists():
        print(f"Error: {actions_path} not found.", file=sys.stderr)
        sys.exit(1)
    if not metadata_path.exists():
        print(f"Error: {metadata_path} not found.", file=sys.stderr)
        sys.exit(1)

    # Load data
    print("[1/3] Loading data...")
    actions = load_jsonl(str(actions_path))
    metadata = load_metadata(str(metadata_path))

    fps = metadata.get("video_meta", {}).get("fps", 60.0)
    bias_ms = args.bias_ms if args.bias_ms is not None else metadata.get("input_latency_bias_ms", 0)

    if bias_ms == 0:
        print("WARNING: input_latency_bias_ms is 0. No offset will be applied.")
        print("  Run estimate_latency.py or benchmark_analyze.py first, or use --bias-ms.")

    offset_frames = round(bias_ms * fps / 1000.0)
    frame_interval_ms = 1000.0 / fps

    print(f"  Actions:       {len(actions)} frames")
    print(f"  FPS:           {fps:.1f}")
    print(f"  Bias:          {bias_ms:.1f} ms")
    print(f"  Offset:        {offset_frames} frames ({offset_frames * frame_interval_ms:.1f} ms)")

    # Apply offset
    print("\n[2/3] Applying frame shift...")
    total = len(actions)
    aligned = []

    for i in range(total):
        # Frame i's video shows the result of input from (offset_frames) earlier
        source_idx = i - offset_frames

        # Keep the frame's own metadata (frame number, timestamps, capture_ns)
        entry = {
            "frame": actions[i].get("frame", i),
            "timestamp_ms": actions[i].get("timestamp_ms", 0),
            "frame_pts_ms": actions[i].get("frame_pts_ms", 0.0),
        }
        # Preserve capture_ns if present
        if "capture_ns" in actions[i]:
            entry["capture_ns"] = actions[i]["capture_ns"]

        # Shift the input state
        if 0 <= source_idx < total:
            entry["key"] = actions[source_idx].get("key", [])
            entry["mouse"] = actions[source_idx].get("mouse", empty_input()["mouse"])
        else:
            # Pad with empty input for frames before the offset
            pad = empty_input()
            entry["key"] = pad["key"]
            entry["mouse"] = pad["mouse"]

        aligned.append(entry)

    # Count how many frames were padded vs. sourced from real data
    padded = max(0, offset_frames)
    sourced = total - padded
    print(f"  Padded (empty): {padded} frames")
    print(f"  Sourced (real): {sourced} frames")

    # Write output
    output_path = session_dir / args.output
    print(f"\n[3/3] Writing {output_path}...")
    with open(output_path, "w") as f:
        for entry in aligned:
            f.write(json.dumps(entry, separators=(",", ":")) + "\n")

    print(f"  Done. {len(aligned)} entries written.")
    print(f"\n  Original:  {actions_path}")
    print(f"  Aligned:   {output_path}")
    print(f"  Shift:     input[frame N] now corresponds to video[frame N + {offset_frames}]")


if __name__ == "__main__":
    main()
