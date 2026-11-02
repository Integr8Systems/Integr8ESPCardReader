# Bluetooth (BLE) MQTT commissioning — prototype v1

This feature is **implemented in firmware**, alongside a lightweight Python
Windows setup utility; it remains **unverified on the physical LILYGO**.
Arduino CLI still builds the firmware without any additional BLE libraries:
the BLE, Preferences and GATT APIs are built into Arduino ESP32 core 3.3.7.

## How configuration works

- MQTT host (DNS name or IPv4 address), port (default 1889), device ID,
  username and password are stored as one checksummed record in the ESP32
  **NVS** namespace `i8mqtt`.
- Values are loaded at boot and used for MQTT subscriptions and publish topics.
  They survive normal reboots, uploads and loss of Ethernet connectivity.
- First boot without settings enables BLE setup for **600 seconds (10 minutes)**.
- To reconfigure an already provisioned reader, **hold BOOT (GPIO0) for
  4 seconds while the firmware is running**, then release. A fresh 600
  second (10-minute) BLE setup window begins. Do not hold BOOT while resetting: doing
  so puts an ESP32-S3 into bootloader mode.
- BLE device advertises as `I8R-XXXXXX`, using the last 6 hex digits
  of the ESP32's eFuse MAC.
- The firmware generates a **device-specific persistent 6-digit BLE PIN**
  at first BLE initialization, writes it to the `i8ble` NVS namespace,
  and prints it in the USB serial monitor at 115200 baud. Keep it secure;
  factory technicians should label each device with its unique PIN or use a
  secure commissioning record.
- BLE pairing is configured with secure connections, bonding and MITM;
  write access requires authentication on Bluedroid/NimBLE stacks.
- Configuration is written in **16-byte fragments** over a write-with-response
  characteristic, validated in the main Arduino loop, and stored to NVS.
- After `SAVED` status the device automatically restarts within 6 seconds
  and connects using the new site settings.

**No configuration secrets are included in read/status characteristics.**
The PIN is not a universal factory default. The byte-level MQTT reverse-hex
transform is not encryption; use network controls and broker ACLs.

## Windows setup app

