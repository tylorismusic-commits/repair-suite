# Changelog

## 0.1.0-alpha.1 — 2026-11-18

First public alpha.

- CLAP + LV2 plug-in with the automatic repair chain, Auto Analyze and the
  in-window spectral editor.
- Full preset handling: save, duplicate, recall, rename, delete, export and
  drag-and-drop import. User presets live in
  `~/.config/RepairSuite/presets`.
- Preset dirty tracking follows the live parameters, including host
  automation.
- Preset list rescans on open and while open; hidden rows no longer steal
  footer clicks.
- User preset values are validated and clamped before reaching the engine.
- Added CLAP and LV2 load/process/state smoke tests alongside the existing
  panel, engine and GUI tests.
