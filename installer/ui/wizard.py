"""Top-level QWizard subclass — wires pages together with the synthwave bg."""
from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QWizard

from .theme import SynthwaveBackground
from .welcome_page import WelcomePage
from .mod_source_page import ModSourcePage
from .source_sa_page import SourceSAPage
from .destination_page import DestinationPage
from .downgrade_page import DowngradePage
from .backup_page import BackupPage
from .prereqs_page import PrereqsPage
from .install_page import InstallPage
from .complete_page import CompletePage

# Page IDs (must be imported by individual pages via .wizard)
PAGE_WELCOME = 1
PAGE_MOD_SOURCE = 2
PAGE_SOURCE_SA = 3
PAGE_DESTINATION = 4
PAGE_DOWNGRADE = 5
PAGE_BACKUP = 6
PAGE_PREREQS = 7
PAGE_INSTALL = 8
PAGE_COMPLETE = 9


class InstallerWizard(SynthwaveBackground):
    """The main wizard window. Inherits the painted synthwave background."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("GTA San Andreas Stories 1987 — Installer")
        self.setWizardStyle(QWizard.ModernStyle)
        self.setMinimumSize(1040, 740)
        self.setOptions(
            QWizard.NoBackButtonOnStartPage
            | QWizard.NoBackButtonOnLastPage
        )
        self.setWindowFlags(self.windowFlags() & ~Qt.WindowContextHelpButtonHint)

        # Add pages
        self.setPage(PAGE_WELCOME, WelcomePage(self))
        self.setPage(PAGE_MOD_SOURCE, ModSourcePage(self))
        self.setPage(PAGE_SOURCE_SA, SourceSAPage(self))
        self.setPage(PAGE_DESTINATION, DestinationPage(self))
        self.setPage(PAGE_DOWNGRADE, DowngradePage(self))
        self.setPage(PAGE_BACKUP, BackupPage(self))
        self.setPage(PAGE_PREREQS, PrereqsPage(self))
        self.setPage(PAGE_INSTALL, InstallPage(self))
        self.setPage(PAGE_COMPLETE, CompletePage(self))

        self.setStartId(PAGE_WELCOME)

        # Repaint the background whenever the page changes (so each page
        # shows its own splash image).
        self.currentIdChanged.connect(self._on_page_changed)

    def _on_page_changed(self, *_args):
        """Force a repaint so the splash image updates for the new page."""
        self.update()

    def next(self):
        super().next()
