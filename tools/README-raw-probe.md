# Raw Input keyboard diagnostic (Windows x64)

This **standalone, opt-in** diagnostic captures physical keyboard make/break
events using the public Windows Raw Input API with `RIDEV_INPUTSINK`.
It does not inject into a game, bypass protections, modify OBS, add any driver,
elevate privileges or use the network.

**Privacy:** It captures key events system-wide for as long as its console is
open, including keys typed into other windows. Run only in a controlled test
session, do not type passwords, and keep the JSONL private.

1. Extract the GitHub Actions artifact `neurominer-raw-input-probe-...`.
2. Start `neurominer-raw-input-probe.exe` normally (do not run as admin).
3. Switch into the game and physically press movement/jump keys for 10s.
4. Switch back to the console and press Ctrl+C to stop.
5. Open the path printed at startup (also `%TEMP%\neurominer-raw-keyboard-probe-<PID>.jsonl`).
6. Check if `source: raw_input` appears for keys pressed while the game was focused,
   with `background: true`.

This is a diagnostic, **not yet a full OBS integration**. If raw messages are
missing only while the game is focused, do not try to defeat game protection;
check game rules and use supported recording/telemetry workflows instead.
