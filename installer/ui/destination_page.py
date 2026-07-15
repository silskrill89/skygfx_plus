"""Destination folder picker — where to create the standalone modded SA install.

This is the folder the wizard will:
    1. Copy the vanilla SA files into (full clone).
    2. Apply NO-CD / downgrade patch (if needed).
    3. Apply all selected mods on top.

The result is a fully portable, standalone modded San Andreas folder that
doesn't touch the original install.
"""
from __future__ import annotations

import os

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QFileDialog, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QPushButton,
    QVBoxLayout, QWizardPage,
)


class DestinationPage(QWizardPage):
    splash_image_name = "destination.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        layout = QVBoxLayout(self)
        layout.setSpacing(14)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("DESTINATION  —  MOD  INSTALL  FOLDER")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        hint = QLabel(
            "<div style='line-height:150%;'>"
            "Pick the folder where the wizard will <b>create</b> your standalone "
            "modded San Andreas install. The wizard copies your vanilla SA files "
            "here, then applies the GTA SAS 1987 mod on top. Your original install "
            "stays untouched."
            "<br><br>"
            "<b>Tip:</b> Use a fresh empty folder, or pick an existing folder and the "
            "wizard will merge into it. Aim for at least <b>5 GB</b> of free space."
            "</div>"
        )
        hint.setWordWrap(True)
        hint.setTextFormat(Qt.RichText)
        layout.addWidget(hint)

        dest_box = QGroupBox("Destination folder")
        dest_layout = QVBoxLayout(dest_box)

        row = QHBoxLayout()
        self.dest_edit = QLineEdit()
        self.dest_edit.setPlaceholderText("e.g.  D:\\Games\\GTA SAS 1987")
        row.addWidget(self.dest_edit, 1)

        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self._browse_dest)
        row.addWidget(browse_btn)

        # Suggest a default based on the source folder's parent + a sub-folder
        suggest_btn = QPushButton("Suggest")
        suggest_btn.clicked.connect(self._suggest)
        row.addWidget(suggest_btn)
        dest_layout.addLayout(row)

        self.dest_status = QLabel("")
        self.dest_status.setWordWrap(True)
        self.dest_status.setTextFormat(Qt.RichText)
        dest_layout.addWidget(self.dest_status)
        layout.addWidget(dest_box)

        # Source-vs-dest comparison box
        self.compare_box = QGroupBox("Source vs destination")
        compare_layout = QVBoxLayout(self.compare_box)
        self.compare_label = QLabel("")
        self.compare_label.setWordWrap(True)
        self.compare_label.setTextFormat(Qt.RichText)
        compare_layout.addWidget(self.compare_label)
        layout.addWidget(self.compare_box)

        layout.addStretch()

        self.registerField("dest_sa_root*", self.dest_edit)

    # ------------------------------------------------------------------
    def initializePage(self):
        # Auto-suggest a destination the first time the page is shown
        if not self.dest_edit.text().strip():
            self._suggest()

    def _browse_dest(self):
        path = QFileDialog.getExistingDirectory(self, "Select destination folder for the modded install")
        if path:
            self.dest_edit.setText(path)
            self._validate_dest(path)

    def _suggest(self):
        """Suggest a destination based on the source folder."""
        source = self.wizard().property("source_sa_root") or ""
        if hasattr(source, "toString"):
            source = source.toString()
        source = str(source).strip()
        if source:
            parent = os.path.dirname(source)
            suggestion = os.path.join(parent, "GTA SAS 1987")
        else:
            suggestion = os.path.expanduser(r"~\Games\GTA SAS 1987")
        self.dest_edit.setText(suggestion)
        self._validate_dest(suggestion)

    def _validate_dest(self, path: str):
        source = self.wizard().property("source_sa_root") or ""
        if hasattr(source, "toString"):
            source = source.toString()
        source = str(source).strip()

        # Check if same as source
        if source and os.path.normpath(path) == os.path.normpath(source):
            self.dest_status.setText(
                "<span style='color:#ff5b5b'>Destination cannot be the same as the source folder.</span>"
            )
            return

        # Check if folder exists (warning, not error — wizard will merge)
        if os.path.isdir(path) and os.listdir(path):
            self.dest_status.setText(
                "<span style='color:#ffe600'>Folder already exists and is non-empty — "
                "the wizard will merge into it. Pick an empty folder for a clean install.</span>"
            )
        else:
            # Try to compute free space on the destination drive
            try:
                import shutil as _sh
                total, used, free = _sh.disk_usage(path if os.path.isdir(path) else os.path.dirname(path) or ".")
                free_gb = free / (1024 ** 3)
                color = "#5bff8a" if free_gb >= 5 else "#ffe600"
                self.dest_status.setText(
                    f"<span style='color:{color}'>OK — destination is fresh. "
                    f"Free space on this drive: {free_gb:.1f} GB (need at least 5 GB).</span>"
                )
            except Exception:
                self.dest_status.setText(
                    "<span style='color:#5bff8a'>OK — destination is fresh.</span>"
                )

        # Show source vs dest
        if source:
            self.compare_label.setText(
                f"<b>Source (vanilla SA):</b><br><code>{source}</code><br><br>"
                f"<b>Destination (modded SA):</b><br><code>{path}</code>"
            )

    # ------------------------------------------------------------------
    def validatePage(self):
        path = self.dest_edit.text().strip()
        if not path:
            self.dest_status.setText(
                "<span style='color:#ff5b5b'>Please pick a destination folder.</span>"
            )
            return False

        source = self.wizard().property("source_sa_root") or ""
        if hasattr(source, "toString"):
            source = source.toString()
        source = str(source).strip()
        if source and os.path.normpath(path) == os.path.normpath(source):
            self.dest_status.setText(
                "<span style='color:#ff5b5b'>Destination cannot be the same as the source folder.</span>"
            )
            return False

        # Create the folder if it doesn't exist
        try:
            os.makedirs(path, exist_ok=True)
        except Exception as e:
            self.dest_status.setText(
                f"<span style='color:#ff5b5b'>Could not create destination folder: {e}</span>"
            )
            return False

        self.wizard().setProperty("dest_sa_root", path)
        return True

    def nextId(self):
        from .wizard import PAGE_DOWNGRADE
        return PAGE_DOWNGRADE
