#include "Reslormitizer.hpp"
#include "Version.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <sstream>
using namespace Aurie;
using namespace YYTK;

YYTKInterface* g_Yytk = nullptr;

static PFUNC_YYGMLScript g_Result = nullptr;
static PFUNC_YYGMLScript g_Potential = nullptr;
static PFUNC_YYGMLScript g_Move = nullptr;
static PFUNC_YYGMLScript g_Activate = nullptr;
static PFUNC_YYGMLScript g_Merge = nullptr;

Settings g_Settings;
Machine g_Machine;
Plan g_Plan;
bool g_Running = false;
bool g_WantLoad = false, g_WantOnce = false, g_WantRun = false, g_WantStop = false, g_WantMerge = false;
std::string g_Status = "idle";
std::deque<std::string> g_Notes;

// Slots of global.inventory[hero]: the Slormitizer's item, its slormite, and its modifiers up to the last slot.
constexpr int ITEM_SLOT = 667, SLORMITE_SLOT = 668, FIRST_MODIFIER = 669;
// Where the stacks of slormites and materials sit.
constexpr int FIRST_STACK = 75, LAST_STACK = 112;
// The Slormitizer takes this many kinds of modifier.
constexpr int MODIFIER_KINDS = 3;
// The game loads this many slormites at a time; merging makes as many.
constexpr int MERGE_BATCH = 100;

// Per unit, from the game's values: what a Slormandrite adds, and what one costs to buy.
struct Effects
{
	bool known = false;
	double fate_chance = 1, fate_critical = 1, fate_twice = 1, fate_failure = 1;
	double virtue_twice = 5, virtue_failure = 3;
	double aptitude_critical = 5;
	double harmony_failure = 1;
	// Slormites of one tier that merge into one of the next.
	int merge_number = 3;
	// Failure for each level a slormite is short of the item, and the tier that is never short by more than one.
	double underlevel = 50;
	int max_tier = 15;
	double price[MATERIAL_COUNT] = {};
};
static Effects g_Effects;

// Stack slots found on the last read; -1 without a stack.
static int g_StackSlot[MATERIAL_COUNT];
static int g_SlormiteSlot[SLORMITE_TIERS + 1];
static RValue g_Hero;

// A run: load, check what the game shows, reinforce, and again.
enum Step { IDLE, LOAD, CHECK, WAIT };
static Step g_Step = IDLE;
// Load only, one reinforcement, or until the level asked for.
enum Mode { ONLY_LOAD, ONCE, REPEAT };
static Mode g_Mode = ONCE;
// Steps a load has been waiting for the game to show what was moved.
static int g_LoadTries = 0;
// The Harmony count was set by trying it against the game's numbers, past what the plan said.
static bool g_Trimmed = false;
// The slormite tier a run keeps in the machine, and steps spent waiting for the game to show one.
static int g_RunTier = 0;
static int g_SlormiteTries = 0;
static CInstance* g_Self = nullptr;
static int g_Wait = 0;
static int g_LevelBefore = 0;

double ToNumber(const RValue& Value)
{
	if (Value.m_Kind == VALUE_STRING)
		return atof(Value.ToString().c_str());
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET || !Value.IsNumberConvertible())
		return 0;
	return Value.ToDouble();
}

std::string ToText(const RValue& Value)
{
	return Value.IsString() ? Value.ToString() : "";
}

int Length(const RValue& Array)
{
	if (!Array.IsArray())
		return -1;
	return static_cast<int>(g_Yytk->CallBuiltin("array_length", { Array }).ToDouble());
}

RValue At(const RValue& Array, int Index)
{
	if (Index < 0 || Index >= Length(Array))
		return RValue();
	return g_Yytk->CallBuiltin("array_get", { Array, RValue(static_cast<double>(Index)) });
}

void Put(const RValue& Array, int Index, const RValue& Value)
{
	g_Yytk->CallBuiltin("array_set", { Array, RValue(static_cast<double>(Index)), Value });
}

RValue Variable(CInstance* Self, const char* Name)
{
	if (g_Yytk->CallBuiltin("variable_global_exists", { RValue(Name) }).ToBoolean())
		return g_Yytk->CallBuiltin("variable_global_get", { RValue(Name) });
	if (Self && g_Yytk->CallBuiltin("variable_instance_exists", { RValue(Self), RValue(Name) }).ToBoolean())
		return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) });
	return RValue();
}

