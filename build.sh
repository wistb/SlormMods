#!/usr/bin/env bash
# ./build.sh <mod>          build mods/<mod>/bin/<mod>.dll
# ./build.sh <mod> deploy   build, then copy into the game folder
# ./build.sh <mod> play     deploy, then start the game through Steam
# ./build.sh <mod> package  build, then write mods/<mod>/dist/<mod>-v<version>.zip
# A mod is the name of its folder under mods/, such as SlormSuite. Each folder's own build.sh passes its name.
# The game must already load Aurie mods; SlormAPI's installer sets that up.
# SLORM_DEV=1 builds a mod's dev variant. Not for release.
set -euo pipefail
cd "$(dirname "$0")"

MOD="${1:-}"
MOD="${MOD%/}"
ACTION="${2:-}"
DIR="mods/$MOD"
[ -n "$MOD" ] && [ -f "$DIR/$MOD.vcxproj" ] || { echo "usage: ./build.sh <mod> [deploy|play|package]; mods: $(ls mods/*/*.vcxproj | xargs -n1 basename | sed 's/\.vcxproj$//' | tr '\n' ' ')" >&2; exit 1; }

# "deploy" and "play" need the game folder: export SLORM_GAME, or put SLORM_GAME="..." in .env (untracked).
[ -f .env ] && . ./.env
GAME="${SLORM_GAME:-}"
# vswhere lives in a fixed place under Program Files (x86); ask Windows where that is. VSWHERE overrides it.
if [ -z "${VSWHERE:-}" ]; then
	PROGRAM_FILES=$(cmd.exe /c 'echo %ProgramFiles(x86)%' 2>/dev/null | tr -d '\r')
	VSWHERE="$(wslpath -u "$PROGRAM_FILES")/Microsoft Visual Studio/Installer/vswhere.exe"
fi

[ -f "$VSWHERE" ] || { echo "vswhere not found: install VS 2022 Build Tools with the C++ workload" >&2; exit 1; }
MSBUILD_WIN=$("$VSWHERE" -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | tr -d '\r' | head -1)
[ -n "$MSBUILD_WIN" ] || { echo "MSBuild not found" >&2; exit 1; }

VARIANT=release
if [ -n "${SLORM_DEV:-}" ]; then
	[ "$ACTION" != package ] || { echo "a dev build cannot be packaged" >&2; exit 1; }
	VARIANT=dev
	# cl reads extra options from CL.
	export CL=/DSLORM_DEV WSLENV="${WSLENV:+$WSLENV:}CL"
fi
# MSBuild does not see the define change, so rebuild when the variant does.
mkdir -p "$DIR/obj"
[ "$(cat "$DIR/obj/variant" 2>/dev/null)" = "$VARIANT" ] || find "$DIR/source" -name '*.cpp' -exec touch {} +
echo "$VARIANT" > "$DIR/obj/variant"

"$(wslpath -u "$MSBUILD_WIN")" "$(wslpath -w "$DIR/$MOD.vcxproj")" -nologo -v:minimal -p:Configuration=Release -p:Platform=x64

if [ "$ACTION" = deploy ] || [ "$ACTION" = play ]; then
	[ -f "$GAME/steam_appid.txt" ] || { echo "set SLORM_GAME to the game folder" >&2; exit 1; }
	[ -f "$GAME/mods/Aurie/YYToolkit.dll" ] || { echo "the game has no Aurie loader: install it with SlormAPI's installer" >&2; exit 1; }
	copy() { cp "$DIR/bin/$MOD.dll" "$GAME/mods/Aurie/"; }
	# The list is read whole: a grep that stops early would fail the pipe.
	running() { [ "$(tasklist.exe 2>/dev/null | grep -ci "^The Slormancer.exe")" != 0 ]; }
	if running; then echo "the game is running; the build goes in when it closes"; else echo "the game is closed"; fi
	until copy 2>/dev/null; do
		sleep 3
	done
	echo "deployed to $GAME"
	if [ "$ACTION" = play ]; then
		# Steam's id for the game is kept next to its exe; Steam itself sits three folders up.
		APP=$(tr -d '\r\n' < "$GAME/steam_appid.txt")
		launch() {
			if [ -f "$GAME/../../../steam.exe" ]; then
				(cd "$GAME/../../.." && ./steam.exe -applaunch "$APP" >/dev/null 2>&1 &)
			else
				(cd /mnt/c && cmd.exe /c start "" "steam://rungameid/$APP" >/dev/null 2>&1) || true
			fi
		}
		# Steam ignores a launch made while it is still closing the game down, so ask again until it shows.
		for try in 1 2 3; do
			launch
			for wait in 1 2 3 4 5 6 7 8 9 10; do
				running && { echo "the game is running with the new build"; exit 0; }
				sleep 3
			done
		done
		echo "the game did not start; start it from Steam" >&2
		exit 1
	fi
fi

if [ "$ACTION" = package ]; then
	VERSION=$(sed -n 's/^#define [A-Z]*_VERSION "\(.*\)"/\1/p' "$DIR/source/Version.hpp")
	[ -n "$VERSION" ] || { echo "no version in $DIR/source/Version.hpp" >&2; exit 1; }
	mkdir -p "$DIR/dist"
	rm -f "$DIR/dist/$MOD-v$VERSION.zip"

	# Laid out to be extracted straight into the game folder.
	python3 - "$MOD" "$DIR" "$DIR/dist/$MOD-v$VERSION.zip" <<'PY'
import sys, zipfile
mod, folder, target = sys.argv[1], sys.argv[2], sys.argv[3]
files = {
    f"{mod}-README.txt": f"{folder}/packaging/README.txt",
    f"mods/licenses/{mod}-NOTICE.txt": f"{folder}/packaging/NOTICE.txt",
    "mods/licenses/AGPL-3.0.txt": "LICENSE",
}
# A mod's zip holds the mod alone; the loader comes with SlormAPI's installer.
files[f"mods/Aurie/{mod}.dll"] = f"{folder}/bin/{mod}.dll"
# Only what draws on the overlay is built with Dear ImGui.
if "imgui" in open(f"{folder}/{mod}.vcxproj").read():
    files["mods/licenses/DearImGui-MIT.txt"] = "vendor/imgui/LICENSE.txt"
with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
    for name, source in files.items():
        archive.write(source, name)
PY
	echo "wrote $DIR/dist/$MOD-v$VERSION.zip"
fi
