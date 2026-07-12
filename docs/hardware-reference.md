# Hardware Reference

This is the implementation reference for the Pocket Journal target: Waveshare
SKU `34211`, `ESP32-S3-Touch-ePaper-1.54`. It records the board wiring used by
`firmware/components/pj_board` and the vendor constraints that matter when that
driver changes. Sources were checked on 2026-07-11.

## Target Identity And Revision

The current target is the touch-equipped, battery-equipped board. Waveshare's
[board page](https://docs.waveshare.com/ESP32-S3-ePaper-1.54) and
[product page](https://www.waveshare.com/product/esp32-s3-epaper-1.54.htm)
identify SKU `34211` and describe the current hardware as an
ESP32-S3-PICO-1-N8R8 with 8 MB flash and 8 MB PSRAM. The
[board schematic](https://files.waveshare.com/wiki/ESP32-S3-ePaper-1.54/ESP32-S3-Touch-ePaper-1.54-Schematic.pdf)
also shows that package and is the source of truth for the pin table below.

There is a vendor naming ambiguity:

- The board page says a V2 rollout began on November 1, 2025 and identifies V2
  by `V2` silkscreen on the back or top-left of the PCB. It warns that V1 and V2
  examples are not interchangeable.
- The official
  [example repository](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54#readme)
  says the non-touch board has V1 and V2 variants but the touch board has only
  one version. Its ESP-IDF examples are nevertheless split into
  [V1](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54/tree/main/02_Example/ESP-IDF/V1)
  and
  [V2](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54/tree/main/02_Example/ESP-IDF/V2),
  with the FT6336 touch example present under V2.

For this project, "V2" means the PICO-1-N8R8 schematic and V2 example family,
not merely a display-panel sticker. Do not substitute V1 pin definitions or a
V1 firmware image. If a replacement board lacks V2 silkscreen, verify its SoC,
flash/PSRAM size, touch controller, and schematic continuity before flashing.

## Board Pin Contract

These connections agree between the Waveshare schematic and the current board
driver. GPIOs not listed as expansion pins are already committed to onboard
hardware.

| GPIO | Board signal | Project use and constraint |
| ---: | --- | --- |
| 0 | `BOOT0` / BOOT key | Active-low AUX input and ROM download strap. Runtime long press is 650 ms on release. Do not reset or power-cycle while holding it unless entering download mode. |
| 3 | LED | Onboard LED; also an ESP32-S3 JTAG-selection strapping pin, so avoid external drive during reset. |
| 4 | `BAT_ADC` | ADC1 channel 3, through the board's 200 kOhm / 200 kOhm divider; battery voltage is approximately 2x ADC input voltage. |
| 5 | `RTC_INT` | PCF85063ATL open-drain, active-low alarm interrupt and RTC-capable external wake input. |
| 6 | `EPD3V3_EN` | Active-low e-paper supply enable through the onboard P-channel switch. |
| 7 | `EPD_TP_RST` | Touch reset. |
| 8 | `EPD_BUSY` | E-paper busy input; high means the controller must not receive commands. |
| 9 | `EPD_RST` | E-paper hardware reset, active low. Required to exit panel deep sleep. |
| 10 | `EPD_D/C` | E-paper data/command select: high for data, low for command. |
| 11 | `EPD_CS` | E-paper chip select, active low. |
| 12 | `EPD_SCLK` | E-paper SPI clock. |
| 13 | `EPD_SDI` | E-paper SPI controller-to-panel data (MOSI); there is no MISO connection. |
| 14 | `I2S_MCLK` | ES8311 master clock. |
| 15 | `I2S_SCLK` | ES8311 bit clock. |
| 16 | `I2S_ASDOUT` | ES8311 ADC data to ESP32-S3. |
| 17 | `BAT_Control` | Battery power-control circuit. |
| 18 | PWR key | Active-low power key input. |
| 21 | `EPD_TP_INT` | Touch interrupt. |
| 38 | `I2S_LRCK` | ES8311 word-select / LR clock. |
| 39 | `SD_CLK` | TF card clock in 1-bit SDMMC mode; also an ESP32-S3 JTAG pin. |
| 40 | `SD_MISO` / D0 | TF card data 0 in 1-bit SDMMC mode; also an ESP32-S3 JTAG pin. |
| 41 | `SD_MOSI` / CMD | TF card command in 1-bit SDMMC mode; also an ESP32-S3 JTAG pin. |
| 42 | `PA_EN` | Audio power-rail enable; also an ESP32-S3 JTAG pin. |
| 45 | `I2S_DSDIN` | ESP32-S3 audio data to ES8311; also the `VDD_SPI` strapping pin. |
| 46 | `PA_CTRL` | NS4150B amplifier control; also a boot/ROM-message strapping pin. |
| 47 | `SDA` | Shared onboard I2C data for touch, RTC, SHTC3, and ES8311 control. |
| 48 | `SCL` | Shared onboard I2C clock. |

GPIO19 and GPIO20 are reserved by the board for native USB. GPIO43 and GPIO44
are UART0. Espressif's
[ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf)
documents GPIO0, 3, 45, and 46 as strapping pins; GPIO19/20 as USB Serial/JTAG;
GPIO39-42 as JTAG; and GPIO43/44 as UART0. Treat their reset-time levels and
alternate functions as part of the board contract.

## E-Paper Driver Constraints

The panel is the 200 x 200, 1-bit black/white
[Waveshare 1.54-inch e-paper V2](https://files.waveshare.com/wiki/common/1.54inch_e-paper_V2_Datasheet.pdf).
The integrated board uses the panel's four-wire SPI selection: clock, MOSI,
D/C, and CS. The current project uses SPI2, mode 0, software-controlled CS, and
a 5,000-byte framebuffer.

Important constraints from the panel datasheet:

- Write data is captured on the rising clock edge. CS must be low for a
  transfer, D/C must be low for commands and high for data, and CS must return
  high between transfers.
- The specified maximum write clock is **20 MHz** at 25 C. The current firmware
  configures 40 MHz. That is outside the panel specification even if a sample
  unit appears to work; reduce it to 20 MHz or below before treating display
  behavior as production-stable.
- BUSY high means the controller is executing a waveform or another internal
  operation. Do not issue another command until BUSY goes low. Use BUSY rather
  than a fixed refresh delay, and make a timeout an explicit driver error.
- The controller supports partial refresh, but its waveform is temperature
  dependent. No authoritative maximum partial-refresh count was found. Keep a
  periodic full-refresh policy configurable and establish its cadence from
  ghosting tests on the shipping enclosure and temperature range.
- A display update is not complete until BUSY deasserts. Do not cut `EPD3V3`,
  reset the controller, or start another update during master activation.
- Command `0x10` with data `0x01` enters panel deep sleep. BUSY then remains
  high and only a hardware reset exits that state. This panel deep sleep is
  distinct from ESP32-S3 light/deep sleep.
- The panel's specified operating range is 0 to 50 C. Waveshare recommends
  refreshing an in-use panel at least once every 24 hours to limit image
  sticking and storing/shipping it with a white image.
- The image is bistable: removing panel power after a completed refresh does
  not clear the visible image. Power the panel for reset/update, wait for BUSY,
  put the controller in its intended idle/deep-sleep state, then disable its
  supply if the power design calls for it.

The generic
[1.54-inch e-Paper Module Manual](https://www.waveshare.com/wiki/1.54inch_e-Paper_Module_Manual#ESP32.2F8266)
and
[standalone module product page](https://www.waveshare.com/1.54inch-e-paper-module.htm)
are useful for controller behavior, but their example wiring is not the
integrated SKU 34211 pinout. Use the board schematic above for wiring. Reference
drivers are available in Waveshare's
[general e-Paper repository](https://github.com/waveshareteam/e-Paper) and its
[board-specific repository](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54).

## ESP32-S3 And ESP-IDF References

The project targets ESP-IDF 6.0.x. Prefer a versioned programming-guide URL when
an implementation decision depends on exact API behavior.

| Area | Primary reference | Project relevance |
| --- | --- | --- |
| ESP-IDF | [ESP-IDF 6.0.1 Programming Guide for ESP32-S3](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/index.html) | Build system, target configuration, component and API contracts. |
| SoC | [ESP32-S3 datasheet](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf) | Pin restrictions, straps, electrical limits, flash/PSRAM variants. |
| SoC internals | [ESP32-S3 Technical Reference Manual](https://documentation.espressif.com/esp32-s3_technical_reference_manual_en.pdf) | GPIO matrix, SPI, reset, clock, eFuse, and power-domain details. |
| Hardware design | [ESP32-S3 Hardware Design Guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/) | Power, reset, USB, RF, flash/PSRAM, and layout constraints. |
| SPI | [SPI Master Driver](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/spi_master.html) | SPI2 setup, GPIO-matrix routing, DMA, transaction ownership, and light-sleep behavior. |
| GPIO | [GPIO and RTC GPIO](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/gpio.html) | Pulls, interrupts, sleep configuration, hold behavior, and valid wake pins. |
| Display APIs | [LCD peripheral APIs](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/peripherals/lcd/index.html) | Relevant only if migrating to `esp_lcd`; the current SPI e-paper driver does not use the ESP32-S3 LCD peripheral. |
| Sleep | [Sleep Modes](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/system/sleep_modes.html) | Wake-source constraints and peripheral/pin state across light/deep sleep. |
| Power | [Power Management](https://docs.espressif.com/projects/esp-idf/en/v6.0.1/esp32s3/api-reference/system/power_management.html) | Dynamic frequency scaling, automatic light sleep, and power-management locks. |

For the current light-sleep path, BOOT on GPIO0 is a valid ESP32-S3 RTC/wakeup
pin, but it is also the download-mode strap. Release the button before any
reset. Wi-Fi/BLE connections are not preserved by manually entering light or
deep sleep unless the documented modem-sleep/automatic-light-sleep integration
is used. Reinitialize or validate display, touch, audio, SD, and network state
after any deeper sleep mode that powers down their domains.

The PCF85063ATL `INT` output is routed as `RTC_INT` to ESP32-S3 GPIO5. Durable
alarms, timers, snoozes, and intervals use that active-low signal as an
external light-sleep wake source. The ESP32-S3 internal RTC timer remains a
short-deadline fallback and independent cross-check while the device stays in
the same boot. Because the PCF alarm has no month or year comparator, firmware
only arms a day-of-month match when it is the next future match; otherwise it
arms the next first-of-month checkpoint and recalculates the durable deadline.

## Change Checklist

Before changing the board driver:

1. Confirm the physical board is SKU 34211 and matches the PICO-1-N8R8
   schematic/V2 examples.
2. Check the pin table for strapping, USB, JTAG, UART, and shared-I2C conflicts.
3. Keep e-paper SPI at or below 20 MHz and preserve CS/D/C/BUSY semantics.
4. Test full refresh, repeated partial refresh, timeout recovery, and panel
   power cycling at representative temperatures.
5. Test BOOT-button wake without accidentally entering ROM download mode.
6. Re-run SD, touch, audio, USB serial, Wi-Fi, and BLE smoke tests after any pin,
   power-domain, or sleep-policy change.