PFUNC_YYGMLScript FindScript(const char* Name)
{
	CScript* script = nullptr;
	std::string full = std::string("gml_Script_") + Name;
	AurieStatus status = g_Yytk->GetNamedRoutinePointer(full.c_str(), reinterpret_cast<PVOID*>(&script));
	if (!AurieSuccess(status) || !script || !script->m_Functions)
		return nullptr;
	return script->m_Functions->m_ScriptFunction;
}

RValue CallScript(PFUNC_YYGMLScript Script, CInstance* Self, std::vector<RValue> Arguments)
{
	RValue result;
	if (!Script)
		return result;
	std::vector<RValue*> pointers;
	for (RValue& argument : Arguments) pointers.push_back(&argument);
	Script(Self, Self, result, static_cast<int>(pointers.size()), pointers.data());
	return result;
}

const char* MaterialName(int Id)
{
	static const char* names[MATERIAL_COUNT] = {
		"Normal Slormeline", "Magic Slormeline", "Rare Slormeline", "Epic Slormeline", "Legendary Slormeline",
		"Fate", "Negation", "True Potential", "Aptitude", "Harmony", "Virtue",
	};
	return Id >= 0 && Id < MATERIAL_COUNT ? names[Id] : "unknown";
}

const char* SlormiteName(int Tier)
{
	static const char* names[SLORMITE_TIERS + 1] = {
		"none", "Incomplete Inferior Slormite", "Inferior Slormite Chunk", "Inferior Flawless Slormite", "Incomplete Modest Slormite", "Modest Slormite Chunk",
		"Modest Flawless Slormite", "Incomplete Greater Slormite", "Greater Slormite Chunk", "Greater Flawless Slormite", "Incomplete Superior Slormite",
		"Superior Slormite Chunk", "Superior Flawless Slormite", "Incomplete Ancestral Slormite", "Ancestral Slormite Chunk", "Ancestral Flawless Slormite",
	};
	return Tier >= 0 && Tier <= SLORMITE_TIERS ? names[Tier] : "unknown";
}

// The four the plan chooses; the rest are left as loaded.
static bool Planned(int Id)
{
	return Id == FATE || Id == APTITUDE || Id == HARMONY || Id == VIRTUE;
}

// Also shown in the overlay.
static void Note(const char* Format, ...)
{
	char text[256];
	va_list arguments;
	va_start(arguments, Format);
	vsnprintf(text, sizeof(text), Format, arguments);
	va_end(arguments);
	Log("%s", text);
	g_Notes.emplace_back(text);
	if (g_Notes.size() > 60)
		g_Notes.pop_front();
}

// The game keeps lists as "a|b|c".
static std::vector<double> Numbers(const RValue& Value)
{
	std::vector<double> out;
	if (!Value.IsString())
	{
		out.push_back(ToNumber(Value));
		return out;
	}
	std::istringstream parts(Value.ToString());
	std::string part;
	while (std::getline(parts, part, '|'))
		out.push_back(atof(part.c_str()));
	return out;
}

static void ReadEffects(CInstance* Self)
{
	RValue values = Variable(Self, "values");
	auto find = [&](const std::string& Key) { return g_Yytk->CallBuiltin("ds_map_find_value", { values, RValue(Key) }); };
	std::vector<double> fate = Numbers(find("Craft Slormandrite of Fate Effect"));
	std::vector<double> virtue = Numbers(find("Craft Slormandrite of Virtue Effect"));
	std::vector<double> aptitude = Numbers(find("Craft Slormandrite of Aptitude Effect"));
	std::vector<double> harmony = Numbers(find("Craft Slormandrite of Harmony Effect"));
	// Fate: reinforce, critical, critical potential, double, failure.
	if (fate.size() < 5 || virtue.size() < 2 || aptitude.size() < 1 || harmony.size() < 1 || fate[1] <= 0)
		return;
	Effects& e = g_Effects;
	e.fate_chance = fate[0], e.fate_critical = fate[1], e.fate_twice = fate[3], e.fate_failure = fate[4];
	e.virtue_twice = virtue[0], e.virtue_failure = virtue[1];
	e.aptitude_critical = aptitude[0];
	e.harmony_failure = harmony[0];
	if (double under = ToNumber(find("Craft Slormite Underlevel")); under > 0) e.underlevel = under;
	if (double number = ToNumber(find("Slormite Merge Number")); number >= 2) e.merge_number = static_cast<int>(number);
	if (double tier = ToNumber(find("Slormite Max Tier")); tier > 0) e.max_tier = static_cast<int>(tier);
	for (int id = FATE; id < MATERIAL_COUNT; id++)
		e.price[id] = ToNumber(find(std::string("Slormandrite of ") + MaterialName(id) + " Purchase Price"));
	e.known = true;
	Log("effects per unit: Fate %g/%g/%g, Virtue %g/%g, Aptitude %g, Harmony %g", e.fate_critical, e.fate_twice, e.fate_failure, e.virtue_twice, e.virtue_failure, e.aptitude_critical, e.harmony_failure);
}

