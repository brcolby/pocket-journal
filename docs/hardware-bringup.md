# Hardware Bring-Up

The target product is Waveshare SKU `34211`, ESP32-S3-Touch-ePaper-1.54. Use
[the hardware reference](hardware-reference.md) for authoritative source links,
the pin contract, display constraints, and the known vendor naming ambiguity.

Before touching low-level drivers, identify the hardware revision:

- Expected project target: PICO-1-N8R8, 8 MB flash, 8 MB PSRAM, touch controller,
  and the V2 example family.
- V2-marked board: `V2` appears on the back or top-left PCB silkscreen.
- Unmarked board: do not infer V1 compatibility from the absence of the mark;
  verify the SoC, memory, touch controller, and schematic first.

Waveshare notes that V1 and V2 example programs are not interchangeable, while
its example repository separately describes the touch SKU as having one
version. Keep all pin maps and driver init values behind the board profile layer
in `firmware/components/pj_board` and follow the PICO-1-N8R8 schematic for this
project.

## Bring-Up Checklist

1. Confirm USB flashing and serial logs.
2. Confirm board revision and update board profile.
3. Mount FAT32 TF card.
4. Full-refresh the e-paper display.
5. Read touch coordinates and calibrate 0..199 display mapping.
6. Record 16 kHz mono WAV from ES8311 microphone path.
7. Play a short test tone through speaker output.
8. Read RTC time and SHTC3 temperature/humidity.
9. Provision Wi-Fi over USB-C; validate optional BLE provisioning separately.
10. Start LAN API and verify `/v1/status`.
11. Run full sync: audio download, partner transcription, transcript upload.

## Audio Leveling

The ES8311 microphone PGA uses `42 dB`, selected from on-device speech measurements that showed no sustained clipping. Recording captures the stronger slot from the mono codec rather than averaging it with an empty I2S slot.

After capture stops, the UI returns home immediately while a lower-priority task processes a temporary WAV before publishing it to the note list. Processing uses one fixed I2S input slot for the entire recording, a three-sample median de-click filter, an approximately 80 Hz high-pass and 4.8 kHz low-pass voice band, and 10 ms edge fades. It then normalizes from average level and the 99.9th-percentile peak, targets peak `28000` and average absolute level `2500`, and caps digital gain at `8x`. Silence below peak `128` or average absolute level `16` is not amplified.

Playback writes silent preroll and postroll around codec mute and PA transitions so amplifier switching occurs outside the note content.

The processor logs raw, filtered, robust-peak, clipping, gain, and normalized statistics. The final `.wav` is renamed into place only after processing succeeds; `.tmp` capture files are not indexed or played.

## Known Risks

- V1/V2 pin and peripheral differences.
- E-paper partial refresh behavior and ghosting.
- BLE provisioning and Wi-Fi coexistence memory pressure.
- Audio codec clocking and sample format.
- TF card availability and write latency.
