#!/usr/bin/env python3
# Repair Suite — standalone stem-separation GUI
# SPDX-License-Identifier: GPL-3.0-or-later
"""Small desktop front-end for the offline repair-separate companion."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PySide6.QtCore import QProcess, QSettings, Qt, QUrl
from PySide6.QtGui import QColor, QDesktopServices, QFont, QPalette, QTextCursor
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

APP_NAME = "Repair Suite Stems"
AUDIO_FILTER = (
    "Audio files (*.wav *.flac *.mp3 *.ogg *.m4a *.aiff *.aif);;"
    "WAV files (*.wav);;All files (*)"
)
DEMUCS_MODELS = [
    "htdemucs",
    "htdemucs_ft",
    "htdemucs_6s",
    "hdemucs_mmi",
    "mdx_extra",
    "mdx_extra_q",
]


def companion_path() -> Path | None:
    """Find repair-separate whether running from the repo or an install."""
    found = shutil.which("repair-separate")
    if found:
        return Path(found)
    local = Path(__file__).resolve().with_name("repair_separate.py")
    return local if local.is_file() else None


def default_output() -> str:
    music = Path.home() / "Music"
    base = music if music.is_dir() else Path.home()
    return str(base / "RepairSuiteStems")


def friendly_size(path: Path) -> str:
    try:
        size = path.stat().st_size
    except OSError:
        return ""
    for unit in ("B", "KB", "MB", "GB"):
        if size < 1024 or unit == "GB":
            return f"{size:.1f} {unit}"
        size /= 1024.0
    return ""


class StemsWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(APP_NAME)
        self.setMinimumSize(980, 700)
        self.resize(1000, 740)
        self.setAcceptDrops(True)
        self.settings = QSettings("RepairSuite", "Stems")
        self.process: QProcess | None = None
        self.convert_process: QProcess | None = None
        self._last_output_dir = ""
        self._build_ui()
        self._load_settings()
        self._set_running(False)
        self._append_log(
            "Ready.  Choose a mix, choose an output folder, then press Separate.\n"
            "The first run downloads the selected Demucs model.\n"
        )

    # ------------------------------------------------------------------ UI
    def _build_ui(self) -> None:
        root = QWidget()
        self.setCentralWidget(root)
        outer = QVBoxLayout(root)
        outer.setContentsMargins(22, 18, 22, 18)
        outer.setSpacing(14)

        title = QLabel("REPAIR SUITE STEMS")
        title.setObjectName("title")
        subtitle = QLabel(
            "Offline four-stem separation for Repair Suite.  "
            "Demucs or a user-supplied ONNX model."
        )
        subtitle.setObjectName("subtitle")
        outer.addWidget(title)
        outer.addWidget(subtitle)

        line = QFrame()
        line.setFrameShape(QFrame.HLine)
        line.setObjectName("rule")
        outer.addWidget(line)

        form = QFormLayout()
        form.setLabelAlignment(Qt.AlignRight | Qt.AlignVCenter)
        form.setFieldGrowthPolicy(QFormLayout.AllNonFixedFieldsGrow)
        form.setHorizontalSpacing(14)
        form.setVerticalSpacing(10)

        input_row = QHBoxLayout()
        self.input_edit = QLineEdit()
        self.input_edit.setPlaceholderText("Drop a WAV/FLAC/MP3 here or browse…")
        input_browse = QPushButton("Browse…")
        input_browse.clicked.connect(self._browse_input)
        input_row.addWidget(self.input_edit, 1)
        input_row.addWidget(input_browse)
        form.addRow("Input mix", input_row)

        output_row = QHBoxLayout()
        self.output_edit = QLineEdit(default_output())
        output_browse = QPushButton("Browse…")
        output_browse.clicked.connect(self._browse_output)
        output_row.addWidget(self.output_edit, 1)
        output_row.addWidget(output_browse)
        form.addRow("Output folder", output_row)

        self.engine_combo = QComboBox()
        self.engine_combo.addItems(["Demucs", "ONNX model"])
        self.engine_combo.currentIndexChanged.connect(self._engine_changed)
        form.addRow("Backend", self.engine_combo)

        self.model_combo = QComboBox()
        self.model_combo.setEditable(True)
        self.model_combo.addItems(DEMUCS_MODELS)
        form.addRow("Demucs model", self.model_combo)

        self.onnx_row = QWidget()
        onnx_layout = QHBoxLayout(self.onnx_row)
        onnx_layout.setContentsMargins(0, 0, 0, 0)
        self.onnx_edit = QLineEdit()
        self.onnx_edit.setPlaceholderText("Choose a time-domain .onnx model")
        onnx_browse = QPushButton("Browse…")
        onnx_browse.clicked.connect(self._browse_onnx)
        onnx_layout.addWidget(self.onnx_edit, 1)
        onnx_layout.addWidget(onnx_browse)
        form.addRow("ONNX model", self.onnx_row)

        self.device_combo = QComboBox()
        self.device_combo.addItems(["cpu", "cuda"])
        form.addRow("Device", self.device_combo)

        self.shifts_spin = QSpinBox()
        self.shifts_spin.setRange(0, 10)
        self.shifts_spin.setValue(1)
        self.shifts_spin.setToolTip("More shifts = cleaner and much slower")
        form.addRow("Quality shifts", self.shifts_spin)

        self.two_combo = QComboBox()
        self.two_combo.addItems(
            ["Four stems", "Vocals + instrumental", "Drums + instrumental",
             "Bass + instrumental", "Other + instrumental"]
        )
        form.addRow("Output", self.two_combo)

        outer.addLayout(form)

        buttons = QHBoxLayout()
        self.check_button = QPushButton("Check backend")
        self.check_button.clicked.connect(self._check_backend)
        self.run_button = QPushButton("Separate")
        self.run_button.setObjectName("primary")
        self.run_button.clicked.connect(self._start)
        self.cancel_button = QPushButton("Cancel")
        self.cancel_button.clicked.connect(self._cancel)
        self.open_button = QPushButton("Open output folder")
        self.open_button.clicked.connect(self._open_output)
        buttons.addWidget(self.check_button)
        buttons.addStretch(1)
        buttons.addWidget(self.cancel_button)
        buttons.addWidget(self.run_button)
        buttons.addWidget(self.open_button)
        outer.addLayout(buttons)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self.status = QLabel("Idle")
        self.status.setObjectName("status")
        outer.addWidget(self.progress)
        outer.addWidget(self.status)

        body = QGridLayout()
        body.setColumnStretch(0, 3)
        body.setColumnStretch(1, 2)
        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setLineWrapMode(QPlainTextEdit.NoWrap)
        self.log.setFont(QFont("monospace", 9))
        self.stems = QListWidget()
        self.stems.setToolTip("Finished stems appear here")
        body.addWidget(self._panel("LOG", self.log), 0, 0)
        body.addWidget(self._panel("STEMS", self.stems), 0, 1)
        outer.addLayout(body, 1)

    def _panel(self, label: str, widget: QWidget) -> QWidget:
        frame = QFrame()
        frame.setObjectName("panel")
        layout = QVBoxLayout(frame)
        layout.setContentsMargins(10, 8, 10, 8)
        layout.setSpacing(6)
        caption = QLabel(label)
        caption.setObjectName("caption")
        layout.addWidget(caption)
        layout.addWidget(widget, 1)
        return frame

    # -------------------------------------------------------------- settings
    def _load_settings(self) -> None:
        self.input_edit.setText(self.settings.value("input", "", str))
        self.output_edit.setText(
            self.settings.value("output", default_output(), str)
        )
        self.engine_combo.setCurrentIndex(
            int(self.settings.value("engine", 0))
        )
        self.model_combo.setCurrentText(
            self.settings.value("model", "htdemucs", str)
        )
        self.onnx_edit.setText(self.settings.value("onnx", "", str))
        self.device_combo.setCurrentText(
            self.settings.value("device", "cpu", str)
        )
        self.shifts_spin.setValue(int(self.settings.value("shifts", 1)))
        self.two_combo.setCurrentIndex(int(self.settings.value("two", 0)))
        self._engine_changed()

    def _save_settings(self) -> None:
        self.settings.setValue("input", self.input_edit.text())
        self.settings.setValue("output", self.output_edit.text())
        self.settings.setValue("engine", self.engine_combo.currentIndex())
        self.settings.setValue("model", self.model_combo.currentText())
        self.settings.setValue("onnx", self.onnx_edit.text())
        self.settings.setValue("device", self.device_combo.currentText())
        self.settings.setValue("shifts", self.shifts_spin.value())
        self.settings.setValue("two", self.two_combo.currentIndex())

    def closeEvent(self, event) -> None:  # noqa: N802 - Qt API
        self._save_settings()
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.process.kill()
        super().closeEvent(event)

    # --------------------------------------------------------------- actions
    def _engine_changed(self) -> None:
        onnx = self.engine_combo.currentIndex() == 1
        self.model_combo.setEnabled(not onnx)
        self.onnx_row.setEnabled(onnx)

    def _browse_input(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Choose a mix", str(Path.home()), AUDIO_FILTER
        )
        if path:
            self.input_edit.setText(path)

    def _browse_output(self) -> None:
        path = QFileDialog.getExistingDirectory(
            self, "Choose output folder", self.output_edit.text()
        )
        if path:
            self.output_edit.setText(path)

    def _browse_onnx(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Choose ONNX model", str(Path.home()), "ONNX models (*.onnx)"
        )
        if path:
            self.onnx_edit.setText(path)

    def _open_output(self) -> None:
        folder = self._last_output_dir or self.output_edit.text().strip()
        if folder:
            QDesktopServices.openUrl(QUrl.fromLocalFile(folder))

    def _cancel(self) -> None:
        if self.process and self.process.state() != QProcess.ProcessState.NotRunning:
            self.process.terminate()
            if not self.process.waitForFinished(2000):
                self.process.kill()
            self._append_log("\nCancelled.\n")
        self._set_running(False)

    def _append_log(self, text: str) -> None:
        text = text.replace("\r", "\n")
        self.log.moveCursor(QTextCursor.End)
        self.log.insertPlainText(text)
        self.log.moveCursor(QTextCursor.End)

    def _set_running(self, running: bool) -> None:
        self.run_button.setEnabled(not running)
        self.cancel_button.setEnabled(running)
        self.check_button.setEnabled(not running)
        self.input_edit.setEnabled(not running)
        self.output_edit.setEnabled(not running)
        self.engine_combo.setEnabled(not running)
        self.model_combo.setEnabled(not running and
                                     self.engine_combo.currentIndex() == 0)
        self.onnx_row.setEnabled(not running and
                                 self.engine_combo.currentIndex() == 1)
        self.device_combo.setEnabled(not running)
        self.shifts_spin.setEnabled(not running)
        self.two_combo.setEnabled(not running)
        if running:
            self.open_button.setEnabled(False)

    def _set_status(self, text: str) -> None:
        self.status.setText(text)

    def _check_backend(self) -> None:
        companion = companion_path()
        if not companion:
            self._append_log("repair-separate was not found.\n")
            return
        self._append_log("\n$ repair-separate --check\n")
        proc = QProcess(self)
        proc.setProcessChannelMode(QProcess.MergedChannels)
        proc.readyReadStandardOutput.connect(
            lambda: self._append_log(
                bytes(proc.readAllStandardOutput()).decode(errors="replace")
            )
        )
        proc.finished.connect(
            lambda code, _status: self._set_status(
                "Backend ready" if code == 0 else "Backend missing"
            )
        )
        proc.start(sys.executable, [str(companion), "--check"])

    # ------------------------------------------------------------- separation
    def _start(self) -> None:
        input_path = Path(self.input_edit.text().strip()).expanduser()
        output_dir = Path(self.output_edit.text().strip()).expanduser()
        if not input_path.is_file():
            QMessageBox.warning(self, APP_NAME, "Choose an existing audio file.")
            return
        if not output_dir:
            QMessageBox.warning(self, APP_NAME, "Choose an output folder.")
            return
        companion = companion_path()
        if not companion:
            QMessageBox.critical(
                self, APP_NAME,
                "repair-separate was not found next to the GUI or on PATH."
            )
            return
        if self.engine_combo.currentIndex() == 1:
            model = Path(self.onnx_edit.text().strip()).expanduser()
            if not model.is_file():
                QMessageBox.warning(self, APP_NAME, "Choose an ONNX model.")
                return
        try:
            output_dir.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            QMessageBox.critical(self, APP_NAME, f"Cannot create output:\n{exc}")
            return

        self.stems.clear()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        self._set_status("Preparing input…")
        self._save_settings()
        self._append_log("\n" + "=" * 72 + "\n")

        work_input = input_path
        if input_path.suffix.lower() != ".wav":
            converted = Path(tempfile.gettempdir()) / (
                "repair-suite-stems-" + input_path.stem + ".wav"
            )
            self._append_log(
                f"Converting {input_path.name} to WAV with ffmpeg…\n"
            )
            ffmpeg = shutil.which("ffmpeg")
            if not ffmpeg:
                QMessageBox.critical(
                    self, APP_NAME,
                    "ffmpeg is required to import non-WAV files."
                )
                return
            result = subprocess.run(
                [ffmpeg, "-y", "-v", "error", "-i", str(input_path),
                 "-ar", "48000", "-ac", "2", str(converted)],
                capture_output=True, text=True,
            )
            if result.returncode != 0:
                QMessageBox.critical(
                    self, APP_NAME,
                    "ffmpeg conversion failed:\n" + result.stderr[-1200:]
                )
                self._set_status("Conversion failed")
                return
            work_input = converted

        command = [sys.executable, str(companion), str(work_input),
                   str(output_dir)]
        if self.engine_combo.currentIndex() == 1:
            command += ["--engine", "onnx", "--model",
                        str(Path(self.onnx_edit.text().strip()).expanduser())]
        else:
            command += ["--model", self.model_combo.currentText().strip(),
                        "--device", self.device_combo.currentText(),
                        "--shifts", str(self.shifts_spin.value())]
        two = self.two_combo.currentIndex()
        if two > 0:
            command += ["--two-stems",
                        ["vocals", "drums", "bass", "other"][two - 1]]

        self._append_log("$ " + " ".join(command) + "\n")
        self._last_output_dir = str(output_dir)
        self._set_running(True)
        self._set_status("Separating…")
        self.progress.setValue(0)

        self.process = QProcess(self)
        self.process.setProcessChannelMode(QProcess.MergedChannels)
        self.process.readyReadStandardOutput.connect(self._read_output)
        self.process.finished.connect(self._finished)
        self.process.errorOccurred.connect(self._process_error)
        self.process.start(sys.executable, command[1:])

    def _read_output(self) -> None:
        if not self.process:
            return
        text = bytes(self.process.readAllStandardOutput()).decode(
            errors="replace"
        )
        self._append_log(text)
        percents = [int(v) for v in re.findall(r"(\d{1,3})%", text)]
        if percents:
            self.progress.setValue(min(100, max(percents)))
        if "Separating track" in text:
            self._set_status("Running Demucs model…")

    def _process_error(self, error) -> None:
        if error == QProcess.ProcessError.FailedToStart:
            self._append_log("Could not start repair-separate.\n")
            self._set_status("Failed to start")
            self._set_running(False)

    def _finished(self, code: int, _status) -> None:
        self._set_running(False)
        if code != 0:
            self._set_status(f"Failed (exit {code})")
            self._append_log(f"\nSeparation failed with exit code {code}.\n")
            return
        self.progress.setValue(100)
        self._set_status("Done")
        self.open_button.setEnabled(True)
        self._collect_stems(Path(self._last_output_dir))
        self._append_log("\nDone.  Stems are ready.\n")

    def _collect_stems(self, folder: Path) -> None:
        self.stems.clear()
        if not folder.is_dir():
            return
        input_stem = Path(self.input_edit.text().strip()).stem
        candidates = sorted(
            folder.glob(f"{input_stem} - *.wav"),
            key=lambda p: p.name,
        )
        if not candidates:
            candidates = sorted(folder.glob("*.wav"))
        for path in candidates:
            suffix = path.stem.replace(input_stem + " - ", "")
            self.stems.addItem(f"{suffix}   ·   {friendly_size(path)}")

    # --------------------------------------------------------------- drag/drop
    def dragEnterEvent(self, event) -> None:  # noqa: N802 - Qt API
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event) -> None:  # noqa: N802 - Qt API
        for url in event.mimeData().urls():
            path = Path(url.toLocalFile())
            if path.is_file():
                self.input_edit.setText(str(path))
                break
        event.acceptProposedAction()


STYLESHEET = """
QWidget {
    background: #15191c;
    color: #e7e2d8;
    font-size: 10pt;
}
QMainWindow { background: #15191c; }
QLabel#title {
    font-size: 20pt;
    font-weight: 700;
    letter-spacing: 2px;
    color: #f0b94f;
}
QLabel#subtitle { color: #9aa4a8; }
QLabel#caption {
    color: #89969a;
    font-size: 8pt;
    font-weight: 700;
    letter-spacing: 1px;
}
QLabel#status { color: #9aa4a8; }
QFrame#rule { color: #2c3438; }
QFrame#panel {
    background: #1c2226;
    border: 1px solid #2c3438;
    border-radius: 5px;
}
QLineEdit, QComboBox, QSpinBox {
    background: #0f1315;
    border: 1px solid #343e43;
    border-radius: 4px;
    padding: 5px 7px;
    selection-background-color: #7a5a24;
}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {
    border: 1px solid #f0b94f;
}
QPushButton {
    background: #293136;
    border: 1px solid #3b474d;
    border-radius: 4px;
    padding: 6px 13px;
}
QPushButton:hover { background: #343f45; }
QPushButton:disabled { color: #606a6e; background: #1c2226; }
QPushButton#primary {
    background: #d99a32;
    color: #17130c;
    font-weight: 700;
    border: 1px solid #f0b94f;
    padding: 7px 22px;
}
QPushButton#primary:hover { background: #f0b94f; }
QProgressBar {
    background: #0f1315;
    border: 1px solid #343e43;
    border-radius: 4px;
    text-align: center;
    height: 18px;
}
QProgressBar::chunk { background: #c98f2e; border-radius: 3px; }
QPlainTextEdit, QListWidget {
    background: #0d1113;
    border: 1px solid #2c3438;
    border-radius: 4px;
    color: #cfd8d9;
}
QListWidget::item { padding: 6px; }
QListWidget::item:selected { background: #3b3220; }
QComboBox QAbstractItemView {
    background: #1c2226;
    selection-background-color: #7a5a24;
}
"""


def set_palette(app: QApplication) -> None:
    palette = QPalette()
    palette.setColor(QPalette.Window, QColor("#15191c"))
    palette.setColor(QPalette.WindowText, QColor("#e7e2d8"))
    palette.setColor(QPalette.Base, QColor("#0d1113"))
    palette.setColor(QPalette.Text, QColor("#e7e2d8"))
    palette.setColor(QPalette.Button, QColor("#293136"))
    palette.setColor(QPalette.ButtonText, QColor("#e7e2d8"))
    palette.setColor(QPalette.Highlight, QColor("#7a5a24"))
    palette.setColor(QPalette.HighlightedText, QColor("#ffffff"))
    app.setPalette(palette)


def main() -> int:
    parser = argparse.ArgumentParser(prog="repair-stems")
    parser.add_argument(
        "--selftest", action="store_true",
        help="construct the window and exit (used by CTest)"
    )
    parser.add_argument(
        "--screenshot", metavar="PATH",
        help="render the window to a PNG and exit"
    )
    args = parser.parse_args()
    os.environ.setdefault("QT_AUTO_SCREEN_SCALE_FACTOR", "1")
    app = QApplication(sys.argv[:1])
    app.setApplicationName(APP_NAME)
    set_palette(app)
    app.setStyleSheet(STYLESHEET)
    window = StemsWindow()
    if args.selftest or args.screenshot:
        assert companion_path() is not None, "repair-separate is missing"
        if args.screenshot:
            window.input_edit.clear()
            window.output_edit.setText(default_output())
            window.two_combo.setCurrentIndex(0)
            window.shifts_spin.setValue(1)
        window.show()
        app.processEvents()
        if args.screenshot:
            window.grab().save(args.screenshot)
        window.close()
        return 0
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
