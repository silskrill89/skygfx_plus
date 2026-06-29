"""Install page — runs all stages on a worker thread, shows live log + progress.

Stages (in order):
    1. detect_version  — hash + size of source gta_sa.exe
    2. copy_sa         — clone source SA folder into dest
    3. apply_nocd      — apply NO-CD / downgrade patch (if needed)
    4. backup          — back up the freshly-prepared dest folder
    5. install_mods    — download/extract/merge each selected mod
"""
from __future__ import annotations

import os
import threading
from datetime import datetime

from PyQt5.QtCore import Qt, pyqtSignal, QObject
from PyQt5.QtWidgets import (
    QHBoxLayout, QLabel, QPlainTextEdit, QProgressBar, QPushButton,
    QVBoxLayout, QWizardPage,
)

from .. import cache, config, installer_stages
from ..installer_stages import InstallContext


class _Signals(QObject):
    log = pyqtSignal(str, str)               # (level, message)
    progress = pyqtSignal(str, str, int)     # (stage_id, message, percent)
    finished = pyqtSignal(bool)


class InstallPage(QWizardPage):
    splash_image_name = "install.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")
        self._started = False
        self._ok = False

        layout = QVBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("INSTALLING...")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        self.stage_label = QLabel("Starting...")
        self.stage_label.setStyleSheet("color: #ffb84d; font-weight: bold;")
        layout.addWidget(self.stage_label)

        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        self.progress.setValue(0)
        layout.addWidget(self.progress)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(2000)
        layout.addWidget(self.log, 1)

        btn_row = QHBoxLayout()
        self.save_log_btn = QPushButton("Save log...")
        self.save_log_btn.clicked.connect(self._save_log)
        btn_row.addWidget(self.save_log_btn)
        btn_row.addStretch()
        layout.addLayout(btn_row)

    # ------------------------------------------------------------------
    def initializePage(self):
        if self._started:
            return
        self._started = True
        self.wizard().setButtonLayout([])  # hide nav buttons during install

        cache.ensure_dirs()

        def _str(v):
            if hasattr(v, "toString"):
                return v.toString()
            return str(v) if v else ""

        source_sa_root = _str(self.wizard().property("source_sa_root")).strip()
        dest_sa_root = _str(self.wizard().property("dest_sa_root")).strip()
        archives_folder = _str(self.wizard().property("archives_folder")).strip() or None
        have_local_mods = bool(self.wizard().property("have_local_mods"))
        needs_downgrade = bool(self.wizard().property("needs_downgrade"))
        nocd_patch_path = _str(self.wizard().property("nocd_patch_path")).strip() or None
        auto_download_nocd = bool(self.wizard().property("auto_download_nocd"))
        skip_backup = self.wizard().property("skip_backup") or False
        enabled_mod_ids = self.wizard().property("enabled_mod_ids") or []
        mod_sources = self.wizard().property("mod_sources") or list(config.ALL_MODS)

        ctx = InstallContext(
            source_sa_root=source_sa_root,
            dest_sa_root=dest_sa_root,
            have_local_mods=have_local_mods,
            archives_folder=archives_folder,
            needs_downgrade=needs_downgrade,
            nocd_patch_path=nocd_patch_path,
            auto_download_nocd=auto_download_nocd,
            skip_backup=bool(skip_backup),
            enabled_mod_ids=list(enabled_mod_ids),
            mod_sources=list(mod_sources),
        )

        self._signals = _Signals()
        self._signals.log.connect(self._on_log)
        self._signals.progress.connect(self._on_progress)
        self._signals.finished.connect(self._on_finished)

        self._thread = threading.Thread(target=self._run, args=(ctx,), daemon=True)
        self._thread.start()

    def _run(self, ctx: InstallContext):
        sig = self._signals
        def progress_cb(stage_id, msg, pct):
            sig.progress.emit(stage_id, msg, pct)
            ts = datetime.now().strftime("%H:%M:%S")
            sig.log.emit("INFO", f"[{ts}] {stage_id}: {msg} ({pct}%)")

        try:
            sig.log.emit("INFO", "=== Install started ===")
            sig.log.emit("INFO", f"Source SA:  {ctx.source_sa_root}")
            sig.log.emit("INFO", f"Destination: {ctx.dest_sa_root}")
            sig.log.emit("INFO", f"Have local mods: {ctx.have_local_mods}")
            if ctx.archives_folder:
                sig.log.emit("INFO", f"Archives folder: {ctx.archives_folder}")
            sig.log.emit("INFO", f"Needs downgrade: {ctx.needs_downgrade}")
            if ctx.nocd_patch_path:
                sig.log.emit("INFO", f"NO-CD patch: {ctx.nocd_patch_path}")
            elif ctx.auto_download_nocd:
                sig.log.emit("INFO", "NO-CD patch: will auto-download from GameCopyWorld")
            sig.log.emit("INFO", f"Mods selected: {ctx.enabled_mod_ids}")
            ok = installer_stages.run_full_install(ctx, progress_cb)
            sig.log.emit(
                "INFO" if ok else "ERROR",
                f"=== Install finished. OK={ok} "
                f"installed={ctx.installed_mods} failed={ctx.failed_mods} ==="
            )
            sig.finished.emit(ok)
        except Exception as e:
            sig.log.emit("ERROR", f"Unhandled exception: {e}")
            sig.finished.emit(False)

    # ------------------------------------------------------------------
    def _on_log(self, level: str, msg: str):
        color = {
            "INFO": "#ffb84d",
            "WARNING": "#ffe600",
            "ERROR": "#ff5b5b",
        }.get(level, "#ffffff")
        self.log.appendHtml(f"<span style='color:{color};'>[{level}] {msg}</span>")

    def _on_progress(self, stage_id: str, msg: str, pct: int):
        self.stage_label.setText(f"<b>{stage_id}</b> — {msg}")
        self.progress.setValue(max(0, min(100, pct)))

    def _on_finished(self, ok: bool):
        self._ok = ok
        from PyQt5.QtWidgets import QWizard
        self.wizard().setButtonLayout([
            QWizard.BackButton, QWizard.NextButton, QWizard.FinishButton,
            QWizard.CancelButton,
        ])
        if ok:
            self.stage_label.setText(
                "<span style='color:#5bff8a;font-weight:bold;'>INSTALL COMPLETE!</span>"
            )
            self.progress.setValue(100)
        else:
            self.stage_label.setText(
                "<span style='color:#ff5b5b;font-weight:bold;'>INSTALL FAILED — see log.</span>"
            )
        self.wizard().next()

    # ------------------------------------------------------------------
    def isComplete(self):
        return self._ok

    def nextId(self):
        from .wizard import PAGE_COMPLETE
        return PAGE_COMPLETE

    def _save_log(self):
        from PyQt5.QtWidgets import QFileDialog
        path, _ = QFileDialog.getSaveFileName(
            self, "Save install log",
            os.path.join(cache.CACHE_LOGS,
                         f"install_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"),
            "Log files (*.log *.txt);;All files (*)",
        )
        if path:
            with open(path, "w", encoding="utf-8") as f:
                f.write(self.log.toPlainText())
