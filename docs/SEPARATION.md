# Offline ML source separation

Repair Suite's real-time chain stays lean: it does not embed a Demucs-class
model inside the CLAP/LV2 audio thread.  Source separation is non-causal and
needs seconds of context, so it runs **offline** in the companion
`repair-separate` tool.

## What it produces

- `vocals.wav`
- `drums.wav`
- `bass.wav`
- `other.wav`

Files are written next to the input name, for example
`Song - vocals.wav`.  Bring those stems back into the DAW and use Repair Suite
on each one.  This is also the cleanest way to repair a vocal without touching
the cymbals or bass.

## Demucs backend

```sh
python3 -m pip install --user demucs
repair-separate song.wav ~/stems
```

Useful options:

```sh
repair-separate song.wav ~/stems --device cuda --shifts 2
repair-separate song.wav ~/stems --model htdemucs_ft
repair-separate interview.wav ~/stems --two-stems vocals
repair-separate --check
```

The first run downloads the selected checkpoint.  `--shifts` trades time for
quality.  `--two-stems vocals` is often the right choice for dialogue repair.

## ONNX backend

Any time-domain four-stem ONNX export can be used when `onnxruntime` is
installed:

```sh
python3 -m pip install --user onnxruntime numpy
repair-separate song.wav ~/stems --engine onnx --model model.onnx
```

The companion expects:

- input `[1, 2, samples]`
- output `[1, sources, 2, samples]` or `[sources, 2, samples]`
- a 44.1 kHz model by default; use `--model-rate` for another rate
- output order `drums,bass,other,vocals` by default; override with
  `--source-order`

Spectral ONNX exports are rejected rather than guessed at.  Convert the model
to a time-domain export first.

## Why this is not a plug-in page

- A CLAP/LV2 audio thread must return promptly.  Demucs needs seconds of audio
  and a large model, so running it per block is not viable.
- Separated stems are not a repair operation; they are new audio files.  The
  plug-in has no file browser or offline bounce.
- Keeping the tool separate means hosts do not need PyTorch or ONNX Runtime,
  and session recall stays deterministic.

For the same reason, ARA/standalone spectral editing remains a separate
VST3/ARA project rather than something faked inside CLAP/LV2.

## Licensing

Demucs itself is MIT-licensed.  Pretrained weights and training datasets may
carry their own terms (MUSDB18, for example, is not unrestricted for every use).
Check the model licence before distributing separated material commercially.
The `repair-separate` wrapper is GPL-3.0-or-later like the rest of Repair Suite.