// A stack of slormites (tier 1) or materials (tier 0): [["trash", tier, id, "slormite", count, ...]].
static bool IsStack(const RValue& Slot, int Tier, RValue& Header)
{
	if (Length(Slot) < 1)
		return false;
	Header = At(Slot, 0);
	return Length(Header) >= 5 && ToText(At(Header, 0)) == "trash" && ToText(At(Header, 3)) == "slormite" && ToNumber(At(Header, 1)) == Tier;
}

// A field keeps the kind it had: the game stores most numbers as text.
static RValue Like(const RValue& Old, double Number)
{
	if (!Old.IsString())
		return RValue(Number);
	char text[32];
	snprintf(text, sizeof(text), "%.0f", Number);
	return RValue(text);
}

static Odds Preview(CInstance* Self)
{
	RValue result = CallScript(g_Result, Self, { RValue(1.0) });
	Odds odds;
	if (Length(result) < 6)
		return odds;
	odds.chance = ToNumber(At(result, 0));
	odds.critical = ToNumber(At(result, 1));
	odds.critical_potential = ToNumber(At(result, 2));
	odds.twice = ToNumber(At(result, 3));
	odds.failure = ToNumber(At(result, 4));
	odds.cost = ToNumber(At(result, 5));
	return odds;
}

// Read again after every move: the game may have put a new list in place of the old one.
static void FetchHero()
{
	g_Hero = At(Variable(g_Self, "inventory"), static_cast<int>(ToNumber(Variable(g_Self, "current_hero"))));
}

static void Read(CInstance* Self)
{
	g_Self = Self;
	Machine& m = g_Machine;
	m.open = ToText(Variable(Self, "menu_citizen")) == "crafter";
	if (!m.open)
		return;
	if (!g_Effects.known)
		ReadEffects(Self);

	m.has_item = false;
	m.tier = 0;
	m.slormites = 0;
	m.loaded.clear();
	std::fill(std::begin(m.stock), std::end(m.stock), 0.0);
	std::fill(std::begin(g_StackSlot), std::end(g_StackSlot), -1);
	std::fill(std::begin(m.slormite_stock), std::end(m.slormite_stock), 0.0);
	std::fill(std::begin(g_SlormiteSlot), std::end(g_SlormiteSlot), -1);
	m.gold = ToNumber(Variable(Self, "gold"));

	FetchHero();
	int slots = Length(g_Hero);
	if (slots <= FIRST_MODIFIER)
		return;

	RValue header;
	for (int slot = FIRST_STACK; slot <= LAST_STACK; slot++)
	{
		RValue stack = At(g_Hero, slot);
		if (IsStack(stack, 1, header))
		{
			int tier = static_cast<int>(ToNumber(At(header, 2)));
			if (tier >= 1 && tier <= SLORMITE_TIERS)
			{
				m.slormite_stock[tier] = ToNumber(At(header, 4));
				g_SlormiteSlot[tier] = slot;
			}
			continue;
		}
		if (!IsStack(stack, 0, header))
			continue;
		int id = static_cast<int>(ToNumber(At(header, 2)));
		if (id < 0 || id >= MATERIAL_COUNT)
			continue;
		m.stock[id] = ToNumber(At(header, 4));
		g_StackSlot[id] = slot;
	}

	RValue item = At(g_Hero, ITEM_SLOT);
	if (ToText(At(At(item, 0), 3)) == "equipment")
	{
		// Level, victims, victims needed, percent.
		RValue potential = CallScript(g_Potential, Self, { item });
		m.has_item = true;
		m.level = static_cast<int>(ToNumber(At(potential, 0)));
		m.potential = ToNumber(At(potential, 3));
	}
	if (IsStack(At(g_Hero, SLORMITE_SLOT), 1, header))
	{
		m.tier = static_cast<int>(ToNumber(At(header, 2)));
		m.slormites = ToNumber(At(header, 4));
	}
	std::vector<RValue> headers;
	for (int slot = FIRST_MODIFIER; slot < slots; slot++)
	{
		if (!IsStack(At(g_Hero, slot), 0, header))
			continue;
		m.loaded.push_back({ slot, static_cast<int>(ToNumber(At(header, 2))), ToNumber(At(header, 4)) });
		headers.push_back(header);
	}

	m.shown = m.base = Odds();
	if (!m.has_item || !m.tier)
		return;
	m.shown = Preview(Self);
	// What the game would show without the planned kinds: their counts are put back at once.
	std::vector<RValue> counts;
	for (size_t i = 0; i < headers.size(); i++)
	{
		counts.push_back(At(headers[i], 4));
		if (Planned(m.loaded[i].id))
			Put(headers[i], 4, Like(counts[i], 0));
	}
	m.base = Preview(Self);
	for (size_t i = 0; i < headers.size(); i++)
		Put(headers[i], 4, counts[i]);
	// The game shows no more than 100 failure; a slormite under the item's level has more to take off.
	if (m.base.failure >= 100 && m.tier < g_Effects.max_tier && m.level >= m.tier)
		m.base.failure = (std::max)(m.base.failure, g_Effects.underlevel * (m.level - m.tier + 1));
	// The chance before failure cuts it: potential, plus 25 for each tier the slormite is over the next level.
	if (m.base.failure < 100) m.raw_chance = m.base.chance / (1 - (std::max)(m.base.failure, 0.0) / 100);
	else m.raw_chance = m.potential + 25.0 * (std::max)(0, m.tier - m.level - 1);
}

