# Contributing

Thanks for taking an interest. This is a small project at **Beta 1**, so the most useful help right now is **testing and bug reports**.

## Reporting a problem
Open an issue and pick **Bug report**. The form asks for what is needed:
- which device and release you used, and how you installed it,
- what you did and what you expected,
- for the watches, the lines from the Serial Monitor (115200 baud); for Android, the phone model and Android version,
- for anything about positions: your rough location (city is enough), the date and time, and what SkyPoint showed versus what you saw.

## Suggesting a feature
Use **Feature request**. Say what you would like and how you would use it under the sky.

## Code changes
Pull requests are welcome. Please keep each one small and focused, build it (see the README), and describe how you tested it. The astronomy is checked by `test/run.sh`; please run it after touching `astro.*` or `sats.*`. The star catalogue, constellation art, logo and guide are generated: change the scripts in `tools/`, not their output. By contributing you agree that your contribution is released under the same licence as the project (GPL v3).
