SKYPOINT  -  BETA 1
===================

Point your watch (or your phone) at the sky and it tells you what you're looking at: stars, constellations,
planets, the Moon in its real phase, galaxies and clusters, and the ISS on its way over.

Runs on:
  * LilyGO T-Watch Ultra          (standalone, or as an app in bmorcelli's Launcher)
  * Waveshare ESP32-C6-Touch-AMOLED-2.06
  * Android phones, Android 8.0 or newer


THE FASTEST WAY TO START
------------------------
  1. Install it
       Watch:   the web flasher, https://skirf-labs.tech/SKYPOINT/ (Chrome or Edge on a computer),
                or esptool with the .bin in firmware/
       Phone:   open https://skirf-labs.tech/SKYPOINT/ on the phone and tap Download APK
                (Play Protect may ask: More details > Install anyway)
  2. Time and place
       T-Watch: go outside, open the GPS page, wait for LOCKED
       C6:      SETUP > Wi-Fi, then Sync over Wi-Fi (or pick a city)
       Phone:   allow location
  3. North (watches only): long-press the sky, pick the Moon, a planet or a bright star you can see,
     centre it in the reticle and long-press again
  4. Hold it up and look. Tap a row on TONIGHT or ORBIT to be guided to it.

Read the user guide in docs/ (SkyPoint_Beta_1_User_Guide.pdf) for every screen and setting.

Never look at the Sun, directly or through binoculars, a telescope or a camera.


WHAT IS IN THIS FOLDER
----------------------
  READ_ME_FIRST.txt     this file
  README.md             overview, install, build
  RELEASE_NOTES.md      what's in Beta 1
  CHANGELOG.md          history
  firmware/             ready-made images and the Android app, with SHA256SUMS.txt
  web/                  the web flasher site (live at https://skirf-labs.tech/SKYPOINT/)
  docs/                 user guide (normal and booklet PDFs), screenshots, logo
  SkyPoint/             watch firmware source (Arduino)
  android/              Android app source
  tools/                catalogue, constellation art, logo and guide generators, simulator
  test/                 astronomy and SGP4 tests
  licenses/             third-party licences
