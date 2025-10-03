#!/usr/bin/env python3
import os
import pty
import subprocess
import base64
import threading
import tkinter as tk
import select

def make_osc(code: int, data: bytes) -> bytes:
    payload = base64.b64encode(data).decode()
    return f"\033]{code};{payload}\007".encode()

class GuiApp:
    def __init__(self):
        # --- PTY + shell setup ---
        self.master_fd, self.slave_fd = pty.openpty()
        shell = os.environ.get("SHELL", "/bin/bash")
        self.proc = subprocess.Popen(
            [shell],
            preexec_fn=os.setsid,
            stdin=self.slave_fd,
            stdout=self.slave_fd,
            stderr=self.slave_fd,
            close_fds=True
        )
        os.close(self.slave_fd)  # GUI only keeps master FD

        # --- GUI setup ---
        self.root = tk.Tk()
        self.root.title("OSC Log GUI")

        frame = tk.Frame(self.root)
        frame.pack(fill="both", expand=True, padx=10, pady=10)

        scrollbar = tk.Scrollbar(frame)
        scrollbar.pack(side="right", fill="y")

        self.text = tk.Text(frame, wrap="word", yscrollcommand=scrollbar.set,
                            state="disabled", font=("Courier", 12), height=15, width=60)
        self.text.pack(side="left", fill="both", expand=True)
        scrollbar.config(command=self.text.yview)

        # Persistent buffer for PTY output
        self.buf = b""

        # Start a thread to monitor the PTY
        threading.Thread(target=self.pty_monitor, daemon=True).start()

        # Ctrl-T sends OSC 9998 ping
        self.root.bind("<Control-t>", self.send_ping)

        print("Interactive shell started. Type 'server.py' here to run the server.")

    def pty_monitor(self):
        """Non-blocking monitor of PTY master for OSC messages."""
        while True:
            rlist, _, _ = select.select([self.master_fd], [], [], 0.1)
            if self.master_fd in rlist:
                try:
                    data = os.read(self.master_fd, 4096)
                    if not data:
                        break
                    # Append new data to persistent buffer
                    self.buf += data
                    self.process_buffer()
                except OSError:
                    break

    def process_buffer(self):
        """Parse complete OSC 9999 sequences from buffer."""
        while b"\x07" in self.buf:
            # Split at first BEL
            seq, rest = self.buf.split(b"\x07", 1)
            self.buf = rest  # keep remaining bytes for next read
            if seq.startswith(b"\033]"):
                try:
                    text = seq.decode(errors="ignore")
                    if text.startswith("\033]9999;"):
                        payload = text[len("\033]9999;"):]
                        msg = base64.b64decode(payload).decode(errors="replace")
                        self.append_message(msg)
                except Exception:
                    pass  # ignore invalid sequences

    def append_message(self, msg):
        """Append a message to the GUI log."""
        self.text.config(state="normal")
        self.text.insert("end", f"Server: {msg}\n")
        self.text.see("end")
        self.text.config(state="disabled")

    def send_ping(self, event=None):
        """Send OSC 9998 ping to the shell/server via PTY."""
        osc = make_osc(9998, b"ping from GUI")
        try:
            os.write(self.master_fd, osc)
            self.append_message("GUI → sent ping")
        except OSError:
            self.append_message("GUI → failed to send ping (PTY closed)")

    def run(self):
        self.root.mainloop()


def main():
    app = GuiApp()
    app.run()

if __name__ == "__main__":
    main()

