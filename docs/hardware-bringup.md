# Hardware Bring-Up

The target product is Waveshare SKU `34211`, ESP32-S3-Touch-ePaper-1.54.

Before touching low-level drivers, identify the hardware revision:

- V2: board has `V2` on the back or top-left PCB silkscreen.
- V1: no V2 marking.

Waveshare notes that V1 and V2 example programs are not interchangeable. Keep all pin maps and driver init values behind the board profile layer in `firmware/components/pj_board`.

## Bring-Up Checklist

1. Confirm USB flashing and serial logs.
2. Confirm board revision and update board profile.
3. Mount FAT32 TF card.
4. Full-refresh the e-paper display.
5. Read touch coordinates and calibrate 0..199 display mapping.
6. Record 16 kHz mono WAV from ES8311 microphone path.
7. Play a short test tone through speaker output.
8. Read RTC time and SHTC3 temperature/humidity.
9. Provision Wi-Fi over BLE.
10. Start LAN API and verify `/v1/status`.
11. Run full sync: audio download, partner transcription, transcript upload.

## Known Risks

- V1/V2 pin and peripheral differences.
- E-paper partial refresh behavior and ghosting.
- BLE provisioning and Wi-Fi coexistence memory pressure.
- Audio codec clocking and sample format.
- TF card availability and write latency.

