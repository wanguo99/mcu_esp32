#!/usr/bin/env python3
"""Append timestamped serial output to a file; reconnect after MCU unplug/reset."""
import argparse
from datetime import datetime
import fcntl
import os
from pathlib import Path
import re
import signal
import sys
import threading

try:
    import serial
except ImportError:
    raise SystemExit("pyserial is required; run this tool with bash tools/serial_log.sh")

ROOT = Path(__file__).resolve().parents[1]
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="/dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output", type=Path, default=ROOT / "logs/serial/serial.log")
    args = parser.parse_args()
    if args.baud <= 0:
        parser.error("--baud must be positive")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    pid_path = output.with_suffix(".pid")
    stop = threading.Event()
    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, lambda *_: stop.set())

    with pid_path.open("a+") as pid, output.open("a", encoding="utf-8", buffering=1) as log:
        try:
            fcntl.flock(pid, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise SystemExit(f"A logger is already writing {output}")
        pid.seek(0)
        pid.truncate()
        pid.write(str(os.getpid()) + "\n")
        pid.flush()

        def write(data):
            text = ANSI.sub("", data.decode("utf-8", errors="replace")).replace("\r", "")
            if text:
                line = f"[{datetime.now().astimezone().isoformat(timespec='milliseconds')}] {text}\n"
                log.write(line)
                if sys.stdout.isatty():
                    print(line, end="", flush=True)

        write(f"HOST logger started: {args.port}, {args.baud} baud".encode())
        last_error = None
        try:
            while not stop.is_set():
                port = None
                pending = b""
                try:
                    port = serial.Serial(port=None, baudrate=args.baud, timeout=0.5, exclusive=True)
                    # Read only: do not deliberately toggle the MCU reset/boot lines.
                    port.dtr = False
                    port.rts = False
                    port.port = args.port
                    port.open()
                    write(f"HOST connected: {args.port}".encode())
                    last_error = None
                    while not stop.is_set():
                        data = port.read(4096)
                        pending += data
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            write(line)
                        # Retain console prompts and incomplete lines on idle or large output.
                        if pending and (not data or len(pending) >= 16384):
                            write(pending)
                            pending = b""
                except (serial.SerialException, OSError) as exc:
                    message = str(exc)
                    if message != last_error:
                        write(f"HOST serial unavailable, retrying: {message}".encode())
                        last_error = message
                finally:
                    if pending:
                        write(pending)
                    if port:
                        port.close()
                stop.wait(1)
        finally:
            write(b"HOST logger stopped")
            pid_path.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
