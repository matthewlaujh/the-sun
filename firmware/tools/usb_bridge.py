"""usb_bridge.py — use the ESP's web page over the USB cable instead of Wi-Fi SUN-DISC.

    browser ── http://localhost:8080 ──▶ this bridge ── USB (CH340, 115200) ──▶ ESP32-GATEWAY

The laptop keeps its normal Wi-Fi (internet); SUN-DISC keeps working too — the firmware serves both at once.
Works with every firmware built with SunUsbLink (gateway_rig_test, gateway_disc_test): the bridge asks the ESP which
firmware it runs and serves that project's src/index_html.h, re-read on every page load (edit the page, refresh, no flash).

Run with PlatformIO's Python (it bundles pyserial), from anywhere:
    ~/.platformio/penv/Scripts/python.exe firmware/tools/usb_bridge.py            (Windows)
    ~/.platformio/penv/bin/python firmware/tools/usb_bridge.py                     (macOS / Linux)
Options: --port COM3 (default: auto-detect the CH340) · --http 8080

Flashing over USB needs the same port: stop the bridge (Ctrl+C), `pio run -t upload`, start it again. It also
reconnects by itself if the cable is unplugged or the port is briefly taken.

Protocol (one line each way; see firmware/shared/SunUsbLink/src/SunUsbLink.h):
    host → ESP   ?  ·  T <text>  ·  B <base64>  ·  G <path>  ·  P <path> <base64>
    ESP → host   @hello <fw>  ·  @ws <text>  ·  @bin <base64>  ·  @get <path> <code> <base64>  ·  @post <path> <code>
    anything else from the ESP is a log line and is printed here.
"""
import argparse
import base64
import mimetypes
import os
import queue
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial missing — run this with PlatformIO's Python (~/.platformio/penv), which includes it.")

FIRMWARE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CH340_VID = 0x1A86
HELLO_EVERY_S = 3          # keepalive; the firmware streams status while it hears from us (10 s window)
REPLY_TIMEOUT_S = 4

# Replaces the page's WebSocket: sends go out as ordered POSTs, ESP messages arrive as Server-Sent Events.
SHIM = """<script>
window.SUN_LINK='usb';
window.WebSocket=class{
 constructor(){this.readyState=0;this.binaryType='blob';this.q=Promise.resolve();this.es=new EventSource('/events');
  this.es.onopen=()=>{this.readyState=1;this.onopen&&this.onopen();};
  this.es.onmessage=e=>this.onmessage&&this.onmessage({data:e.data});
  this.es.addEventListener('bin',e=>{if(!this.onmessage)return;const s=atob(e.data),b=new Uint8Array(s.length);
   for(let i=0;i<s.length;i++)b[i]=s.charCodeAt(i);this.onmessage({data:this.binaryType==='arraybuffer'?b.buffer:new Blob([b])});});
  this.es.onerror=()=>{if(this.readyState===3)return;this.readyState=3;this.es.close();this.onclose&&this.onclose();};}
 send(d){const bin=typeof d!=='string';
  this.q=this.q.then(()=>fetch(bin?'/_bin':'/_text',{method:'POST',body:d})).catch(()=>{});}
 close(){this.readyState=3;this.es.close();}};
</script>
"""


def log(msg):
    print(msg, flush=True)


def find_port():
    for p in serial.tools.list_ports.comports():
        if p.vid == CH340_VID:
            return p.device
    return None