// The cheapest counts that reach the targets. Fate moves all three, so every count of it is tried
// with the least of the others that still gets there.
static void MakePlan()
{
	const Machine& m = g_Machine;
	const Effects& e = g_Effects;
	Plan plan;
	if (!m.open) plan.why = "open the Slormitizer";
	else if (!m.has_item) plan.why = "put an item in the Slormitizer";
	else if (!m.tier) plan.why = "put a slormite in the Slormitizer";
	else if (!e.known) plan.why = "the game's values were not read";
	if (!plan.why.empty())
	{
		g_Plan = plan;
		return;
	}

	int kept = 0;
	for (const Loaded& loaded : m.loaded)
		if (!Planned(loaded.id)) kept++;
	double need_critical = std::clamp(g_Settings.critical, 0, 100) - m.base.critical;
	double need_twice = std::clamp(g_Settings.twice, 0, 100) - m.base.twice;
	double target_failure = std::clamp(g_Settings.failure, 0, 100);
	double target_chance = std::clamp(g_Settings.chance, 0, 100);
	auto units = [](double Need, double Each) { return Need <= 0 || Each <= 0 ? 0 : static_cast<int>(ceil(Need / Each - 1e-9)); };

	int most_fate = (std::max)({ units(need_critical, e.fate_critical), units(need_twice, e.fate_twice), units(100 - m.raw_chance, e.fate_chance) });
	bool short_stock = false, short_slots = false;
	double best = -1;
	for (int fate = 0; fate <= most_fate; fate++)
	{
		// Failure takes its share off the chance, so a chance target may ask for less failure than its own.
		double raw = (std::min)(m.raw_chance + fate * e.fate_chance, 100.0);
		if (raw < target_chance - 1e-9)
			continue;
		double failure = target_chance > 0 ? (std::min)(target_failure, 100 * (1 - target_chance / raw)) : target_failure;

		int count[MATERIAL_COUNT] = {};
		count[FATE] = fate;
		count[APTITUDE] = units(need_critical - fate * e.fate_critical, e.aptitude_critical);
		count[VIRTUE] = units(need_twice - fate * e.fate_twice, e.virtue_twice);
		count[HARMONY] = units(m.base.failure + fate * e.fate_failure + count[VIRTUE] * e.virtue_failure - failure, e.harmony_failure);

		int kinds = kept;
		double value = 0;
		bool in_stock = true;
		for (int id = FATE; id < MATERIAL_COUNT; id++)
		{
			if (!count[id])
				continue;
			kinds++;
			value += count[id] * e.price[id];
			// What is loaded already came out of the stack.
			double have = m.stock[id];
			for (const Loaded& loaded : m.loaded)
				if (loaded.id == id) have += loaded.count;
			if (count[id] > have) in_stock = false;
		}
		if (kinds > MODIFIER_KINDS) { short_slots = true; continue; }
		if (!in_stock) { short_stock = true; continue; }
		if (best >= 0 && value >= best)
			continue;
		best = value;
		plan.possible = true;
		std::copy(std::begin(count), std::end(count), std::begin(plan.count));
		plan.value = value;
	}
	if (!plan.possible)
		plan.why = short_stock ? "not enough Slormandrites in stock" : short_slots ? "the targets need more kinds than the Slormitizer has slots" : "no way to reach the targets";
	else
	{
		plan.odds = m.base;
		plan.odds.critical += plan.count[FATE] * e.fate_critical + plan.count[APTITUDE] * e.aptitude_critical;
		plan.odds.twice += plan.count[FATE] * e.fate_twice + plan.count[VIRTUE] * e.virtue_twice;
		plan.odds.failure += plan.count[FATE] * e.fate_failure + plan.count[VIRTUE] * e.virtue_failure - plan.count[HARMONY] * e.harmony_failure;
		plan.odds.chance = (std::min)(m.raw_chance + plan.count[FATE] * e.fate_chance, 100.0) * (1 - std::clamp(plan.odds.failure, 0.0, 100.0) / 100);
	}
	g_Plan = plan;
}

