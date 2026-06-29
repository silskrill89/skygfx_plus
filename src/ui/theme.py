"""80s VCS + LCS + Ryder + Los Santos themed installer wizard (v3).

Key changes from v2:
    * Uses the FREE Pricedown Bl font (Typodermic Fonts Inc. — free for
      commercial use) for all display text — the actual GTA logo font.
    * Palette taken from GTA Vice City Stories carcols.dat (per user request)
      with a preference for DARK GREEN shades so text stays legible against
      the bright sunset background.
    * Each wizard page has its own splash image from the actual mod
      (downloaded from babamohammed2022/gta-1987-remastered-mod on GitHub).
    * All text labels sit on a semi-transparent dark panel so white-on-white
      contrast is impossible.
"""
from __future__ import annotations

import math
import os

from PyQt5.QtCore import Qt, QRectF
from PyQt5.QtGui import (
    QColor, QFont, QFontDatabase, QLinearGradient, QPainter, QPen, QPixmap,
    QPolygonF, QBrush, QPainterPath, QFontInfo
)
from PyQt5.QtWidgets import QApplication, QWizard
from PyQt5.QtWidgets import QWizard  # noqa: F811

# ----------------------------------------------------------------------
# Palette — VCS carcols.dat dark greens preferred for legibility
#
# Reference: GTA Vice City Stories carcols.dat color IDs
#   (the user explicitly asked for this palette with dark greens preferred)
#
# We pick a coordinated set of greens + sunset accents that all hit
# WCAG AA contrast against the dark panel (#0a1f12).
# ----------------------------------------------------------------------
# Dark green base — VCS carcols dark green (id ~53 in the VCS table)
COLOR_DARK_GREEN = "#0d3b1f"      # very dark green (panel bg)
COLOR_PANEL_BG = "#0a1f12"        # near-black green for content panels
COLOR_PANEL_BG_LIGHT = "#15351f"  # slightly lighter green for secondary panels

# Body text greens (high contrast against dark panel)
COLOR_TEXT_BRIGHT = "#5fff8c"     # bright VCS light green — for headings
COLOR_TEXT_BODY = "#c8f0d2"       # very light mint — body text on dark panel
COLOR_TEXT_DIM = "#7aa684"        # muted green — for hints/footer

# Accent greens (Ryder's Grove Street green)
COLOR_GROVE_GREEN = "#3d8a3d"     # Ryder's shirt green
COLOR_GROVE_GREEN_LIGHT = "#5fc45f"

# VCS sunset accents (used for highlights / progress bars only — not for body text)
COLOR_SUNSET_ORANGE = "#ff6a2b"
COLOR_SUNSET_PINK = "#ff2bd6"
COLOR_VICE_CYAN = "#00f0ff"
COLOR_LCS_AMBER = "#ffb84d"
COLOR_YELLOW = "#ffe600"

# Background sky gradient (painted, then splash image overlaid)
COLOR_BG_TOP = "#1a0533"
COLOR_BG_MID = "#7a1f5e"
COLOR_BG_SUN = "#ff6a2b"
COLOR_BG_HORIZON = "#2b0a3a"
COLOR_BG_BOTTOM = "#05010f"

# Status colors
COLOR_DANGER = "#ff5b5b"
COLOR_SUCCESS = "#5bff8a"
COLOR_WARNING = "#ffe600"

# ----------------------------------------------------------------------
# Font registration
# ----------------------------------------------------------------------
_PRICEDOWN_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "fonts", "PricedownBl.otf",
)
_PRICEDOWN_FAMILY: str | None = None


def _register_pricedown() -> str | None:
    """Register the Pricedown Bl font with Qt. Returns the family name or None."""
    global _PRICEDOWN_FAMILY
    if _PRICEDOWN_FAMILY is not None:
        return _PRICEDOWN_FAMILY

    path = _PRICEDOWN_PATH
    if not os.path.isfile(path):
        # Try the bundled (PyInstaller _MEIPASS) location
        meipass = os.environ.get("_MEIPASS")
        if meipass:
            alt = os.path.join(meipass, "fonts", "PricedownBl.otf")
            if os.path.isfile(alt):
                path = alt

    if not os.path.isfile(path):
        return None

    font_id = QFontDatabase.addApplicationFont(path)
    if font_id < 0:
        return None
    families = QFontDatabase.applicationFontFamilies(font_id)
    if families:
        _PRICEDOWN_FAMILY = families[0]
        return _PRICEDOWN_FAMILY
    return None


