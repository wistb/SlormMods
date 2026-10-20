# SlormSuite

A collection of mods for The Slormancer, bundled into one. Each mod is off until turned on in the overlay (F7).

## Mods

### Macros

On the Macros tab. Presses skills for the hero by rules; the game still applies its own cost, cooldown and animation checks.

- **Priority.** Macros are tried from the top, and a higher one that can start takes over from a lower one.
- **Trigger.** Automatic, while a key is held, or switched by a key, with an option for standing still or moving and a wait between uses.
- **Conditions.** A tree of all of / any of / none of over life, mana, cooldowns, casting, in combat, enemies within a range, buffs on the hero (an aura that is on counts as one) and time since the macro fired, each with its live value.
- **Steps.** Press a skill or aura (tap or hold), wait, or wait until conditions hold.
- **Pause key.** F5 pauses and resumes every macro. Nothing is pressed in town or in a menu.
- **Save and share.** Saved by name, and exported or imported as one line of text.

### Farm

On the Farm tab. Plays expeditions for the hero, start to finish, and goes again from town. Needs Click to Move turned on in the game's options, and suits a build whose skills fire by themselves.

- **Run the expedition.** Switched by the tick box or a key (Home).
- **From town.** Walks to Almerolis and starts the chosen expedition, on the level the launcher was left on. Battlefield, The Slorm Temple, The Great Forge and The Netherworld.
- **Battlefield.** Closes breaches by the shortest way round on foot, opens the portal once enough are closed, and takes it. Breach V can be skipped.
- **The Slorm Temple.** Closes every breach of a floor, room after room, picks up pure slorm left on the ground, uses the shrine and takes the door. Can start each expedition on a floor you set.
- **The Great Forge.** Starts each wave, fights to the wave you set, opens the War Chest, picks up what it held and heads home. Needs "Auto pick Great Forge paths" on. The hero can hold the center of the room.
- **The Netherworld.** Runs the warlord or place you choose, or the one with the largest share of its rewards in the gear slots you want, in the ordinary or the Primordial Netherworld. Can run the place that gives double Nether Charge until the charge reaches a number you set, then the warlord. Closes breaches until the bar is full or the warlord is called, fights him, opens his chest and heads home.
- **A warlord's chest.** The pick is left to you, or made for you: a Nether item first, then by your slot priority, then a random one or the one worth the most goldus. Slorm can be spent on rolling every offer that is not a Nether item again, while a reserve you set is kept. With no Nether item on offer, the expedition can be abandoned instead, which leaves the warlords' rewards as they are; this can be kept to warlords with at least a number you set of rewards in your wanted slots.
- **Walking.** Finds the way round walls on the floor's map, and turns to the next place without stopping.
- **Loot.** Picks up what drops within a range; on the way to a breach, only what the detour keeps within it. A log lists it, reapers by name, with a switch for each kind of item.
- **Limits.** Stop after a number of expeditions, head home after a number of Battlefield or temple floors, and stop when the bag is full.

### Auto pick Great Forge paths

On the Farm tab, under Forge. When "Choose your Path" opens, the mod takes the path that stands highest in your priority lists.

- **Priority lists.** Rewards and challenges in lists, and lists inside lists, reordered by dragging. Higher lines win.
- **Want or avoid.** An avoided entry rules out its paths for every line below it.
- **Nothing listed.** Pick at random, leave it to you, always left, middle or right, or the longest challenge.
- **Delay and hold key.** A countdown shows the pick before it is made; holding a key (Left Ctrl) pauses it.
- **Pick log.** What was taken on each wave, and by what.
- **Save and share.** Lists are saved by name, and exported or imported as one line of text.

### Mark drawings on screen

On the ESP tab. Marks every open breach on the map.

- **Tiers.** Breach I, II, III by waves and Breach V for Chaos, each set to Mark, Avoid or Hide.
- **Markers.** A line from the hero, a circle, and name and distance text, colored by kind of breach.
- **Nearest breach.** An arrow by the hero pointing at it.
- **Hide key.** F8 shows or hides every drawing.
- **Reveal map.** The minimap and the large map show the whole map and its icons from the start of a floor. It has its own switch and works with the drawings off.

### Auto open chests

On the Mods tab. Uses a chest when the hero is close enough to do it by hand.

- **Chests** on expeditions are opened, then searched. A warlord's chest, where equipment is picked, is left alone.
- **War Chest** in the Great Forge is opened and emptied, once per visit.

### Auto unlock reaper drops

On the Mods tab. A reaper picked up is unlocked at once, without opening the character screen, and its screen is clicked away.

### Auto spend slorm on Ancestral Legacy

On the Mods tab. Used with the Ancestral Legacy open. The elemental damage the legacy grants grows with the slorm put into it, whichever nodes it goes to.

- **Spend slorm.** Buys ranks on nodes picked at random until no more can be afforded, keeping at least the slorm you set.
- **Give ranks back.** Gives up ranks on nodes picked at random until at least the slorm you set is back, leaving nodes in use alone.
- **Checked.** Each rank is checked against its price, and it stops if the game takes or gives anything else.

### Auto enter Temppopotamus' Bastion

On the Mods tab. Walking up to the Mage Academy entrance starts the Bastion without the menu.

- **Auto exit** takes the exit key and the portal when you reach them; holding a key (Left Ctrl) keeps you in to pick up loot.
- **Skip the result screen** closes it as soon as the game allows.

## Build

Needs VS 2022 Build Tools with the C++ workload, run from WSL.

```
./build.sh          # bin/SlormSuite.dll
./build.sh deploy   # also copies into the game's mods/Aurie (set SLORM_GAME, or put it in the .env at the top of the repo)
./build.sh play     # deploy, then start the game through Steam
./build.sh package  # dist/SlormSuite-v<version>.zip
```

A mod is one file in `source/features/`: a `Feature` subclass with a static instance.
