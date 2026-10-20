#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// An id with the rank the build puts in it: a skill upgrade or an ancestral legacy.
struct Ranked
{
	int id = 0;
	int rank = 0;
};

struct BuildSkill
{
	int id = 0;
	int level = 0;
	std::vector<Ranked> upgrades;
};

struct BuildAffix
{
	// N, D, M, R or E.
	std::string tier;
	std::string stat;
	// On the planner's full scale: 100 normal, 65 defense to rare, 40 epic.
	int roll = 0;
	bool pure = false;
};

struct BuildItem
{
	// The game's name for the slot; both rings are "ring".
	std::string slot;
	// Which equipment slot, in the game's order: helm, body, shoulder, bracer, glove, boot, both rings, amulet, belt, cape.
	int index = 0;
	std::string label;
	int level = 0;
	int reinforcement = 0;
	std::vector<BuildAffix> affixes;
	// Ids of the reaper smith, mastery skill, attribute and legendary; -1 for none.
	int smith = -1, smith_value = 0;
	int mastery = -1, mastery_value = 0;
	int attribute = -1, attribute_value = 0;
	int legendary = -1, legendary_value = 0;
};

struct BuildReaper
{
	int id = 0;
	bool primordial = false;
	int level = 0;
	int affinity = 0;
	int mastery = 0;
};

constexpr int ATTRIBUTE_COUNT = 8;
constexpr int RUNE_COUNT = 3;
constexpr int ACTIVABLE_COUNT = 4;

// A build imported from a slorm-planner link.
struct Build
{
	std::string name;
	// The link's last part, kept so the build can be saved and read again.
	std::string key;
	// 0 Knight, 1 Huntress, 2 Mage.
	int hero = 0;
	int level = 0;
	BuildReaper reaper;
	// Activation, effect, enhancement; id -1 for none.
	Ranked runes[RUNE_COUNT] = { { -1, 0 }, { -1, 0 }, { -1, 0 } };
	// Stones on the ancestral legacy map, and the legacies they turn on.
	int first_node = -1;
	std::vector<int> nodes;
	std::vector<Ranked> legacies;
	std::vector<BuildSkill> skills;
	// Equipped skill ids, -1 for none.
	int support = -1, primary = -1, secondary = -1;
	// Reaper or legendary activables below 200, ancestral legacies from 200; -1 for none.
	int activables[ACTIVABLE_COUNT] = { -1, -1, -1, -1 };
	std::vector<BuildItem> items;
	int ultimatum = -1, ultimatum_level = 0;
	int attributes[ATTRIBUTE_COUNT] = {};
};

// One row of dat_sta.json; columns kept as text.
struct StatInfo
{
	std::string ref;
	// name is label plus " %" for percent stats; label is the game's own text.
	std::string name;
	std::string label;
	std::map<std::string, std::string> columns;
};

// Overlay appearance, saved with the rest of the config.
struct Appearance
{
	int theme = 2;
	bool custom_accent = false;
	float accent[3] = { 0.78f, 0.62f, 0.25f };
	// Title bar, tabs, input fields and table headers.
	bool custom_primary = false;
	float primary[3] = { 0.16f, 0.29f, 0.48f };
	float opacity = 1.0f;
	float scale = 1.0f;
	// Virtual-key codes: one shows or hides the overlay, one the outlines on the game's screens.
	int hotkey = 0x72;
	int marks_hotkey = 0x73;
};

// Guards everything below; the game step writes it, the overlay reads and edits it.
extern std::recursive_mutex g_Lock;

extern Appearance g_Appearance;
// Set while the hotkey is being rebound, so the key press does not also toggle the overlay.
extern bool g_SuppressToggle;
extern bool g_Visible;
// Outline the build on the game's own screens.
extern bool g_Marks;

