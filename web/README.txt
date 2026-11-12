SkyPoint web flasher
====================

UPLOAD
  Upload this whole folder (keep the structure) to your webspace, e.g.
    https://skirf-labs.tech/SKYPOINT/   (the live copy)
  It is all static files: no server code, no build step.

  Flashing needs HTTPS (browsers only allow Web Serial on secure pages).
  The install buttons load ESP Web Tools from unpkg.com; everything else is local.

WHAT'S HERE
  index.html                     the page
  manifest_twatch_ultra.json     ESP Web Tools manifest, T-Watch Ultra (ESP32-S3, full image at 0x0)
  manifest_waveshare_c6.json     ESP Web Tools manifest, Waveshare C6 AMOLED 2.06 (ESP32-C6, 4 MB, image at 0x0)
  firmware/                      the .bin files + SHA256SUMS.txt (the page shows the checksums, APK included)
  android/                       SkyPoint_1.0_Beta_1.apk, the Android app (linked from the ANDROID section)
  img/                           screenshots
  fonts/                         Orbitron, Rajdhani, Share Tech Mono (SIL OFL, licences included)

PUSHING AN UPDATE
  Android: drop the new APK in android/ (signed with the SAME key so it installs over the old one)
  and update the link + version in the ANDROID section of index.html.

  1. Replace the files in firmware/ with the new builds (same file names), or use new names
     and change the "path" in the two manifest files.
  2. Bump "version" in both manifests and the 1.0 BETA 1 badge in index.html.
  3. Regenerate the checksums:
       cd firmware && (sha256sum *.bin; cd ../android && sha256sum *.apk) > SHA256SUMS.txt
  4. Upload. Browsers cache .bin files: if you keep the same names, hard-refresh (Ctrl+F5)
     before flashing, or add ?v=2 to the paths in the manifests.

NOTES
  - The T-Watch "Install" writes a full image and replaces Launcher. Launcher users should use
    the "LAUNCHER APP .BIN" download instead and install it from the SD card.
  - The installer offers "Erase device"; recommend it the first time on the C6.
  - Some hosts serve .apk files as text. If the download opens as gibberish on the phone, add
    the MIME type application/vnd.android.package-archive for .apk (e.g. .htaccess:
    AddType application/vnd.android.package-archive .apk).
