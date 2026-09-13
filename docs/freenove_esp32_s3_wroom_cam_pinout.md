# Freenove ESP32-S3-WROOM CAM Board — Pinout

Board kit: Freenove ESP32-S3 WROOM CAM (8 MB flash, 8 MB octal PSRAM — N8R8), OV2640
camera, on-board microSD slot (1 GB card included).

Vendor reference: <https://github.com/Freenove/Freenove_ESP32_S3_WROOM_Board>

PlatformIO env: `[env:wroom_32_camera]`, board id `freenove_esp32_s3_wroom`.

## Module / power

| Item | Value |
| --- | --- |
| MCU | ESP32-S3-WROOM-1 N8R8, dual-core Xtensa LX7 @ 240 MHz |
| Flash | 8 MB QSPI |
| PSRAM | 8 MB **octal** (OPI) — needs `CONFIG_SPIRAM` + octal mode in sdkconfig |
| USB-C (left, "UART") | CH343 USB–serial bridge; use this one for flashing/monitor |
| USB-C (right, "USB") | ESP32-S3 native USB (GPIO19 D-, GPIO20 D+) |
| Logic level | 3.3 V (pins are **not** 5 V tolerant) |
| 5V pin | USB VBUS in/out, ~500 mA budget |

## Camera (OV2640, DVP)

| Signal | GPIO |
| --- | --- |
| XCLK | 15 |
| PCLK | 13 |
| VSYNC | 6 |
| HREF | 7 |
| SIOD (SCCB SDA) | 4 |
| SIOC (SCCB SCL) | 5 |
| D0 / Y2 | 11 |
| D1 / Y3 | 9 |
| D2 / Y4 | 8 |
| D3 / Y5 | 10 |
| D4 / Y6 | 12 |
| D5 / Y7 | 18 |
| D6 / Y8 | 17 |
| D7 / Y9 | 16 |
| PWDN | not connected (`-1`) |
| RESET | not connected (`-1`) |

## microSD slot (SDMMC, 1-bit)

The slot is wired for the SDMMC peripheral in 1-bit mode, not SPI.

| Signal | GPIO |
| --- | --- |
| CLK | 39 |
| CMD | 38 |
| DATA0 | 40 |

Note: this differs from [lib/SD_Card/sd_card.h](lib/SD_Card/sd_card.h), which targets the
Heltec board over SPI (MISO 37 / MOSI 35 / SCLK 36 / CS 34). Those pins are
unavailable here — see "Reserved" below.

## On-board I/O

| Function | GPIO |
| --- | --- |
| RGB LED (WS2812) | 48 |
| BOOT button | 0 (pulled high, active low) |
| RESET button | EN (hardware only) |
| Default UART0 TX / RX | 43 / 44 |

## Reserved / unusable GPIO

| GPIO | Why |
| --- | --- |
| 26–32 | SPI flash |
| 33–37 | Octal PSRAM (N8R8) — do **not** use, even though 35/36/37 appear on the header |
| 19, 20 | Native USB D- / D+ (usable as GPIO only if native USB is unused) |
| 0 | BOOT strapping pin |
| 3, 45, 46 | Strapping pins — safe as GPIO, but must not be held at the wrong level at reset |

## Free GPIO with camera + SD in use

`1, 2, 3, 14, 21, 41, 42, 47` plus `43/44` if you give up the serial console and
`19/20` if you give up native USB.

ADC1 channels (safe to use while Wi-Fi is on) land on GPIO 1–10, so with the camera
attached only **GPIO1 (ADC1_CH0), GPIO2 (ADC1_CH1) and GPIO3 (ADC1_CH2)** remain for
analog input. The moisture sensor currently uses `ADC_CHANNEL_4` (GPIO5) in
[lib/Protocols/gpio.h](lib/Protocols/gpio.h) — that pin is the camera SIOC line on this
board and must be remapped before porting.

## Porting notes for this project

- I2C display (17/18) collides with camera D6/D5 — move to free pins, e.g. 1/2 or 41/42.
- I2S speaker (4/6/7) collides with SIOD/VSYNC/HREF — move to e.g. 14/21/47.
- `VEXT_PIN` (36) and the SPI SD pins are Heltec-only; both fall in the PSRAM range here.
- Keep pin macros per-board (e.g. `#ifdef CONFIG_IDF_TARGET_ESP32S3`) rather than editing
  the shared headers in place.
