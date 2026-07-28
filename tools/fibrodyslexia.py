import sys
import os
import time
import threading
import datetime
import tkinter as tk
from tkinter import scrolledtext
import keyboard
import pyttsx3
from flask import Flask, render_template_string, jsonify

# Set path foundations for local user workspace archives
MEMORY_FILE = os.path.expanduser("~/my_memory_history.txt")
CONFIG_FILE = os.path.expanduser("~/ads_20_config.txt")

# Extract the default Windows OS system icon file for the helper identity
# Icon index 23 inside shell32.dll is the classic blue Question Mark help icon.
SYSTEM_SHELL_ICON = "C:\\Windows\\System32\\shell32.dll"
ICON_RESOURCE_STRING = f"{SYSTEM_SHELL_ICON},23"

try:
    import ctypes
    myappid = 'artystaszef.ads.engine.20'
    ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(myappid)
except Exception:
    pass

SETTINGS = {
    "boot_launch": False,
    "tts_enabled": True,
    "auto_close": False
}

def load_settings():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, "r", encoding="utf-8") as f:
                for line in f:
                    if "=" in line:
                        k, v = line.strip().split("=")
                        SETTINGS[k] = v == "True"
        except Exception:
            pass

def save_settings():
    try:
        with open(CONFIG_FILE, "w", encoding="utf-8") as f:
            for k, v in SETTINGS.items():
                f.write(f"{k}={v}\n")
    except Exception:
        pass

load_settings()

try:
    tts = pyttsx3.init()
    tts.setProperty('rate', 140)
except Exception:
    tts = None

LINK_CONNECTIONS = []

TYPO_MAP = {
    "iwe": "we", "buiulkd": "build", "taht": "that", "laucnhes": "launches", 
    "inmput": "input", "siabled": "disabled", "peopel": "people", 
    "artysta": "artist", "szwef": "chef", "dsiabled": "disabled", 
    "asssitant": "assistant", "sti": "sit", "betyween": "between", 
    "asomeoen": "someone", "elimibntaing": "eliminating", "bottlnercks": "bottlenecks",
    "wtofklow": "workflow", "wtih": "with", "tols": "tools", "ingetdnwed": "intended",
    "ap": "app", "adi": "add/aid", "an": "and", "teh": "the", "conects": "connections"
}

app = Flask(__name__)
HTML_DASHBOARD = """
<!DOCTYPE html>
<html>
<head>
    <title>[ADS 2.0 Connection Hub]</title>
    <style>
        body { background-color: #0d1117; color: #c9d1d9; font-family: sans-serif; padding: 40px; }
        h1 { color: #58a6ff; font-size: 36px; border-bottom: 2px solid #30363d; padding-bottom: 10px; }
        .card { background-color: #161b22; border: 2px solid #30363d; padding: 25px; border-radius: 12px; margin-bottom: 20px; }
        .text-stabilized { font-size: 24px; color: #7ee787; font-weight: bold; line-height: 1.6; }
        .timestamp { font-size: 16px; color: #8b949e; margin-bottom: 10px; font-family: monospace; }
    </style>
</head>
<body>
    <h1>[ADS 2.0 HUB - CURRENT STREAM LOG]</h1>
    <div id="feed"></div>
    <script>
        function loadConnections() {
            fetch('/api/connections').then(res => res.json()).then(data => {
                let html = '';
                data.reverse().forEach(item => {
                    html += `<div class="card"><div class="timestamp">--- ${item.time} ---</div><div class="text-stabilized">>> ${item.text}</div></div>`;
                });
                document.getElementById('feed').innerHTML = html || '<div class="card"><div class="text-stabilized">Pipeline empty. Ready for hotkey text intercepts.</div></div>';
            });
        }
        setInterval(loadConnections, 1500);
        loadConnections();
    </script>
</body>
</html>
"""

@app.route('/')
def home(): return render_template_string(HTML_DASHBOARD)

@app.route('/api/connections')
def get_connections(): return jsonify(LINK_CONNECTIONS)


