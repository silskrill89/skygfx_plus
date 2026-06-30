"""Completion page — final summary + launch button."""
from __future__ import annotations

import os
import subprocess

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWizardPage,
)

from .. import cache


class CompletePage(QWizardPage):
    splash_image_name = "install.png"  # reuse the install splash for completion

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")
        self._dest_sa_root = ""

        layout = QVBoxLayout(self)
        layout.setSpacing(14)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("INSTALLATION  COMPLETE")
        title.setProperty("heading", True)
        title.setAlignment(Qt.AlignCenter)
        layout.addWidget(title)

        self.summary = QLabel("")
        self.summary.setWordWrap(True)
        self.summary.setTextFormat(Qt.RichText)
        layout.addWidget(self.summary)

        layout.addSpacing(12)

        btn_row = QHBoxLayout()

        launch_btn = QPushButton("Launch GTA SA")
        launch_btn.setProperty("primary", True)
        launch_btn.clicked.connect(self._launch)
        btn_row.addWidget(launch_btn)

        open_dest_btn = QPushButton("Open install folder")
        open_dest_btn.clicked.connect(self._open_dest)
        btn_row.addWidget(open_dest_btn)

        open_cache_btn = QPushButton("Open cache folder")
        open_cache_btn.clicked.connect(self._open_cache)
        btn_row.addWidget(open_cache_btn)

        btn_row.addStretch()
        layout.addLayout(btn_row)

        layout.addStretch()

        footer = QLabel(
            "Tip: your standalone modded install is fully portable — copy the destination "
            "folder anywhere. The original SA install was never modified. "
            f"Backups live at <code>{cache.CACHE_BACKUPS}</code>."
        )
        footer.setProperty("dim", True)
        footer.setWordWrap(True)
        layout.addWidget(footer)

    # ------------------------------------------------------------------
    def initializePage(self):
        dest = self.wizard().property("dest_sa_root") or ""
        if hasattr(dest, "toString"):
            dest = dest.toString()
        self._dest_sa_root = str(dest).strip()

        installed = self.wizard().property("enabled_mod_ids") or []
        source_sa = self.wizard().property("source_sa_root") or ""
        if hasattr(source_sa, "toString"):
            source_sa = source_sa.toString()

        self.summary.setText(
            "<div style='line-height:160%;'>"
            "<span style='color:#5bff8a;font-size:14pt;'>✓ All stages completed.</span><br><br>"
            f"<b>Source (vanilla SA):</b> <code>{source_sa}</code><br>"
            f"<b>Modded install:</b> <code>{self._dest_sa_root}</code><br>"
            f"<b>Mods installed:</b> {', '.join(installed) if installed else '(none — check log)'}<br>"
            f"<b>Backup location:</b> <code>{cache.CACHE_BACKUPS}</code><br><br>"
            f"Click <b>Launch GTA SA</b> to start the game from the new modded folder, "
            f"or use the buttons below to open the install folder or restore-files cache."
            "</div>"
        )

    # ------------------------------------------------------------------
    def _launch(self):
        if not self._dest_sa_root:
            return
        exe = os.path.join(self._dest_sa_root, "gta_sa.exe")
        if not os.path.isfile(exe):
            exe = os.path.join(self._dest_sa_root, "gta-sa.exe")
        if not os.path.isfile(exe):
            return
        try:
            if os.name == "nt":
                os.startfile(exe)  # type: ignore[attr-defined]
            else:
                subprocess.Popen([exe], cwd=self._dest_sa_root)
        except Exception:
            pass

    def _open_dest(self):
        if not self._dest_sa_root or not os.path.isdir(self._dest_sa_root):
            return
        if os.name == "nt":
            os.startfile(self._dest_sa_root)  # type: ignore[attr-defined]
        else:
            subprocess.Popen(["xdg-open", self._dest_sa_root])

    def _open_cache(self):
        cache.ensure_dirs()
        if os.name == "nt":
            os.startfile(cache.CACHE_ROOT)  # type: ignore[attr-defined]
        else:
            subprocess.Popen(["xdg-open", cache.CACHE_ROOT])
