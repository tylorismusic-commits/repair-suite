#!/usr/bin/env python3
# Repair Suite — offline four-stem companion
# SPDX-License-Identifier: GPL-3.0-or-later
"""Separate a WAV into vocals, drums, bass and other stems.

This is an offline companion to the real-time Repair Suite plug-in.  It uses
Demucs (PyTorch) for the model itself; the plug-in stays small, real-time and
free of a heavy inference dependency.  Run it from a terminal, then bring the
stems back into the DAW for spectral repair or remixing.

Typical use:

    repair-separate song.wav ~/stems
    repair-separate dialog.wav ~/stems --two-stems vocals
    repair-separate song.wav ~/stems --model htdemucs --device cpu

The first run downloads the selected Demucs checkpoint unless it is already
cached.  Model availability and weight licensing are the user's responsibility;
the Demucs source is MIT-licensed, while some training datasets have their own
terms.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
from pathlib import Path

DEFAULT_STEMS = ("vocals", "drums", "bass", "other")
ALL_STEMS = DEFAULT_STEMS + ("instrumental",)


def maybe_use_managed_venv() -> None:
    """Use the per-user Demucs venv when one exists.

    Python packages are often installed in a venv on externally managed
    distributions.  The installed `repair-separate` script should work from a
    normal shell without asking the user to activate that venv first.
    """
    if os.environ.get("REPAIR_SEPARATE_REEXEC"):
        return
    default = Path.home() / ".local" / "share" / "repair-suite" / "venv"
    venv = Path(os.environ.get("REPAIR_SEPARATE_VENV", str(default))).expanduser()
    python = venv / "bin" / "python"
    if not python.is_file():
        return
    if Path(sys.prefix).resolve() == venv.resolve():
        return
    os.environ["REPAIR_SEPARATE_REEXEC"] = "1"
    os.execv(str(python), [str(python), str(Path(__file__).resolve()),
                           *sys.argv[1:]])


def have_module(name: str) -> bool:
    try:
        return importlib.util.find_spec(name) is not None
    except (ImportError, ModuleNotFoundError, AttributeError):
        return False


def demucs_available() -> bool:
    return have_module("demucs") or have_module("demucs.separate")


def onnx_available() -> bool:
    return have_module("onnxruntime")


def check_environment(engine: str = "auto", verbose: bool = True) -> int:
    """Report whether the selected offline backend is ready."""
    checks = [
        ("numpy", have_module("numpy")),
        ("demucs", demucs_available()),
        ("torch", have_module("torch")),
        ("onnxrt", onnx_available()),
        ("ffmpeg", shutil.which("ffmpeg") is not None),
    ]
    for name, present in checks:
        if verbose:
            print(f"{name:8s} {'yes' if present else 'no'}")
    if engine == "onnx":
        ok = have_module("numpy") and onnx_available()
        install = "python3 -m pip install --user onnxruntime numpy"
    elif engine == "demucs":
        ok = have_module("numpy") and demucs_available() and have_module("torch")
        install = "python3 -m pip install --user demucs"
    else:
        ok = have_module("numpy") and (
            (demucs_available() and have_module("torch")) or onnx_available()
        )
        install = (
            "python3 -m pip install --user demucs   # Demucs backend\n"
            "  python3 -m pip install --user onnxruntime   # ONNX backend"
        )
    if not ok:
        print(
            "\nInstall an offline backend with:\n  " + install + "\n"
            "Demucs downloads the selected checkpoint on first use. "
            "ONNX models are supplied by the user.",
            file=sys.stderr,
        )
        return 2
    return 0


def read_wav(path: Path) -> tuple[np.ndarray, int]:
    """Read PCM WAV as float32 [frames, channels]."""
    import wave

    import numpy as np

    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        width = wav.getsampwidth()
        rate = wav.getframerate()
        frames = wav.getnframes()
        raw = wav.readframes(frames)
    if width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 3:
        bytes_ = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        value = (
            bytes_[:, 0].astype(np.int32)
            | (bytes_[:, 1].astype(np.int32) << 8)
            | (bytes_[:, 2].astype(np.int32) << 16)
        )
        value = (value << 8) >> 8
        data = value.astype(np.float32) / 8388608.0
    elif width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float32) / 2147483648.0
    else:
        raise ValueError(f"unsupported WAV sample width: {width * 8} bits")
    data = data.reshape(-1, channels)
    return data, rate


def write_wav(path: Path, data: np.ndarray, rate: int) -> None:
    """Write float32 [frames, channels] as 16-bit PCM WAV."""
    import wave

    import numpy as np

    pcm = np.clip(data, -1.0, 1.0)
    pcm = (pcm * 32767.0).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(pcm.shape[1])
        wav.setsampwidth(2)
        wav.setframerate(rate)
        wav.writeframes(pcm.tobytes())


def resample(data: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
    import numpy as np

    if source_rate == target_rate or data.size == 0:
        return data
    out_len = max(1, int(round(data.shape[0] * target_rate / source_rate)))
    source = np.arange(out_len, dtype=np.float64) * source_rate / target_rate
    lo = np.floor(source).astype(np.int64)
    hi = np.minimum(lo + 1, data.shape[0] - 1)
    frac = (source - lo).astype(np.float32)[:, None]
    return (data[lo] * (1.0 - frac) + data[hi] * frac).astype(np.float32)


def parse_stems(text: str) -> list[str]:
    wanted = [s.strip().lower() for s in text.split(",") if s.strip()]
    if not wanted:
        raise argparse.ArgumentTypeError("no stems selected")
    for stem in wanted:
        if stem not in ALL_STEMS:
            raise argparse.ArgumentTypeError(
                f"unknown stem '{stem}' (choose from {', '.join(ALL_STEMS)})"
            )
    return wanted


def build_command(args: argparse.Namespace, input_path: Path,
                  output_dir: Path) -> list[str]:
    command = [
        sys.executable,
        "-m",
        "demucs.separate",
        "--out",
        str(output_dir),
        "--name",
        args.model,
        "--device",
        args.device,
        "--shifts",
        str(args.shifts),
    ]
    if args.two_stems:
        command += ["--two-stems", args.two_stems]
    if args.float32:
        command += ["--float32"]
    if args.jobs > 0:
        command += ["--jobs", str(args.jobs)]
    command.append(str(input_path))
    return command


def find_stem_files(root: Path) -> dict[str, Path]:
    """Find Demucs' per-track output regardless of its nested folder name."""
    found: dict[str, Path] = {}
    for path in root.rglob("*.wav"):
        stem = path.stem.lower()
        if stem == "no_vocals":
            stem = "instrumental"
        if stem in ALL_STEMS and stem not in found:
            found[stem] = path
    return found


