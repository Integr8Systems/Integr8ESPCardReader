# TTL UART RFID wiring (T-ETH-Elite ESP32-S3)

## Proposed GPIO wiring (bare board only)

| RFID reader signal | T-ETH-Elite connection | Notes |
| --- | --- | --- |
| Reader TX | GPIO39 (ESP32 RX) | 3.3 V TTL only; use a level shifter if reader TX is 5 V |
| Reader RX (optional) | GPIO42 (ESP32 TX) | Only if the reader accepts commands and needs RX |
| Reader GND | Board GND | Common ground is mandatory |
| Reader VCC | Appropriate external regulated supply | Use the reader's rated voltage; do not assume 3.3 V or draw high power from a GPIO |

```text
 TTL RFID READER                          LILYGO T-ETH-ELITE
 +----------------+                       +-----------------+
 | TX ------------+---------------------->| GPIO39 (RX1)    |
 | RX <-----------+-----------------------+ GPIO42 (TX1)    |  optional
 | GND -----------+-----------------------+ GND             |
 | VCC -----------+---- rated PSU         |                 |
 +----------------+                       +-----------------+
           PSU GND -----------------------+ GND             |
                                          +-----------------+
```

GPIO39/42 are *also* used by the optional GPS/modem expansion hardware. Do not attach that expansion while these pins are allocated to the RFID reader. The RFID reader needs to send ASCII decimal digits terminated by CR or LF; adjust the parser for a binary frame reader.

The onboard W5500 already uses GPIO47 (MISO), 21 (MOSI), 48 (SCK), 45 (CS), and 14 (IRQ). Do not reuse them. GPIO0/3/45/46 are boot strapping pins and need special care. Consult the exact board revision's silk screen and official pin diagram before wiring.

No relay, lock drive, buzzer, or DOS inputs are enabled in this initial firmware. Never attach a door strike or relay coil directly to an ESP32 GPIO.

Hardware references:
- https://wiki.lilygo.cc/products/t-eth-series/t-eth-elite/
- https://github.com/Xinyuan-LilyGO/LilyGo-Modem-Series/blob/main/docs/en/esp32s3/t-eth-elite-s3/README.MD
