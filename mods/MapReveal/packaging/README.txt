Map Reveal
Shows the whole map from the start of a floor in The Slormancer.

REQUIREMENTS
  The Slormancer on Steam (Windows, 64-bit)
  The Aurie mod loader, installed first. The easiest way is SlormAPI's
  installer: https://github.com/tsotsas/SlormAPI/releases
  Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs already
  have it. If the mod does not load, install it from
  https://aka.ms/vs/17/release/vc_redist.x64.exe

INSTALL
  1. Back up your save: %LocalAppData%\The_Slormancer
  2. Open the game folder
     (Steam: right-click the game > Manage > Browse local files)
  3. Extract this zip into it, so MapReveal.dll lands in mods\Aurie
  4. Start the game from Steam as usual

USE
  Nothing to set up. The minimap and the large map show every corridor
  and icon as soon as a floor begins, instead of what the hero has
  walked past. With SlormAPI installed, the mod is listed on the game's
  MODS page and has a switch under Options > Mods. Switching it off
  takes the icons away at once; the map clears again on the next floor.

UNINSTALL
  Delete mods\Aurie\MapReveal.dll and MapReveal-README.txt from the
  game folder. Your setting is in
  mods\Aurie\SlormAPI\settings\Map_Reveal.json. The game's own files
  are never changed.

FILES IN THIS ZIP
  mods\Aurie\MapReveal.dll     the mod
  mods\licenses\               license and source of the above
