#!/usr/bin/env python3
"""
Pont HTTP -> Modbus TCP pour piloter le LOGO! 8.4 Siemens.

Route :
  GET /notify?memento=<n>&action=<ON|OFF>

  Exemples :
    GET /notify?memento=1&action=ON   -> M1 = 1
    GET /notify?memento=1&action=OFF  -> M1 = 0
    GET /notify?memento=2&action=ON   -> M2 = 1
    GET /notify?memento=2&action=OFF  -> M2 = 0

Usage :
  python3 modbus_bridge.py

Mobotix IP Notify :
  Data protocol  : HTTP/1.0 Request
  Destination    : 192.168.129.3:8080
  CGI Path       : /notify?memento=1&action=ON
"""

import socket
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

# --- Configuration ---------------------------------------------------
LOGO_IP     = "192.168.129.199"   # IP du LOGO! 8.4
LOGO_PORT   = 510                  # Port Modbus TCP du LOGO! (502~510)
LISTEN_PORT = 8080                 # Port d'écoute HTTP du bridge
# ----------------------------------------------------------------------

# Adresse Modbus base pour M1 (0x2040). M(n) = 0x2040 + (n-1)
M_BASE_ADDR = 0x2040


def build_frame(memento: int, on: bool) -> bytes:
    addr = M_BASE_ADDR + (memento - 1)
    val  = 0xFF00 if on else 0x0000
    return bytes([
        0x00, 0x01,             # Transaction ID
        0x00, 0x00,             # Protocol ID
        0x00, 0x06,             # Length
        0x01,                   # Unit ID
        0x05,                   # FC05 Write Single Coil
        (addr >> 8) & 0xFF,     # Addr hi
        addr & 0xFF,            # Addr lo
        (val  >> 8) & 0xFF,     # Val hi
        val  & 0xFF,            # Val lo
    ])


def send_modbus(frame: bytes) -> bool:
    hex_str = ' '.join(f'{b:02X}' for b in frame)
    print(f"  -> Modbus frame : {hex_str}")
    try:
        with socket.create_connection((LOGO_IP, LOGO_PORT), timeout=3) as s:
            s.settimeout(3)
            s.sendall(frame)
            resp = s.recv(16)
            resp_hex = ' '.join(f'{b:02X}' for b in resp)
            print(f"  <- LOGO response: {resp_hex}")
        return True
    except OSError as e:
        print(f"  Modbus error: {e}")
        return False


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"  [{self.client_address[0]}] {fmt % args}")

    def do_GET(self):
        parsed = urlparse(self.path)
        params = parse_qs(parsed.query)

        try:
            memento = int(params["memento"][0])
            action  = params["action"][0].upper()
            if action not in ("ON", "OFF"):
                raise ValueError(f"action invalide: {action}")
            if memento < 1 or memento > 64:
                raise ValueError(f"memento invalide: {memento}")
        except (KeyError, ValueError, IndexError) as e:
            self.send_response(400)
            self.send_header("Content-Type", "text/plain")
            self.end_headers()
            self.wfile.write(f"Erreur parametres: {e}\n".encode())
            return

        frame = build_frame(memento, action == "ON")
        ok    = send_modbus(frame)
        code  = 200 if ok else 503
        msg   = f"M{memento} {action} {'OK' if ok else 'ERREUR MODBUS'}\n"
        print(f"  M{memento} {action} -> {'OK' if ok else 'ECHEC'}")

        self.send_response(code)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(msg.encode())


if __name__ == "__main__":
    srv = HTTPServer(("0.0.0.0", LISTEN_PORT), Handler)
    print(f"Modbus bridge listening on 0.0.0.0:{LISTEN_PORT}")
    print(f"LOGO! target : {LOGO_IP}:{LOGO_PORT}")
    print("Route : GET /notify?memento=<1-64>&action=<ON|OFF>")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
