# Map Reveal

Shows the whole map from the start of a floor in [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/).

The minimap and the large map show every corridor and icon as soon as a floor begins, instead of what the hero has walked past. There is nothing to set up. With SlormAPI installed, the mod is listed on the game's MODS page and has a switch under Options > Mods; switching it off takes the icons away at once, and the map clears again on the next floor.

## Install

1. Back up your save: `%LocalAppData%\The_Slormancer`
2. If the game does not load Aurie mods yet, install the Aurie loader. The easiest way is the installer from [SlormAPI](https://github.com/tsotsas/SlormAPI/releases).
3. Download the latest `MapReveal` zip from [Releases](https://github.com/wistb/SlormMods/releases).
4. Extract it into the game folder, so `MapReveal.dll` lands in `mods\Aurie`.
5. Start the game from Steam as usual.

The game's own files are never changed. To uninstall, delete `mods\Aurie\MapReveal.dll`.

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh          # bin/MapReveal.dll
./build.sh deploy   # also copy into the game's mods/Aurie (set SLORM_GAME, or put it in the .env at the top of the repo)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/MapReveal-v<version>.zip
```

Built on [Aurie](https://github.com/AurieFramework/Aurie), [YYToolkit](https://github.com/AurieFramework/YYToolkit) and [SlormAPI](https://github.com/tsotsas/SlormAPI); see [THIRD-PARTY.md](../../THIRD-PARTY.md). Licensed under AGPL-3.0.
