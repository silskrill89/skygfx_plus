"""Welcome page — title, mod description, start button."""
from __future__ import annotations

from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QLabel, QVBoxLayout, QWizardPage

from .. import config
from .theme import heading_font, body_font


class WelcomePage(QWizardPage):
    splash_image_name = "welcome.png"

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setTitle("")

        layout = QVBoxLayout(self)
        layout.setSpacing(14)
        layout.setContentsMargins(40, 30, 40, 30)

        title = QLabel("GTA SAN ANDREAS\nSTORIES  1987")
        title.setProperty("heading", True)
        title.setFont(heading_font(38))
        title.setAlignment(Qt.AlignCenter)
        title.setStyleSheet("color: #ff6a2b;")
        layout.addWidget(title)

        subtitle = QLabel("// INSTALLER WIZARD  v" + config.APP_VERSION)
        subtitle.setProperty("subheading", True)
        subtitle.setAlignment(Qt.AlignCenter)
        subtitle.setStyleSheet("color: #3d8a3d; letter-spacing: 4px;")
        layout.addWidget(subtitle)

        layout.addSpacing(20)

        body = QLabel(
            "<div style='line-height:160%;'>"
            "This wizard builds a <b>standalone, portable modded San Andreas</b> folder. "
            "It will:<br><br>"
            "&nbsp;&nbsp;1. Ask if you already have mod files, or want the wizard to download them<br>"
            "&nbsp;&nbsp;2. Locate your <b>vanilla</b> GTA San Andreas install (the source)<br>"
            "&nbsp;&nbsp;3. Pick a <b>destination</b> folder where the standalone modded install will live<br>"
            "&nbsp;&nbsp;4. Hash-check your <code>gta_sa.exe</code> and offer a NO-CD downgrade "
            "patch from GameCopyWorld if needed<br>"
            "&nbsp;&nbsp;5. <b>Copy</b> your vanilla SA files to the destination (original stays untouched)<br>"
            "&nbsp;&nbsp;6. <b>Back up</b> the freshly-cloned destination to a safe .zip<br>"
            "&nbsp;&nbsp;7. <b>Download</b> the GTA SAS 1987 main mod (MediaFire) + optional prereqs "
            "(CLEO 5, CLEO+, NewOpcodes from GitHub/MixMods/LibertyCity)<br>"
            "&nbsp;&nbsp;8. <b>Install</b> everything on top of the destination in the right order"
            "</div>"
        )
        body.setWordWrap(True)
        body.setAlignment(Qt.AlignLeft)
        body.setFont(body_font(10))
        layout.addWidget(body)

        layout.addStretch()

        footer = QLabel(
            "Fan-made installer. Not affiliated with Rockstar Games or Take-Two. "
            "You must own a legal copy of GTA San Andreas. The NO-CD patch is for use "
            "with your own legally-purchased game only."
        )
        footer.setProperty("dim", True)
        footer.setWordWrap(True)
        footer.setAlignment(Qt.AlignCenter)
        layout.addWidget(footer)

    def nextId(self):
        from .wizard import PAGE_MOD_SOURCE
        return PAGE_MOD_SOURCE
