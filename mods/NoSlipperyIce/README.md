# NoSlipperyIce

Removes the Slippery Ice cataclysm from [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/).

The floor is never iced and the hero never slides, wherever the cataclysm would come from: cataclysm breaches, The Great Forge, and the Netherworld places and warlords that always have it. On the Netherworld map those no longer list it. There is nothing to set up.

## Install

1. Back up your save: `%LocalAppData%\The_Slormancer`
2. If the game does not load Aurie mods yet, install the Aurie loader. The easiest way is the installer from [SlormAPI](https://github.com/tsotsas/SlormAPI/releases).
3. Download the latest `NoSlipperyIce` zip from [Releases](https://github.com/wistb/SlormMods/releases).
4. Extract it into the game folder, so `NoSlipperyIce.dll` lands in `mods\Aurie`.
5. Start the game from Steam as usual.

The game's own files are never changed. To uninstall, delete `mods\Aurie\NoSlipperyIce.dll`.

## Known limits

- The Slippery Ossuary shows an empty cataclysm icon on the Netherworld map.
- The Great Forge may still offer Slippery Ice as a challenge; taking it has no effect.

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh          # bin/NoSlipperyIce.dll
./build.sh deploy   # also copy into the game's mods/Aurie (set SLORM_GAME, or put it in the .env at the top of the repo)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/NoSlipperyIce-v<version>.zip
```

Built on [Aurie](https://github.com/AurieFramework/Aurie) and [YYToolkit](https://github.com/AurieFramework/YYToolkit); see [THIRD-PARTY.md](../../THIRD-PARTY.md). Licensed under AGPL-3.0.
