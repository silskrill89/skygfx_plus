#!/usr/bin/env python3
"""GTA San Andreas Stories 1987 — Installer Wizard.

Entry point. Run with:

    python installer.py

Build a standalone .exe with:

    build_portable.bat       (Windows)
    pyinstaller installer.spec

If anything crashes, the full traceback is written to:
    crash_<timestamp>.log   (next to this script)
    cache/logs/install_*.log  (the normal install log, if logging started)
"""
from __future__ import annotations

import datetime
import logging
import os
import sys
import traceback

# Make sure the `src` package is importable when running from source.
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)


def _crash_log_path() -> str:
    """Return the path to a fresh crash log file (next to this script)."""
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    return os.path.join(HERE, f"crash_{ts}.log")


def _write_crash_log(exc_type, exc_value, exc_tb) -> str:
    """Write the full traceback to a crash log file. Returns the path."""
    log_path = _crash_log_path()
    try:
        os.makedirs(os.path.dirname(log_path) or ".", exist_ok=True)
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("GTA SAS 1987 Installer — Crash Log\n")
            f.write("=" * 60 + "\n")
            f.write(f"Timestamp: {datetime.datetime.now().isoformat()}\n")
            f.write(f"Python:    {sys.version}\n")
            f.write(f"Platform:  {sys.platform}\n")
            try:
                import platform
                f.write(f"OS:        {platform.platform()}\n")
            except Exception:
                pass
            f.write(f"Executable: {sys.executable}\n")
            f.write(f"CWD:       {os.getcwd()}\n")
            f.write(f"Script:    {os.path.abspath(__file__)}\n")
            f.write("=" * 60 + "\n\n")
            traceback.print_exception(exc_type, exc_value, exc_tb, file=f)
            f.write("\n" + "=" * 60 + "\n")
            f.write("End of crash log. Share this file when reporting the bug.\n")
    except Exception:
        # If we can't even write the crash log, fall back to stderr
        traceback.print_exception(exc_type, exc_value, exc_tb, file=sys.stderr)
        return ""
    return log_path


def _show_crash_dialog(log_path: str, exc_value: Exception) -> None:
    """Try to show a GUI dialog telling the user where the crash log is.

    Falls back to a console message if no GUI toolkit is available.
    """
    msg = (
        f"The GTA SAS 1987 Installer crashed.\n\n"
        f"Error: {exc_value}\n\n"
        f"A full crash log has been written to:\n"
        f"{log_path}\n\n"
        f"Please share this file when reporting the bug."
    )
    # Try Qt first (if PyQt5 is installed)
    try:
        from PyQt5.QtWidgets import QApplication, QMessageBox
        app = QApplication.instance() or QApplication(sys.argv)
        QMessageBox.critical(None, "GTA SAS 1987 Installer — Crash", msg)
        return
    except Exception:
        pass
    # Try Tkinter as a fallback
    try:
        import tkinter as tk
        from tkinter import messagebox
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror("GTA SAS 1987 Installer — Crash", msg)
        root.destroy()
        return
    except Exception:
        pass
    # Last resort: print to stderr
    print("\n" + "=" * 60, file=sys.stderr)
    print("CRASH: " + msg, file=sys.stderr)
    print("=" * 60 + "\n", file=sys.stderr)


def _setup_logging() -> str:
    """Set up logging to both stdout and a log file. Returns the log file path."""
    from src import cache, config

    cache.ensure_dirs()
    log_file = cache.log_path()
    fmt = "%(asctime)s %(levelname)-7s %(name)s — %(message)s"
    handlers = [
        logging.StreamHandler(sys.stdout),
        logging.FileHandler(log_file, encoding="utf-8"),
    ]
    logging.basicConfig(level=logging.INFO, format=fmt, handlers=handlers, force=True)
    logging.info("=== %s v%s ===", config.APP_NAME, config.APP_VERSION)
    logging.info("Python: %s", sys.version)
    logging.info("Executable: %s", sys.executable)
    logging.info("CWD: %s", os.getcwd())
    logging.info("Script: %s", os.path.abspath(__file__))
    logging.info("Cache root: %s", config.CACHE_ROOT)
    return log_file


def main() -> int:
    """Main entry point. Wraps everything in a crash handler."""
    # Set up logging early so we capture import errors
    try:
        log_file = _setup_logging()
    except Exception as e:
        # If logging setup fails, we still want to run — just print to stderr
        print(f"[WARNING] Logging setup failed: {e}", file=sys.stderr)
        log_file = None

    # Lazy-import PyQt5 so `--help` and log setup work even without it.
    try:
        from PyQt5.QtWidgets import QApplication
    except ImportError as e:
        msg = (
            "PyQt5 is not installed.\n\n"
            "Install dependencies with:\n"
            "    pip install -r requirements.txt\n\n"
            f"Error: {e}"
        )
        print("ERROR: " + msg, file=sys.stderr)
        try:
            import tkinter as tk
            from tkinter import messagebox
            root = tk.Tk()
            root.withdraw()
            messagebox.showerror("GTA SAS 1987 Installer — Missing Dependency", msg)
            root.destroy()
        except Exception:
            pass
        return 2

    from src.ui.theme import apply_theme
    from src.ui.wizard import InstallerWizard

    app = QApplication(sys.argv)
    app.setApplicationName("GTA SAS 1987 Installer")
    from src import config
    app.setApplicationVersion(config.APP_VERSION)
    apply_theme(app)

    wizard = InstallerWizard()
    wizard.show()
    return app.exec_()


def main_with_crash_handler() -> int:
    """Wrap main() in a sys.excepthook that writes a crash log + shows a dialog."""
    def _excepthook(exc_type, exc_value, exc_tb):
        # Don't catch KeyboardInterrupt
        if issubclass(exc_type, KeyboardInterrupt):
            sys.__excepthook__(exc_type, exc_value, exc_tb)
            return
        log_path = _write_crash_log(exc_type, exc_value, exc_tb)
        _show_crash_dialog(log_path, exc_value)
        # Also call the default hook so it prints to stderr too
        sys.__excepthook__(exc_type, exc_value, exc_tb)

    sys.excepthook = _excepthook
    try:
        return main()
    except SystemExit:
        raise
    except BaseException as e:
        # Catch anything that escapes main() before Qt's event loop starts
        log_path = _write_crash_log(type(e), e, e.__traceback__)
        _show_crash_dialog(log_path, e)
        traceback.print_exception(type(e), e, e.__traceback__)
        return 1


if __name__ == "__main__":
    sys.exit(main_with_crash_handler())
