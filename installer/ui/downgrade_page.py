"""Downgrade / NO-CD page.

Shows the SHA-1 hash-based detection result. If the source exe isn't v1.0,
offers three options:
    1. Auto-download the HOODLUM v1.0 NO-CD patch from GameCopyWorld.
    2. Browse for a manually-downloaded NO-CD patch archive (.zip / .rar).
    3. Skip — proceed at your own risk (mods may not work).
"""
from __future__ import annotations

import os

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QButtonGroup, QCheckBox, QFileDialog, QGroupBox, QHBoxLayout, QLabel,
    QLineEdit, QProgressBar, QPushButton, QRadioButton, QVBoxLayout, QWizardPage,
)

from .. import config, gamecopyworld, sa_hashes


class DowngradePage(QWizardPage):
    splash_image_name = "downgrade.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        self._detection: sa_hashes.DetectionResult | None = None
        self._patches: list[gamecopyworld.NoCDPatch] = []

        layout = QVBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("VERSION  CHECK  &  DOWNGRADE")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        # --- Detection result box ---
        det_box = QGroupBox("Detection result")
        det_layout = QVBoxLayout(det_box)
        self.det_status = QLabel("")
        self.det_status.setWordWrap(True)
        self.det_status.setTextFormat(Qt.RichText)
        det_layout.addWidget(self.det_status)

        self.det_hash = QLabel("")
        self.det_hash.setWordWrap(True)
        self.det_hash.setTextFormat(Qt.RichText)
        det_layout.addWidget(self.det_hash)
        layout.addWidget(det_box)

        # --- NO-CD patch options (visible only if downgrade needed) ---
        self.nocd_box = QGroupBox("NO-CD patch (downgrade)")
        nocd_layout = QVBoxLayout(self.nocd_box)

        self.rb_auto = QRadioButton(
            "<b>Auto-download from GameCopyWorld</b><br>"
            "<span style='color:#7a6a9b'>Wizard fetches the recommended HOODLUM v1.0 "
            "No-CD/Fixed EXE patch from gamecopyworld.com automatically.</span>"
        )
        self.rb_manual = QRadioButton(
            "<b>I have a NO-CD patch archive</b><br>"
            "<span style='color:#7a6a9b'>You downloaded the patch .zip / .rar from GCW "
            "or another site. Browse to it below.</span>"
        )
        self.rb_skip = QRadioButton(
            "<b>Skip — proceed at my own risk</b><br>"
            "<span style='color:#7a6a9b'>Continue without a NO-CD patch. Most mods will "
            "fail to load on Steam v3.0 / SecuROM exes.</span>"
        )

        self._choice_group = QButtonGroup(self)
        self._choice_group.addButton(self.rb_auto, 1)
        self._choice_group.addButton(self.rb_manual, 2)
        self._choice_group.addButton(self.rb_skip, 3)
        self.rb_auto.setChecked(True)

        nocd_layout.addWidget(self.rb_auto)
        nocd_layout.addWidget(self.rb_manual)
        nocd_layout.addWidget(self.rb_skip)

        # Manual patch picker
        man_row = QHBoxLayout()
        self.patch_edit = QLineEdit()
        self.patch_edit.setPlaceholderText("Path to NO-CD patch .zip / .rar / .7z")
        self.patch_edit.setEnabled(False)
        man_row.addWidget(self.patch_edit, 1)
        self.patch_browse = QPushButton("Browse...")
        self.patch_browse.setEnabled(False)
        self.patch_browse.clicked.connect(self._browse_patch)
        man_row.addWidget(self.patch_browse)
        nocd_layout.addLayout(man_row)

        # GCW info
        self.gcw_label = QLabel(
            f"<span style='color:#7a6a9b'>"
            f"Browse GameCopyWorld's GTA SA patches page manually:<br>"
            f"<a href='{config.GAMECOPYWORLD_SA_URL}' style='color:#00f0ff;'>"
            f"{config.GAMECOPYWORLD_SA_URL}</a></span>"
        )
        self.gcw_label.setTextFormat(Qt.RichText)
        self.gcw_label.setTextInteractionFlags(Qt.TextBrowserInteraction)
        self.gcw_label.setOpenExternalLinks(True)
        self.gcw_label.setWordWrap(True)
        nocd_layout.addWidget(self.gcw_label)

        # Fetch patches button
        fetch_row = QHBoxLayout()
        self.fetch_btn = QPushButton("Fetch patch list from GameCopyWorld")
        self.fetch_btn.clicked.connect(self._fetch_patches)
        fetch_row.addWidget(self.fetch_btn)
        fetch_row.addStretch()
        nocd_layout.addLayout(fetch_row)

        self.patch_list_label = QLabel("")
        self.patch_list_label.setWordWrap(True)
        self.patch_list_label.setTextFormat(Qt.RichText)
        nocd_layout.addWidget(self.patch_list_label)

        layout.addWidget(self.nocd_box)

        # Wire choice change
        self._choice_group.buttonClicked.connect(self._on_choice_changed)

        layout.addStretch()

    # ------------------------------------------------------------------
    def initializePage(self):
        source_sa = self.wizard().property("source_sa_root") or ""
        if hasattr(source_sa, "toString"):
            source_sa = source_sa.toString()
        source_sa = str(source_sa).strip()

        # Find the exe
        exe_path = None
        for name in ("gta_sa.exe", "gta-sa.exe"):
            p = os.path.join(source_sa, name)
            if os.path.isfile(p):
                exe_path = p
                break

        if not exe_path:
            self.det_status.setText(
                "<span style='color:#ff5b5b'>No gta_sa.exe found in source folder.</span>"
            )
            self.det_hash.setText("")
            self.nocd_box.setVisible(False)
            return

        # Compute hash + detect
        self._detection = sa_hashes.detect(exe_path)
        self._update_detection_display()

    def _update_detection_display(self):
        d = self._detection
        if not d:
            return

        if d.matched:
            v = d.matched
            if v.mod_compatible:
                color = "#5bff8a"
                status = (f"DETECTED: {v.label} — v1.0 mod-compatible. "
                          "Your exe is already patched (NO-CD / v1.0 retail) — no downgrade needed.")
            else:
                color = "#ffe600"
                status = f"DETECTED: {v.label} — needs downgrade."
            self.det_status.setText(f"<span style='color:{color}'>{status}</span>")
            self.det_hash.setText(
                f"<b>Method:</b> {d.match_method}<br>"
                f"<b>Size:</b> {d.file_size:,} bytes<br>"
                f"<b>SHA-1:</b> <code>{d.sha1 or '(unknown)'}</code><br>"
                f"<b>Source:</b> {v.source}<br>"
                f"<b>Notes:</b> {v.notes}"
            )
            self.nocd_box.setVisible(v.needs_downgrade)
            self._set_skip_if_no_downgrade(v.needs_downgrade)
        else:
            if d.is_v10:
                self.det_status.setText(
                    "<span style='color:#5bff8a'>Likely v1.0 / already NO-CD patched "
                    "(heuristic by file size). No downgrade needed — you're good to go.</span>"
                )
                self.nocd_box.setVisible(False)
            else:
                self.det_status.setText(
                    "<span style='color:#ffe600'>Unknown version — proceed with downgrade "
                    "to be safe.</span>"
                )
                self.nocd_box.setVisible(True)
            self.det_hash.setText(
                f"<b>Method:</b> {d.match_method} (heuristic)<br>"
                f"<b>Size:</b> {d.file_size:,} bytes<br>"
                f"<b>SHA-1:</b> <code>{d.sha1 or '(unknown)'}</code><br>"
                "<span style='color:#7aa684'>Add this hash to data/exe_hashes.json "
                "to improve future detection.</span>"
            )

    def _set_skip_if_no_downgrade(self, needs_downgrade: bool):
        if not needs_downgrade:
            self.rb_skip.setChecked(True)
            self.nocd_box.setVisible(False)

    # ------------------------------------------------------------------
    def _on_choice_changed(self, *_args):
        manual = self.rb_manual.isChecked()
        self.patch_edit.setEnabled(manual)
        self.patch_browse.setEnabled(manual)

    def _browse_patch(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select NO-CD patch archive",
            "", "Archives (*.zip *.rar *.7z);;All files (*)",
        )
        if path:
            self.patch_edit.setText(path)

    def _fetch_patches(self):
        from PyQt5.QtWidgets import QApplication
        self.patch_list_label.setText("<span style='color:#ffe600'>Fetching patch list from GameCopyWorld...</span>")
        QApplication.processEvents()
        try:
            self._patches = gamecopyworld.fetch_patches(timeout=20)
        except Exception as e:
            self.patch_list_label.setText(
                f"<span style='color:#ff5b5b'>Failed: {e}</span>"
            )
            return

        if not self._patches:
            self.patch_list_label.setText(
                "<span style='color:#ffe600'>No patches found (Cloudflare?). "
                "Open the GCW page in your browser and download the patch manually.</span>"
            )
            return

        # Show top 5
        rec = gamecopyworld.recommended_patch(self._patches)
        lines = [f"<b>{len(self._patches)} patches found.</b> Recommended: "
                 f"<span style='color:#5bff8a'>{rec.name}</span> ({rec.group}, {rec.date}, {rec.file_size})<br><br>"]
        lines.append("All v1.0 patches:<br>")
        for p in self._patches[:8]:
            mark = "★ " if p.recommended else "  "
            lines.append(f"{mark}<code>{p.name}</code> — {p.group}, {p.date}, {p.file_size}<br>")
        if len(self._patches) > 8:
            lines.append(f"<br><i>...and {len(self._patches) - 8} more.</i>")
        self.patch_list_label.setText("".join(lines))

    # ------------------------------------------------------------------
    def validatePage(self):
        d = self._detection
        needs_downgrade = (d.matched.needs_downgrade if d and d.matched else (not d.is_v10 if d else True))

        if not needs_downgrade:
            self.wizard().setProperty("needs_downgrade", False)
            self.wizard().setProperty("nocd_patch_path", None)
            self.wizard().setProperty("auto_download_nocd", False)
            return True

        # User picked an option
        if self.rb_auto.isChecked():
            self.wizard().setProperty("needs_downgrade", True)
            self.wizard().setProperty("auto_download_nocd", True)
            self.wizard().setProperty("nocd_patch_path", None)
            return True

        if self.rb_manual.isChecked():
            path = self.patch_edit.text().strip()
            if not path or not os.path.isfile(path):
                self.patch_list_label.setText(
                    "<span style='color:#ff5b5b'>Please pick a valid patch archive file.</span>"
                )
                return False
            self.wizard().setProperty("needs_downgrade", True)
            self.wizard().setProperty("auto_download_nocd", False)
            self.wizard().setProperty("nocd_patch_path", path)
            return True

        # Skip
        self.wizard().setProperty("needs_downgrade", False)
        self.wizard().setProperty("auto_download_nocd", False)
        self.wizard().setProperty("nocd_patch_path", None)
        return True

    def nextId(self):
        from .wizard import PAGE_BACKUP
        return PAGE_BACKUP