// The shapes the outlines come in, each with its own look.
enum OutlineKind { OUTLINE_LEGACY, OUTLINE_SOCKET, OUTLINE_PASSIVE, OUTLINE_UPGRADE, OUTLINE_SKILL, OUTLINE_BAR, OUTLINE_ATTRIBUTE, OUTLINE_REAPER, OUTLINE_RUNE, OUTLINE_GEAR, OUTLINE_PIP, OUTLINE_SPECIAL, OUTLINE_ULTIMATUM, OUTLINE_COUNT };
struct OutlineStyle
{
	// Line thickness, and how far outside the icon's edge the line starts; both in screen units.
	float width = 3;
	float grow = 0;
	// Moved across and down by this much.
	float x = 0;
	float y = 0;
};
extern OutlineStyle g_Outlines[OUTLINE_COUNT];
extern const OutlineStyle OUTLINE_DEFAULTS[OUTLINE_COUNT];
const char* OutlineName(int Kind);
// Where the rank text sits against its icon, per screen.
enum LabelKind { LABEL_LEGACY, LABEL_UPGRADE, LABEL_ATTRIBUTE, LABEL_NETHER, LABEL_COUNT };
enum LabelSide { SIDE_BOTTOM, SIDE_TOP, SIDE_LEFT, SIDE_RIGHT, SIDE_COUNT };
struct LabelStyle
{
	int side = SIDE_BOTTOM;
	// Moved from there by this much, in screen units.
	float x = 0;
	float y = 0;
};
extern LabelStyle g_Labels[LABEL_COUNT];
extern const LabelStyle LABEL_DEFAULTS[LABEL_COUNT];

// Colour of each Mark, as red, green, blue; indexed by the Mark's number, None unused.
constexpr int MARK_COUNT = 6;
extern float g_MarkColors[MARK_COUNT][3];
extern const float MARK_DEFAULTS[MARK_COUNT][3];

extern std::vector<Build> g_Builds;
// Build shown on the Build tab, -1 for none.
extern int g_ActiveBuild;
bool ParseBuild(const std::string& Text, Build& Out, std::string& Error);

// Asks for the settings to be saved from the next step.
void SaveConfig();
// The overlay's window layout, saved with the settings.
extern std::string g_Layout;

// One line of an equipped item: a stat in its tier, or the reaper smith, mastery, attribute or legendary by id.
struct LiveAffix
{
	// N, D, M, R, E for stats; RP, MA, AT, L for the others.
	std::string tier;
	std::string stat;
	int id = -1;
	// A stat's roll, or the value of the others.
	double roll = 0;
	// The best roll the game allows this stat on this item, 0 when not known.
	double best = 0;
	// A stat's purity: above 100 when pure.
	int pure = 0;
};

struct LiveItem
{
	bool present = false;
	// The game's name for the slot it goes in; both rings are "ring".
	std::string slot;
	int level = 0;
	std::vector<LiveAffix> affixes;
};

constexpr int GEAR_SLOTS = 11;

// The character being played, read from the game each step.
struct Live
{
	bool valid = false;
	int hero = 0;
	// Menu the game's UI shows: "none", "elements", ...
	std::string menu;
	// By legacy id.
	std::vector<int> legacy_ranks;
	// By stone slot: 1 for a stone, 2 for the first stone.
	std::vector<int> stones;
	// By realm, the tile a legacy sits on: not zero when stones turn it on.
	std::vector<int> realms;
	// By skill or upgrade id. An upgrade's rank; for a skill itself, its mastery experience.
	std::vector<int> skill_ranks;
	// By skill or upgrade id: above zero when equipped. Skills hold their slot, upgrades 1.
	std::vector<int> skill_slots;
	// The equipped reaper, -1 when unknown. Primordial ones are numbered after the plain ones.
	int reaper = -1;
	// The equipped ultimatum and its level, -1 for none.
	int ultimatum = -1;
	int ultimatum_level = 0;
	// By rune id: 1 when equipped, -1 when the game has not worked it out yet.
	std::vector<int> runes;
	// Equipped items by slot, in the game's order.
	std::vector<LiveItem> gear;
	// Points put into each attribute.
	std::vector<int> attributes;
	// The numbered slots of the skill bar, -1 when empty. Numbered as the build's activables are.
	std::vector<int> activables;
	// By warlord: the legendaries it can reward this hero with.
	std::map<int, std::vector<int>> nether_rewards;
};
extern Live g_Live;

// The Netherworld search: warlords whose possible rewards hold enough legendaries of a slot, named on the map.
struct NetherRule
{
	// Index into GEAR_SLOT_REFS; -1 for whichever slot has enough.
	int slot = 0;
	int count = 1;
};
extern bool g_NetherSearch;
// A warlord must meet every rule, not just one.
extern bool g_NetherAll;
extern std::vector<NetherRule> g_NetherRules;
constexpr int SLOT_KINDS = 10;
// The game's names for the slots, and the ones it shows.
extern const char* const GEAR_SLOT_REFS[SLOT_KINDS];
extern const char* const GEAR_SLOT_NAMES[SLOT_KINDS];
// Whether a warlord's rewards meet the search; Note lists the rules met, as "2 Ring".
bool NetherMatch(const std::vector<int>& Rewards, std::string& Note);

