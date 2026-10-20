# SlormHUD

Follow a slorm-planner build inside [The Slormancer](https://store.steampowered.com/app/1104280/The_Slormancer/).

SlormHUD imports a build from a [slorm-planner](https://cayrac.github.io/slorm-planner/) share link and marks it on the game's own screens, so you can see at a glance what your character still needs. It only reads. It does not edit the character, items, or save.

![The ancestral legacy map with the build's stones and legacies marked](nexus/legacy.png)

## Features

Four colors are used everywhere: **green** matches the build, **orange** is partly there, **magenta** is still to place, **red** is not in the build.

### Ancestral legacy

Rings on the stone sockets and legacies, with each legacy's rank against the build's, such as `0 < 10`. They follow the map as you pan and zoom.

### Skills

Outlines on the skill row, the specializations and the upgrade grid, with ranks where they differ. The slot pickers on the skill bar frame what the build has in that slot.

<details>
<summary>Screenshot</summary>

![The skill screen with upgrades outlined](nexus/skills.png)

</details>

### Attributes

An outline on each attribute, its points against the build's, and a tick on its bar where the build stops.

<details>
<summary>Screenshot</summary>

![The attribute page](nexus/attributes.png)

</details>

### Gear, runes and ultimatum

A frame on each equipment slot, the reaper, the runes and the ultimatum. On an item's tooltip, a pip beside each stat and a diamond on its reaper smith, mastery and attribute, with rolls checked against the build's. It works on bag items too.

<details>
<summary>Screenshots</summary>

![The gear page, with the rune list at the forge](nexus/gear.png)

![An item's tooltip with a pip beside each stat and diamonds on its special stats](nexus/tooltip.png)

</details>

### Reaper

The build's reaper is framed in the collection, even before you own it.

<details>
<summary>Screenshot</summary>

![The reaper collection with the build's reaper framed](nexus/reapers.png)

</details>

### Netherworld search

On the Nether tab, add rules such as "at least 2 Ring" or "at least 3 Any slot". Every warlord on the Netherworld map whose possible rewards hold that many legendaries of the slot gets the count written under it, such as `2 Ring`, and the tab lists them by name with their rewards. It needs no build.

### Builds

Paste a share link on the Imports tab. Several builds can be kept; the selected one is drawn. The Build tab lists the whole build with the same colors and what is missing on each line.

<details>
<summary>Screenshot</summary>

![The Imports tab](nexus/imports.png)

</details>

### Options

Thickness, size and position of every kind of outline, where the rank and Netherworld text sits, the colors, themes, and two hotkeys. A right click or double click on a slider resets it.

<details>
<summary>Screenshots</summary>

![Outline options](nexus/options-outlines.png) ![Color options](nexus/options-colors.png)

![Rank text options](nexus/options-text.png) ![Window options](nexus/options-window.png)

</details>

## Install

Requires The Slormancer on Steam (Windows, 64-bit) and the [Microsoft Visual C++ 2015-2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

1. Back up your save folder: `%LocalAppData%\The_Slormancer`
2. If the game does not load Aurie mods yet, install the Aurie loader. The easiest way is the installer from [SlormAPI](https://github.com/tsotsas/SlormAPI/releases).
3. Download the latest `SlormHUD` zip from [Releases](https://github.com/wistb/SlormMods/releases).
4. Open the game folder. In Steam: right-click the game, Manage, Browse local files.
5. Extract the zip there, so `SlormHUD.dll` lands in `mods\Aurie`.
6. Start the game from Steam as usual.

The mod changes none of the game's files. SlormHUD runs alongside [SlormReforger](../SlormReforger) 1.0.1 or later.

To uninstall, delete `mods\Aurie\SlormHUD.dll` and `SlormHUD-README.txt` from the game folder.

## Use

1. Press F3 to show or hide the window. On the Imports tab, paste a slorm-planner share link and press Import.
2. Play a character of the build's class. Open the ancestral legacy map, the skill screen or the inventory, and the build is drawn on it.
3. Work toward green. The Build tab lists the same comparison, section by section.

F4 shows or hides the outlines and the Netherworld text on the game's screens. Both keys can be changed on the Options tab.

Mastery levels, reaper and rune levels, item level, reinforcement and pure stats are not compared.

## How it works

The Slormancer is a GameMaker game. The mod is a plugin for [YYToolkit](https://github.com/AurieFramework/YYToolkit), which runs on the [Aurie Framework](https://github.com/AurieFramework/Aurie). SlormHUD reads the character from the game's own variables, and the outlines are drawn with the game's drawing functions at the end of its interface pass, placed from the list of clickable areas the game keeps for each screen. The window is drawn with [Dear ImGui](https://github.com/ocornut/imgui).

The frameworks are not part of the mod; [SlormAPI](https://github.com/tsotsas/SlormAPI)'s installer sets them up.

## Building

Needs Windows with the Visual Studio 2022 Build Tools (C++ workload). The build script runs from WSL.

```sh
./build.sh           # build bin/SlormHUD.dll
./build.sh deploy    # build, then copy the mod into the game's mods/Aurie
./build.sh play      # deploy, then start the game through Steam
./build.sh package   # build, then write dist/SlormHUD-v<version>.zip
```

`deploy` needs the game folder: export `SLORM_GAME`, or put `SLORM_GAME="..."` in the `.env` at the top of the repo.

Where the vendored sources come from is listed in [THIRD-PARTY.md](../../THIRD-PARTY.md).

## License

[AGPL-3.0](../../LICENSE). The mod is built against Aurie and YYToolkit, which are AGPL-3.0.

## Credits

Aurie Framework and YYToolkit by the AurieFramework team. Dear ImGui by Omar Cornut. The share link format is from [slorm-planner](https://github.com/cayrac/slorm-planner) by cayrac.
