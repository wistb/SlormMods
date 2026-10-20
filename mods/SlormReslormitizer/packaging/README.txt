SlormReslormitizer
Operates Rick Manalan's Slormitizer in The Slormancer: loads the
Slormandrites that make reaching a reinforce level cheapest, and
reinforces.

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
  3. Extract this zip into it, so SlormReslormitizer.dll lands in
     mods\Aurie
  4. Start the game from Steam as usual

USE
  Open the Slormitizer at Rick Manalan and click the slormite left of
  its item slot: the options take the place of the character page, and
  the inventory beside them stays in use.

  Plan        The Slormandrites of Fate, Aptitude, Virtue and Harmony
              to load from your stock: the load that makes reaching
              the level you set the cheapest on average, counting
              goldus, Slormandrites and slormites at their purchase
              price. It knows a success spends the item's potential,
              a Critical Reinforce refills it, a failure zeroes it and
              a Double Reinforce skips a level.
  Floors      The chance of success, Critical Reinforce and Double
              Reinforce every activation must reach at least, and the
              Failure it must stay under. None by default; the plan is
              the cheapest way within them.
  Slormite    Use whichever slormite is in the Slormitizer, or choose
              one. More are loaded when it runs out, and lower
              slormites can be merged up into the one in use.
  Load        Puts the plan in the Slormitizer. Nothing is spent.
  Reinforce   Loads the plan and activates the Slormitizer once.
  once
  Repeat      Keeps reinforcing until the item reaches the level you
              set, or the stock can no longer get there.

  Nothing is activated unless the numbers the game itself shows are
  the ones the plan foresaw.

UNINSTALL
  Delete mods\Aurie\SlormReslormitizer.dll and
  SlormReslormitizer-README.txt from the game folder; SlormAPI keeps
  your settings in mods\Aurie\SlormAPI\data\SlormReslormitizer.json.
  The game's own files are never changed.

FILES IN THIS ZIP
  mods\Aurie\SlormReslormitizer.dll   the mod
  mods\licenses\                      license and source of the above

Created while playing: aurie.log, and through SlormAPI
mods\Aurie\SlormAPI\data\SlormReslormitizer.json (your settings).
