#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <SlormAPI/SlormAPI_Interface.hpp>
#include <SlormAPI/Json.hpp>
#include <deque>
#include <optional>
#include <string>
#include <vector>

extern YYTK::YYTKInterface* g_Yytk;
extern SlormAPIInterface* g_Api;
constexpr const char* MOD_NAME = "SlormReslormitizer";

template <typename... Args>
void Log(const char* Format, Args... Arguments)
{
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, (std::string("[SlormReslormitizer] ") + Format).c_str(), Arguments...);
}

// Numbers the game keeps as text count too; 0 for anything else.
double ToNumber(const YYTK::RValue& Value);
std::string ToText(const YYTK::RValue& Value);
// -1 for anything but an array.
int Length(const YYTK::RValue& Array);
YYTK::RValue At(const YYTK::RValue& Array, int Index);
void Put(const YYTK::RValue& Array, int Index, const YYTK::RValue& Value);
// A global, or failing that a variable of the UI object.
YYTK::RValue Variable(YYTK::CInstance* Self, const char* Name);
// A game script by its name without the gml_Script_ prefix; null when the game has none.
YYTK::PFUNC_YYGMLScript FindScript(const char* Name);
YYTK::RValue CallScript(YYTK::PFUNC_YYGMLScript Script, YYTK::CInstance* Self, std::vector<YYTK::RValue> Arguments = {});

// Material ids as the game numbers them: 0-4 are the Slormelines.
enum Material { FATE = 5, NEGATION = 6, TRUE_POTENTIAL = 7, APTITUDE = 8, HARMONY = 9, VIRTUE = 10, MATERIAL_COUNT = 11 };
const char* MaterialName(int Id);
// Slormites come in tiers 1 to 15.
constexpr int SLORMITE_TIERS = 15;
const char* SlormiteName(int Tier);

// What the Slormitizer shows for what is in it.
struct Odds
{
	double chance = 0, critical = 0, critical_potential = 0, twice = 0, failure = 0, cost = 0;
};

struct Loaded
{
	int slot = 0;
	int id = 0;
	double count = 0;
};

// The Slormitizer as last read from the game.
struct Machine
{
	// Rick Manalan's panel is open.
	bool open = false;
	bool has_item = false;
	int level = 0;
	double potential = 0;
	// 0 without a slormite.
	int tier = 0;
	double slormites = 0;
	std::vector<Loaded> loaded;
	Odds shown;
	// The same without Slormandrites of Fate, Aptitude, Harmony and Virtue.
	Odds base;
	// The chance of success before failure cuts it, without those four.
	double raw_chance = 0;
	double stock[MATERIAL_COUNT] = {};
	double slormite_stock[SLORMITE_TIERS + 1] = {};
	double gold = 0;
};

// The Slormandrites to load for the targets.
struct Plan
{
	bool possible = false;
	// The optimizer is still at work on another thread; why says so, and the run waits for it.
	bool planning = false;
	std::string why;
	int count[MATERIAL_COUNT] = {};
	Odds odds;
	// Goldus the Slormandrites would cost to buy.
	double value = 0;
	// With the optimal load: goldus, materials and slormites at purchase price, until the level is reached.
	double expected_cost = 0;
	double expected_activations = 0;
};

struct Settings
{
	// Floors every activation must respect; none by default.
	int chance = 0;
	int critical = 0;
	int twice = 0;
	int failure = 100;
	// Repeating stops at this reinforce level.
	int until_level = 15;
	// The slormite tier to use; 0 for whichever is in the machine.
	int slormite = 0;
	// Put more of them in when the machine runs out.
	bool reload = true;
	// With none left, merge lower slormites into it.
	bool merge_up = false;
};

// Everything below runs on the game thread: the step and SlormAPI's events.
extern Settings g_Settings;
extern Machine g_Machine;
extern Plan g_Plan;
extern bool g_Running;
// Asked for by the panel, done on the next step.
extern bool g_WantLoad, g_WantOnce, g_WantRun, g_WantStop, g_WantMerge;
extern std::string g_Status;
extern std::deque<std::string> g_Notes;

// The loaded Slormandrites match the plan.
bool PlanLoaded();
// There is enough in the machine, or chosen, to start.
bool CanStart();
// What the game shows is what the plan foresaw.
bool TargetsMet();

sj::Json Command(const char* Name);
// The request's result; nothing when SlormAPI is missing or refuses it (logged).
std::optional<sj::Json> Request(const sj::Json& Command);
// Registers the mod and its settings with SlormAPI; false without SlormAPI.
bool PanelStart();
// Takes the clicks on the panel before the game's step sees them.
void PanelInput(YYTK::CInstance* Self);
// Closes the panel when Rick's or the character page closes; from here, tooltips wait for the panel.
void PanelTick(YYTK::CInstance* Self);
// After the UI's draw event: the switch, the panel, and the tooltips held back since the step.
void PanelDraw(YYTK::CInstance* Self);

#ifdef SLORM_DEV
// Dev builds only: logs how the game runs the Slormitizer and samples its odds.
constexpr int PROBE_KEY = 0x79;
void ProbeStart(Aurie::AurieModule* Module, const std::string& Folder);
void ProbeTick(YYTK::CInstance* Self);
#endif
