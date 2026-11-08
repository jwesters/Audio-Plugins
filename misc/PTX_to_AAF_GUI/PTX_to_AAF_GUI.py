#!/usr/bin/env python3
"""Desktop GUI bundling Jacob Todd's original Pro Tools to AAF converter.

Bundled original: ptx_to_aaf.py (non-commercial license; see LICENSE).
Source: https://github.com/jacobtodd/protools-to-logic
The interface sets the source and output paths without editing converter code.
"""

from __future__ import annotations

import json
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, scrolledtext, ttk

APP_TITLE = "Pro Tools → AAF Converter"
SETTINGS_FILE = Path.home() / ".ptx_to_aaf_gui_settings.json"
AUDIO_EXTENSIONS = {".wav", ".aif", ".aiff"}

# Load the bundled original converter in a separate Python process and override
# its path constants before running main(). This allows explicit PTX selection,
# a separate audio location, and an independent AAF destination.
RUNNER = r'''
import importlib.util
import sys
from pathlib import Path

script_path, session_dir, ptx_file, audio_dir, output_dir = map(Path, sys.argv[1:6])
sys.argv = [str(script_path)]
spec = importlib.util.spec_from_file_location("original_ptx_to_aaf", str(script_path))
if not spec or not spec.loader:
    raise RuntimeError("Cannot load the selected converter script")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

module.SESSION_DIR = session_dir
module.PTX_FILE = ptx_file
module.AUDIO_DIR = audio_dir
module.OUT_AAF = output_dir / (ptx_file.stem + ".aaf")
module.main()
'''