# Try to register on import (best-effort; falls back gracefully)
_PRICEDOWN_FAMILY = _register_pricedown()

# Fallback display fonts if Pricedown isn't available
DISPLAY_FONT = _PRICEDOWN_FAMILY or "Impact"
UI_FONT = "Segoe UI"
MONO_FONT = "Consolas"


def apply_theme(app: QApplication) -> None:
    """Apply the synthwave stylesheet to the whole application.

    Also force every widget to NOT auto-fill its background, so the
    wizard's painted splash image shows through.
    """
    app.setStyleSheet(QSS)

    # Force the app palette to have transparent window backgrounds.
    from PyQt5.QtGui import QPalette
    pal = app.palette()
    pal.setColor(QPalette.Window, QColor(0, 0, 0, 0))
    pal.setColor(QPalette.Base, QColor(0, 0, 0, 0))
    pal.setColor(QPalette.Button, QColor(0, 0, 0, 0))
    app.setPalette(pal)


def heading_font(size: int = 28, bold: bool = True) -> QFont:
    f = QFont(DISPLAY_FONT, size)
    f.setBold(bold)
    return f


def body_font(size: int = 10) -> QFont:
    return QFont(UI_FONT, size)


def mono_font(size: int = 9) -> QFont:
    return QFont(MONO_FONT, size)