Requirements: Windows 10/11, Bluetooth LE adapter, Python 3.10+ and the
lightweight [Bleak](https://bleak.readthedocs.io/) package.

From `C:\workspace\Integr8ESPCardReader`:

```powershell
py -m pip install bleak
py tools\integr8_reader_setup.py
```

Or:

```powershell
.\scripts\run-ble-setup.ps1
```

The setup app contains an explicit **Bluetooth pairing PIN** field,
**Pair & Test Bluetooth**, and **Save MQTT settings** buttons. It uses
Windows WinRT `DeviceInformationCustomPairing` with `PROVIDE_PIN` and
requires authenticated encryption, instead of Bleak's default Windows
`pair=True` path that only supports Just Works. The app pairs before
connecting to the GATT service. Do not use the normal Windows add-device
list as the required provisioning workflow. It does not save credentials or PINs to a file on the laptop.

Scan and select `I8R-XXXXXX`, enter the per-reader PIN from the USB serial
monitor or label, click **Pair & Test Bluetooth** first, then enter the
MQTT host/port/ID/username/password and click **Save MQTT settings**.
Wait for `SAVED`.
A successful BLE save proves local NVS persistence, **not** a successful
connection to the specified MQTT broker. Check the device's serial console
or heartbeat messages to verify Ethernet/MQTT connectivity after reboot.

The Windows app requires the exact BLE GATT service declared below;
changes to the service UUIDs are protocol-breaking.

## BLE GATT application protocol

Service UUID: `875d0000-e1a7-4dca-9a63-a18b1122c001`

Write characteristic (authenticated write with response):
`875d0001-e1a7-4dca-9a63-a18b1122c001`

Status characteristic (read-only, no secrets):
`875d0002-e1a7-4dca-9a63-a18b1122c001`

Write messages:

- `0x01`: BEGIN — clears buffered previous configuration
- `0x02 + 1..16 raw JSON bytes`: DATA fragment
- `0x03`: COMMIT — validate and persist the collected JSON

Maximum payload JSON size: 512 bytes. Fragments are sequential, and every
write requires a BLE write response. Example JSON:

```json
{"host":"192.168.2.100","port":1889,"deviceId":"reader-001","username":"site-reader","password":"example"}
```

Status values: `READY`, `SAVED`, `EXPIRED`, or `ERROR:JSON`,
`ERROR:PORT`, `ERROR:VALUES`, `ERROR:NVS`, `ERROR:FRAME`.

Do not use plaintext JSON from this BLE setup service on MQTT. MQTT continues
to use the existing Integr8 UTF-8/reversed-hex protocol.

## Security and recovery limits

This is **commissioning prototype**, not yet a hardened field interface.
Pairing behaviour must be validated on actual Windows Bluetooth adapters,
including re-pairing, bonds, passkey prompts, disconnects, timeouts and errors.
The PIN is visible to whoever has access to the serial port or device label.
For production, use authenticated installer credentials and stronger device
identity/commissioning enrolment, secure boot/flash encryption, encrypted
NVS or a protected secrets store, plus locked-down broker access.

The v1 firmware **persists then reboots**; it does not first test the
new broker and automatically roll back if unreachable. An installer can
recover by reopening the BLE setup window using the BOOT-button hold.
No NVS erase is required. A future release should test candidates over
Ethernet, keep the last-good settings and roll back automatically.

Your current 50,000-cardholder LittleFS data and 100,000-transaction
records are in a separate partition and **are not erased** by an NVS
configuration change. Avoid any "erase all flash" command.

Board pin use must be verified on the actual board and its expansion:
GPIO0 is the ESP32 boot strap button, not a dedicated production tamper
or configuration button. Consider adding a separate protected service
button before field deployment.

## Troubleshooting unreliable discovery / PIN pairing on Windows

The original v1 app attempted `BleakClient(..., pair=True)`, which on Windows
currently uses the Just Works confirmation ceremony. The device requires
an authenticated six-digit PIN, so that pairing method cannot satisfy the
reader's BLE security policy. The replacement app uses WinRT's native
`PROVIDE_PIN` custom-pairing ceremony and requires authenticated encryption.

The original firmware also explicitly set
`advertiseOnDisconnect(false)`, meaning one failed Windows connection could
stop all future advertisements until setup was restarted. The updated
firmware **restarts advertising on disconnect**, places the short device
name in the primary advertisement (not just the scan response), advertises
faster, and extends setup to **10 minutes**. Both the firmware and Windows
app updates must be deployed to benefit from these changes.

1. Pull the latest repository, compile using Arduino CLI and upload the new
   firmware **without erase-all**. Keep `ALLOW_FIRST_BOOT_FORMAT=false`.
2. Attach USB and open the 115200 baud serial monitor. Confirm an
   `[BLE] Device I8R-XXXXXX / unique setup PIN NNNNNN` message appears
   when BLE setup starts. Preserve the PIN securely; it is device-specific.
3. If the device has been commissioned previously, hold BOOT for 4 seconds
   **after normal boot**, then release it. On an unconfigured unit, BLE
   starts automatically when it boots. The discovery window lasts 10 minutes.
4. Turn Windows Bluetooth on. Run `py -m pip install -U bleak` once, then
   launch `py tools\\integr8_reader_setup.py`. Click **Scan**. Select the
   discovered reader. BLE advertisements may not always appear in Windows
   Settings > Bluetooth & devices, even while direct BLE scanning works.
5. Enter the PIN and click **Pair & Test Bluetooth**. Wait for the status
   message to confirm Windows pairing and GATT connectivity. Only then
   fill in MQTT settings and click **Save MQTT settings**.
6. If pairing fails repeatedly, close other Bluetooth tools, remove any
   old `I8R-XXXXXX` bond from Windows Bluetooth settings **if it appears**,
   reopen setup mode and try again. A saved ESP32 bond might also be stale;
   do not erase NVS or all Flash to work around this without a safe
   key-reset/recovery procedure.
7. For diagnosis collect the full app error text and USB serial lines
   containing `[BLE]` and note whether it fails during Scan, Pair, GATT
   Test or Save.

Do not disable MITM, use `Just Works` pairing for production credentials,
or send passwords via an unauthenticated BLE characteristic just to bypass
a Windows pairing issue.
