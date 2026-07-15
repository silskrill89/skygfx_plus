"""Source SA folder picker — where the user's vanilla SA install lives.

Auto-detects via registry / Steam libraryfolders.vdf / common paths.
Lets the user override by browsing. The chosen folder must contain
gta_sa.exe (or gta-sa.exe).
"""
from __future__ import annotations

import os

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QFileDialog, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QPushButton,
    QVBoxLayout, QWizardPage,
)

from .. import sa_detector


class SourceSAPage(QWizardPage):
    splash_image_name = "source_sa.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        layout = QVBoxLayout(self)
        layout.setSpacing(14)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("SOURCE  —  VANILLA  SAN  ANDREAS  FOLDER")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        hint = QLabel(
            "<div style='line-height:150%;'>"
            "Point the wizard at your <b>existing, vanilla GTA San Andreas</b> install. "
            "This folder must contain <code>gta_sa.exe</code> (or <code>gta-sa.exe</code>). "
            "The wizard will <b>copy</b> these files to a new mod install folder (which "
            "you'll pick next) — your original game is never modified."
            "</div>"
        )
        hint.setWordWrap(True)
        hint.setTextFormat(Qt.RichText)
        layout.addWidget(hint)

        sa_box = QGroupBox("Source San Andreas folder")
        sa_layout = QVBoxLayout(sa_box)

        row1 = QHBoxLayout()
        self.sa_edit = QLineEdit()
        self.sa_edit.setPlaceholderText("e.g.  C:\\Games\\GTA San Andreas")
        row1.addWidget(self.sa_edit, 1)

        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self._browse_sa)
        row1.addWidget(browse_btn)

        autodetect_btn = QPushButton("Auto-detect")
        autodetect_btn.clicked.connect(self._autodetect)
        row1.addWidget(autodetect_btn)
        sa_layout.addLayout(row1)

        self.sa_status = QLabel("")
        self.sa_status.setWordWrap(True)
        self.sa_status.setTextFormat(Qt.RichText)
        sa_layout.addWidget(self.sa_status)
        layout.addWidget(sa_box)

        layout.addStretch()

        self.registerField("source_sa_root*", self.sa_edit)

        self._auto_ran = False

    # ------------------------------------------------------------------
    def initializePage(self):
        if not self._auto_ran:
            self._auto_ran = True
            self._autodetect(silent=True)

    def _browse_sa(self):
        path = QFileDialog.getExistingDirectory(self, "Select your vanilla San Andreas install folder")
        if path:
            self.sa_edit.setText(path)
            self._validate_sa(path)

    def _autodetect(self, silent: bool = False):
        install = sa_detector.detect_install()
        if install:
            self.sa_edit.setText(install.root)
            self._show_sa_ok(install)
        elif not silent:
            self.sa_status.setText(
                "<span style='color:#ff5b5b'>Could not auto-detect a San Andreas install. "
                "Please browse to it manually.</span>"
            )

    def _validate_sa(self, path: str):
        install = sa_detector.validate_root(path)
        if install:
            self._show_sa_ok(install)
        else:
            self.sa_status.setText(
                "<span style='color:#ff5b5b'>No gta_sa.exe found in that folder. "
                "Pick the folder that contains the game executable.</span>"
            )

    def _show_sa_ok(self, install: sa_detector.SAInstall):
        if install.is_v10:
            color = "#5bff8a"
            ver_tag = f"v1.0 retail (size={install.exe_size:,} bytes"
            if install.version_string:
                ver_tag += f", version={install.version_string}"
            ver_tag += ")"
        elif install.is_steam:
            color = "#ffe600"
            ver_tag = (f"Steam v3.0 detected — you'll need to apply a NO-CD patch in the next step. "
                       f"(size={install.exe_size:,} bytes)")
        else:
            color = "#ffe600"
            ver_tag = (f"Unrecognised version (size={install.exe_size:,} bytes"
                       + (f", version={install.version_string}" if install.version_string else "")
                       + "). The next page will let you hash-check and apply a downgrade.")
        self.sa_status.setText(
            f"<span style='color:{color}'>OK — detected via {install.source}. {ver_tag}</span>"
        )

    # ------------------------------------------------------------------
    def validatePage(self):
        path = self.sa_edit.text().strip()
        if not path or not os.path.isdir(path):
            self.sa_status.setText(
                "<span style='color:#ff5b5b'>Please pick a valid San Andreas install folder.</span>"
            )
            return False
        install = sa_detector.validate_root(path)
        if not install:
            self.sa_status.setText(
                "<span style='color:#ff5b5b'>That folder does not contain gta_sa.exe. "
                "Please pick the San Andreas install root.</span>"
            )
            return False
        self.wizard().setProperty("source_sa_root", path)
        return True

    def nextId(self):
        from .wizard import PAGE_DESTINATION
        return PAGE_DESTINATION