class EspLink:
    """Owns the serial port: reads lines, routes replies, fans ESP messages out to the open pages."""

    def __init__(self, port, http_port):
        self.port = port
        self.http_port = http_port
        self.ser = None
        self.firmware = None
        self.wlock = threading.Lock()
        self.subscribers = set()
        self.slock = threading.Lock()
        self.pending = {}          # ("get"|"post", path) → [Event, result]
        self.plock = threading.Lock()
        self.hello = threading.Event()

    # ---- serial side ----
    def _open(self):
        port = self.port or find_port()
        if not port:
            raise serial.SerialException("no ESP32-GATEWAY (CH340) found on USB")
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.5
        s.dtr = False              # leave DTR/RTS low so opening the port doesn't reset the ESP
        s.rts = False
        s.open()
        return s

    def run(self):
        threading.Thread(target=self._keepalive, daemon=True).start()
        while True:
            try:
                self.ser = self._open()
                self.firmware = None
                self.hello.clear()
                log(f"[bridge] connected to {self.ser.port}")
                self.send("?")
                buf = b""
                while True:
                    buf += self.ser.read(self.ser.in_waiting or 1)
                    while b"\n" in buf:
                        raw, buf = buf.split(b"\n", 1)
                        self._on_line(raw.decode("utf-8", "replace").rstrip("\r"))
            except (serial.SerialException, OSError) as e:
                self._close()
                log(f"[bridge] {e} — retrying in 2 s")
                time.sleep(2)

    def _close(self):
        with self.wlock:
            if self.ser:
                try:
                    self.ser.close()
                except Exception:
                    pass
            self.ser = None
        self.hello.clear()

    def _keepalive(self):
        while True:
            time.sleep(HELLO_EVERY_S)
            self.send("?")

    def send(self, line):
        with self.wlock:
            if not self.ser:
                return False
            try:
                self.ser.write((line + "\n").encode("utf-8"))
                return True
            except (serial.SerialException, OSError):
                return False

    def _on_line(self, s):
        if not s:
            return
        tag, _, rest = s.partition(" ")
        if tag == "@ws":
            self._publish(f"data: {rest}\n\n")
        elif tag == "@bin":
            self._publish(f"event: bin\ndata: {rest}\n\n")
        elif tag == "@hello":
            if rest != self.firmware:
                self.firmware = rest
                log(f"[bridge] ESP runs {rest} → open http://localhost:{self.http_port}")
            self.hello.set()
        elif tag in ("@get", "@post"):
            parts = rest.split(" ", 2)
            path, code = parts[0], int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 500
            body = base64.b64decode(parts[2]) if len(parts) > 2 and parts[2] else b""
            with self.plock:
                w = self.pending.get((tag[1:], path))
            if w:
                w[1] = (code, body)
                w[0].set()
        else:
            log(f"[esp] {s}")

    # ---- page side ----
    def _publish(self, event):
        with self.slock:
            for q in list(self.subscribers):
                try:
                    q.put_nowait(event)
                except queue.Full:  # a stalled page drops messages instead of holding up the serial reader
                    pass

    def subscribe(self):
        q = queue.Queue(maxsize=256)
        with self.slock:
            self.subscribers.add(q)
        return q

    def unsubscribe(self, q):
        with self.slock:
            self.subscribers.discard(q)

    def request(self, kind, path, line):
        key = (kind, path)
        w = [threading.Event(), None]
        with self.plock:
            self.pending[key] = w
        try:
            if not self.send(line):
                return 503, b"ESP not connected"
            if not w[0].wait(REPLY_TIMEOUT_S):
                return 504, b"ESP did not answer"
            return w[1]
        finally:
            with self.plock:
                if self.pending.get(key) is w:
                    del self.pending[key]

    def page(self):
        if not self.hello.wait(REPLY_TIMEOUT_S) or not self.firmware:
            return None, "The ESP isn't answering on USB — is it plugged in and running a SunUsbLink firmware?"
        if not re.fullmatch(r"[A-Za-z0-9_\-]+", self.firmware):
            return None, f"unexpected firmware name {self.firmware!r}"
        src = os.path.join(FIRMWARE_DIR, self.firmware, "src", "index_html.h")
        try:
            text = open(src, encoding="utf-8").read()
        except OSError:
            return None, f"page source not found: {src}"
        m = re.search(r'R"SUNHTML\((.*)\)SUNHTML"', text, re.S)
        if not m:
            return None, f"no R\"SUNHTML(...)SUNHTML\" page in {src}"
        html = m.group(1)
        i = html.find("<script>")
        return (html[:i] + SHIM + html[i:]) if i >= 0 else (SHIM + html), None


def make_handler(esp):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *args):
            pass

        def reply(self, code, body, ctype="text/plain; charset=utf-8", headers=None):
            data = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(data)

        def body(self):
            n = int(self.headers.get("Content-Length") or 0)
            return self.rfile.read(n) if n else b""

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/":
                html, err = esp.page()
                return self.reply(200, html, "text/html; charset=utf-8") if html else self.reply(503, err)
            if path == "/events":
                return self.events()
            code, data = esp.request("get", path, f"G {path}")
            ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"
            return self.reply(code, data, ctype if code == 200 else "text/plain; charset=utf-8")

        def do_POST(self):
            path = self.path.split("?")[0]
            data = self.body()
            if path == "/_text":
                text = data.decode("utf-8", "replace").replace("\r", " ").replace("\n", " ")
                return self.reply(200 if esp.send("T " + text) else 503, "")
            if path == "/_bin":
                return self.reply(200 if esp.send("B " + base64.b64encode(data).decode()) else 503, "")
            code, _ = esp.request("post", path, f"P {path} {base64.b64encode(data).decode()}")
            return self.reply(code, "ok" if code == 200 else "failed")

        def events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            q = esp.subscribe()
            esp.send("?")          # (re)start the status stream right away
            try:
                self.wfile.write(b": connected\n\n")
                self.wfile.flush()
                while True:
                    try:
                        event = q.get(timeout=15)
                    except queue.Empty:
                        event = ": keepalive\n\n"
                    self.wfile.write(event.encode("utf-8"))
                    self.wfile.flush()
            except OSError:
                pass
            finally:
                esp.unsubscribe(q)
                self.close_connection = True

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Use the ESP's web page over USB instead of Wi-Fi SUN-DISC.")
    ap.add_argument("--port", help="serial port, e.g. COM3 (default: auto-detect the CH340)")
    ap.add_argument("--http", type=int, default=8080, help="local web port (default 8080)")
    args = ap.parse_args()

    esp = EspLink(args.port, args.http)
    threading.Thread(target=esp.run, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", args.http), make_handler(esp))
    server.daemon_threads = True
    log(f"[bridge] page at http://localhost:{args.http}   (Ctrl+C to stop and free the USB port for flashing)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("[bridge] stopped")


if __name__ == "__main__":
    main()
