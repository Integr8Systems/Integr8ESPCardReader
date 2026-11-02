"""Integr8 Reader Setup - lightweight Windows BLE commissioning utility.

Requires: Python 3.10+, pip install bleak
Launch:   python tools/integr8_reader_setup.py

The application does not store MQTT passwords or pairing PINs on the PC.
Windows handles authenticated BLE pairing (PIN provided by the ESP32).
"""
import asyncio
import json
import queue
import threading
import tkinter as tk
from tkinter import messagebox, ttk

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    raise SystemExit("Missing dependency: run 'py -m pip install bleak' and relaunch.")

SERVICE_UUID = "875d0000-e1a7-4dca-9a63-a18b1122c001"
WRITE_UUID = "875d0001-e1a7-4dca-9a63-a18b1122c001"
STATUS_UUID = "875d0002-e1a7-4dca-9a63-a18b1122c001"
MAX_CONFIG_BYTES = 512
CHUNK_SIZE = 16


def validate(host, port, device_id, username, password):
    if not host or len(host.encode("utf-8")) >= 96:
        raise ValueError("MQTT server must contain 1-95 characters.")
    if any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_:" for ch in host):
        raise ValueError("MQTT server supports a hostname or IPv4 address.")
    if not port.isdecimal() or not 1 <= int(port) <= 65535:
        raise ValueError("MQTT port must be 1-65535.")
    if not device_id or len(device_id.encode("utf-8")) >= 48:
        raise ValueError("Device ID must contain 1-47 characters.")
    if any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_" for ch in device_id):
        raise ValueError("Device ID accepts letters, digits, '-' and '_'.")
    if len(username.encode("utf-8")) >= 64:
        raise ValueError("MQTT username must be shorter than 64 bytes.")
    if len(password.encode("utf-8")) >= 96:
        raise ValueError("MQTT password must be shorter than 96 bytes.")
    if "\r" in username + password or "\n" in username + password:
        raise ValueError("Username/password cannot contain newlines.")
    payload = json.dumps({
        "host": host, "port": int(port), "deviceId": device_id,
        "username": username, "password": password
    }, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    if len(payload) > MAX_CONFIG_BYTES:
        raise ValueError("Encoded BLE configuration exceeds 512 bytes.")
    return payload


class SetupApp:
    def __init__(self, root):
        self.root = root
        root.title("Integr8 Reader Setup")
        root.geometry("485x470")
        root.resizable(False, False)
        self.events = queue.Queue()
        self.devices = {}
        self.busy = False

        frame = ttk.Frame(root, padding=18)
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Integr8 Reader Setup", font=("Segoe UI", 16, "bold")).pack(anchor="w")
        ttk.Label(frame, text="Commission the ESP32-S3 using Bluetooth Low Energy").pack(anchor="w", pady=(1, 14))

        row = ttk.Frame(frame)
        row.pack(fill="x")
        ttk.Label(row, text="Nearby reader").pack(anchor="w")
        self.device_select = ttk.Combobox(row, state="readonly", width=47)
        self.device_select.pack(side="left", fill="x", expand=True, padx=(0, 6))
        self.scan_button = ttk.Button(row, text="Scan", command=self.scan)
        self.scan_button.pack(side="right")

        self.fields = {}
        defaults = [
            ("MQTT server / IP", "host", ""),
            ("MQTT port", "port", "1889"),
            ("Device ID", "deviceId", ""),
            ("MQTT username", "username", ""),
            ("MQTT password", "password", ""),
        ]
        for title, key, initial in defaults:
            ttk.Label(frame, text=title).pack(anchor="w", pady=(9, 2))
            widget = ttk.Entry(frame, show="*" if key == "password" else "")
            widget.insert(0, initial)
            widget.pack(fill="x")
            self.fields[key] = widget

        ttk.Label(
            frame,
            text="Pairing requires the device-specific 6-digit PIN.\n"
                 "Read it from the USB serial monitor or device label.",
            foreground="#555555", justify="left",
        ).pack(anchor="w", pady=(13, 8))
        self.save_button = ttk.Button(frame, text="Pair, save MQTT settings and restart reader", command=self.save)
        self.save_button.pack(fill="x")
        self.status = tk.StringVar(value="Press Scan after enabling reader setup mode.")
        ttk.Label(frame, textvariable=self.status, wraplength=435).pack(anchor="w", pady=(12, 0))

        root.after(150, self.pump)

    def set_busy(self, busy):
        self.busy = busy
        self.scan_button.configure(state="disabled" if busy else "normal")
        self.save_button.configure(state="disabled" if busy else "normal")

    def run_worker(self, name, work):
        if self.busy:
            return
        self.set_busy(True)
        self.status.set(name + "...")
        def wrapper():
            try:
                result = asyncio.run(work())
                self.events.put(("ok", name, result))
            except Exception as exc:
                self.events.put(("error", name, str(exc)))
        threading.Thread(target=wrapper, daemon=True).start()

    def scan(self):
        async def work():
            devices = await BleakScanner.discover(timeout=7)
            return [(f"{d.name} ({d.address})", d) for d in devices
                    if d.name and d.name.startswith("I8R-")]
        self.run_worker("Scanning", work)

    def save(self):
        selected = self.device_select.get()
        device = self.devices.get(selected)
        if device is None:
            messagebox.showwarning("Reader required", "Scan and select an Integr8 reader first.")
            return
        try:
            payload = validate(
                self.fields["host"].get().strip(),
                self.fields["port"].get().strip(),
                self.fields["deviceId"].get().strip(),
                self.fields["username"].get(),
                self.fields["password"].get(),
            )
        except ValueError as exc:
            messagebox.showerror("Invalid MQTT settings", str(exc))
            return
        if not messagebox.askokcancel(
            "Save configuration?",
            "Pair with this reader and replace its saved MQTT settings?\n"
            "The reader will reboot after the change."
        ):
            return

        async def work():
            # pair=True lets Windows show the system PIN entry dialogue.
            async with BleakClient(device, pair=True, timeout=45) as client:
                if not client.is_connected:
                    raise RuntimeError("BLE connection unsuccessful.")
                if SERVICE_UUID not in [str(s.uuid).lower() for s in client.services]:
                    raise RuntimeError("Selected device does not expose the Integr8 configuration service.")
                await client.write_gatt_char(WRITE_UUID, b"\x01", response=True)  # BEGIN
                for offset in range(0, len(payload), CHUNK_SIZE):
                    await client.write_gatt_char(
                        WRITE_UUID, b"\x02" + payload[offset:offset + CHUNK_SIZE],
                        response=True
                    )
                await client.write_gatt_char(WRITE_UUID, b"\x03", response=True)  # COMMIT

                # A BLE Write Response is NOT proof the firmware saved to NVS.
                for _ in range(20):
                    await asyncio.sleep(0.25)
                    status = bytes(await client.read_gatt_char(
                        STATUS_UUID, use_cached=False
                    )).decode("ascii", errors="replace")
                    if status == "SAVED":
                        return "Saved to NVS. Reader will restart and connect to MQTT."
                    if status.startswith("ERROR:"):
                        raise RuntimeError("Reader rejected settings: " + status)
                raise TimeoutError("No NVS-save confirmation. Check serial monitor before retrying.")
        self.run_worker("Configuring", work)

    def pump(self):
        while True:
            try:
                kind, action, value = self.events.get_nowait()
            except queue.Empty:
                break
            self.set_busy(False)
            if kind == "error":
                self.status.set(f"{action} failed: {value}")
                messagebox.showerror("Integr8 Reader Setup", value)
            elif action == "Scanning":
                self.devices = {name: device for name, device in value}
                self.device_select.configure(values=list(self.devices))
                if self.devices:
                    self.device_select.current(0)
                else:
                    self.device_select.set("")
                self.status.set(f"Found {len(self.devices)} Integr8 reader(s).")
            else:
                self.status.set(value)
                messagebox.showinfo("Integr8 Reader Setup", value)
        self.root.after(150, self.pump)


if __name__ == "__main__":
    root = tk.Tk()
    ttk.Style(root)
    SetupApp(root)
    root.mainloop()
