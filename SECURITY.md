# Security

SkyPoint only receives: it reads its own sensors and GPS and downloads public data. It has no accounts and stores no personal data beyond your settings.

Things worth knowing:

- **Wi-Fi password (watches).** The network name and password you enter are stored in the watch's flash (the T-Watch can also read `/skypoint/wifi.txt` from the SD card). Anyone holding the watch or the card could read them.
- **Approximate location (C6).** *Sync over Wi-Fi* asks ip-api.com (plain HTTP) for the city of your internet address. Pick a city under SETUP › Location if you'd rather not.
- **Orbit downloads.** Satellite data comes from CelesTrak over HTTPS. In this beta the watches do not check the server's certificate, so a network you don't trust could feed them wrong orbits (wrong predictions, nothing worse).
- **Android.** The app asks for location (to compute your sky), and camera only if you turn on Camera view. It contacts CelesTrak for orbits and, when you open an info sheet, Wikipedia with the object's name. Nothing else leaves the phone. It is signed by the project's own key; install it only from the Releases page or the project's web installer.

If you find a security problem, please use GitHub's **Report a vulnerability** option (Security tab > Advisories) rather than a public issue, so it can be fixed before details are shared.

This is a beta. Do not rely on it for navigation or safety-critical decisions, and never use it to look at the Sun.
