#!/usr/bin/env python3
"""Live IMU viewer bridge for SkiSim.

Reads the board's serial output (the skisim_hw_check firmware prints one
"acc g [...] gyro dps [...]" line per sample) and pushes each sample to a
browser page over Server-Sent Events.

Usage:
    python3 tools/imu_viewer/serve.py                       # auto-detect the port
    python3 tools/imu_viewer/serve.py --serial /dev/cu.usbmodem1301
Then open http://localhost:8765 in a browser.

Standard library only; no pyserial needed (macOS / Linux).
"""

import argparse
import glob
import json
import os
import queue
import re
import termios
import threading
import time
import tty
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))

NUM = r"([-+]?\d+(?:\.\d+)?)"
SAMPLE_RE = re.compile(
    r"acc g \[\s*" + NUM + r"\s+" + NUM + r"\s+" + NUM + r"\s*\]\s+"
    r"gyro dps \[\s*" + NUM + r"\s+" + NUM + r"\s+" + NUM + r"\s*\]"
)

subscribers = []                 # one queue.Queue per open browser tab
subscribers_lock = threading.Lock()
state = {"port": None, "connected": False}


def broadcast(event):
    data = json.dumps(event)
    with subscribers_lock:
        for q in list(subscribers):
            try:
                q.put_nowait(data)
            except queue.Full:
                pass             # slow tab: drop samples rather than lag


def open_serial(path):
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY)
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = attrs[5] = termios.B115200      # ispeed, ospeed
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


def candidate_ports():
    return sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))


def serial_loop(fixed_path):
    """Read lines forever, reconnecting whenever the board reboots."""
    while True:
        path = fixed_path or next(iter(candidate_ports()), None)
        if not path or not os.path.exists(path):
            time.sleep(0.5)
            continue
        try:
            fd = open_serial(path)
        except OSError:
            time.sleep(0.5)
            continue

        state.update(port=path, connected=True)
        broadcast({"type": "status", "connected": True, "port": path})
        buf = b""
        try:
            while True:
                chunk = os.read(fd, 4096)
                if not chunk:
                    raise OSError("serial port closed")
                buf += chunk
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("ascii", "replace").strip()
                    if not line:
                        continue
                    m = SAMPLE_RE.search(line)
                    if m:
                        ax, ay, az, gx, gy, gz = (float(v) for v in m.groups())
                        broadcast({"type": "sample", "t": time.time(),
                                   "ax": ax, "ay": ay, "az": az,
                                   "gx": gx, "gy": gy, "gz": gz})
                    else:
                        broadcast({"type": "log", "line": line})
        except OSError:
            pass
        finally:
            try:
                os.close(fd)
            except OSError:
                pass
        state.update(connected=False)
        broadcast({"type": "status", "connected": False, "port": path})
        time.sleep(0.5)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass                     # keep the terminal quiet

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/stream":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            q = queue.Queue(maxsize=500)
            with subscribers_lock:
                subscribers.append(q)
            q.put(json.dumps({"type": "status", "connected": state["connected"],
                              "port": state["port"]}))
            try:
                while True:
                    try:
                        data = q.get(timeout=5)
                        self.wfile.write(b"data: " + data.encode() + b"\n\n")
                    except queue.Empty:
                        self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                with subscribers_lock:
                    subscribers.remove(q)
        else:
            self.send_error(404)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--serial", help="serial device (default: first /dev/cu.usbmodem*)")
    ap.add_argument("--http-port", type=int, default=8765)
    args = ap.parse_args()

    threading.Thread(target=serial_loop, args=(args.serial,), daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), Handler)
    print(f"IMU viewer on http://localhost:{args.http_port}  "
          f"(serial: {args.serial or 'auto'})", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
