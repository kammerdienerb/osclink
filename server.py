#!/usr/bin/env python3

import sys, base64

def send_gui(data: bytes):
    payload = base64.b64encode(data).decode()
    sys.stdout.write(f"\033]9998;{payload}\007")
    sys.stdout.flush()

def main():
    send_gui(b"hello from server")

    buf = ""
    while True:
        ch = sys.stdin.read(1)
        if not ch:
            break
        buf += ch
        if ch == "\x07":  # BEL
            if buf.startswith("\033]9999;"):
                payload = buf[len("\033]9999;"):-1]
                msg = base64.b64decode(payload)
                send_gui(b"echo: " + msg)
            buf = ""

if __name__ == "__main__":
    main()
