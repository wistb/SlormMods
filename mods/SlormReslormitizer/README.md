# SlormReslormitizer

Operates Rick Manalan's Slormitizer in [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/): loads the cheapest Slormandrites for the odds you ask for, and reinforces.

Open the Slormitizer and put an item in it; the window shows by itself there (F8 hides or shows it).

- **Targets.** The chance of success, Critical Reinforce and Double Reinforce to reach at least, and the Failure to stay under.
- **Plan.** The cheapest mix of Slormandrites of Fate, Aptitude, Virtue and Harmony in your stock that reaches the targets, for the item and slormite in the Slormitizer. Slormelines, Negation and True Potential you load yourself are left in place.
- **Slormite.** Use whichever slormite is in the Slormitizer, or choose one. More are loaded when it runs out, and lower slormites can be merged up into the one in use, as on Rick's Merge Slormites tab.
- **Load.** Puts the plan in the Slormitizer. Nothing is spent.
- **Reinforce once.** Loads the plan and activates the Slormitizer.
- **Repeat.** Keeps reinforcing until the item reaches the level you set, or the targets can no longer be met.

Nothing is activated unless the numbers the game itself shows meet your targets.

## Install

1. Back up your save: `%LocalAppData%\The_Slormancer`
2. If the game does not load Aurie mods yet, install the Aurie loader. The easiest way is the installer from [SlormAPI](https://github.com/tsotsas/SlormAPI/releases).
3. Download the latest `SlormReslormitizer` zip from [Releases](https://github.com/wistb/SlormMods/releases).
4. Extract it into the game folder, so `SlormReslormitizer.dll` lands in `mods\Aurie`.
5. Start the game from Steam as usual.

The game's own files are never changed. To uninstall, delete `mods\Aurie\SlormReslormitizer.dll`.

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh          # bin/SlormReslormitizer.dll
./build.sh deploy   # also copy into the game's mods/Aurie (set SLORM_GAME, or put it in the .env at the top of the repo)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/SlormReslormitizer-v<version>.zip
```

`SLORM_DEV=1` builds the dev variant, which logs the game's Slormitizer calls and samples its odds on F10.

Built on [Aurie](https://github.com/AurieFramework/Aurie), [YYToolkit](https://github.com/AurieFramework/YYToolkit) and [Dear ImGui](https://github.com/ocornut/imgui); see [THIRD-PARTY.md](../../THIRD-PARTY.md). Licensed under AGPL-3.0.
