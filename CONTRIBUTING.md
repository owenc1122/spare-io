# Contributing to Spare io (launcher UI)

Issues and pull requests are welcome in English or Chinese.

This repository holds the open part of Spare io: the launcher pages, animations and the app-facing SDK header. The device runtime, phone link and firmware tooling are not public, so changes here are tested on the host.

## Rules

- **Only your own work.** No RayNeo firmware, binaries, app code, images, icons or fonts, and no code taken from other modding projects or decompiled vendor code.
- UI logic stays in the pure, host-testable modules (`fe_state`, `fe_registry`, `fe_pages`); everything the UI needs from the device goes through `frontend_port.h`.
- Never commit signing material, API keys, device identifiers or absolute home paths.

## Workflow

1. Fork, then branch from `main`.
2. Keep changes small and focused.
3. Run the tests:
   ```sh
   make -C firmware/apps/frontend-v1 test
   ```
   For visual changes, attach before/after renders from `make -C firmware/apps/frontend-v1 preview`.

## Style

- C11, no heap in the pure modules, `-Wall -Wextra` clean, ASan/UBSan clean.
- Commit messages: `module: short imperative summary`, for example `frontend: shorten bubble overshoot`.

By contributing you agree your work is released under the [PolyForm Noncommercial License 1.0.0](LICENSE) and that Technologie Spare inc. may also offer it under separate commercial licences.
