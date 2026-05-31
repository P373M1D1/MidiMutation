#!/usr/bin/env python3
"""Send MIDI clock, start, and stop messages to an output port.

This is a small host-side utility for Linux systems where DAW MIDI routing is
unreliable. It targets an ALSA/PipeWire MIDI output port by exact name or by a
substring match such as "Komplete".

Dependencies:
  pip install mido python-rtmidi

Example:
  python3 tools/send_midi_clock.py --list
  python3 tools/send_midi_clock.py --port Komplete --bpm 120
"""

from __future__ import annotations

import argparse
import sys
import time
from typing import Optional

import mido


CLOCKS_PER_QUARTER = 24


def list_ports() -> int:
    ports = mido.get_output_names()
    if not ports:
        print("No MIDI output ports found.")
        return 1

    print("Available MIDI output ports:")
    for port in ports:
        print(f"  {port}")
    return 0


def choose_port(requested: Optional[str]) -> str:
    ports = mido.get_output_names()
    if not ports:
        raise RuntimeError("No MIDI output ports were detected.")

    if requested:
        for port in ports:
            if requested.lower() in port.lower():
                return port
        raise RuntimeError(
            f'Could not find an output port matching "{requested}". '
            f"Run with --list to see the available ports."
        )

    for port in ports:
        if "komplete" in port.lower():
            return port

    return ports[0]


def sleep_until(deadline: float) -> None:
    delay = deadline - time.perf_counter()
    if delay > 0.0:
        time.sleep(delay)


def run_clock(port_name: str, bpm: float, send_start: bool) -> int:
    interval_s = 60.0 / (bpm * CLOCKS_PER_QUARTER)
    clock_message = mido.Message("clock")
    start_message = mido.Message("start")
    stop_message = mido.Message("stop")

    print(f'Sending MIDI clock to "{port_name}" at {bpm:.2f} BPM. Press Ctrl-C to stop.')

    next_tick = time.perf_counter()
    with mido.open_output(port_name) as port:
        if send_start:
            port.send(start_message)

        try:
            while True:
                port.send(clock_message)
                next_tick += interval_s
                sleep_until(next_tick)
        except KeyboardInterrupt:
            port.send(stop_message)
            print("Stopped.")

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Send MIDI clock to an output port.")
    parser.add_argument("--list", action="store_true", help="List MIDI output ports and exit.")
    parser.add_argument(
        "--port",
        default=None,
        help='Exact output port name or substring match, for example "Komplete".',
    )
    parser.add_argument("--bpm", type=float, default=120.0, help="Tempo in beats per minute.")
    parser.add_argument(
        "--no-start",
        action="store_true",
        help="Send clock without an initial MIDI Start message.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.list:
        return list_ports()

    if args.bpm <= 0.0:
        print("BPM must be greater than zero.", file=sys.stderr)
        return 2

    try:
        port_name = choose_port(args.port)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    return run_clock(port_name, args.bpm, send_start=not args.no_start)


if __name__ == "__main__":
    raise SystemExit(main())