static bool Meets(const Odds& Odds);

bool CanStart()
{
	const Machine& m = g_Machine;
	if (!m.open || !m.has_item)
		return false;
	return m.tier ? g_Plan.possible || (g_Settings.slormite && g_Settings.slormite != m.tier) : g_Settings.slormite != 0;
}

bool PlanLoaded()
{
	if (!g_Plan.possible)
		return false;
	double loaded[MATERIAL_COUNT] = {};
	for (const Loaded& entry : g_Machine.loaded)
		if (entry.id >= 0 && entry.id < MATERIAL_COUNT) loaded[entry.id] += entry.count;
	for (int id = FATE; id < MATERIAL_COUNT; id++)
		if (Planned(id) && loaded[id] != g_Plan.count[id]) return false;
	return true;
}

bool TargetsMet()
{
	return Meets(g_Machine.shown);
}

// The modifier slot holding a material, read fresh; -1 when none does.
static int LoadedSlot(int Id, RValue& Header)
{
	FetchHero();
	for (int slot = FIRST_MODIFIER; slot < Length(g_Hero); slot++)
		if (IsStack(At(g_Hero, slot), 0, Header) && ToNumber(At(Header, 2)) == Id) return slot;
	return -1;
}

enum LoadResult { LOADED, PENDING, FAILED };

// Puts exactly this many of a material in the Slormitizer. The game's own move loads a batch;
// the difference goes back on the stack.
static LoadResult LoadMaterial(CInstance* Self, int Id, int Count, std::string& Why)
{
	RValue header, stack;
	int slot = LoadedSlot(Id, header);
	int stack_slot = g_StackSlot[Id];
	if (Count == 0)
	{
		if (slot < 0)
			return LOADED;
		Log("load: taking %s out of slot %d", MaterialName(Id), slot);
		CallScript(g_Move, Self, { RValue(static_cast<double>(slot)), RValue(-1.0) });
		return LoadedSlot(Id, header) < 0 ? LOADED : PENDING;
	}
	if (slot < 0)
	{
		if (stack_slot < 0) { Why = std::string("no ") + MaterialName(Id) + " in stock"; return FAILED; }
		// The move may only show a step later; it is not asked for twice.
		if (g_LoadTries == 0)
			CallScript(g_Move, Self, { RValue(static_cast<double>(stack_slot)), RValue(-1.0) });
		slot = LoadedSlot(Id, header);
		if (slot < 0)
			return PENDING;
	}
	double loaded = ToNumber(At(header, 4));
	bool stacked = stack_slot >= 0 && IsStack(At(g_Hero, stack_slot), 0, stack) && ToNumber(At(stack, 2)) == Id;
	double left = stacked ? ToNumber(At(stack, 4)) : 0;
	Log("load: %s in slot %d holds %.0f, wanted %d, stack %.0f", MaterialName(Id), slot, loaded, Count, left);
	if (loaded == Count)
		return LOADED;

	double rest = left + loaded - Count;
	if (rest < 0) { Why = std::string("not enough ") + MaterialName(Id); return FAILED; }
	if (stacked)
	{
		if (rest > 0) Put(stack, 4, Like(At(stack, 4), rest));
		else Put(g_Hero, stack_slot, RValue(0.0));
	}
	else if (rest > 0)
	{
		// The move took the whole stack: make one again for what is not used.
		if (stack_slot < 0 || Length(At(g_Hero, stack_slot)) > 0) { Why = std::string("nowhere to put back ") + MaterialName(Id); return FAILED; }
		RValue copy = g_Yytk->CallBuiltin("array_create", { RValue(static_cast<double>(Length(header))) });
		for (int i = 0; i < Length(header); i++)
			Put(copy, i, At(header, i));
		Put(copy, 4, Like(At(header, 4), rest));
		RValue item = g_Yytk->CallBuiltin("array_create", { RValue(1.0) });
		Put(item, 0, copy);
		Put(g_Hero, stack_slot, item);
	}
	Put(header, 4, Like(At(header, 4), Count));
	return LOADED;
}

