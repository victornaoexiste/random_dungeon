#!/bin/bash
# Random Dungeon - Android build setup (Linux).
#
# Installs the Android command-line SDK/NDK (if missing) into ~/Android/Sdk,
# downloads the SDL2 / SDL2_image / SDL2_mixer / SDL2_ttf / ENet sources
# into ~/Android/src and links them into app/src/main/jni/deps/.
#
# Then:  python3 pack_data.py && ./gradlew assembleDebug
# APK:   app/build/outputs/apk/debug/app-debug.apk
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/Android/Sdk}"
SRC="$HOME/Android/src"
DEPS="$HERE/app/src/main/jni/deps"

# SDL2 must match the Java files in app/src/main/java/org/libsdl (2.32.10)
SDL=2.32.10; IMG=2.8.8; MIX=2.8.1; TTF=2.24.0; ENET=1.3.18

if [ ! -x "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" ]; then
  mkdir -p "$ANDROID_HOME/cmdline-tools"
  tmp=$(mktemp -d)
  curl -sSL -o "$tmp/tools.zip" https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
  unzip -q "$tmp/tools.zip" -d "$tmp"
  mv "$tmp/cmdline-tools" "$ANDROID_HOME/cmdline-tools/latest"
  rm -rf "$tmp"
fi
yes | "$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" --licenses >/dev/null 2>&1 || true
"$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager" "platform-tools" "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264"

mkdir -p "$SRC"
fetch() { # url dir
  [ -d "$SRC/$2" ] && return
  curl -sSLf "$1" | tar xz -C "$SRC"
}
fetch https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL2-$SDL.tar.gz SDL2-$SDL
fetch https://github.com/libsdl-org/SDL_image/releases/download/release-$IMG/SDL2_image-$IMG.tar.gz SDL2_image-$IMG
fetch https://github.com/libsdl-org/SDL_mixer/releases/download/release-$MIX/SDL2_mixer-$MIX.tar.gz SDL2_mixer-$MIX
fetch https://github.com/libsdl-org/SDL_ttf/releases/download/release-$TTF/SDL2_ttf-$TTF.tar.gz SDL2_ttf-$TTF
fetch http://enet.bespin.org/download/enet-$ENET.tar.gz enet-$ENET

# SDL_mixer: only built-in decoders (OGG via stb_vorbis); no external codec libs
sed -i 's/^SUPPORT_WAVPACK ?= true/SUPPORT_WAVPACK ?= false/; s/^SUPPORT_GME ?= true/SUPPORT_GME ?= false/' "$SRC/SDL2_mixer-$MIX/Android.mk"

ln -sfn "$SRC/SDL2-$SDL" "$DEPS/SDL2"
ln -sfn "$SRC/SDL2_image-$IMG" "$DEPS/SDL2_image"
ln -sfn "$SRC/SDL2_mixer-$MIX" "$DEPS/SDL2_mixer"
ln -sfn "$SRC/SDL2_ttf-$TTF" "$DEPS/SDL2_ttf"
ln -sfn "$SRC/enet-$ENET" "$DEPS/enet/enet-src"
echo "sdk.dir=$ANDROID_HOME" > "$HERE/local.properties"
echo "Android deps ready."
