#!/usr/bin/env python3
"""Live plot of the strip discharge curve sent by the ESP32 over USB serial.

Usage:
    pip install pyserial pyqtgraph PyQt6
    python tools/plot_discharge.py                    # auto-detect port
    python tools/plot_discharge.py /dev/ttyACM0       # explicit port
    python tools/plot_discharge.py --csv log.csv      # also save every sample

Close the serial monitor in VS Code first - only one program can open the port.
"""
import argparse
import csv
import os
import signal
import sys
import threading
import time

import pyqtgraph as pg
import serial
import serial.tools.list_ports
from pyqtgraph.Qt import QtCore

BAUD = 921600          # must match SERIAL_BAUD in include/config.h
THRESHOLD_MV = 1200    # level used to measure "discharge time"
HISTORY = 200          # how many discharge times to keep in the lower plot
SMOOTH = 8             # default moving-average window (bursts) for the filtered line


def find_port():
    for p in serial.tools.list_ports.comports():
        if any(k in (p.description or "") for k in ("CP210", "CH34", "USB", "JTAG")):
            return p.device
    return None


def moving_average(xs, n):
    """Trailing moving average: point i = mean of the last n values up to i.
    Noise drops ~sqrt(n) times; a step (touch) shows up after ~n bursts."""
    out, acc = [], 0.0
    for i, x in enumerate(xs):
        acc += x
        if i >= n:
            acc -= xs[i - n]
        out.append(acc / min(i + 1, n))
    return out


def discharge_time(ts, mvs, threshold):
    """Time (us) when voltage first drops below threshold, linearly interpolated."""
    for i in range(1, len(mvs)):
        if mvs[i - 1] >= threshold > mvs[i]:
            t0, t1, v0, v1 = ts[i - 1], ts[i], mvs[i - 1], mvs[i]
            return t0 + (v0 - threshold) * (t1 - t0) / (v0 - v1)
    return None