static LoadResult LoadPlan(CInstance* Self, std::string& Why)
{
	if (!g_Plan.possible) { Why = g_Plan.why; return FAILED; }
	bool pending = false;
	// Unwanted kinds leave first, to free their slots.
	for (int pass = 0; pass < 2; pass++)
		for (int id = FATE; id < MATERIAL_COUNT; id++)
		{
			if (!Planned(id) || (g_Plan.count[id] == 0) != (pass == 0))
				continue;
			LoadResult result = LoadMaterial(Self, id, g_Plan.count[id], Why);
			if (result == FAILED) return FAILED;
			if (result == PENDING) pending = true;
		}
	if (!pending)
		return LOADED;
	if (++g_LoadTries > 30) { Why = "the game did not take the Slormandrites"; return FAILED; }
	return PENDING;
}

static bool Meets(const Odds& Odds)
{
	return Odds.critical >= (std::min)(g_Settings.critical, 100) - 1e-6 && Odds.twice >= (std::min)(g_Settings.twice, 100) - 1e-6
		&& Odds.failure <= (std::max)(g_Settings.failure, 0) + 1e-6 && Odds.chance >= (std::min)(g_Settings.chance, 100) - 1e-6;
}

// When the plan's numbers were off: the least Harmony the game itself says is enough, by trying
// counts in the slot. False when no amount in stock does it.
static LoadResult TrimHarmony(CInstance* Self, std::string& Why)
{
	RValue header, stack;
	int slot = LoadedSlot(HARMONY, header);
	if (slot < 0)
	{
		if (g_StackSlot[HARMONY] < 0) { Why = "no Harmony in stock"; return FAILED; }
		if (g_LoadTries++ == 0)
			CallScript(g_Move, Self, { RValue(static_cast<double>(g_StackSlot[HARMONY])), RValue(-1.0) });
		if (g_LoadTries > 30) { Why = "the game did not take the Harmony"; return FAILED; }
		return PENDING;
	}
	RValue old = At(header, 4);
	int stack_slot = g_StackSlot[HARMONY];
	double most = ToNumber(old) + (stack_slot >= 0 && IsStack(At(g_Hero, stack_slot), 0, stack) && static_cast<int>(ToNumber(At(stack, 2))) == HARMONY ? ToNumber(At(stack, 4)) : 0);
	auto meets = [&](double Count)
	{
		Put(header, 4, Like(old, Count));
		return Meets(Preview(Self));
	};
	int low = 0, high = static_cast<int>(most);
	if (!meets(high))
	{
		Odds odds = Preview(Self);
		Put(header, 4, old);
		char text[160];
		snprintf(text, sizeof(text), "even %d Harmony leaves chance %.0f%%, failure %.0f%%: this slormite is too low for the item", high, odds.chance, odds.failure);
		Why = text;
		return FAILED;
	}
	while (low < high)
	{
		int middle = (low + high) / 2;
		if (meets(middle)) high = middle;
		else low = middle + 1;
	}
	Put(header, 4, old);
	Log("trim: Harmony %d by the game's numbers", high);
	g_LoadTries = 1;
	return LoadMaterial(Self, HARMONY, (std::max)(high, 1), Why);
}

// Makes up to this many slormites of a tier by merging the tiers under it, lowest first, as Rick's
// Merge Slormites tab does. Returns how many the plan made; 0 when the lower tiers do not hold enough.
static int MergeUp(CInstance* Self, int Tier, int Most)
{
	const Machine& m = g_Machine;
	int each = g_Effects.merge_number;
	if (!g_Merge || Tier < 2 || Tier > SLORMITE_TIERS)
		return 0;
	// What each tier must make so the one above has enough; the lowest tier is made of fragments and is left alone.
	int make[SLORMITE_TIERS + 2] = {};
	auto plan = [&](int Count)
	{
		std::fill(std::begin(make), std::end(make), 0);
		make[Tier] = Count;
		for (int tier = Tier - 1; tier >= 1; tier--)
		{
			double needed = static_cast<double>(make[tier + 1]) * each - m.slormite_stock[tier];
			if (needed <= 0)
				return true;
			if (tier == 1 || needed > 1e9)
				return false;
			make[tier] = static_cast<int>(needed);
		}
		return true;
	};
	int low = 0, high = Most;
	while (low < high)
	{
		int middle = (low + high + 1) / 2;
		if (plan(middle)) low = middle;
		else high = middle - 1;
	}
	if (low < 1 || !plan(low))
		return 0;

	RValue selected = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("crafter_selected_slormite") });
	for (int tier = 2; tier <= Tier; tier++)
	{
		if (!make[tier])
			continue;
		// The game's list counts from 0.
		g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("crafter_selected_slormite"), RValue(static_cast<double>(tier - 1)) });
		CallScript(g_Merge, Self, { RValue(static_cast<double>(make[tier])) });
		Log("merge: %d x %s", make[tier], SlormiteName(tier));
	}
	g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("crafter_selected_slormite"), selected });
	return low;
}

