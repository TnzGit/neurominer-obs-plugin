# Windows x64 build request

This branch triggers the repository's existing GitHub Actions Windows build on push, without changes to the plugin source code or build configuration.

## Source review (2026-11-02)

- Base commit: b99658e06ca2dabf4d2d095fb809a561f2dedceb
- Reviewed Windows keyboard/mouse capture, recording logic, OBS dock integration, packaging and workflow scripts, and dependency definitions.
- The Windows plugin intentionally installs global low-level keyboard/mouse hooks while OBS recording is active. Do not record passwords or other secrets.
- No obvious exfiltration or hidden persistence code found in reviewed sources. Static inspection cannot prove absolute safety; validate the produced executable and monitor runtime behavior before trusting it.
- Dependencies are fetched from obsproject GitHub with fixed SHA-256 hashes.
- This file is a build trigger and review note only; it is not compiled into the plugin.

## Build trigger after Actions enablement

GitHub Actions was enabled in the fork after the first commit. This follow-up documentation-only commit asks the existing `push` workflow to build Windows x64 without changing executable source files.
