#!/usr/bin/env python3
"""
evaluate_alignment.py — Residual alignment quality report for one session.

This script evaluates alignment quality by:
1) Recomputing latency samples with the same detection pipeline as estimate_latency.py
2) Using median latency as bias
3) Reporting residual distribution (latency - bias), in both ms and frame units

Usage:
    uv run python evaluate_alignment.py /path/to/<UUID>/
    uv run python evaluate_alignment.py /path/to/<UUID>/ --window-size 45 --threshold-sigma 4.0
    uv run python evaluate_alignment.py /path/to/<UUID>/ --json-output alignment_report.json
"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np

from estimate_latency import (
    compute_frame_diffs,
    estimate_latency,
    find_change_frames,
    load_events,
    load_metadata,
)


def pct(condition: np.ndarray) -> float:
    if condition.size == 0:
        return 0.0
    return float(np.mean(condition) * 100.0)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Evaluate post-bias residual alignment quality for a session."
    )
    parser.add_argument("session_dir", type=str, help="Path to UUID session directory")
    parser.add_argument("--window-size", type=int, default=45, help="Rolling MAD window size")
    parser.add_argument(
        "--threshold-sigma",
        type=float,
        default=4.0,
        help="MAD threshold sigma for visual change detection",
    )
    parser.add_argument(
        "--max-latency-ms",
        type=float,
        default=120.0,
        help="Maximum plausible latency in milliseconds",
    )
    parser.add_argument(
        "--min-latency-ms",
        type=float,
        default=0.0,
        help="Minimum plausible latency in milliseconds",
    )
    parser.add_argument(
        "--json-output",
        type=str,
        default="",
        help="Optional path to save machine-readable report JSON",
    )
    args = parser.parse_args()

    session_dir = Path(args.session_dir)
    video_path = session_dir / "recording.mp4"
    events_path = session_dir / "events.jsonl"
    metadata_path = session_dir / "metadata.json"

    for p in (video_path, events_path, metadata_path):
        if not p.exists():
            print(f"Error: {p} not found.", file=sys.stderr)
            sys.exit(1)

    print("[1/4] Loading events and metadata...")
    events = load_events(str(events_path))
    metadata = load_metadata(str(metadata_path))
    session_start_ms = metadata.get("session_start_timestamp_ms", 0)
    meta_fps = metadata.get("video_meta", {}).get("fps", 60.0)

    print("[2/4] Decoding video and computing frame MAD...")
    mad_list, video_fps = compute_frame_diffs(str(video_path))
    fps = video_fps if video_fps > 0 else meta_fps
    frame_ms = 1000.0 / fps

    print("[3/4] Detecting visual change frames...")
    change_frames = find_change_frames(
        mad_list,
        window_size=args.window_size,
        threshold_sigma=args.threshold_sigma,
    )

    print("[4/4] Estimating latency samples and residuals...")
    latencies = estimate_latency(
        events,
        change_frames,
        fps,
        session_start_ms,
        max_latency_ms=args.max_latency_ms,
        min_latency_ms=args.min_latency_ms,
    )

    if not latencies:
        print("No valid latency samples. Try relaxing thresholds or using a clearer scene.")
        sys.exit(1)

    lat = np.array(latencies, dtype=float)
    bias_ms = float(np.median(lat))
    residual = lat - bias_ms
    abs_res = np.abs(residual)

    report = {
        "fps": float(fps),
        "frame_ms": float(frame_ms),
        "events_total": int(len(events)),
        "change_frames_total": int(len(change_frames)),
        "samples_total": int(len(lat)),
        "bias_ms_median": float(bias_ms),
        "residual_mean_ms": float(np.mean(residual)),
        "residual_std_ms": float(np.std(residual)),
        "residual_std_frames": float(np.std(residual) / frame_ms),
        "abs_residual_p50_ms": float(np.percentile(abs_res, 50)),
        "abs_residual_p90_ms": float(np.percentile(abs_res, 90)),
        "abs_residual_p95_ms": float(np.percentile(abs_res, 95)),
        "within_0_5_frame_pct": pct(abs_res <= 0.5 * frame_ms),
        "within_1_0_frame_pct": pct(abs_res <= 1.0 * frame_ms),
        "within_2_0_frame_pct": pct(abs_res <= 2.0 * frame_ms),
    }

    print("\n==================================================")
    print("Alignment Residual Report")
    print("==================================================")
    print(f"FPS:                     {report['fps']:.2f}")
    print(f"Frame duration:          {report['frame_ms']:.3f} ms")
    print(f"Events total:            {report['events_total']}")
    print(f"Change frames:           {report['change_frames_total']}")
    print(f"Matched samples:         {report['samples_total']}")
    print(f"Estimated bias (median): {report['bias_ms_median']:.3f} ms")
    print("--------------------------------------------------")
    print(f"Residual mean:           {report['residual_mean_ms']:.3f} ms")
    print(f"Residual std:            {report['residual_std_ms']:.3f} ms")
    print(f"Residual std:            {report['residual_std_frames']:.3f} frames")
    print(f"|Residual| P50:          {report['abs_residual_p50_ms']:.3f} ms")
    print(f"|Residual| P90:          {report['abs_residual_p90_ms']:.3f} ms")
    print(f"|Residual| P95:          {report['abs_residual_p95_ms']:.3f} ms")
    print("--------------------------------------------------")
    print(f"Within +/-0.5 frame:     {report['within_0_5_frame_pct']:.2f}%")
    print(f"Within +/-1.0 frame:     {report['within_1_0_frame_pct']:.2f}%")
    print(f"Within +/-2.0 frame:     {report['within_2_0_frame_pct']:.2f}%")
    print("==================================================")

    if report["samples_total"] < 100:
        print("Warning: sample count is low; results may be unstable across sessions.")
    if report["change_frames_total"] < 500:
        print(
            "Warning: detected visual changes are sparse; consider brighter/high-motion scenes for calibration."
        )

    if args.json_output:
        out = Path(args.json_output)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"Saved JSON report to: {out}")


if __name__ == "__main__":
    main()