// Merges, then checks the stock went up by what was made.
static bool MergeChecked(CInstance* Self, int Tier, int Most)
{
	// With the Slormitizer empty the game puts what it made straight in.
	auto held = [&]() { return g_Machine.slormite_stock[Tier] + (g_Machine.tier == Tier ? g_Machine.slormites : 0); };
	double before = held();
	int made = MergeUp(Self, Tier, Most);
	if (!made)
		return false;
	Read(Self);
	double got = held() - before;
	if (got != made)
	{
		Note("merging gave %.0f %s, expected %d", got, SlormiteName(Tier), made);
		return false;
	}
	Note("merged %d x %s", made, SlormiteName(Tier));
	return true;
}

static void Stop(const char* Reason)
{
	g_Running = false;
	g_Step = IDLE;
	g_Status = Reason;
	Note("stopped: %s", Reason);
}

static void Run(CInstance* Self)
{
	const Machine& m = g_Machine;
	if (!m.open) { Stop("the Slormitizer was closed"); return; }
	if (g_Wait > 0) { g_Wait--; return; }

	switch (g_Step)
	{
	case LOAD:
	{
		if (!m.has_item) { Stop("no item in the Slormitizer"); return; }
		if (g_Mode == REPEAT && m.level >= g_Settings.until_level) { Stop("reached the level asked for"); return; }
		// The wrong slormite goes back to its stack; an empty slot gets the one the run uses.
		if (m.tier && g_RunTier && m.tier != g_RunTier)
		{
			if (g_SlormiteTries++ > 30) { Stop("the game did not take the slormite back"); return; }
			CallScript(g_Move, Self, { RValue(static_cast<double>(SLORMITE_SLOT)), RValue(-1.0) });
			g_Wait = 1;
			return;
		}
		if (!m.tier)
		{
			if (!g_RunTier) { Stop("no slormite in the Slormitizer"); return; }
			if (!g_Settings.reload && !g_Settings.slormite) { Stop("out of slormites in the Slormitizer"); return; }
			if (g_SlormiteSlot[g_RunTier] < 0)
			{
				// One load's worth at a time.
				if (!g_Settings.merge_up || !MergeChecked(Self, g_RunTier, MERGE_BATCH)) { Stop((std::string("no ") + SlormiteName(g_RunTier) + " left").c_str()); return; }
				g_Wait = 1;
				return;
			}
			if (g_SlormiteTries++ > 30) { Stop("the game did not take the slormite"); return; }
			CallScript(g_Move, Self, { RValue(static_cast<double>(g_SlormiteSlot[g_RunTier])), RValue(-1.0) });
			g_Wait = 1;
			return;
		}
		g_SlormiteTries = 0;
		g_RunTier = m.tier;
		std::string why;
		LoadResult result = LoadPlan(Self, why);
		if (result == FAILED) { Stop(why.c_str()); return; }
		if (result == PENDING)
			return;
		g_Step = CHECK;
		g_Trimmed = false;
		g_LoadTries = 0;
		g_Wait = 1;
		return;
	}
	case CHECK:
	{
		if (!g_Trimmed && !PlanLoaded()) { Stop("the Slormitizer does not hold what was loaded"); return; }
		if (!TargetsMet() && !g_Trimmed)
		{
			std::string why;
			LoadResult result = TrimHarmony(Self, why);
			if (result == FAILED) { Stop(why.c_str()); return; }
			if (result == LOADED) { g_Trimmed = true; g_Wait = 2; }
			return;
		}
		if (g_Mode == ONLY_LOAD) { Stop(TargetsMet() ? "loaded" : "loaded, but the game's numbers miss the targets"); return; }
		if (!TargetsMet())
		{
			Note("game shows chance %.0f, critical %.0f, double %.0f, failure %.0f", m.shown.chance, m.shown.critical, m.shown.twice, m.shown.failure);
			Stop("the game's numbers miss the targets; nothing was spent");
			return;
		}
		if (m.gold < m.shown.cost) { Stop("not enough goldus"); return; }
		g_LevelBefore = m.level;
		Note("reinforcing +%d: chance %.0f%%, critical %.0f%%, double %.0f%%, failure %.0f%%", m.level, m.shown.chance, (std::min)(m.shown.critical, 100.0), (std::min)(m.shown.twice, 100.0), (std::max)(m.shown.failure, 0.0));
		CallScript(g_Activate, Self, { RValue(m.shown.cost) });
		g_Step = WAIT;
		g_Wait = 1;
		return;
	}
	case WAIT:
	{
		Note(m.level > g_LevelBefore ? "now +%d" : "failed, still +%d", m.level);
		if (g_Mode == ONCE) { Stop("done"); return; }
		g_Step = LOAD;
		g_LoadTries = 0;
		return;
	}
	default:
		Stop("done");
	}
}

