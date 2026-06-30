"""Prerequisites & mod selection page.

Shows the mod list resolved by the scraper (or the hardcoded fallback).
User can tick/untick optional mods. The main mod is always installed and
shown as locked.
"""
from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import (
    QCheckBox, QGridLayout, QGroupBox, QLabel, QProgressBar, QPushButton,
    QVBoxLayout, QWizardPage,
)

from .. import config, scraper
from .theme import body_font


class PrereqsPage(QWizardPage):
    splash_image_name = "prereqs.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        self._mod_sources: list[config.ModSource] = []
        self._checkboxes: dict[str, QCheckBox] = {}

        layout = QVBoxLayout(self)
        layout.setSpacing(12)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("MODS  TO  INSTALL")
        title.setProperty("subheading", True)
        layout.addWidget(title)

        body = QLabel(
            "<div style='line-height:150%;'>"
            "The wizard scrapes <code>gtasas.netlify.app</code> for the "
            "latest official download links. The list below is what will be "
            "installed, in order. Tick/untick optional mods as you like — "
            "the main mod is required and cannot be turned off."
            "</div>"
        )
        body.setWordWrap(True)
        body.setTextFormat(Qt.RichText)
        layout.addWidget(body)

        # Refresh button — re-scrape the site
        refresh_row = __import__("PyQt5.QtWidgets", fromlist=["QHBoxLayout"]).QHBoxLayout()
        self.refresh_btn = QPushButton("Re-scrape mod site")
        self.refresh_btn.clicked.connect(self._rescrape)
        refresh_row.addWidget(self.refresh_btn)
        refresh_row.addStretch()
        layout.addLayout(refresh_row)

        self.grid_box = QGroupBox("Mod list")
        self.grid = QGridLayout(self.grid_box)
        self.grid.setColumnStretch(1, 1)
        layout.addWidget(self.grid_box)

        self.status_label = QLabel("")
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)

        layout.addStretch()

        self._rescrape(silent=True)

    # ------------------------------------------------------------------
    def _rescrape(self, silent: bool = False):
        # Clear existing widgets
        while self.grid.count():
            item = self.grid.takeAt(0)
            w = item.widget()
            if w:
                w.deleteLater()
        self._checkboxes.clear()

        if not silent:
            self.status_label.setText("Scraping gtasas.netlify.app...")
            from PyQt5.QtWidgets import QApplication
            QApplication.processEvents()

        try:
            self._mod_sources = scraper.resolve_mod_sources()
        except Exception as e:
            self.status_label.setText(
                f"<span style='color:#ff5b5b'>Scrape failed ({e}); "
                "using hardcoded list.</span>"
            )
            self._mod_sources = list(config.ALL_MODS)

        # Header row
        header_order = QLabel("ORDER")
        header_name = QLabel("MOD")
        header_desc = QLabel("DESCRIPTION")
        header_url = QLabel("SOURCE")
        for h in (header_order, header_name, header_desc, header_url):
            f = h.font(); f.setBold(True); h.setFont(f)
            h.setStyleSheet("color: #00f0ff;")
        self.grid.addWidget(header_order, 0, 0)
        self.grid.addWidget(header_name, 0, 1)
        self.grid.addWidget(header_desc, 0, 2)
        self.grid.addWidget(header_url, 0, 3)

        # Rows
        for i, mod in enumerate(self._mod_sources, start=1):
            order_lbl = QLabel(f"#{mod.install_order}")
            order_lbl.setStyleSheet("color: #ff2bd6; font-weight: bold;")

            if mod.is_main_mod:
                name_lbl = QLabel(f"<b>{mod.name}</b><br><span style='color:#5bff8a'>(required — main mod)</span>")
                name_lbl.setTextFormat(Qt.RichText)
                cb = QCheckBox()
                cb.setChecked(True)
                cb.setEnabled(False)
                cb.setToolTip("The main mod cannot be turned off.")
            else:
                name_lbl = QLabel(mod.name)
                cb = QCheckBox()
                cb.setChecked(mod.enabled_by_default)
                if not mod.optional:
                    cb.setEnabled(False)
                    cb.setChecked(True)
                    cb.setToolTip("Required prerequisite.")

            desc_lbl = QLabel(mod.description)
            desc_lbl.setProperty("dim", True)
            desc_lbl.setWordWrap(True)

            url_short = mod.url
            if len(url_short) > 60:
                url_short = url_short[:57] + "..."
            url_lbl = QLabel(f"<a href='{mod.url}' style='color:#00f0ff;'>{url_short}</a>")
            url_lbl.setTextFormat(Qt.RichText)
            url_lbl.setTextInteractionFlags(Qt.TextBrowserInteraction)
            url_lbl.setOpenExternalLinks(True)
            url_lbl.setWordWrap(True)

            self.grid.addWidget(order_lbl, i, 0)
            self.grid.addWidget(name_lbl, i, 1)
            self.grid.addWidget(desc_lbl, i, 2)
            self.grid.addWidget(url_lbl, i, 3)

            self._checkboxes[mod.id] = cb

        self.status_label.setText(
            f"<span style='color:#5bff8a'>Loaded {len(self._mod_sources)} mod(s) "
            "from the official site.</span>"
        )

    # ------------------------------------------------------------------
    def validatePage(self):
        # Stash the selection on the wizard object for later pages
        enabled = [mid for mid, cb in self._checkboxes.items() if cb.isChecked()]
        self.wizard().setProperty("enabled_mod_ids", enabled)
        self.wizard().setProperty("mod_sources", self._mod_sources)
        return True

    def nextId(self):
        from .wizard import PAGE_INSTALL
        return PAGE_INSTALL
