SlormHUD
Follow a slorm-planner build inside The Slormancer.

REQUIREMENTS
  The Slormancer on Steam (Windows, 64-bit)
  SlormAPI 1.0.0 or later, installed first with its installer, which
  also sets up the Aurie mod loader:
  https://github.com/tsotsas/SlormAPI/releases
  Microsoft Visual C++ 2015-2022 Redistributable (x64). Most PCs already
  have it. If the mod does not load, install it from
  https://aka.ms/vs/17/release/vc_redist.x64.exe

INSTALL
  1. Back up your save: %LocalAppData%\The_Slormancer
  2. Open the game folder
     (Steam: right-click the game > Manage > Browse local files)
  3. Extract this zip into it, so SlormHUD.dll lands in
     mods\Aurie
  4. Start the game from Steam as usual

USE
  Press F3 to show or hide the SlormHUD window, and F4 to show or hide the
  outlines on the game's screens; both keys can be changed on the Options
  tab, and both switches are also under Options > Mods in the game. On
  the Imports tab, paste a slorm-planner share link and press Import.

  Play a character of the build's class and open the ancestral legacy
  map, the skill screen or the inventory: the build is drawn on it.
  Green matches the build, orange is partly there, magenta is still to
  place, red is not in the build.

  To find warlords by their rewards, add a rule on the Nether tab, such
  as "at least 2 Ring", and open the Netherworld: each warlord that can
  reward that many legendaries of the slot has the count written under
  it. No build is needed for this.

UNINSTALL
  Delete mods\Aurie\SlormHUD.dll and SlormHUD-README.txt from the
  game folder. Your builds and options are in
  mods\Aurie\SlormAPI\data\SlormHUD.json and
  mods\Aurie\SlormAPI\settings\SlormHUD.json. The game's own files
  are never changed.

FILES IN THIS ZIP
  mods\Aurie\SlormHUD.dll             the mod
  mods\licenses\                      license and source of the above

Settings from before 1.3.0 (SlormHUD.txt and SlormHUD.layout.ini in
mods\Aurie) are taken over the first time; you can delete those files
afterwards.
