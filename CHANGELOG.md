# Changelog

## Unreleased

- The editor remembers its exact width and height, including when the host
  embeds it in a differently sized FX window.
- Spectral capture shows a live preview while armed, and works without a host
  transport timeline.
- Auto Analyze results are drawn as an overlay above the knobs.
- Footer status now sits inside the faceplate instead of under its edge.
- De-click ignores low-level noise relative to the block peak, so clean
  passages are not replaced with interpolated samples.

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
