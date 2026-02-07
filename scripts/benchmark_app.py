#!/usr/bin/env python3
"""
benchmark_app.py — NeuroMiner Ground Truth Latency Benchmark

A zero-overhead visual indicator for measuring the input-to-photon latency
of the recording pipeline. The app renders a full-window black/white toggle
that responds INSTANTLY to the SPACE key.

How to use:
  1. Run this script:        python scripts/benchmark_app.py
  2. In OBS, add a "Window Capture" source pointing to this window.
  3. Start OBS recording (with the NeuroMiner plugin active).
  4. Press and release SPACE repeatedly (10-20 times, ~1s apart).
  5. Stop OBS recording.
  6. Run the analyzer:       python scripts/benchmark_analyze.py /path/to/<UUID>/

Design:
  - Uses tkinter (zero dependencies, ships with Python).
  - The window alternates between pure BLACK (#000) and pure WHITE (#fff)
     on every SPACE key-down event.
  - Large text shows the current state ("BLACK" / "WHITE") and a toggle count.
  - The rendering latency of tkinter itself is typically < 1ms, so this is
    effectively a "zero-delay standard object" for the measurement.

The measured latency = (OBS plugin capture delay) + (display capture delay).
This is exactly the bias the recording pipeline introduces.

Dependencies: None (tkinter is included with Python).
"""

import tkinter as tk
import sys


class BenchmarkApp:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("NeuroMiner Latency Benchmark")
        self.root.geometry("800x600")
        self.root.configure(bg="black")

        # State
        self.is_white = False
        self.toggle_count = 0

        # Full-window label for the color indicator
        self.label = tk.Label(
            self.root,
            text="BLACK\n\nPress SPACE to toggle\nToggle count: 0",
            font=("Helvetica", 36, "bold"),
            fg="white",
            bg="black",
        )
        self.label.pack(expand=True, fill="both")

        # Instructions at bottom
        self.info = tk.Label(
            self.root,
            text="Record this window with OBS. Press SPACE 10-20 times, ~1s apart. Press ESC to quit.",
            font=("Helvetica", 12),
            fg="gray",
            bg="black",
        )
        self.info.pack(side="bottom", pady=5)

        # Bind keys
        self.root.bind("<space>", self.on_space)
        self.root.bind("<Escape>", lambda e: self.root.destroy())

        # Make sure the window is focused
        self.root.focus_force()

    def on_space(self, event):
        self.is_white = not self.is_white
        self.toggle_count += 1

        if self.is_white:
            bg = "white"
            fg = "black"
            state_text = "WHITE"
        else:
            bg = "black"
            fg = "white"
            state_text = "BLACK"

        self.root.configure(bg=bg)
        self.label.configure(
            bg=bg,
            fg=fg,
            text=f"{state_text}\n\nPress SPACE to toggle\nToggle count: {self.toggle_count}",
        )
        self.info.configure(bg=bg, fg="gray")

    def run(self):
        print("Benchmark app running. Press SPACE to toggle, ESC to quit.")
        print("Capture this window in OBS for latency measurement.")
        self.root.mainloop()


def main():
    app = BenchmarkApp()
    app.run()


if __name__ == "__main__":
    main()