static void Tick(CInstance* Self)
{
	// Reading takes a few hundred calls into the game, so not every step while nothing happens.
	static int since = 0;
	bool asked = g_WantLoad || g_WantOnce || g_WantRun || g_WantStop || g_WantMerge;
	if (g_Running || asked || ++since >= 6)
	{
		since = 0;
		Read(Self);
		MakePlan();
	}

	if (g_WantStop && g_Running)
		Stop("stopped by you");
	else if (g_WantMerge && !g_Running && g_Machine.open)
	{
		int tier = g_Settings.slormite ? g_Settings.slormite : g_Machine.tier;
		if (!tier) g_Status = "choose a slormite to merge up to";
		else if (!MergeChecked(Self, tier, MERGE_BATCH)) g_Status = std::string("the lower slormites cannot make a ") + SlormiteName(tier);
		else g_Status = "merged";
	}
	else if ((g_WantLoad || g_WantOnce || g_WantRun) && !g_Running && g_Machine.open)
	{
		g_Running = true;
		g_Mode = g_WantLoad ? ONLY_LOAD : g_WantOnce ? ONCE : REPEAT;
		g_Step = LOAD;
		g_Wait = g_LoadTries = g_SlormiteTries = 0;
		g_RunTier = g_Settings.slormite ? g_Settings.slormite : g_Machine.tier;
		g_Status = "running";
	}
	g_WantLoad = g_WantOnce = g_WantRun = g_WantStop = g_WantMerge = false;

	if (g_Running)
		Run(Self);
}

// Runs once per frame, after the UI object's Step event.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the events are known.
	static CCode* step_code = nullptr;
	static CCode* draw_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
	if (!code)
		return;
	if (code != step_code && code != draw_code)
	{
		std::string_view name = code->GetName() ? code->GetName() : "";
		if (!step_code && name == "gml_Object_obj_ui_next_gen_Step_0") step_code = code;
		else if (!draw_code && name == "gml_Object_obj_ui_next_gen_Draw_64") draw_code = code;
		else return;
	}
	if (code == draw_code)
	{
		if (!Event.CalledOriginal()) Event.Call();
		PanelDraw(self);
		return;
	}

	// Modules load in any order, so SlormAPI is asked for from the first step, not at load.
	static bool tried = false;
	if (!g_Api)
	{
		if (tried)
			return;
		tried = true;
		if (!PanelStart())
		{
			DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormReslormitizer] SlormAPI is not loaded; the mod needs it");
			return;
		}
		Log("connected to SlormAPI");
	}

	// Another mod may have run the event already; running it twice doubles every click.
	PanelInput(self);
	if (!Event.CalledOriginal()) Event.Call();
	Tick(self);
	PanelTick(self);
#ifdef SLORM_DEV
	ProbeTick(self);
#endif
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_Result = FindScript("scr_crafter_craft_result");
	g_Potential = FindScript("scr_get_potential");
	g_Move = FindScript("scr_inventory_move_item");
	g_Activate = FindScript("scr_crafter_activate_craft");
	g_Merge = FindScript("scr_crafter_slormite_merge");
	if (!g_Result || !g_Potential || !g_Move || !g_Activate)
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormReslormitizer] game scripts not found");
		return AURIE_OBJECT_NOT_FOUND;
	}
#ifdef SLORM_DEV
	ProbeStart(Module, ModulePath.parent_path().string());
#endif

	AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormReslormitizer] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" SLORMRESLORMITIZER_VERSION " loaded");
	return AURIE_SUCCESS;
}
