"""Mod source choice page.

The user must choose:
    * "I already have mod files (.zip/.rar/.7z) downloaded" — wizard will
      scan a folder of archives and use those instead of downloading.
    * "Download everything for me" — wizard will fetch every mod from the
      internet (MediaFire / GitHub / MixMods / LibertyCity as configured).
"""
from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QFileDialog, QGroupBox, QHBoxLayout, QLabel, QLineEdit, QPushButton,
    QRadioButton, QVBoxLayout, QWizardPage, QButtonGroup,
)

from .. import extractor


class ModSourcePage(QWizardPage):
    splash_image_name = "mod_source.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        layout = QVBoxLayout(self)
        layout.setSpacing(14)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("MOD  SOURCE")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        intro = QLabel(
            "<div style='line-height:150%;'>"
            "Do you already have the mod files downloaded on your computer, "
            "or should the wizard download them for you?"
            "</div>"
        )
        intro.setWordWrap(True)
        intro.setTextFormat(Qt.RichText)
        layout.addWidget(intro)

        # --- Radio choices ---
        choice_box = QGroupBox("Choose one")
        choice_layout = QVBoxLayout(choice_box)

        self.rb_have = QRadioButton(
            "<b>I already have mod files</b><br>"
            "<span style='color:#7a6a9b'>You have .zip / .rar / .7z archives of the mods "
            "already. The wizard will scan a folder and use them instead of downloading.</span>"
        )
        self.rb_have.setStyleSheet("QRadioButton::indicator { width: 20px; height: 20px; }")
        choice_layout.addWidget(self.rb_have)

        self.rb_download = QRadioButton(
            "<b>Download everything for me</b><br>"
            "<span style='color:#7a6a9b'>The wizard will fetch the main mod from MediaFire, "
            "prerequisites from GitHub/MixMods, and (if needed) the NO-CD patch from "
            "GameCopyWorld.</span>"
        )
        self.rb_download.setStyleSheet("QRadioButton::indicator { width: 20px; height: 20px; }")
        choice_layout.addWidget(self.rb_download)

        # Default: download (the common case)
        self.rb_download.setChecked(True)

        self._choice_group = QButtonGroup(self)
        self._choice_group.addButton(self.rb_have, 1)
        self._choice_group.addButton(self.rb_download, 2)
        self._choice_group.buttonClicked.connect(self._on_choice_changed)

        layout.addWidget(choice_box)

        # --- Archives folder (only visible if "I have mods" selected) ---
        self.arch_box = QGroupBox("Archives folder")
        arch_layout = QVBoxLayout(self.arch_box)

        arch_hint = QLabel(
            "Point the wizard at the folder containing your mod archives. "
            "It will scan for .zip / .rar / .7z and use them instead of downloading."
        )
        arch_hint.setProperty("dim", True)
        arch_hint.setWordWrap(True)
        arch_layout.addWidget(arch_hint)

        row = QHBoxLayout()
        self.arch_edit = QLineEdit()
        self.arch_edit.setPlaceholderText("e.g.  D:\\Downloads\\GTA SAS 1987 mods")
        row.addWidget(self.arch_edit, 1)
        browse_btn = QPushButton("Browse...")
        browse_btn.clicked.connect(self._browse_archives)
        row.addWidget(browse_btn)
        arch_layout.addLayout(row)

        self.arch_status = QLabel("")
        self.arch_status.setWordWrap(True)
        arch_layout.addWidget(self.arch_status)
        layout.addWidget(self.arch_box)

        layout.addStretch()

        # Register fields
        self.registerField("have_local_mods", self.rb_have, "checked")
        self.registerField("archives_folder", self.arch_edit)

        # Initial state
        self._on_choice_changed()

    # ------------------------------------------------------------------
    def _on_choice_changed(self, *_args):
        have = self.rb_have.isChecked()
        self.arch_box.setVisible(have)
        self.arch_edit.setEnabled(have)

    def _browse_archives(self):
        path = QFileDialog.getExistingDirectory(self, "Select folder containing mod archives")
        if path:
            self.arch_edit.setText(path)
            archives = extractor.scan_archives(path)
            if not archives:
                self.arch_status.setText(
                    "<span style='color:#ffe600'>No .zip / .rar / .7z files found in that folder. "
                    "Pick another folder or switch to 'Download for me'.</span>"
                )
            else:
                self.arch_status.setText(
                    f"<span style='color:#5bff8a'>Found {len(archives)} archive(s). "
                    "The wizard will look here before downloading.</span>"
                )

    # ------------------------------------------------------------------
    def validatePage(self):
        # Store choice on the wizard for later pages
        have = self.rb_have.isChecked()
        self.wizard().setProperty("have_local_mods", have)
        self.wizard().setProperty("archives_folder", self.arch_edit.text().strip() if have else "")
        if have:
            folder = self.arch_edit.text().strip()
            if not folder:
                self.arch_status.setText(
                    "<span style='color:#ff5b5b'>Please pick an archives folder, "
                    "or switch to 'Download for me'.</span>"
                )
                return False
        return True

    def nextId(self):
        from .wizard import PAGE_SOURCE_SA
        return PAGE_SOURCE_SA