def load_preferences() -> dict:
    try:
        data = json.loads(SETTINGS_FILE.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


def save_preferences(values: dict) -> None:
    try:
        SETTINGS_FILE.write_text(json.dumps(values, indent=2), encoding="utf-8")
    except OSError:
        pass  # Saving preferences should not interrupt a conversion.


class ConverterGUI(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title(APP_TITLE)
        self.geometry("820x630")
        self.minsize(680, 560)

        self.prefs = load_preferences()
        self.session_var = tk.StringVar(value=self.prefs.get("session_dir", ""))
        self.ptx_var = tk.StringVar(value=self.prefs.get("ptx_file", ""))
        self.audio_var = tk.StringVar(value=self.prefs.get("audio_dir", ""))
        self.output_dir_var = tk.StringVar(
            value=self.prefs.get("output_dir") or self.prefs.get("session_dir", "")
        )
        self.status_var = tk.StringVar(value="Choose your paths, then convert to AAF.")
        self.output_var = tk.StringVar(value="")
        self._messages: queue.Queue[tuple[str, str]] = queue.Queue()
        self._running = False
        self._process: subprocess.Popen | None = None
        self._build_interface()
        self._update_output_label()
        self.after(100, self._poll_messages)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_interface(self) -> None:
        style = ttk.Style(self)
        if "aqua" not in style.theme_names():
            try:
                style.theme_use("clam")
            except tk.TclError:
                pass
        style.configure("TLabel", font=("Helvetica", 11))
        style.configure("Title.TLabel", font=("Helvetica", 19, "bold"))
        style.configure("Help.TLabel", font=("Helvetica", 10))
        style.configure("Convert.TButton", font=("Helvetica", 12, "bold"), padding=(15, 9))
        style.configure("Browse.TButton", padding=(12, 5))

        body = ttk.Frame(self, padding=(23, 20, 23, 14))
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=1)
        body.rowconfigure(8, weight=1)

        ttk.Label(body, text="Pro Tools → AAF", style="Title.TLabel").grid(row=0, column=0, sticky="w")
        ttk.Label(
            body,
            text="Select a session, PTX file, audio folder, and destination for your AAF.",
            style="Help.TLabel",
        ).grid(row=1, column=0, sticky="w", pady=(5, 20))

        self._make_picker(
            body, 2, "1. Session folder  (SESSION_DIR)", self.session_var, self._browse_session
        )
        self._make_picker(
            body, 3, "2. Pro Tools file  (PTX_FILE)", self.ptx_var, self._browse_ptx
        )
        self._make_picker(
            body, 4, "3. Audio folder  (AUDIO_DIR)", self.audio_var, self._browse_audio
        )
        self._make_picker(
            body, 5, "4. Save AAF to  (OUTPUT_DIR)", self.output_dir_var, self._browse_output
        )
        self.ptx_var.trace_add("write", lambda *_: self._update_output_label())
        self.session_var.trace_add("write", lambda *_: self._update_output_label())
        self.output_dir_var.trace_add("write", lambda *_: self._update_output_label())

        ttk.Label(body, textvariable=self.output_var, style="Help.TLabel", wraplength=725).grid(
            row=6, column=0, sticky="w", pady=(3, 10)
        )
        actions = ttk.Frame(body)
        actions.grid(row=7, column=0, sticky="ew", pady=(0, 12))
        actions.columnconfigure(0, weight=1)
        ttk.Label(actions, textvariable=self.status_var, style="Help.TLabel", wraplength=470).grid(
            row=0, column=0, sticky="w", padx=(0, 12)
        )
        self.convert_button = ttk.Button(
            actions, text="Convert to AAF", style="Convert.TButton", command=self._start_conversion
        )
        self.convert_button.grid(row=0, column=1, sticky="e")

        log_frame = ttk.LabelFrame(body, text="Conversion log", padding=(8, 5))
        log_frame.grid(row=8, column=0, sticky="nsew")
        self.log = scrolledtext.ScrolledText(
            log_frame, height=8, wrap="word", font=("Menlo" if sys.platform == "darwin" else "Courier", 10),
            state="disabled", relief="flat", borderwidth=0,
        )
        self.log.pack(fill="both", expand=True)

    def _make_picker(self, parent, row, title, var, callback) -> None:
        wrapper = ttk.Frame(parent)
        wrapper.grid(row=row, column=0, sticky="ew", pady=(0, 12))
        wrapper.columnconfigure(0, weight=1)
        ttk.Label(wrapper, text=title).grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 5))
        entry = ttk.Entry(wrapper, textvariable=var)
        entry.grid(row=1, column=0, sticky="ew", padx=(0, 10))
        ttk.Button(wrapper, text="Browse…", style="Browse.TButton", command=callback).grid(
            row=1, column=1, sticky="e"
        )

    def _browse_session(self) -> None:
        initial = self.session_var.get()
        folder = filedialog.askdirectory(
            title="Choose the Pro Tools session folder",
            initialdir=str(Path(initial).expanduser()) if initial and Path(initial).is_dir() else str(Path.home()),
            mustexist=True,
        )
        if not folder:
            return
        previous_session = self.session_var.get().strip()
        previous_audio = self.audio_var.get().strip()
        previous_ptx = self.ptx_var.get().strip()
        previous_output = self.output_dir_var.get().strip()
        self.session_var.set(folder)
        if not previous_output or previous_output == previous_session:
            self.output_dir_var.set(folder)
        selected = Path(folder)
        candidates = sorted(
            (p for p in selected.iterdir() if p.is_file() and p.suffix.lower() == ".ptx"),
            key=lambda p: p.name.lower(),
        )
        previous_was_auto = (
            previous_session and previous_ptx
            and Path(previous_ptx).parent == Path(previous_session)
        )
        if not previous_ptx or previous_was_auto:
            if len(candidates) == 1:
                self.ptx_var.set(str(candidates[0]))
            else:
                self.ptx_var.set("")
                if len(candidates) > 1:
                    self.status_var.set("Multiple PTX files found — please choose one.")
        guessed = selected / "Audio Files"
        old_default = Path(previous_session) / "Audio Files" if previous_session else None
        if guessed.is_dir() and (not previous_audio or (old_default and Path(previous_audio) == old_default)):
            self.audio_var.set(str(guessed))
        self._save_current_preferences()

    def _browse_ptx(self) -> None:
        start = self.session_var.get().strip()
        if not start or not Path(start).is_dir():
            start = str(Path.home())
        filename = filedialog.askopenfilename(
            title="Choose a Pro Tools .ptx session",
            initialdir=start,
            filetypes=[("Pro Tools sessions", "*.ptx"), ("All files", "*.*")],
        )
        if not filename:
            return
        session = Path(filename).parent
        previous_session = self.session_var.get().strip()
        previous_audio = self.audio_var.get().strip()
        previous_output = self.output_dir_var.get().strip()
        self.ptx_var.set(filename)
        if not previous_session or Path(previous_session) != session:
            self.session_var.set(str(session))
            if not previous_output or previous_output == previous_session:
                self.output_dir_var.set(str(session))
        guessed = session / "Audio Files"
        previous_default = (Path(previous_session) / "Audio Files") if previous_session else None
        if guessed.is_dir() and (not previous_audio or (previous_default and Path(previous_audio) == previous_default)):
            self.audio_var.set(str(guessed))
        self._save_current_preferences()

    def _browse_audio(self) -> None:
        start = self.audio_var.get().strip() or self.session_var.get().strip()
        if not start or not Path(start).is_dir():
            start = str(Path.home())
        folder = filedialog.askdirectory(title="Choose the Audio Files folder", initialdir=start, mustexist=True)
        if folder:
            self.audio_var.set(folder)
            self._save_current_preferences()

    def _browse_output(self) -> None:
        start = self.output_dir_var.get().strip() or self.session_var.get().strip()
        if not start or not Path(start).expanduser().is_dir():
            start = str(Path.home())
        folder = filedialog.askdirectory(
            title="Choose where to save the AAF file",
            initialdir=start,
            mustexist=True,
        )
        if folder:
            self.output_dir_var.set(folder)
            self._save_current_preferences()

    def _update_output_label(self) -> None:
        output_dir = self.output_dir_var.get().strip()
        ptx = self.ptx_var.get().strip()
        if output_dir and ptx:
            destination = Path(output_dir).expanduser() / (Path(ptx).stem + ".aaf")
            self.output_var.set(f"AAF file: {destination}")
        else:
            self.output_var.set("AAF file: Select a PTX file and output folder")

    def _save_current_preferences(self) -> None:
        save_preferences({
            "session_dir": self.session_var.get().strip(),
            "ptx_file": self.ptx_var.get().strip(),
            "audio_dir": self.audio_var.get().strip(),
            "output_dir": self.output_dir_var.get().strip(),
        })

    def _locate_converter(self) -> Path | None:
        """Find the original conversion engine supplied inside this package."""
        script = Path(__file__).resolve().with_name("ptx_to_aaf.py")
        if script.is_file():
            return script
        messagebox.showerror(
            "Converter missing",
            "The bundled ptx_to_aaf.py file is missing. Re-extract the entire ZIP "
            "and keep the package files together.",
        )
        return None

    def _start_conversion(self) -> None:
        if self._running:
            return
        session = Path(self.session_var.get().strip()).expanduser()
        ptx = Path(self.ptx_var.get().strip()).expanduser()
        audio = Path(self.audio_var.get().strip()).expanduser()
        output_dir = Path(self.output_dir_var.get().strip()).expanduser()
        if not self.session_var.get().strip() or not session.is_dir():
            messagebox.showerror("Invalid session folder", "Choose an existing session folder.")
            return
        if not ptx.is_file() or ptx.suffix.lower() != ".ptx":
            messagebox.showerror("Invalid PTX file", "Choose an existing .ptx session file.")
            return
        if not self.audio_var.get().strip() or not audio.is_dir():
            messagebox.showerror("Invalid audio folder", "Choose an existing audio folder.")
            return
        if not self.output_dir_var.get().strip() or not output_dir.is_dir():
            messagebox.showerror("Invalid output folder", "Choose an existing folder to save the AAF.")
            return
        converter = self._locate_converter()
        if converter is None:
            self.status_var.set("The bundled converter could not be found.")
            return
        try:
            import aaf2  # noqa: F401  (installed with pyaaf2)
        except ImportError:
            messagebox.showerror(
                "AAF dependency missing",
                "The conversion engine is included, but the pyaaf2 library needs to be "
                "installed once. Open Terminal and run:\n\npython3 -m pip install pyaaf2",
            )
            return
        out_path = output_dir / (ptx.stem + ".aaf")
        if out_path.exists() and not messagebox.askyesno(
            "Replace existing AAF?",
            f"An AAF already exists at:\n{out_path}\n\nReplace it?",
            icon="warning",
        ):
            return
        if not any(p.suffix.lower() in AUDIO_EXTENSIONS for p in audio.iterdir() if p.is_file()):
            if not messagebox.askyesno(
                "No audio files found",
                "The selected audio folder contains no WAV, AIF, or AIFF files. "
                "The converter may replace missing clips with silence. Continue?",
                icon="warning",
            ):
                return

        self._save_current_preferences()
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self._running = True
        self.convert_button.configure(state="disabled", text="Converting…")
        self.status_var.set("Converting — see log below.")
        thread = threading.Thread(
            target=self._run_conversion,
            args=(converter, session.resolve(), ptx.resolve(), audio.resolve(), output_dir.resolve(), out_path.resolve()),
            daemon=True,
        )
        thread.start()

    def _run_conversion(
        self, converter: Path, session: Path, ptx: Path,
        audio: Path, output_dir: Path, out_path: Path,
    ) -> None:
        command = [sys.executable, "-u", "-c", RUNNER,
                   str(converter), str(session), str(ptx), str(audio), str(output_dir)]
        try:
            self._messages.put(("line", f"Session: {session}\nPTX file: {ptx}\nAudio:   {audio}\nOutput:  {out_path}\n\n"))
            kwargs = {"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT, "text": True, "bufsize": 1}
            self._process = subprocess.Popen(command, **kwargs)
            assert self._process.stdout is not None
            for line in self._process.stdout:
                self._messages.put(("line", line))
            status = self._process.wait()
            if status == 0 and out_path.is_file():
                self._messages.put(("done", str(out_path)))
            elif status == 0:
                self._messages.put(("error", "The converter exited without creating the expected AAF file."))
            else:
                self._messages.put(("error", f"Conversion stopped (exit code {status}). See the log for details."))
        except Exception as exc:
            self._messages.put(("error", f"Could not run the converter: {exc}"))
        finally:
            self._process = None

    def _poll_messages(self) -> None:
        try:
            while True:
                kind, value = self._messages.get_nowait()
                if kind == "line":
                    self._append_log(value)
                elif kind == "done":
                    self._finish("Conversion complete.")
                    messagebox.showinfo("AAF created", f"Successfully created:\n{value}")
                elif kind == "error":
                    self._append_log("\nERROR: " + value + "\n")
                    self._finish("Conversion failed. See log for details.")
                    messagebox.showerror("Conversion failed", value)
        except queue.Empty:
            pass
        self.after(100, self._poll_messages)

    def _append_log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", text)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _finish(self, status: str) -> None:
        self._running = False
        self.convert_button.configure(state="normal", text="Convert to AAF")
        self.status_var.set(status)

    def _on_close(self) -> None:
        if self._running and not messagebox.askyesno(
            "Conversion in progress", "Closing may interrupt the conversion. Close anyway?"
        ):
            return
        process = self._process
        if process and process.poll() is None:
            process.terminate()
        self._save_current_preferences()
        self.destroy()


if __name__ == "__main__":
    ConverterGUI().mainloop()
