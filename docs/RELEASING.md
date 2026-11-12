# Making a release

For the maintainer. Do these in order.

1. Update `CHANGELOG.md` and `RELEASE_NOTES.md`. Raise `android:versionCode` and `android:versionName` in `android/AndroidManifest.xml`.
2. Build both watch images (see the README) and copy them into `firmware/` with the release names:
   `SkyPoint_TWatchUltra_Beta_N.bin` (merged, 0x0), `SkyPoint_TWatchUltra_Beta_N_Launcher.bin` (app only), `SkyPoint_WaveshareC6_AMOLED206_Beta_N.bin` (merged, 4 MB, 0x0).
3. Flash each image on real hardware: boot, align, lock on, Tonight, Orbit, GPS lock (T-Watch), Wi-Fi sync (C6), sounds. On the T-Watch also install the Launcher app from Launcher and use Exit to Launcher.
4. Android: `cd android && SKYPOINT_KS_PASS=... ./build.sh` with the project's signing key (never commit it). Install over the previous APK to confirm the key matches.
5. `python3 tools/guide/make_guide.py` if screens or steps changed.
6. `cd firmware && sha256sum *.bin *.apk > SHA256SUMS.txt`.
7. Web installer: copy the new files into `web/firmware/` and `web/android/`, update the paths and `version` in both `web/manifest_*.json`, the APK link and size in `web/index.html`, and `web/firmware/SHA256SUMS.txt`. Zip `web/` as `SkyPoint_Web_Installer_Beta_N.zip` and upload it to your host.
8. Commit and push. On GitHub: **Releases › Draft a new release**.
   - Tag: `v0.1.0-beta.N`, target `main`
   - Title: `SkyPoint - Beta N`
   - Description: paste `RELEASE_NOTES.md`
   - Attach the three `.bin` files, the `.apk`, `SHA256SUMS.txt`, the three guide PDFs and the web-installer zip
   - Tick **Set as a pre-release**
9. Settings › General: description, topics, and `docs/images/social-preview.png` as the social preview.
10. Open the release page in a private window, download a `.bin` and check it against `SHA256SUMS.txt`.