class Reader(threading.Thread):
    def __init__(self, port, csv_path):
        super().__init__(daemon=True)
        self.stop_event = threading.Event()
        self.port = port         # None -> auto-detect on every (re)connect
        self.status = "connecting..."
        self.lock = threading.Lock()
        self.latest = None       # (ts, mvs, charged_mV)
        self.times = []          # discharge time history (or touchRead values)
        self.mode = "rc"         # "rc" = discharge curves, "touch" = touchRead()
        self.bursts = 0
        self.arrivals = []       # PC time of recent bursts, for the update rate
        self.csv = None
        if csv_path:
            self.csv_file = open(csv_path, "w", newline="")
            self.csv = csv.writer(self.csv_file)
            self.csv.writerow(["burst", "pc_time_s", "t_us", "mV"])

    def run(self):
        # Survive unplugging / board resets / re-uploads: reconnect forever.
        while not self.stop_event.is_set():
            port = self.port or find_port()
            if port is None:
                self.status = "no serial port found - plug the board in"
                time.sleep(1)
                continue
            try:
                with serial.Serial(port, BAUD, timeout=1) as ser:
                    self.status = f"connected: {port}"
                    print(self.status)
                    self.read_loop(ser)
            except (serial.SerialException, OSError) as e:
                self.status = f"disconnected ({port}), retrying..."
                print(f"{self.status} [{e}]")
                time.sleep(1)

    def read_loop(self, ser):
        ts, mvs, in_burst, charged, expected = [], [], False, None, 0
        while not self.stop_event.is_set():
            line = ser.readline().decode(errors="ignore").strip()
            if not line:
                continue
            if line.startswith("#"):
                print(line)  # status / DC leak test messages from the board
                continue
            if line.startswith("T"):
                # "T <value>" from TOUCH_READ_TEST mode
                try:
                    value = int(line.split()[1])
                except (ValueError, IndexError):
                    continue
                with self.lock:
                    self.mode = "touch"
                    self.bursts += 1
                    self.arrivals = (self.arrivals + [time.monotonic()])[-20:]
                    self.times = (self.times + [value])[-HISTORY:]
                if self.csv:
                    self.csv.writerow([self.bursts, time.time(), "", value])
                continue
            if line.startswith("B"):
                # Header "B <count> [<mV while charging>]". Lines can arrive
                # damaged (e.g. right after connecting) - drop anything malformed.
                try:
                    parts = [int(p) for p in line.split()[1:]]
                except ValueError:
                    in_burst = False
                    continue
                if not 1 <= len(parts) <= 2:
                    in_burst = False
                    continue
                expected = parts[0]
                charged = parts[1] if len(parts) == 2 else None
                ts, mvs, in_burst = [], [], True
            elif line == "E" and in_burst:
                in_burst = False
                if len(ts) != expected:
                    continue  # some sample lines were lost - skip this burst
                td = discharge_time(ts, mvs, THRESHOLD_MV)
                with self.lock:
                    if self.mode != "rc":
                        self.times = []  # switched back from touch mode
                    self.mode = "rc"
                    self.latest = (ts, mvs, charged)
                    self.bursts += 1
                    self.arrivals = (self.arrivals + [time.monotonic()])[-20:]
                    if td is not None:
                        self.times = (self.times + [td])[-HISTORY:]
                if self.csv:
                    now = time.time()
                    self.csv.writerows([self.bursts, now, t, v] for t, v in zip(ts, mvs))
            elif in_burst:
                try:
                    t, v = line.split(",")
                    ts.append(int(t))
                    mvs.append(int(v))
                except ValueError:
                    in_burst = False  # corrupted burst, drop it


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?")
    ap.add_argument("--csv", help="save every sample to this CSV file")
    ap.add_argument("--smooth", type=int, default=SMOOTH,
                    help=f"moving-average window for the filtered line (default {SMOOTH})")
    args = ap.parse_args()

    reader = Reader(args.port, args.csv)
    reader.start()

    app = pg.mkQApp("Strip discharge")
    pg.setConfigOptions(antialias=True, background="w", foreground="k")
    win = pg.GraphicsLayoutWidget(title="Strip discharge")
    win.resize(1000, 750)

    p_v = win.addPlot(row=0, col=0, title="Discharge curve (latest burst)")
    p_v.setLabel("bottom", "time since release, ms")
    p_v.setLabel("left", "voltage, mV")
    p_v.setYRange(0, 3400)
    p_v.showGrid(x=True, y=True, alpha=0.3)
    p_v.addLine(y=THRESHOLD_MV, pen=pg.mkPen("gray", style=QtCore.Qt.PenStyle.DashLine))
    curve = p_v.plot(pen=pg.mkPen("#1f77b4", width=2),
                     symbol="o", symbolSize=5, symbolBrush="#1f77b4")  # show every sample

    p_t = win.addPlot(row=1, col=0, title=f"Time to fall below {THRESHOLD_MV} mV")
    p_t.setLabel("bottom", "burst #")
    p_t.setLabel("left", "discharge time, µs")
    p_t.getAxis("left").enableAutoSIPrefix(False)
    p_v.getAxis("left").enableAutoSIPrefix(False)
    p_v.getAxis("bottom").enableAutoSIPrefix(False)
    p_t.showGrid(x=True, y=True, alpha=0.3)
    p_t.addLegend(offset=(10, 10))
    hist = p_t.plot(pen=pg.mkPen((214, 39, 40, 140), width=1), name="raw")
    hist_f = p_t.plot(pen=pg.mkPen("#1f1f1f", width=3),
                      name=f"moving average, {args.smooth} bursts")
    win.show()

    last_burst = [-1]
    shown_mode = [None]

    def set_mode(mode):
        if mode == "touch":
            p_t.setTitle("touchRead(): ESP32-S3 hardware touch sensor (finger → goes up)")
            p_t.setLabel("left", "touchRead value, counts")
            p_t.setLabel("bottom", "measurement #")
            p_v.setTitle("TOUCH_READ_TEST mode: no discharge curve")
            curve.setData([], [])
        else:
            p_t.setTitle(f"Time to fall below {THRESHOLD_MV} mV")
            p_t.setLabel("left", "discharge time, µs")
            p_t.setLabel("bottom", "burst #")
        shown_mode[0] = mode

    def update():
        with reader.lock:
            latest, times = reader.latest, list(reader.times)
            bursts, arrivals, mode = reader.bursts, list(reader.arrivals), reader.mode
        rate = (len(arrivals) - 1) / (arrivals[-1] - arrivals[0]) if len(arrivals) > 1 else 0
        win.setWindowTitle(f"{reader.status} | {rate:.1f} measurements/s")
        if bursts == last_burst[0]:
            return  # nothing new - don't redraw
        last_burst[0] = bursts
        if mode != shown_mode[0]:
            set_mode(mode)
        hist.setData(times)
        hist_f.setData(moving_average(times, max(args.smooth, 1)))
        if mode == "touch" or latest is None:
            return
        ts, mvs, charged = latest
        dt = ts[-1] / max(len(ts) - 1, 1)
        curve.setData([t / 1000 for t in ts], mvs)
        p_v.setXRange(0, ts[-1] / 1000, padding=0.02)  # always fit the whole burst
        p_v.setTitle(f"Discharge curve (latest burst) | while charging: {charged} mV"
                     f" | {dt:.0f} µs/sample")

    timer = QtCore.QTimer()
    timer.timeout.connect(update)
    timer.start(20)

    # Ctrl+C in the terminal: Qt's C++ loop blocks Python's default handler,
    # so install our own that closes the window. The 20 ms timer above keeps
    # returning control to Python, which lets the handler run promptly.
    signal.signal(signal.SIGINT, lambda *_: app.quit())
    app.exec()

    # Orderly shutdown: stop the GUI timer, let the serial thread finish its
    # current readline (timeout=1 s) and close the port, then save the CSV.
    timer.stop()
    reader.stop_event.set()
    reader.join(timeout=2)

    if reader.csv:
        reader.csv_file.close()  # flush everything recorded so far
        print(f"saved {args.csv}")
    print(f"stopped after {reader.bursts} bursts")
    # Skip Python's interpreter teardown: destroying PyQt6/pyqtgraph objects
    # in random order at exit is what segfaulted. Everything is saved by now.
    sys.stdout.flush()
    os._exit(0)


if __name__ == "__main__":
    main()
