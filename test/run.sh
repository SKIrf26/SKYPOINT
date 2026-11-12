#!/usr/bin/env bash
# Checks the astronomy engine and satellite tracker against PyEphem (pip install ephem).
set -euo pipefail
cd "$(dirname "$0")"
LIBS="${1:-$HOME/Arduino/libraries}"
g++ -O2 -o astro_test astro_test.cpp ../SkyPoint/astro.cpp
python3 astro_check.py
S="$LIBS/Sgp4/src"
g++ -std=c++17 -O2 -Uunix -Istub -I"$S" -w sats_test.cpp ../SkyPoint/sats.cpp ../SkyPoint/astro.cpp "$S"/*.cpp -o sats_test
./sats_test
