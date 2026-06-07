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
from collections import deque

import mido

from send_midi_clock import CLOCKS_PER_QUARTER, choose_port


class MidiClockApp:
    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("Komplete MIDI Clock")
        self.root.geometry("680x420")
        self.root.minsize(680, 420)

        self.send_stop_event = threading.Event()
        self.monitor_stop_event = threading.Event()
        self.send_thread: threading.Thread | None = None
        self.monitor_thread: threading.Thread | None = None
        self.running_mode = "stopped"
        self.bar_count = 1
        self.clock_pulse_count = 0
        self.last_measured_bpm = 0.0

        self.output_port_var = tk.StringVar()
        self.input_port_var = tk.StringVar()
        self.bpm_var = tk.StringVar(value="120")
        self.bar_rollover_var = tk.StringVar(value="4")
        self.bar_display_var = tk.StringVar(value="Bar 1 / 4")
        self.incoming_bpm_var = tk.StringVar(value="--.--")
        self.status_var = tk.StringVar(value="Ready")

        self._build_ui()
        self.refresh_ports()
        self.root.protocol("WM_DELETE_WINDOW", self._handle_window_close)

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
        self.output_port_combo = ttk.Combobox(form, textvariable=self.output_port_var, state="readonly")
        self.output_port_combo.grid(row=0, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="MIDI input").grid(row=1, column=0, sticky="w", pady=4)
        self.input_port_combo = ttk.Combobox(form, textvariable=self.input_port_var, state="readonly")
        self.input_port_combo.grid(row=1, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="Send BPM").grid(row=2, column=0, sticky="w", pady=4)
        self.bpm_entry = ttk.Entry(form, textvariable=self.bpm_var)
        self.bpm_entry.grid(row=2, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="Incoming BPM").grid(row=3, column=0, sticky="w", pady=4)
        ttk.Label(form, textvariable=self.incoming_bpm_var).grid(row=3, column=1, sticky="w", pady=4)

        ttk.Label(form, text="Bar rollover").grid(row=4, column=0, sticky="w", pady=4)
        self.bar_rollover_entry = ttk.Entry(form, textvariable=self.bar_rollover_var)
        self.bar_rollover_entry.grid(row=4, column=1, sticky="ew", pady=4)

        ttk.Label(form, text="Bar counter").grid(row=5, column=0, sticky="w", pady=4)
        ttk.Label(form, textvariable=self.bar_display_var).grid(row=5, column=1, sticky="w", pady=4)

        controls = ttk.Frame(container)
        controls.pack(fill="x", pady=(12, 0))

        ttk.Button(controls, text="Start", command=self.start_clock).pack(side="left")
        ttk.Button(controls, text="Continue", command=self.continue_clock).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="Stop", command=self.stop_clock).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="Monitor Input", command=self.start_input_monitor).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="Stop Monitor", command=self.stop_input_monitor).pack(side="left", padx=(8, 0))
        ttk.Button(controls, text="Refresh Ports", command=self.refresh_ports).pack(side="right")

        status_frame = ttk.Frame(container)
        status_frame.pack(fill="x", pady=(16, 0))

        ttk.Separator(status_frame).pack(fill="x", pady=(0, 8))
        ttk.Label(status_frame, textvariable=self.status_var).pack(anchor="w")

    def refresh_ports(self) -> None:
        output_ports = mido.get_output_names()
        input_ports = mido.get_input_names()

        self.output_port_combo["values"] = output_ports
        self.input_port_combo["values"] = input_ports

        if output_ports:
            current_output = self.output_port_var.get()
            if current_output in output_ports:
                self.output_port_var.set(current_output)
            else:
                try:
                    self.output_port_var.set(choose_port(None))
                except RuntimeError:
                    self.output_port_var.set(output_ports[0])
        else:
            self.output_port_var.set("")

        if input_ports:
            current_input = self.input_port_var.get()
            if current_input in input_ports:
                self.input_port_var.set(current_input)
            else:
                self.input_port_var.set(input_ports[0])
        else:
            self.input_port_var.set("")

        self.set_status(
            f"Found {len(output_ports)} MIDI output port(s), {len(input_ports)} MIDI input port(s)."
        )

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
        requested = self.output_port_var.get().strip()
        if not requested:
            raise ValueError("Choose a MIDI output port first.")

        ports = mido.get_output_names()
        if requested in ports:
            return requested

        return choose_port(requested)

    def selected_input_port(self) -> str:
        requested = self.input_port_var.get().strip()
        if not requested:
            raise ValueError("Choose a MIDI input port first.")

        ports = mido.get_input_names()
        if requested in ports:
            return requested

        for port in ports:
            if requested.lower() in port.lower():
                return port

        raise ValueError(f'Could not find an input port matching "{requested}".')

    def start_clock(self) -> None:
        self.reset_bar_counter()
        self._launch_clock("start")

    def continue_clock(self) -> None:
        self._launch_clock("continue")

    def stop_clock(self) -> None:
        if self.send_thread and self.send_thread.is_alive():
            self.send_stop_event.set()
            self.send_thread.join(timeout=1.5)

        # Always send an explicit MIDI Stop when the user presses Stop so the
        # receiver can hand off immediately instead of waiting for clock timeout.
        self._send_realtime_message("stop")

        self.send_thread = None
        self.running_mode = "stopped"
        self.set_status(f"Stopped at {self.bar_display_var.get()}.")

    def start_input_monitor(self) -> None:
        try:
            input_port_name = self.selected_input_port()
        except ValueError as exc:
            messagebox.showerror("MIDI Clock", str(exc))
            return

        if self.monitor_thread and self.monitor_thread.is_alive():
            self.monitor_stop_event.set()
            self.monitor_thread.join(timeout=1.5)

        self.monitor_stop_event.clear()
        self.monitor_thread = threading.Thread(
            target=self._input_monitor_worker,
            args=(input_port_name,),
            daemon=True,
        )
        self.monitor_thread.start()
        self.set_status(f"Monitoring incoming MIDI clock on {input_port_name}.")

    def stop_input_monitor(self) -> None:
        if self.monitor_thread and self.monitor_thread.is_alive():
            self.monitor_stop_event.set()
            self.monitor_thread.join(timeout=1.5)

        self.monitor_thread = None
        self.last_measured_bpm = 0.0
        self.incoming_bpm_var.set("--.--")
        self.set_status("Incoming MIDI clock monitor stopped.")

    def _launch_clock(self, mode: str) -> None:
        try:
            port_name = self.selected_port()
            bpm = self.parse_bpm()
            rollover = self.parse_bar_rollover()
        except ValueError as exc:
            messagebox.showerror("MIDI Clock", str(exc))
            return

        if self.send_thread and self.send_thread.is_alive():
            self.send_stop_event.set()
            self.send_thread.join(timeout=1.5)

        self.send_stop_event.clear()
        self.running_mode = mode
        self.send_thread = threading.Thread(
            target=self._clock_worker,
            args=(port_name, bpm, mode),
            daemon=True,
        )
        self.send_thread.start()
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

                while not self.send_stop_event.is_set():
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

    def _input_monitor_worker(self, input_port_name: str) -> None:
        # Track recent clock intervals and report a smoothed BPM estimate with
        # decimal precision to keep the readout stable but responsive.
        recent_intervals = deque(maxlen=96)
        last_clock_time: float | None = None
        last_ui_update = 0.0

        try:
            with mido.open_input(input_port_name) as port:
                while not self.monitor_stop_event.is_set():
                    now = time.perf_counter()

                    for message in port.iter_pending():
                        if message.type != "clock":
                            continue

                        if last_clock_time is not None:
                            interval = now - last_clock_time
                            if 0.0 < interval < 1.0:
                                recent_intervals.append(interval)

                        last_clock_time = now

                    if recent_intervals and now - last_ui_update >= 0.10:
                        avg_interval = sum(recent_intervals) / len(recent_intervals)
                        measured_bpm = 60.0 / (avg_interval * CLOCKS_PER_QUARTER)
                        self.last_measured_bpm = measured_bpm
                        self.root.after(0, lambda bpm=measured_bpm: self.incoming_bpm_var.set(f"{bpm:.2f}"))
                        last_ui_update = now

                    if last_clock_time is not None and now - last_clock_time > 1.0:
                        self.last_measured_bpm = 0.0
                        self.root.after(0, lambda: self.incoming_bpm_var.set("--.--"))
                        recent_intervals.clear()
                        last_clock_time = None

                    time.sleep(0.002)
        except Exception as exc:
            self.root.after(0, lambda: messagebox.showerror("MIDI Clock", f"Input monitor error: {exc}"))
            self.root.after(0, lambda: self.set_status("Error while monitoring incoming MIDI clock."))

    def _handle_window_close(self) -> None:
        self.stop_clock()
        self.stop_input_monitor()
        self.root.destroy()


def main() -> None:
    root = tk.Tk()
    MidiClockApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()