class ADS20Engine:
    def __init__(self):
        self.root = None

    def speak(self, text):
        if SETTINGS["tts_enabled"] and tts:
            try: tts.say(text); tts.runAndWait()
            except Exception: pass

    def fix_text(self, text):
        return " ".join([TYPO_MAP.get(w.lower().strip(",.?!\"'()[]{}"), w) for w in text.split()])

    def manage_startup_link(self):
        try:
            import winshell
            from win32com.client import Dispatch
            lnk_file = os.path.join(winshell.startup(), "ADS_2.0_Assistant.lnk")
            if SETTINGS["boot_launch"]:
                shell = Dispatch('WScript.Shell')
                shortcut = shell.CreateShortCut(lnk_file)
                shortcut.Targetpath = sys.executable if getattr(sys, 'frozen', False) else os.path.abspath(__file__)
                shortcut.WorkingDirectory = os.path.dirname(shortcut.Targetpath)
                shortcut.save()
            elif os.path.exists(lnk_file):
                os.remove(lnk_file)
        except Exception:
            pass

    def apply_native_icon(self, window):
        """Forces the Tkinter window wrapper to bind onto the OS default help icon asset."""
        try:
            # Tkinter uses the -iconbitmap flag to pull direct compiled dll icons via resource path indexing
            window.iconbitmap(default=ICON_RESOURCE_STRING)
        except Exception:
            try:
                # Fallback to general executable assignment if dll index is blocked by system policies
                window.iconbitmap(sys.executable)
            except Exception:
                pass

    def open_control_panel(self):
        panel = tk.Tk()
        panel.title("ADS 2.0 - Configuration Control Panel")
        panel.geometry("600x450")
        panel.configure(bg="#0c0f12")
        panel.attributes('-topmost', True)
        
        # Apply the default OS Helper icon to the control panel window frame
        self.apply_native_icon(panel)

        tk.Label(panel, text="USER PREFERENCES HUB", font=("Comic Sans MS", 22, "bold"), bg="#0c0f12", fg="#58a6ff").pack(pady=20)

        def toggle_boot():
            SETTINGS["boot_launch"] = not SETTINGS["boot_launch"]
            btn_boot.config(text=f"RUN AT PC BOOT: {'[ ON ]' if SETTINGS['boot_launch'] else '[ OFF ]'}", bg="#7ee787" if SETTINGS['boot_launch'] else "#ff7b72")
            save_settings(); self.manage_startup_link()

        def toggle_tts():
            SETTINGS["tts_enabled"] = not SETTINGS["tts_enabled"]
            btn_tts.config(text=f"TEXT-TO-SPEECH VOICE: {'[ ON ]' if SETTINGS['tts_enabled'] else '[ OFF ]'}", bg="#7ee787" if SETTINGS['tts_enabled'] else "#ff7b72")
            save_settings()

        def toggle_close():
            SETTINGS["auto_close"] = not SETTINGS["auto_close"]
            btn_close.config(text=f"AUTO-CLOSE PANEL (3s): {'[ ON ]' if SETTINGS['auto_close'] else '[ OFF ]'}", bg="#7ee787" if SETTINGS['auto_close'] else "#ff7b72")
            save_settings()

        FNT = ("Comic Sans MS", 16, "bold")
        btn_boot = tk.Button(panel, text=f"RUN AT PC BOOT: {'[ ON ]' if SETTINGS['boot_launch'] else '[ OFF ]'}", font=FNT, bg="#7ee787" if SETTINGS['boot_launch'] else "#ff7b72", fg="#000000", width=35, command=toggle_boot)
        btn_boot.pack(pady=12)

        btn_tts = tk.Button(panel, text=f"TEXT-TO-SPEECH VOICE: {'[ ON ]' if SETTINGS['tts_enabled'] else '[ OFF ]'}", font=FNT, bg="#7ee787" if SETTINGS['tts_enabled'] else "#ff7b72", fg="#000000", width=35, command=toggle_tts)
        btn_tts.pack(pady=12)

        btn_close = tk.Button(panel, text=f"AUTO-CLOSE PANEL (3s): {'[ ON ]' if SETTINGS['auto_close'] else '[ OFF ]'}", font=FNT, bg="#7ee787" if SETTINGS['auto_close'] else "#ff7b72", fg="#000000", width=35, command=toggle_close)
        btn_close.pack(pady=12)

        tk.Button(panel, text="MINIMIZE ENGINE TO BACKGROUND", font=FNT, bg="#58a6ff", fg="#000000", width=35, command=panel.destroy).pack(pady=25)
        panel.mainloop()

    def trigger_pipeline(self):
        if self.root is not None: return
        keyboard.send('ctrl+c'); time.sleep(0.15)
        try:
            t = tk.Tk(); raw = t.clipboard_get().strip(); t.destroy()
        except Exception:
            raw = ""

        clean = self.fix_text(raw) if raw else ""
        now_str = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        if clean:
            with open(MEMORY_FILE, "a", encoding="utf-8") as f: f.write(f"[{now_str}] [ADS 2.0] {clean}\n")
            LINK_CONNECTIONS.append({"time": now_str, "text": clean})

        self.root = tk.Tk(); self.root.title("ADS 2.0 Canvas Overlay"); self.root.state('zoomed'); self.root.configure(bg="#080b0e"); self.root.attributes('-topmost', True)
        
        # Apply the default OS Helper icon to the fullscreen popup window frame
        self.apply_native_icon(self.root)

        self.display = scrolledtext.ScrolledText(self.root, bg="#010409", fg="#7ee787", font=("Courier New", 24, "bold"), wrap=tk.WORD, spacing1=15, spacing3=15)
        self.display.pack(fill=tk.BOTH, expand=True, padx=60, pady=25)

        if clean:
            self.root.clipboard_clear(); self.root.clipboard_append(clean)
            self.display.insert(tk.END, f"[+] ADS 2.0 STABILIZED TEXT:\n\n>> {clean}\n\n[Copied to clipboard. Archive logged.]")
            self.speak(clean)
        else:
            self.display.insert(tk.END, "[!] Pipeline empty. Highlight text anywhere and hit Ctrl+Space.")

        def close():
            if self.root: self.root.destroy(); self.root = None

        self.root.bind('<Escape>', lambda e: close())
        if SETTINGS["auto_close"]: self.root.after(3000, close)
        self.root.mainloop()

if __name__ == "__main__":
    engine = ADS20Engine()
    threading.Thread(target=lambda: app.run(port=5000, debug=False, use_reloader=False), daemon=True).start()
    keyboard.add_hotkey('ctrl+space', engine.trigger_pipeline)
    
    print("[+] ADS 2.0 Background Loop Engaged.")
    engine.open_control_panel()
    keyboard.wait()
