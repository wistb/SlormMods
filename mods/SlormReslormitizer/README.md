# SlormReslormitizer

Operates Rick Manalan's Slormitizer in [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/): loads the Slormandrites that make reaching a reinforce level cheapest, and reinforces.

Open the Slormitizer and click the slormite left of its item slot: the options take the place of the character page, and the inventory beside them stays in use. The same settings are in Options > Mods.

- **Plan.** The Slormandrites of Fate, Aptitude, Virtue and Harmony to load from your stock: the load that makes reaching the level you set the cheapest on average, counting goldus, the Slormandrites and the slormites at their purchase price. It knows that a success spends the item's potential, that a Critical Reinforce refills it, that a failure zeroes it, and that a Double Reinforce skips a level, so it may take a risk now to save on the goes after it. Slormelines, Negation and True Potential you load yourself are left in place.
- **Floors.** The chance of success, Critical Reinforce and Double Reinforce every activation must reach at least, and the Failure it must stay under. None by default; set them when a risk is not worth any saving, and the plan is the cheapest way within them.
- **Slormite.** Use whichever slormite is in the Slormitizer, or choose one. More are loaded when it runs out, and lower slormites can be merged up into the one in use, as on Rick's Merge Slormites tab.
- **Load.** Puts the plan in the Slormitizer. Nothing is spent.
- **Reinforce once.** Loads the plan and activates the Slormitizer.
- **Repeat.** Keeps reinforcing until the item reaches the level you set, or the stock can no longer get there.

Nothing is activated unless the numbers the game itself shows are the ones the plan foresaw.

## Install

1. Back up your save: `%LocalAppData%\The_Slormancer`
2. Install [SlormAPI](https://github.com/tsotsas/SlormAPI/releases) 1.0.0 or later with its installer, which also sets up the Aurie loader. The mod needs it.
3. Download the latest `SlormReslormitizer` zip from [Releases](https://github.com/wistb/SlormMods/releases).
4. Extract it into the game folder, so `SlormReslormitizer.dll` lands in `mods\Aurie`.
5. Start the game from Steam as usual.

The game's own files are never changed. To uninstall, delete `mods\Aurie\SlormReslormitizer.dll`; SlormAPI keeps the settings in `mods\Aurie\SlormAPI\settings\SlormReslormitizer.json`.

## Build

Needs Windows with VS 2022 Build Tools (C++ workload), run from WSL.

```
./build.sh          # bin/SlormReslormitizer.dll
./build.sh deploy   # also copy into the game's mods/Aurie (set SLORM_GAME, or put it in the .env at the top of the repo)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/SlormReslormitizer-v<version>.zip
```

`SLORM_DEV=1` builds the dev variant, which logs the game's Slormitizer calls and samples its odds on F10.

Built on [Aurie](https://github.com/AurieFramework/Aurie), [YYToolkit](https://github.com/AurieFramework/YYToolkit) and [SlormAPI](https://github.com/tsotsas/SlormAPI); see [THIRD-PARTY.md](../../THIRD-PARTY.md). Licensed under AGPL-3.0.
