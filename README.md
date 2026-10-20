# SlormMods

Mods for [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/). Each has its own folder, readme and releases.

| Mod | What it does |
|---|---|
| [SlormSuite](mods/SlormSuite) | A collection of smaller mods behind one overlay. |
| [SlormReforger](mods/SlormReforger) | Automatic reforging at Friedrich's blacksmith. |
| [SlormHUD](mods/SlormHUD) | Follow a slorm-planner build inside the game. |
| [SlormReslormitizer](mods/SlormReslormitizer) | Operates Rick Manalan's Slormitizer for the odds you ask for. |
| [NoSlipperyIce](mods/NoSlipperyIce) | Removes the Slippery Ice cataclysm. |

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh <mod>           # mods/<mod>/bin/<mod>.dll
./build.sh <mod> deploy    # also copy into the game folder (set SLORM_GAME, or put it in .env)
./build.sh <mod> play      # deploy, then start the game through Steam
./build.sh <mod> package   # mods/<mod>/dist/<mod>-v<version>.zip
```

`./build.sh` inside a mod's folder does the same for that mod. The mods share `include/` (Aurie and YYToolkit headers) and `vendor/imgui/`; the game must already load Aurie mods, which [SlormAPI](https://github.com/tsotsas/SlormAPI)'s installer sets up. See [THIRD-PARTY.md](THIRD-PARTY.md).

Releases are tagged `<mod>-v<version>`.

## License

[AGPL-3.0](LICENSE).