def run_onnx(model_path: Path, audio: np.ndarray, model_rate: int,
             source_order: list[str]) -> dict[str, np.ndarray]:
    """Run a time-domain 4-stem ONNX model and return stem arrays.

    The supported export layout is the common Demucs/Open-Unmix style:
    input [1, 2, samples], output [1, sources, 2, samples] or
    [sources, 2, samples].  Spectral ONNX exports are intentionally rejected
    rather than guessed at.
    """
    import numpy as np
    import onnxruntime as ort

    session = ort.InferenceSession(
        str(model_path), providers=["CPUExecutionProvider"]
    )
    input_info = session.get_inputs()[0]
    shape = input_info.shape
    if len(shape) != 3:
        raise ValueError(
            "this companion expects a time-domain model with input "
            "[1, 2, samples]"
        )
    channels = audio.shape[1]
    if channels == 1:
        model_input = np.repeat(audio, 2, axis=1)
    else:
        model_input = audio[:, :2]
    model_input = model_input.T[None, :, :].astype(np.float32)
    outputs = session.run(None, {input_info.name: model_input})
    if not outputs:
        raise ValueError("model produced no outputs")
    out = np.asarray(outputs[0])
    if out.ndim == 4:
        out = out[0]
    if out.ndim != 3 or out.shape[1] != 2:
        raise ValueError(
            f"unsupported model output shape {tuple(out.shape)}; expected "
            "[sources, 2, samples]"
        )
    count = out.shape[0]
    names = source_order[:count]
    if len(names) < count:
        names += [f"stem{i}" for i in range(len(names), count)]
    stems: dict[str, np.ndarray] = {}
    for i, name in enumerate(names):
        stems[name] = out[i].T.astype(np.float32)
    if count == 2 and "instrumental" not in stems:
        stems["instrumental"] = stems.get("other", out[1].T).copy()
    return stems


def copy_stems(root: Path, output_dir: Path, stem_names: list[str],
               input_path: Path) -> int:
    found = find_stem_files(root)
    missing = [name for name in stem_names if name not in found]
    if missing:
        print(
            "demucs finished but these stems were not found: "
            + ", ".join(missing),
            file=sys.stderr,
        )
        return 4
    output_dir.mkdir(parents=True, exist_ok=True)
    base = input_path.stem
    for name in stem_names:
        destination = output_dir / f"{base} - {name}.wav"
        shutil.copy2(found[name], destination)
        print(destination)
    return 0


