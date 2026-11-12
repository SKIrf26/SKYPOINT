#!/bin/bash
# Builds SkyPoint for Android without Gradle:
#   aapt2 (resources) -> javac (Java 8 bytecode) -> dx (dex) -> zipalign -> apksigner
# Needs: tools/android.jar (API 34), aapt2, dalvik-exchange, zipalign, apksigner, a JDK,
# and a signing key: tools/release.jks (or SKYPOINT_KEYSTORE) with SKYPOINT_KS_PASS set.
# The key is NOT in the repo - keep yours private, and keep using the same one so updates install over the old app.
set -euo pipefail
cd "$(dirname "$0")"
T=tools
J=${ANDROID_JAR:-$T/android.jar}
AAPT2=${AAPT2:-$(command -v aapt2 || echo $T/aapt2)}
KS=${SKYPOINT_KEYSTORE:-$T/release.jks}
KP=${SKYPOINT_KS_PASS:?Set SKYPOINT_KS_PASS to the signing key password}
for f in $J $AAPT2; do [ -f $f ] || { echo "Missing $f (see README.md)"; exit 1; }; done
[ -f "$KS" ] || { echo "Missing signing key $KS (see README.md to make one)"; exit 1; }
O=build
rm -rf $O && mkdir -p $O/gen $O/cls
VER=$(sed -n 's/.*android:versionName="\([^"]*\)".*/\1/p' AndroidManifest.xml | tr ' ' '_')

$AAPT2 compile --dir res -o $O/res.zip
$AAPT2 link -I $J --manifest AndroidManifest.xml -A assets -o $O/base.apk --java $O/gen $O/res.zip \
    --min-sdk-version 26 --target-sdk-version 34 --auto-add-overlay -0 woff2

javac -nowarn -encoding UTF-8 -source 8 -target 8 -bootclasspath $J -Xlint:-options \
    -d $O/cls $(find src $O/gen -name '*.java') 2>&1 | grep -v "JAVA_TOOL_OPTIONS\|^Note:" || true
[ -f $O/cls/com/skirf/skypoint/MainActivity.class ] || { echo "javac failed"; exit 1; }

dalvik-exchange --dex --min-sdk-version=26 --output=$O/classes.dex $O/cls 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
[ -f $O/classes.dex ] || { echo "dx failed"; exit 1; }

cp $O/base.apk $O/unsigned.apk
(cd $O && zip -q unsigned.apk classes.dex)
zipalign -f -p 4 $O/unsigned.apk $O/aligned.apk
apksigner sign --ks "$KS" --ks-pass pass:$KP --key-pass pass:$KP \
    --out $O/SkyPoint_$VER.apk $O/aligned.apk 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
apksigner verify $O/SkyPoint_$VER.apk 2>&1 | grep -v JAVA_TOOL_OPTIONS || true
ls -la $O/SkyPoint_$VER.apk
