#pragma once
namespace BleSetup {
// Starts setup automatically on an unconfigured device; otherwise BOOT
// button must be held for 4 seconds to enable a 3-minute BLE window.
void begin(bool unconfigured);
void poll();
}
