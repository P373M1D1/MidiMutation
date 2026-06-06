#!/usr/bin/env python3
"""Small GUI for sending MIDI clock to a selected output port.

This launcher is intended for Fedora/Linux setups where you want a simple
desktop app with Start, Stop, and Continue controls plus a BPM field.

Dependencies:
  pip install mido python-rtmidi

If the Komplete Audio 6 exposes an output port, this GUI can send the MIDI
clock stream to it directly without involving a DAW.
"""

from __future__ import annotations

import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

import mido

from send_midi_clock import CLOCKS_PER_QUARTER, choose_port


class MidiClockApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Komplete MIDI Clock")
        self.root.geometry("560x360")
        self.root.minsize(560, 360)

        self.stop_event = threading.Event()
        self.worker_thread: threading.Thread | None = None
        self.running_mode = "stopped"
        self.bar_count = 1
        self.clock_pulse_count = 0

        self.port_var = tk.StringVar()
        self.bpm_var = tk.StringVar(value="120")
        self.bar_rollover_var = tk.StringVar(value="4")
        self.bar_display_var = tk.StringVar(value="Bar 1 / 4")
        self.status_var = tk.StringVar(value="Ready")

        self._build_ui()
        self.refresh_ports()

    def _build_ui(self) -> None:
        container = ttk.Frame(self.root, padding=16)
        container.pack(fill="both", expand=True)

        title = ttk.Label(container, text="Komplete MIDI Clock", font=("Sans", 16, "bold"))
        title.pack(anchor="w")

        subtitle = ttk.Label(
            container,
            text="Send MIDI Start, Stop, and Continue plus 24 PPQN clock ticks.",
        )
        subtitle.pack(anchor="w", pady=(2, 12))

        form = ttk.Frame(container)
        form.pack(fill="x")
        form.columnconfigure(1, weight=1)

        ttk.Label(form, text="MIDI output").grid(row=0, column=0, sticky="w", pady=4)
        self.port_combo = ttk.Combobox(form, textvariable=self.port_var, state="readonly")
        self.port_combo.grid(row=0, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="BPM").grid(row=1, column=0, sticky="w", pady=4)
        self.bpm_entry = ttk.Entry(form, textvariable=self.bpm_var)
        self.bpm_entry.grid(row=1, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="Bar rollover").grid(row=2, column=0, sticky="w", pady=4)
        self.bar_rollover_entry = ttk.Entry(form, textvariable=self.bar_rollover_var)
        self.bar_rollover_entry.grid(row=2, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="Bar counter").grid(row=3, column=0, sticky="w", pady=4)
        ttk.Label(form, textvariable=self.bar_display_var).grid(row=3, column=1, sticky="w", pady=4)

        controls = ttk.Frame(container)
        controls.pack(fill="x", pady=(12, 0))

        ttk.Button(controls, text="Start", command=self.start_clock).pack(side="left")
        ttk.Button(controls, text="Continue", command=self.continue_clock).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="Stop", command=self.stop_clock).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="Refresh Ports", command=self.refresh_ports).pack(side="right")

        status_frame = ttk.Frame(container)
        status_frame.pack(fill="x", pady=(16, 0))

        ttk.Separator(status_frame).pack(fill="x", pady=(0, 8))
        ttk.Label(status_frame, textvariable=self.status_var).pack(anchor="w")

    def refresh_ports(self) -> None:
        ports = mido.get_output_names()
        self.port_combo["values"] = ports

        if not ports:
            self.port_var.set("")
            self.set_status("No MIDI output ports found.")
            return

        current = self.port_var.get()
        if current in ports:
            self.port_var.set(current)
        else:
            try:
                self.port_var.set(choose_port(None))
            except RuntimeError:
                self.port_var.set(ports[0])

        self.set_status(f"Found {len(ports)} MIDI output port(s).")

    def set_status(self, text: str) -> None:
        self.status_var.set(text)

    def parse_bpm(self) -> float:
        try:
            bpm = float(self.bpm_var.get().strip())
        except ValueError as exc:
            raise ValueError("BPM must be a number.") from exc

        if bpm <= 0.0:
            raise ValueError("BPM must be greater than zero.")

        return bpm

    def parse_bar_rollover(self) -> int:
        try:
            rollover = int(self.bar_rollover_var.get().strip())
        except ValueError as exc:
            raise ValueError("Bar rollover must be a whole number.") from exc

        if rollover <= 0:
            raise ValueError("Bar rollover must be greater than zero.")

        return rollover

    def set_bar_display(self, bar_count: int, rollover: int) -> None:
        self.bar_display_var.set(f"Bar {bar_count} / {rollover}")

    def reset_bar_counter(self) -> None:
        try:
            rollover = self.parse_bar_rollover()
        except ValueError:
            rollover = 4

        self.bar_count = 1
        self.clock_pulse_count = 0
        self.set_bar_display(self.bar_count, rollover)

    def advance_bar_counter(self) -> None:
        try:
            rollover = self.parse_bar_rollover()
        except ValueError:
            return

        self.clock_pulse_count += 1
        if self.clock_pulse_count >= CLOCKS_PER_QUARTER * 4:
            self.clock_pulse_count = 0
            self.bar_count += 1
            if self.bar_count > rollover:
                self.bar_count = 1

            self.root.after(0, lambda: self.set_bar_display(self.bar_count, rollover))

    def selected_port(self) -> str:
        requested = self.port_var.get().strip()
        if not requested:
            raise ValueError("Choose a MIDI output port first.")

        ports = mido.get_output_names()
        if requested in ports:
            return requested

        return choose_port(requested)

    def start_clock(self) -> None:
        self.reset_bar_counter()
        self._launch_clock("start")

    def continue_clock(self) -> None:
        self._launch_clock("continue")

    def stop_clock(self) -> None:
        if self.worker_thread and self.worker_thread.is_alive():
            self.stop_event.set()
            self.worker_thread.join(timeout=1.5)

        # Always send an explicit MIDI Stop when the user presses Stop so the
        # receiver can hand off immediately instead of waiting for clock timeout.
        self._send_realtime_message("stop")

        self.worker_thread = None
        self.running_mode = "stopped"
        self.set_status(f"Stopped at {self.bar_display_var.get()}.")

    def _launch_clock(self, mode: str) -> None:
        try:
            port_name = self.selected_port()
            bpm = self.parse_bpm()
            rollover = self.parse_bar_rollover()
        except ValueError as exc:
            messagebox.showerror("MIDI Clock", str(exc))
            return

        if self.worker_thread and self.worker_thread.is_alive():
            self.stop_event.set()
            self.worker_thread.join(timeout=1.5)

        self.stop_event.clear()
        self.running_mode = mode
        self.worker_thread = threading.Thread(
            target=self._clock_worker,
            args=(port_name, bpm, mode),
            daemon=True,
        )
        self.worker_thread.start()
        self.set_status(f"Sending {mode} to {port_name} at {bpm:.2f} BPM, {self.bar_display_var.get()}.")
        self.set_bar_display(self.bar_count, rollover)

    def _send_realtime_message(self, message_type: str) -> None:
        try:
            port_name = self.selected_port()
        except ValueError as exc:
            messagebox.showerror("MIDI Clock", str(exc))
            return

        try:
            with mido.open_output(port_name) as port:
                port.send(mido.Message(message_type))
        except Exception as exc:
            messagebox.showerror("MIDI Clock", f"Could not send {message_type}: {exc}")

    def _clock_worker(self, port_name: str, bpm: float, mode: str) -> None:
        clock_message = mido.Message("clock")
        start_message = mido.Message(mode)
        interval_s = 60.0 / (bpm * CLOCKS_PER_QUARTER)

        try:
            with mido.open_output(port_name) as port:
                port.send(start_message)
                next_tick = time.perf_counter()

                while not self.stop_event.is_set():
                    self.advance_bar_counter()

                    try:
                        bpm = float(self.bpm_var.get().strip())
                        if bpm > 0.0:
                            interval_s = 60.0 / (bpm * CLOCKS_PER_QUARTER)
                    except ValueError:
                        pass

                    port.send(clock_message)
                    next_tick += interval_s
                    delay = next_tick - time.perf_counter()
                    if delay > 0.0:
                        time.sleep(delay)
        except Exception as exc:
            self.root.after(0, lambda: messagebox.showerror("MIDI Clock", str(exc)))
            self.root.after(0, lambda: self.set_status("Error while sending MIDI clock."))
            return

        self.root.after(0, lambda: self.set_status("Clock stopped."))


def main() -> None:
    root = tk.Tk()
    MidiClockApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()