def main(argv: list[str] | None = None) -> int:
    maybe_use_managed_venv()
    parser = argparse.ArgumentParser(
        prog="repair-separate",
        description=(
            "Offline four-stem separation for Repair Suite.  Produces "
            "vocals, drums, bass and other WAV files; the real-time plug-in "
            "is not involved."
        ),
    )
    parser.add_argument("input", nargs="?", type=Path,
                        help="input WAV file")
    parser.add_argument("output", nargs="?", type=Path,
                        help="directory for the separated WAV files")
    parser.add_argument("--engine", choices=("auto", "demucs", "onnx"),
                        default="auto",
                        help="backend; auto uses .onnx paths with ONNX Runtime, "
                             "otherwise Demucs")
    parser.add_argument("--model", default="htdemucs",
                        help="Demucs model name, or a .onnx model path "
                             "(default: htdemucs)")
    parser.add_argument("--model-rate", type=int, default=44100,
                        help="ONNX model sample rate (default: 44100)")
    parser.add_argument("--source-order", type=parse_stems,
                        default=["drums", "bass", "other", "vocals"],
                        help="ONNX output order (default: drums,bass,other,vocals)")
    parser.add_argument("--device", default="cpu",
                        help="cpu or cuda (default: cpu)")
    parser.add_argument("--shifts", type=int, default=1,
                        help="random shifts; higher is slower and cleaner")
    parser.add_argument("--jobs", type=int, default=0,
                        help="parallel jobs; 0 lets Demucs decide")
    parser.add_argument("--two-stems", metavar="STEM",
                        choices=("vocals", "drums", "bass", "other"),
                        help="separate only this stem and an instrumental")
    parser.add_argument("--stems", type=parse_stems,
                        default=list(DEFAULT_STEMS),
                        help="comma-separated stems to copy "
                             "(default: vocals,drums,bass,other)")
    parser.add_argument("--float32", action="store_true",
                        help="write 32-bit float WAV stems")
    parser.add_argument("--keep-work", action="store_true",
                        help="keep Demucs' intermediate folder")
    parser.add_argument("--check", action="store_true",
                        help="check whether the Demucs backend is installed")
    parser.add_argument("--dry-run", action="store_true",
                        help="print the Demucs command without running it")
    args = parser.parse_args(argv)

    if args.check:
        return check_environment(args.engine)

    if args.input is None or args.output is None:
        parser.error("input and output are required unless --check is used")
    if not args.input.is_file():
        print(f"input not found: {args.input}", file=sys.stderr)
        return 3
    if args.input.suffix.lower() != ".wav":
        print(
            "repair-separate currently expects a WAV input; convert other "
            "formats to WAV first",
            file=sys.stderr,
        )
        return 3
    if args.shifts < 0:
        print("--shifts must be zero or greater", file=sys.stderr)
        return 3

    output_dir = args.output.expanduser()
    output_dir.mkdir(parents=True, exist_ok=True)
    work_root = output_dir / ".repair-separate-work"
    work_root.mkdir(parents=True, exist_ok=True)

    if args.engine == "onnx" or (
        args.engine == "auto" and args.model.lower().endswith(".onnx")
    ):
        if args.dry_run:
            print(f"onnxruntime: {args.model}")
            return 0
    else:
        command = build_command(args, args.input, work_root)
        if args.dry_run:
            print(" ".join(command))
            return 0

    if args.engine == "onnx" or (
        args.engine == "auto" and args.model.lower().endswith(".onnx")
    ):
        if not onnx_available():
            return check_environment("onnx")
        model_path = Path(args.model).expanduser()
        if not model_path.is_file():
            print(f"ONNX model not found: {model_path}", file=sys.stderr)
            return 3
        audio, rate = read_wav(args.input)
        audio = resample(audio, rate, args.model_rate)
        print("Running ONNX separation.  This is offline and can take a while.")
        stems = run_onnx(model_path, audio, args.model_rate, args.source_order)
        wanted = list(args.stems)
        if args.two_stems:
            wanted = [args.two_stems, "instrumental"]
        if "instrumental" in wanted and "instrumental" not in stems:
            import numpy as np

            keep = args.two_stems or "vocals"
            stems["instrumental"] = sum(
                (value for key, value in stems.items() if key != keep),
                np.zeros_like(stems[keep]),
            )
        missing = [name for name in wanted if name not in stems]
        if missing:
            print(
                "model did not produce: " + ", ".join(missing) + "\n"
                "Use --source-order in the model's output order.",
                file=sys.stderr,
            )
            return 4
        for name in wanted:
            stem = resample(stems[name], args.model_rate, rate)
            destination = output_dir / f"{args.input.stem} - {name}.wav"
            write_wav(destination, stem, rate)
            print(destination)
        return 0

    if not demucs_available():
        return check_environment("demucs")
    if not have_module("torch"):
        return check_environment("demucs")

    print("Separating with Demucs.  This is offline and can take minutes.")
    print("  " + " ".join(command))
    try:
        completed = subprocess.run(command, check=False)
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130
    if completed.returncode != 0:
        print(
            f"demucs failed with exit code {completed.returncode}",
            file=sys.stderr,
        )
        return completed.returncode

    stem_names = list(args.stems)
    if args.two_stems:
        stem_names = [args.two_stems, "instrumental"]
    result = copy_stems(work_root, output_dir, stem_names, args.input)
    if result == 0 and not args.keep_work:
        shutil.rmtree(work_root, ignore_errors=True)
    return result


if __name__ == "__main__":
    raise SystemExit(main())