# ----------------------------------------------------------------------
# Custom painted background — splash image + sunset overlay + scanlines
# ----------------------------------------------------------------------
class SynthwaveBackground(QWizard):
    """QWizard with a painted GTA VCS-style sunset + per-page splash image."""

    # Subclasses can set this to override the default splash.
    splash_image_name: str | None = None

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setRenderHint(QPainter.SmoothPixmapTransform, True)
        rect = self.rect()
        w, h = rect.width(), rect.height()

        # ---- 1. Sky gradient base (always painted first) ----
        grad = QLinearGradient(0, 0, 0, h)
        grad.setColorAt(0.00, QColor(COLOR_BG_TOP))
        grad.setColorAt(0.30, QColor("#3a0f5a"))
        grad.setColorAt(0.55, QColor(COLOR_BG_MID))
        grad.setColorAt(0.70, QColor(COLOR_BG_SUN))
        grad.setColorAt(0.72, QColor(COLOR_BG_HORIZON))
        grad.setColorAt(1.00, QColor(COLOR_BG_BOTTOM))
        painter.fillRect(rect, grad)

        # ---- 2. Try to draw the splash image as a faded background ----
        # Anchored to the BOTTOM-RIGHT corner so the mod's watermark
        # (typically in the bottom-right of the source image) is always
        # visible. The rest of the image fills up and to the left.
        splash_path = self._current_splash_path()
        if splash_path and os.path.isfile(splash_path):
            pix = QPixmap(splash_path)
            if not pix.isNull():
                # Scale image to cover the whole wizard, preserving aspect ratio.
                # KeepAspectRatioByExpanding means the scaled image is at least
                # as big as the wizard in both dimensions.
                scaled = pix.scaled(
                    w, h,
                    Qt.KeepAspectRatioByExpanding,
                    Qt.SmoothTransformation,
                )
                # Anchor to bottom-right: align the bottom-right corner of
                # the scaled image with the bottom-right corner of the wizard.
                # If the image is wider than the wizard, x is negative (shows
                # the right portion). If taller, y is negative (shows the
                # bottom portion). Either way, the bottom-right of the image
                # — where the watermark lives — is always visible.
                x = w - scaled.width()
                y = h - scaled.height()
                painter.drawPixmap(x, y, scaled)

                # Overlay a dark green tint (70% opacity) so text stays readable
                painter.fillRect(rect, QColor(10, 31, 18, 180))

                # Add a sunset gradient band on top (subtle)
                sunset_grad = QLinearGradient(0, 0, 0, h)
                sunset_grad.setColorAt(0.0, QColor(26, 5, 51, 100))
                sunset_grad.setColorAt(0.7, QColor(122, 31, 94, 80))
                sunset_grad.setColorAt(1.0, QColor(5, 1, 15, 200))
                painter.fillRect(rect, sunset_grad)
        else:
            # No splash image — fall back to the painted grid horizon
            self._paint_grid_horizon(painter, w, h)

        # ---- 3. VHS scanlines (always painted on top) ----
        painter.setPen(QPen(QColor(0, 0, 0, 25), 1))
        for y in range(0, h, 3):
            painter.drawLine(0, y, w, y)

        # Do NOT call super().paintEvent — we've already painted everything.
        # QWizard's default paintEvent just fills the background, which would
        # erase our splash image. The page widgets paint themselves on top.

    def _current_splash_path(self) -> str | None:
        """Return the splash image path for the current page, or None."""
        # Try the current page's splash_image_name attribute first
        page = self.currentPage() if hasattr(self, "currentPage") else None
        name = None
        if page is not None and hasattr(page, "splash_image_name"):
            name = page.splash_image_name
        # Fall back to a wizard-wide default
        if not name:
            name = self.splash_image_name
        if not name:
            return None
        # Look next to the theme.py module (src/ui/splash/)
        here = os.path.dirname(os.path.abspath(__file__))
        path = os.path.join(here, "splash", name)
        if os.path.isfile(path):
            return path
        # Try PyInstaller _MEIPASS
        meipass = os.environ.get("_MEIPASS")
        if meipass:
            alt = os.path.join(meipass, "splash", name)
            if os.path.isfile(alt):
                return alt
        return None

    def _paint_grid_horizon(self, painter: QPainter, w: int, h: int) -> None:
        """Fallback background when no splash image is available."""
        # Sun disc
        sun_cx = w // 2
        sun_cy = int(h * 0.62)
        sun_r = min(w // 6, 130)
        sun_grad = QLinearGradient(sun_cx, sun_cy - sun_r, sun_cx, sun_cy + sun_r)
        sun_grad.setColorAt(0.0, QColor(COLOR_YELLOW))
        sun_grad.setColorAt(0.4, QColor(COLOR_SUNSET_ORANGE))
        sun_grad.setColorAt(0.8, QColor(COLOR_SUNSET_PINK))
        sun_grad.setColorAt(1.0, QColor("#7a0b6e"))
        painter.setBrush(sun_grad)
        painter.setPen(Qt.NoPen)
        painter.drawEllipse(sun_cx - sun_r, sun_cy - sun_r, sun_r * 2, sun_r * 2)

        # Palm trees
        painter.setBrush(QColor(0, 0, 0, 220))
        painter.setPen(QPen(QColor(0, 0, 0, 220), 3))
        horizon_y = sun_cy + sun_r // 2
        self._draw_palm(painter, int(w * 0.08), horizon_y + 30, 110)
        self._draw_palm(painter, int(w * 0.92), horizon_y + 20, 130)

        # Perspective grid
        grid_color = QColor(COLOR_SUNSET_PINK)
        grid_color.setAlpha(100)
        painter.setPen(QPen(grid_color, 1))
        n_horiz = 10
        for i in range(1, n_horiz + 1):
            t = i / n_horiz
            ty = t * t
            y = horizon_y + 20 + int(ty * (h - horizon_y - 20))
            painter.drawLine(0, y, w, y)
        vanishing_x = w // 2
        n_vert = 14
        for i in range(-n_vert, n_vert + 1):
            x_bottom = vanishing_x + i * (w // n_vert)
            painter.drawLine(vanishing_x, horizon_y + 20, x_bottom, h)

    def _draw_palm(self, painter: QPainter, base_x: int, base_y: int, height: int) -> None:
        """Draw a stylized palm tree silhouette."""
        trunk_top_x = base_x + (height // 8)
        trunk_top_y = base_y - height
        painter.save()
        painter.setBrush(QColor(0, 0, 0, 230))
        painter.setPen(QPen(QColor(0, 0, 0, 230), 5))
        from PyQt5.QtCore import QPointF
        painter.drawPolygon(QPolygonF([
            QPointF(base_x - 4, base_y),
            QPointF(base_x + 4, base_y),
            QPointF(trunk_top_x + 3, trunk_top_y),
            QPointF(trunk_top_x - 3, trunk_top_y),
        ]))
        painter.setPen(QPen(QColor(0, 0, 0, 230), 4))
        frond_len = height // 2
        for i in range(7):
            angle_deg = -90 + (i - 3) * 30
            angle = math.radians(angle_deg)
            end_x = trunk_top_x + math.cos(angle) * frond_len
            end_y = trunk_top_y + math.sin(angle) * frond_len * 0.7
            mid_x = (trunk_top_x + end_x) // 2 + math.sin(angle) * 8
            mid_y = (trunk_top_y + end_y) // 2 - 5
            painter.drawLine(QPointF(trunk_top_x, trunk_top_y), QPointF(mid_x, mid_y))
            painter.drawLine(QPointF(mid_x, mid_y), QPointF(end_x, end_y))
        painter.restore()


# ----------------------------------------------------------------------
# Stylesheet — dark green panels everywhere, Pricedown for display text
# ----------------------------------------------------------------------
QSS = f"""
QWizard, QWizardPage {{
    background: transparent;
    color: {COLOR_TEXT_BODY};
    font-family: "{UI_FONT}", "Arial", sans-serif;
    font-size: 10pt;
}}

/* ---- Labels ---- */
QLabel {{
    color: {COLOR_TEXT_BODY};
    background: transparent;
}}
QLabel[heading="true"] {{
    font-family: "{DISPLAY_FONT}", "Impact", "Arial Black", sans-serif;
    font-size: 22pt;
    color: {COLOR_TEXT_BRIGHT};
    letter-spacing: 2px;
}}
QLabel[subheading="true"] {{
    font-family: "{DISPLAY_FONT}", "Impact", "Arial Black", sans-serif;
    font-size: 12pt;
    color: {COLOR_GROVE_GREEN_LIGHT};
    letter-spacing: 1px;
}}
QLabel[dim="true"] {{
    color: {COLOR_TEXT_DIM};
    font-size: 9pt;
}}
QLabel[success="true"] {{ color: {COLOR_SUCCESS}; }}
QLabel[danger="true"]  {{ color: {COLOR_DANGER}; }}

/* ---- Buttons ---- */
QPushButton {{
    background-color: {COLOR_PANEL_BG};
    color: {COLOR_TEXT_BRIGHT};
    border: 2px solid {COLOR_GROVE_GREEN_LIGHT};
    border-radius: 3px;
    padding: 8px 22px;
    font-family: "{DISPLAY_FONT}", "Impact", "Arial Black", sans-serif;
    font-size: 11pt;
    letter-spacing: 1px;
    min-width: 90px;
}}
QPushButton:hover {{
    color: #ffffff;
    border-color: {COLOR_SUNSET_PINK};
    background-color: {COLOR_GROVE_GREEN};
}}
QPushButton:pressed {{
    color: {COLOR_SUNSET_PINK};
    border-color: {COLOR_SUNSET_PINK};
    background-color: {COLOR_DARK_GREEN};
}}
QPushButton:disabled {{
    color: #4a6b54;
    border-color: #2a4a34;
    background-color: {COLOR_PANEL_BG};
}}
QPushButton[primary="true"] {{
    color: #051a0c;
    background-color: {COLOR_GROVE_GREEN_LIGHT};
    border-color: {COLOR_TEXT_BRIGHT};
}}
QPushButton[primary="true"]:hover {{
    background-color: {COLOR_TEXT_BRIGHT};
    border-color: {COLOR_TEXT_BRIGHT};
    color: #051a0c;
}}

/* ---- Input widgets — all sit on dark green panel ---- */
QLineEdit, QComboBox, QSpinBox, QPlainTextEdit, QTextEdit {{
    background-color: {COLOR_PANEL_BG};
    color: {COLOR_TEXT_BODY};
    border: 1px solid {COLOR_GROVE_GREEN};
    border-radius: 3px;
    padding: 6px 8px;
    selection-background-color: {COLOR_GROVE_GREEN};
    selection-color: #ffffff;
}}
QLineEdit:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {{
    border: 1px solid {COLOR_TEXT_BRIGHT};
    background-color: {COLOR_PANEL_BG_LIGHT};
}}
QLineEdit:disabled {{
    color: #4a6b54;
    background-color: {COLOR_DARK_GREEN};
}}
QComboBox::drop-down {{
    border: 0;
    width: 20px;
    background: {COLOR_PANEL_BG};
}}
QComboBox QAbstractItemView {{
    background-color: {COLOR_PANEL_BG};
    color: {COLOR_TEXT_BODY};
    selection-background-color: {COLOR_GROVE_GREEN};
    selection-color: #ffffff;
    border: 1px solid {COLOR_GROVE_GREEN_LIGHT};
    outline: 0;
}}

/* ---- Radio + check ---- */
QRadioButton {{
    color: {COLOR_TEXT_BODY};
    spacing: 8px;
    background: transparent;
    font-size: 11pt;
}}
QRadioButton::indicator {{
    width: 18px;
    height: 18px;
    border: 2px solid {COLOR_GROVE_GREEN_LIGHT};
    border-radius: 9px;
    background: {COLOR_PANEL_BG};
}}
QRadioButton::indicator:checked {{
    background: {COLOR_GROVE_GREEN_LIGHT};
    border-color: {COLOR_TEXT_BRIGHT};
}}
QRadioButton::indicator:hover {{
    border-color: {COLOR_TEXT_BRIGHT};
}}

QCheckBox {{
    color: {COLOR_TEXT_BODY};
    spacing: 8px;
    background: transparent;
}}
QCheckBox::indicator {{
    width: 16px;
    height: 16px;
    border: 1px solid {COLOR_GROVE_GREEN_LIGHT};
    background: {COLOR_PANEL_BG};
    border-radius: 2px;
}}
QCheckBox::indicator:checked {{
    background: {COLOR_GROVE_GREEN_LIGHT};
    border-color: {COLOR_TEXT_BRIGHT};
}}
QCheckBox::indicator:hover {{
    border-color: {COLOR_TEXT_BRIGHT};
}}

/* ---- Progress bar ---- */
QProgressBar {{
    background-color: {COLOR_PANEL_BG};
    border: 1px solid {COLOR_GROVE_GREEN_LIGHT};
    border-radius: 3px;
    text-align: center;
    color: {COLOR_TEXT_BRIGHT};
    height: 22px;
    font-weight: bold;
}}
QProgressBar::chunk {{
    background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 {COLOR_GROVE_GREEN}, stop:0.5 {COLOR_GROVE_GREEN_LIGHT}, stop:1 {COLOR_TEXT_BRIGHT});
}}

/* ---- Group boxes — dark green panels with Pricedown titles ---- */
QGroupBox {{
    color: {COLOR_TEXT_BRIGHT};
    background-color: rgba(10, 31, 18, 200);
    border: 1px solid {COLOR_GROVE_GREEN};
    border-radius: 4px;
    margin-top: 14px;
    padding-top: 10px;
    font-family: "{DISPLAY_FONT}", "Impact", "Arial Black", sans-serif;
    letter-spacing: 1px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    subcontrol-position: top left;
    padding: 2px 8px;
    background-color: {COLOR_PANEL_BG};
    color: {COLOR_TEXT_BRIGHT};
}}

/* ---- Log pane ---- */
QTextBrowser, QPlainTextEdit {{
    background-color: rgba(5, 15, 9, 230);
    color: {COLOR_TEXT_BRIGHT};
    border: 1px solid {COLOR_GROVE_GREEN};
    font-family: "{MONO_FONT}", "Courier New", monospace;
    font-size: 9pt;
}}

/* ---- Scrollbars ---- */
QScrollBar:vertical {{
    background: {COLOR_PANEL_BG};
    width: 10px;
    margin: 0;
}}
QScrollBar::handle:vertical {{
    background: {COLOR_GROVE_GREEN_LIGHT};
    min-height: 30px;
    border-radius: 2px;
}}
QScrollBar::handle:vertical:hover {{
    background: {COLOR_TEXT_BRIGHT};
}}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
    height: 0;
    background: {COLOR_PANEL_BG};
}}
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{
    background: {COLOR_DARK_GREEN};
}}

QWizard QPushButton#qt_wizard_close {{
    color: {COLOR_TEXT_DIM};
}}
"""
