"""Backup page — confirm backup before mods are applied."""
from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QCheckBox, QLabel, QVBoxLayout, QWizardPage,
)

from .. import cache


class BackupPage(QWizardPage):
    splash_image_name = "backup.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        layout = QVBoxLayout(self)
        layout.setSpacing(14)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("BACKUP  ORIGINAL  FILES")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        body = QLabel(
            "<div style='line-height:150%;'>"
            "Before any mod is applied, the wizard will back up your "
            "original San Andreas files to a single timestamped .zip in "
            "the shared cache. This lets you fully restore the game later."
            "<br><br>"
            "Files that will be backed up (if present):<br>"
            "&nbsp;&nbsp;<code>gta_sa.exe</code>, <code>gta-sa.exe</code>, "
            "<code>data/</code>, <code>models/</code>, <code>scripts/</code>, "
            "<code>cleo/</code>, <code>modloader/</code>, <code>text/</code>, "
            "<code>audio/</code>, <code>SAMP/</code>"
            "<br><br>"
            f"Backup destination: <code>{cache.CACHE_BACKUPS}</code>"
            "</div>"
        )
        body.setWordWrap(True)
        body.setTextFormat(Qt.RichText)
        layout.addWidget(body)

        self.skip_cb = QCheckBox("Skip backup (NOT recommended — no easy restore)")
        layout.addWidget(self.skip_cb)

        layout.addStretch()

    def nextId(self):
        from .wizard import PAGE_PREREQS
        return PAGE_PREREQS