// How one part of the character stands against the build. Todo is in the build and not there yet;
// Wrong is there and not in the build, or in it with no rank; Partial has some ranks.
// Search is a warlord the Netherworld search found.
enum class Mark { None, Match, Partial, Wrong, Todo, Search };
// An upgrade of a skill. Rank and Wanted come back as the ranks put in and the build's, -1 when the build has none.
Mark UpgradeMark(const Build& Build, int Id, int& Rank, int& Wanted);
// A skill: whether it is equipped as the build has it, and its upgrades with it.
Mark SkillMark(const Build& Build, int Id);
// An attribute. Rank and Wanted come back as the points put in and the build's.
Mark AttributeMark(const Build& Build, int Id, int& Rank, int& Wanted);
// Reapers each class has; a primordial reaper's number is its plain one plus this.
constexpr int REAPER_COUNT = 120;
// The reaper the build has, numbered as the game numbers the equipped one.
inline int BuildReaperNumber(const Build& Build) { return Build.reaper.id + (Build.reaper.primordial ? REAPER_COUNT : 0); }
// An ultimatum: the build's and equipped at its level, equipped below it, not equipped, or equipped and not the build's.
Mark UltimatumMark(const Build& Build, int Id);
// A rune: in the build and equipped, in it and not, or equipped and not in it. None while the game has no answer.
Mark RuneMark(const Build& Build, int Id);
// A stat the build has on an item: Match in the same tier at the build's roll, Todo when it is not on the item,
// Partial when it is there in another tier, at a lower roll, or not pure where the build's is; Note then says which.
Mark GearStatMark(const BuildItem& Item, const BuildAffix& Affix, std::string& Note);
// The item's reaper smith, mastery, attribute or legendary, by tier code and id: Partial below the build's value.
Mark GearSpecialMark(const BuildItem& Item, const char* Tier, int Id, int Value, std::string& Note);
// The whole item: Match with everything the build lists, Partial with some, Todo with none or an empty slot.
Mark GearMark(const BuildItem& Item);
// The game's slot numbers for the two attack skills.
constexpr int SLOT_PRIMARY = 2;
constexpr int SLOT_SECONDARY = 3;
// Each class's first three skills are its support skills.
constexpr int SUPPORT_SKILLS = 3;
// True when the skill is equipped, but in the other attack slot than the build has it.
bool SkillInWrongSlot(const Build& Build, int Id);

#ifdef SLORM_DEV
// Dev builds only: write the game's variables to SlormHUD.dump.txt on the next step.
extern bool g_WantDump;
extern std::string g_DumpNote;
extern std::string g_DumpResult;
// Dumps without moving the mouse.
constexpr int DUMP_KEY = 0x78;
#endif

inline std::string Lower(std::string Text)
{
	for (char& c : Text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return Text;
}

extern std::vector<StatInfo> g_Stats;
void LoadGameData();
const StatInfo* FindStat(const std::string& Ref);
const StatInfo* StatById(int Id);
std::string StatName(const std::string& Ref);
const char* HeroName(int Class);
// Skills and their upgrades share one id space per class.
std::string SkillName(int Hero, int Id);
// The skill an upgrade belongs to, -1 when it is not an upgrade.
int UpgradeParent(int Hero, int Id);
// The game draws its "upgrade" kind in a square frame and its "passive" kind in a round one.
bool UpgradeIsSquare(int Hero, int Id);
std::string LegacyName(int Id);
// The tile of the ancestral legacy map a legacy sits on, -1 when unknown.
int LegacyRealm(int Id);
std::string ReaperName(int Id, int Hero, bool Primordial);
// Where the reaper sits in the game's list, counted from zero; -1 when unknown.
int ReaperOrder(int Id);
std::string RuneName(int Id);
std::string ActivableName(int Id);
std::string AttributeName(int Id);
std::string SmithName(int Id);
std::string LegendaryName(int Id);
// Whether any of the legendary's values rolls in a range; the rest are fixed whatever the item's roll says.
bool LegendaryHasRange(int Id);
// Whether any shown value of the legendary differs between two rolls; small values round to the same number.
bool LegendaryGains(int Id, double From, double To);
std::string UltimatumName(int Id);
// The slot a legendary goes in, as an index into GEAR_SLOT_REFS; -1 when unknown.
int LegendarySlot(int Id);
// A warlord of the Netherworld map, by its number in dat_war.json.
struct WarlordInfo
{
	std::string name;
	// On the map, in the table's own units.
	double x = 0, y = 0;
};
const std::map<int, WarlordInfo>& Warlords();

bool OverlayInstall(YYTK::YYTKInterface* Yytk);
bool OverlayWantsMouse();
bool OverlayWantsKeys();
