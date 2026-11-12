# SkyPoint - Beta 1

First public release. **SkyPoint** is a pocket planetarium you point at the sky: hold your watch or phone up and it shows what's behind it, live, and locks onto whatever you centre.

Runs on the **LilyGO T-Watch Ultra** (standalone or from bmorcelli's Launcher), the **Waveshare ESP32-C6-Touch-AMOLED-2.06**, and **Android** phones.

## Highlights
- 1,675 stars coloured by temperature, 88 constellations, the Sun, Moon (real phase) and seven planets, 109 Messier objects
- ISS, Tiangong and Hubble live with predicted tracks, every pass for the next 36 hours, visible-pass alerts 2 minutes before rise
- Lock-on reticle with an info card; Find mode with an arrow and rising tones; tap-to-zoom (watch) or pinch (phone)
- **Tonight**: sunset, full dark, sunrise, the Moon, planets, bright stars and deep-sky objects for your place
- **GPS page** on the T-Watch with a satellite radar; Wi-Fi sync on the C6 (time, approximate location, orbits)
- Sounds and vibration, a big-key Wi-Fi keyboard, night vision (all red)
- Astronomy checked against PyEphem (Sun, Moon and inner planets within 1′, rise/set within ~2 minutes, ISS passes to the second)

## New: SkyPoint for Android (1.0 Beta 1)
The same sky on a phone, Android 8.0+. True north from the compass (no alignment needed), optional camera view, pinch zoom, Layers (stars, constellations, constellation drawings, planets, Sun and Moon, satellites, deep sky), an info sheet for every object with its Wikipedia summary, portrait / auto / landscape, and a GPS sky plot.

Install `SkyPoint_1.0_Beta_1.apk` from the files below: open it on the phone and allow the install (Play Protect may ask: **More details › Install anyway**).

## Install
The easiest way is the web installer (the `web/` folder of the repo, hosted on HTTPS) with Chrome or Edge on a computer. Or with esptool:

```
# T-Watch Ultra, standalone (replaces Launcher)
esptool --chip esp32s3 write-flash 0x0 SkyPoint_TWatchUltra_Beta_1.bin

# Waveshare C6 AMOLED 2.06 (erase first the first time)
esptool --chip esp32c6 erase-flash
esptool --chip esp32c6 write-flash 0x0 SkyPoint_WaveshareC6_AMOLED206_Beta_1.bin
```

**Launcher users:** copy `SkyPoint_TWatchUltra_Beta_1_Launcher.bin` to the SD card and install it from Launcher instead.

## Files
| File | Purpose |
|---|---|
| `SkyPoint_TWatchUltra_Beta_1.bin` | T-Watch Ultra, complete image, write at `0x0` |
| `SkyPoint_TWatchUltra_Beta_1_Launcher.bin` | T-Watch Ultra, app for bmorcelli's Launcher |
| `SkyPoint_WaveshareC6_AMOLED206_Beta_1.bin` | Waveshare C6 AMOLED 2.06 (4 MB), complete image, write at `0x0` |
| `SkyPoint_1.0_Beta_1.apk` | Android app, Android 8.0+ |
| `SHA256SUMS.txt` | Checksums |
| `SkyPoint_Beta_1_User_Guide.pdf` (+ two booklet versions) | User guide |
| `SkyPoint_Web_Installer_Beta_1.zip` | The web installer site, ready to upload to any HTTPS host |

## Please know
This is a **beta**.

- The watches have no compass chip: align north once per session on something you can see (long-press the sky). Phones use their compass.
- Satellite predictions need fresh orbit data: update it when the ORBIT page says it's over a week old (needs Wi-Fi on the watches).
- The C6's *Sync over Wi-Fi* looks up your approximate location from your internet address (ip-api.com). Pick a city instead if you'd rather not.
- Never look at the Sun, directly or through any optics.

Please report problems with the **Bug report** form on the Issues tab, with the Serial Monitor output (115200 baud) if you can.
