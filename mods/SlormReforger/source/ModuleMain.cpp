#include "Reforger.hpp"
#include "Version.hpp"
#include <SlormAPI/SlormAPI_Interface.hpp>
#include <SlormAPI/Json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
// As shown on SlormAPI's MODS page, and the name of its settings and data file.
constexpr const char* MOD_NAME = "SlormReforger";
static SlormAPIInterface* g_Api = nullptr;
// The old settings file, imported once.
static fs::path g_ConfigPath;

static PFUNC_YYGMLScript g_StatFromScore = nullptr;
static PFUNC_YYGMLScript g_Apply = nullptr;
static PFUNC_YYGMLScript g_CurrencySpend = nullptr;
static PFUNC_YYGMLScript g_RollStats = nullptr;

std::recursive_mutex g_Lock;

std::vector<Target> g_Targets;
int g_MaxAttempts = 50;
bool g_AllowPureLoss = false;
int g_MinStock[MATERIAL_COUNT] = {};
bool g_AutoLock = false;
bool g_AutoAdd = false;
bool g_MoveTiers = false;
bool g_AutoImport = false;
Appearance g_Appearance;
bool g_SuppressToggle = false;
// The switch as Options > Mods last showed it.
static bool g_SettingVisible = true;
static bool g_WantSave = false;
std::string g_Layout;

bool g_Visible = true;
bool g_Manual = false;
bool g_Running = false;
bool g_WantStart = false;
bool g_WantStop = false;
int g_Attempts = 0;
std::string g_Status = "idle";
std::deque<std::string> g_Notes;

std::map<std::string, std::vector<PoolEntry>> g_Pools;
bool g_HasItem = false;
static bool g_PanelOpen = false;
std::vector<Affix> g_Item;
std::string g_ItemSlot;
int g_ItemLevel = 0;
std::vector<Recipe> g_Recipes;
double g_Stock[MATERIAL_COUNT] = {};
double g_Gold = 0;

static bool g_KeyWasDown = false;
static int g_Cooldown = 0;
static int g_Refresh = 0;

// The blacksmith's reforge slot in global.inventory[hero].
static constexpr size_t REFORGE_SLOT = 666;

template <typename... Args>
static void Log(const char* Format, Args... Arguments)
{
	DbgPrintEx(LOG_SEVERITY_INFO, (std::string("[SlormReforger] ") + Format).c_str(), Arguments...);
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

static double ToNumber(const RValue& Value)
{
	if (Value.m_Kind == VALUE_STRING)
	{
		try { return std::stod(Value.ToString()); }
		catch (...) { return 0; }
	}
	return Value.IsNumberConvertible() ? Value.ToDouble() : 0;
}

// Item layout: [header, [tier, stat, roll, locked, pure], ...]
static bool ReadItem(const RValue& Item, std::vector<Affix>& Out)
{
	if (!Item.IsArray())
		return false;
	std::vector<RValue> parts = Item.ToVector();
	if (parts.empty() || !parts[0].IsArray())
		return false;

	std::vector<RValue> header = parts[0].ToVector();
	g_ItemLevel = header.size() > 2 ? static_cast<int>(ToNumber(header[2])) : 0;
	g_ItemSlot = header.size() > 1 ? header[1].ToString() : "";

	Out.clear();
	for (size_t i = 1; i < parts.size(); i++)
	{
		if (!parts[i].IsArray())
			continue;
		std::vector<RValue> fields = parts[i].ToVector();
		if (fields.size() >= 3 && fields[0].IsString() && IsSpecialTier(fields[0].ToString()))
		{
			Affix affix;
			affix.tier = fields[0].ToString();
			affix.stat = Lower(affix.tier) + "_" + std::to_string(static_cast<int>(ToNumber(fields[1])));
			affix.roll = affix.shown = ToNumber(fields[2]);
			affix.has_shown = true;
			affix.index = static_cast<int>(i);
			Out.push_back(affix);
			continue;
		}
		if (fields.size() >= 4 && fields[0].IsString() && IsLegendaryTier(fields[0].ToString()))
		{
			Affix affix;
			affix.tier = "L";
			affix.stat = "leg_" + std::to_string(static_cast<int>(ToNumber(fields[1])));
			affix.roll = affix.shown = ToNumber(fields[2]);
			affix.has_shown = true;
			affix.locked = ToNumber(fields[3]) != 0;
			affix.index = static_cast<int>(i);
			Out.push_back(affix);
			continue;
		}
		if (fields.size() < 5 || !fields[0].IsString() || !fields[1].IsString())
			continue;
		Affix affix;
		affix.tier = fields[0].ToString();
		affix.stat = fields[1].ToString();
		affix.roll = ToNumber(fields[2]);
		affix.locked = ToNumber(fields[3]) != 0;
		affix.pure = ToNumber(fields[4]);
		affix.index = static_cast<int>(i);
		Out.push_back(affix);
	}
	return true;
}

static bool IsItem(const RValue& Value)
{
	if (!Value.IsArray())
		return false;
	std::vector<RValue> parts = Value.ToVector();
	return parts.size() > 1 && parts[0].IsArray();
}

// The game's own "Possible Outcomes": scr_loot_roll_stats(item, tier, 1) lists
// [min roll, max roll, stat, ?, already on item, ?] without rolling anything.
static void ReadPools(CInstance* Self, CInstance* Other, const RValue& Item)
{
	static std::string last;
	std::string signature = g_ItemSlot + ' ' + std::to_string(g_ItemLevel);
	for (const Affix& affix : g_Item)
		signature += ' ' + affix.tier + affix.stat;
	if (signature == last || !g_RollStats)
		return;
	last = signature;

	g_Pools.clear();
	for (const char* tier : { "N", "D", "M", "R", "E" })
	{
		RValue arguments[3] = { Item, RValue(tier), RValue(1.0) };
		RValue* pointers[3] = { &arguments[0], &arguments[1], &arguments[2] };
		RValue list;
		g_RollStats(Self, Other, list, 3, pointers);
		if (!list.IsArray())
			continue;
		for (const RValue& row : list.ToVector())
		{
			if (!row.IsArray())
				continue;
			std::vector<RValue> fields = row.ToVector();
			if (fields.size() < 5 || !fields[2].IsString())
				continue;
			g_Pools[tier].push_back({ fields[2].ToString(), ToNumber(fields[0]), ToNumber(fields[1]), ToNumber(fields[4]) != 0 });
		}
	}
}

// The item in the reforge slot and its hero's index: -1 when no single hero has one there,
// -2 while there is no inventory to read.
static int FindSlotItem(RValue& Item)
{
	RValue inventory = g_Yytk->CallBuiltin("variable_global_get", { RValue("inventory") });
	if (!inventory.IsArray())
		return -2;

	std::vector<RValue> heroes = inventory.ToVector();
	int owner = -1;
	int found = 0;
	for (size_t h = 0; h < heroes.size(); h++)
	{
		const RValue& hero = heroes[h];
		if (!hero.IsArray())
			continue;
		// Fetch the one slot; copying all 674 is slow.
		if (ToNumber(g_Yytk->CallBuiltin("array_length", { hero })) <= REFORGE_SLOT)
			continue;
		RValue slot = g_Yytk->CallBuiltin("array_get", { hero, RValue(static_cast<double>(REFORGE_SLOT)) });
		if (IsItem(slot))
		{
			Item = slot;
			owner = static_cast<int>(h);
			found++;
		}
	}
	// More than one hero has an item parked in the slot; can't tell which is open.
	return found == 1 ? owner : -1;
}

// Reads the item in the reforge slot, with the values the game would display.
static bool ReadSlotItem(CInstance* Self, CInstance* Other)
{
	RValue item;
	int owner = FindSlotItem(item);
	if (owner < 0 || !ReadItem(item, g_Item))
		return false;
	g_HeroClass = owner;

	std::vector<RValue> parts = item.ToVector();
	for (Affix& affix : g_Item)
	{
		size_t i = static_cast<size_t>(affix.index);
		if (IsSpecialTier(affix.tier) || IsLegendaryTier(affix.tier) || i >= parts.size() || !parts[i].IsArray())
			continue;
		std::vector<RValue> fields = parts[i].ToVector();
		if (fields.size() < 5)
			continue;

		// Arguments: roll, stat, tier, index, level, item.
		RValue arguments[6] = { fields[2], fields[1], fields[0], RValue(static_cast<double>(i)), RValue(static_cast<double>(g_ItemLevel)), item };
		RValue* pointers[6] = { &arguments[0], &arguments[1], &arguments[2], &arguments[3], &arguments[4], &arguments[5] };
		RValue shown;
		g_StatFromScore(Self, Other, shown, 6, pointers);

		affix.shown = ToNumber(shown);
		affix.has_shown = true;
	}
	ReadPools(Self, Other, item);
	return true;
}

static void ResetConfig()
{
	g_Targets.clear();
	g_MaxAttempts = 50;
	g_AllowPureLoss = false;
	std::fill(std::begin(g_MinStock), std::end(g_MinStock), 0);
	g_AutoLock = false;
	g_AutoAdd = false;
	g_MoveTiers = false;
	g_AutoImport = false;
	g_Presets.clear();
	g_ActivePreset = -1;
	g_Appearance = Appearance();
	g_Layout.clear();
}

// Before 1.3.0 the settings were in SlormReforger.txt and the window layout in SlormReforger.layout.ini, next to the DLL.
static bool ImportOldConfig()
{
	std::ifstream file(g_ConfigPath);
	if (!file)
		return false;

	std::string line;
	while (std::getline(file, line))
	{
		std::istringstream words(line);
		std::string first;
		if (!(words >> first) || first[0] == '#')
			continue;
		if (first == "max_attempts") { words >> g_MaxAttempts; continue; }
		// "min_stock N" from older configs applies to every material; "min_stock ID N" to one.
		if (first == "min_stock")
		{
			int a = 0, b = 0;
			words >> a;
			if (words >> b) { if (a >= 0 && a < MATERIAL_COUNT) g_MinStock[a] = b; }
			else std::fill(std::begin(g_MinStock), std::end(g_MinStock), a);
			continue;
		}
		if (first == "auto_lock") { int flag = 0; words >> flag; g_AutoLock = flag != 0; continue; }
		if (first == "auto_add") { int flag = 0; words >> flag; g_AutoAdd = flag != 0; continue; }
		if (first == "move_tiers") { int flag = 0; words >> flag; g_MoveTiers = flag != 0; continue; }
		if (first == "auto_import") { int flag = 0; words >> flag; g_AutoImport = flag != 0; continue; }
		if (first == "active_preset") { words >> g_ActivePreset; continue; }
		if (first == "preset")
		{
			// preset KEY NAME
			std::string key, name, error;
			words >> key;
			std::getline(words >> std::ws, name);
			Preset preset;
			if (ParsePreset(key, preset, error))
			{
				if (!name.empty()) preset.name = name;
				g_Presets.push_back(std::move(preset));
			}
			continue;
		}
		if (first == "theme") { words >> g_Appearance.theme; continue; }
		if (first == "accent") { int on = 0; words >> on >> g_Appearance.accent[0] >> g_Appearance.accent[1] >> g_Appearance.accent[2]; g_Appearance.custom_accent = on != 0; continue; }
		if (first == "primary") { int on = 0; words >> on >> g_Appearance.primary[0] >> g_Appearance.primary[1] >> g_Appearance.primary[2]; g_Appearance.custom_primary = on != 0; continue; }
		if (first == "opacity") { words >> g_Appearance.opacity; continue; }
		if (first == "scale") { words >> g_Appearance.scale; continue; }
		if (first == "hotkey") { words >> g_Appearance.hotkey; if (g_Appearance.hotkey < 8 || g_Appearance.hotkey > 254) g_Appearance.hotkey = VK_F6; continue; }
		if (first == "show_costs") { int flag = 1; words >> flag; g_Appearance.show_costs = flag != 0; continue; }
		if (first == "allow_pure_loss") { int flag = 0; words >> flag; g_AllowPureLoss = flag != 0; continue; }
		Target target;
		target.tier = first;
		std::string goal;
		if (!(words >> target.stat))
			continue;
		if (!(words >> goal))
			goal = "any";
		try
		{
			if (goal == "any") target.goal = Goal::Any;
			else if (goal == "max") target.goal = Goal::MaxRoll;
			else if (goal.rfind("roll:", 0) == 0) { target.goal = Goal::Roll; target.amount = std::stod(goal.substr(5)); }
			else if (goal.rfind("share:", 0) == 0) { target.goal = Goal::Share; target.amount = std::stod(goal.substr(6)); }
			else { target.goal = Goal::Value; target.amount = std::stod(goal); }
		}
		catch (...) { continue; }
		g_Targets.push_back(target);
	}
	std::ifstream layout(g_ConfigPath.parent_path() / "SlormReforger.layout.ini");
	g_Layout.assign(std::istreambuf_iterator<char>(layout), std::istreambuf_iterator<char>());
	return true;
}

static double NumberAt(const sj::Json& Array, size_t At, double Default)
{
	return At < Array.a.size() && Array.a[At].type == sj::Json::Num ? Array.a[At].n : Default;
}

// Floats as JSON numbers without their binary tail.
static double Rounded(float Value)
{
	return std::round(Value * 10000.0) / 10000.0;
}

static sj::Json ColorJson(const float* Color)
{
	sj::Json color = sj::Json::Array();
	for (int at = 0; at < 3; at++)
		color.Push(Rounded(Color[at]));
	return color;
}

static void ReadColor(const sj::Json* Saved, float* Color)
{
	if (Saved)
		for (int at = 0; at < 3; at++)
			Color[at] = static_cast<float>(NumberAt(*Saved, at, Color[at]));
}

static const char* const GOAL_NAMES[] = { "any", "max", "value", "roll", "share" };

// Over the defaults: anything missing or out of range keeps its default.
static void ApplyConfig(const sj::Json& Config)
{
	if (const sj::Json* targets = Config.Find("targets"))
		for (const sj::Json& saved : targets->a)
		{
			Target target;
			target.tier = saved.GetStr("tier");
			target.stat = saved.GetStr("stat");
			std::string goal = saved.GetStr("goal", "any");
			auto known = std::find(std::begin(GOAL_NAMES), std::end(GOAL_NAMES), goal);
			if (target.tier.empty() || target.stat.empty() || known == std::end(GOAL_NAMES))
				continue;
			target.goal = static_cast<Goal>(known - std::begin(GOAL_NAMES));
			target.amount = saved.GetNum("amount", 0);
			g_Targets.push_back(target);
		}
	if (const sj::Json* presets = Config.Find("presets"))
		for (const sj::Json& saved : presets->a)
		{
			Preset preset;
			std::string error;
			if (!ParsePreset(saved.GetStr("key"), preset, error))
				continue;
			std::string name = saved.GetStr("name");
			if (!name.empty()) preset.name = name;
			g_Presets.push_back(std::move(preset));
		}
	g_ActivePreset = static_cast<int>(Config.GetNum("active_preset", g_ActivePreset));
	// Indexed by material id.
	if (const sj::Json* stock = Config.Find("min_stock"))
		for (int id = 0; id < MATERIAL_COUNT; id++)
			g_MinStock[id] = static_cast<int>(NumberAt(*stock, id, g_MinStock[id]));
	g_MaxAttempts = static_cast<int>(Config.GetNum("max_attempts", g_MaxAttempts));
	g_AllowPureLoss = Config.GetBool("allow_pure_loss", g_AllowPureLoss);
	g_AutoLock = Config.GetBool("auto_lock", g_AutoLock);
	g_AutoAdd = Config.GetBool("auto_add", g_AutoAdd);
	g_MoveTiers = Config.GetBool("move_tiers", g_MoveTiers);
	g_AutoImport = Config.GetBool("auto_import", g_AutoImport);
	g_Appearance.theme = static_cast<int>(Config.GetNum("theme", g_Appearance.theme));
	g_Appearance.custom_accent = Config.GetBool("custom_accent", false);
	ReadColor(Config.Find("accent"), g_Appearance.accent);
	g_Appearance.custom_primary = Config.GetBool("custom_primary", false);
	ReadColor(Config.Find("primary"), g_Appearance.primary);
	g_Appearance.opacity = static_cast<float>(Config.GetNum("opacity", g_Appearance.opacity));
	g_Appearance.scale = static_cast<float>(Config.GetNum("scale", g_Appearance.scale));
	g_Appearance.show_costs = Config.GetBool("show_costs", g_Appearance.show_costs);
	double hotkey = Config.GetNum("hotkey", VK_F6);
	g_Appearance.hotkey = hotkey >= 8 && hotkey <= 254 ? static_cast<int>(hotkey) : VK_F6;
	g_Layout = Config.GetStr("layout");
	if (g_ActivePreset < -1 || g_ActivePreset >= static_cast<int>(g_Presets.size()))
		g_ActivePreset = g_Presets.empty() ? -1 : 0;
}

static sj::Json ConfigJson()
{
	sj::Json config = sj::Json::Object();
	sj::Json targets = sj::Json::Array();
	for (const Target& target : g_Targets)
	{
		sj::Json saved = sj::Json::Object();
		saved.Add("tier", target.tier);
		saved.Add("stat", target.stat);
		saved.Add("goal", GOAL_NAMES[static_cast<int>(target.goal)]);
		if (target.goal != Goal::Any && target.goal != Goal::MaxRoll)
			saved.Add("amount", target.amount);
		targets.Push(std::move(saved));
	}
	config.Add("targets", std::move(targets));
	sj::Json presets = sj::Json::Array();
	for (const Preset& preset : g_Presets)
	{
		sj::Json saved = sj::Json::Object();
		saved.Add("key", preset.key);
		saved.Add("name", preset.name);
		presets.Push(std::move(saved));
	}
	config.Add("presets", std::move(presets));
	config.Add("active_preset", g_ActivePreset);
	sj::Json stock = sj::Json::Array();
	for (int reserve : g_MinStock)
		stock.Push(reserve);
	config.Add("min_stock", std::move(stock));
	config.Add("max_attempts", g_MaxAttempts);
	config.Add("allow_pure_loss", g_AllowPureLoss);
	config.Add("auto_lock", g_AutoLock);
	config.Add("auto_add", g_AutoAdd);
	config.Add("move_tiers", g_MoveTiers);
	config.Add("auto_import", g_AutoImport);
	config.Add("theme", g_Appearance.theme);
	config.Add("custom_accent", g_Appearance.custom_accent);
	config.Add("accent", ColorJson(g_Appearance.accent));
	config.Add("custom_primary", g_Appearance.custom_primary);
	config.Add("primary", ColorJson(g_Appearance.primary));
	config.Add("opacity", Rounded(g_Appearance.opacity));
	config.Add("scale", Rounded(g_Appearance.scale));
	config.Add("show_costs", g_Appearance.show_costs);
	config.Add("hotkey", g_Appearance.hotkey);
	config.Add("layout", g_Layout);
	return config;
}

static sj::Json Command(const char* Name)
{
	sj::Json command = sj::Json::Object();
	command.Add("cmd", Name);
	return command;
}

// The request's result; nothing when SlormAPI refuses it (logged).
static std::optional<sj::Json> Request(const sj::Json& Command)
{
	const char* reply = g_Api->Request(sj::Dump(Command).c_str());
	try
	{
		sj::Json parsed = sj::Parse(reply ? reply : "");
		if (!parsed.GetBool("ok", false))
		{
			Log("%s failed: %s", Command.GetStr("cmd").c_str(), parsed.GetStr("error", "no reply").c_str());
			return std::nullopt;
		}
		const sj::Json* result = parsed.Find("result");
		return result ? *result : sj::Json();
	}
	catch (const std::exception& error)
	{
		Log("%s: bad reply: %s", Command.GetStr("cmd").c_str(), error.what());
		return std::nullopt;
	}
}

// Everything but the switch under Options > Mods lives in SlormAPI's data file for the mod.
static void StoreConfig()
{
	sj::Json command = Command("mod_data");
	command.Add("mod", MOD_NAME);
	command.Add("value", ConfigJson());
	Request(command);
}

// Saved from the next step: SlormAPI takes requests only on the game thread.
void SaveConfig()
{
	g_WantSave = true;
}

// The saved settings, or the old files' the first time.
static void LoadConfig()
{
	ResetConfig();
	sj::Json command = Command("mod_data");
	command.Add("mod", MOD_NAME);
	std::optional<sj::Json> saved = Request(command);
	if (!saved)
		return;
	if (saved->type == sj::Json::Obj)
	{
		ApplyConfig(*saved);
		return;
	}
	if (!ImportOldConfig())
		return;
	Log("imported %d targets and %d presets from %s", static_cast<int>(g_Targets.size()), static_cast<int>(g_Presets.size()), g_ConfigPath.string().c_str());
	StoreConfig();
}

static void SetSetting(const char* Key, bool Value)
{
	sj::Json command = Command("set_setting");
	command.Add("mod", MOD_NAME);
	command.Add("key", Key);
	command.Add("value", Value);
	Request(command);
}

// Once per frame on the game thread, after the player moved a switch under Options > Mods.
static void SettingChanged(const char* Event, const char* DataJson, void* User)
{
	try
	{
		sj::Json data = sj::Parse(DataJson ? DataJson : "");
		if (data.GetStr("mod") != MOD_NAME)
			return;
		std::lock_guard guard(g_Lock);
		if (data.GetStr("key") == "visible")
			g_Visible = g_SettingVisible = data.GetBool("value", g_Visible);
	}
	catch (const std::exception& error)
	{
		Log("bad setting event: %s", error.what());
	}
}

// The hotkey and the window's close button change the switch too; Options > Mods follows them.
static void SyncSettings()
{
	if (g_Visible == g_SettingVisible)
		return;
	g_SettingVisible = g_Visible;
	SetSetting("visible", g_Visible);
}

// Lists the mod on the MODS page, loads its settings and puts a switch under Options > Mods; false without SlormAPI.
static bool Register()
{
	AurieInterfaceBase* base = nullptr;
	if (!AurieSuccess(ObGetInterface(SLORMAPI_INTERFACE_NAME, base)) || !base)
		return false;
	g_Api = static_cast<SlormAPIInterface*>(base);
	std::lock_guard guard(g_Lock);

	sj::Json registration = Command("register_mod");
	registration.Add("name", MOD_NAME);
	registration.Add("file", "SlormReforger.dll");
	registration.Add("version", SLORMREFORGER_VERSION);
	registration.Add("author", "Crash");
	registration.Add("description", "Reforges at Friedrich's until your targets are met");
	sj::Json needs = sj::Json::Object();
	needs.Add("SlormAPI", "1.0.0");
	registration.Add("requires", std::move(needs));
	registration.Add("game", "1.0.2c");
	Request(registration);

	LoadConfig();
	// SlormAPI's tab cuts a help line past about fifty characters.
	sj::Json setting = sj::Json::Object();
	setting.Add("key", "visible");
	setting.Add("label", "Show the window");
	setting.Add("help", "With an item to reforge; F6 unless changed");
	setting.Add("type", "bool");
	setting.Add("default", true);
	sj::Json settings = sj::Json::Array();
	settings.Push(std::move(setting));
	sj::Json command = Command("register_settings");
	command.Add("mod", MOD_NAME);
	command.Add("settings", std::move(settings));
	std::optional<sj::Json> values = Request(command);
	g_Visible = g_SettingVisible = values ? values->GetBool("visible", true) : true;
	if (!g_Api->Subscribe("setting_changed", SettingChanged, nullptr))
		Log("the switch under Options > Mods will not take effect until the game restarts");
	return true;
}

// Best roll for a tier. Percent stats scale down on low-level items.
const PoolEntry* FindInPool(const std::string& Tier, const std::string& Stat)
{
	auto pool = g_Pools.find(Tier);
	if (pool == g_Pools.end())
		return nullptr;
	for (const PoolEntry& entry : pool->second)
		if (entry.stat == Stat) return &entry;
	return nullptr;
}

double MaxRoll(const std::string& Tier, const std::string& Stat)
{
	// Fixed value ranges: reaper 1-5, mastery 1-2, attribute 1-3.
	if (IsSpecialTier(Tier))
		return Tier == "RP" ? 5 : Tier == "MA" ? 2 : 3;
	if (IsLegendaryTier(Tier))
		return 100;
	if (const PoolEntry* entry = FindInPool(Tier, Stat))
		return entry->max;

	double base = Tier == "N" ? 100 : Tier == "E" ? 40 : 65;
	bool percent = Stat.ends_with("_percent") || Stat.ends_with("_mult");
	if (!percent)
		return base;
	int step = g_ItemLevel >= 52 ? 5 : g_ItemLevel >= 45 ? 4 : g_ItemLevel >= 35 ? 3 : g_ItemLevel >= 20 ? 2 : 1;
	return base * step / 5;
}

// Legendary on the item that the game offers no score reroll for, "" for none.
// Only an unlocked one tells: while it is locked the game lists just the unlock.
static std::string g_FixedLegendary;

static bool Met(const Target& Target, const Affix& Affix)
{
	// A legendary without a range has no score to improve; having it is all there is.
	if (IsLegendaryTier(Target.tier) && Target.stat == g_FixedLegendary)
		return true;
	if (IsSpecialTier(Target.tier) || IsLegendaryTier(Target.tier))
	{
		double wanted = Target.goal == Goal::MaxRoll ? MaxRoll(Target.tier, Target.stat) : Target.amount;
		if (Target.goal == Goal::Any || Affix.roll >= wanted)
			return true;
		// A higher roll that shows the same values is not worth a reroll.
		try { return IsLegendaryTier(Target.tier) && !LegendaryGains(std::stoi(Target.stat.substr(4)), Affix.roll, wanted); }
		catch (...) { return false; }
	}
	switch (Target.goal)
	{
	case Goal::Any: return true;
	case Goal::MaxRoll: return Affix.roll >= MaxRoll(Target.tier, Target.stat);
	case Goal::Roll: return Affix.roll >= Target.amount;
	// Rolls are whole numbers.
	case Goal::Share: return Affix.roll >= std::round(MaxRoll(Target.tier, Target.stat) * Target.amount / 100);
	default: return Affix.has_shown && Affix.shown >= Target.amount;
	}
}

// Order the game offers its "Add" recipes in: Magic, then Rare, then Epic. -1 for other tiers.
int AddRank(const std::string& Tier)
{
	return Tier == "M" ? 0 : Tier == "R" ? 1 : Tier == "E" ? 2 : -1;
}

// The only type 2 recipe without a tier code; the "Add" recipes all carry one.
bool UpdateOffered()
{
	for (const Recipe& recipe : g_Recipes)
		if (recipe.type == 2 && recipe.tier.empty()) return true;
	return false;
}

// "a Rare", "an Epic".
static std::string WithArticle(const std::string& Name)
{
	bool vowel = !Name.empty() && std::string("AEIOUaeiou").find(Name[0]) != std::string::npos;
	return (vowel ? "an " : "a ") + Name;
}

// 0 met, 1 still to do, 2 cannot be reached by reforging, 3 only by a graft, which a run leaves alone.
int TargetState(const Target& Target, std::string& Text)
{
	if (IsLevelTier(Target.tier))
	{
		bool offered = UpdateOffered();
		Text = (offered ? "level " : "met, level ") + std::to_string(g_ItemLevel) + (offered ? ", will update" : "");
		return offered ? 1 : 0;
	}
	const Affix* match = nullptr;
	const Affix* elsewhere = nullptr;
	int in_tier = 0, unlocked = 0;
	for (const Affix& affix : g_Item)
	{
		if (affix.tier == Target.tier)
		{
			in_tier++;
			if (!affix.locked) unlocked++;
			if (affix.stat == Target.stat) match = &affix;
		}
		else if (affix.stat == Target.stat) elsewhere = &affix;
	}

	char text[96];
	// More targets than the tier has stats can never all be met.
	int wanted = 0;
	for (const ::Target& other : g_Targets)
		if (other.tier == Target.tier) wanted++;
	// Except in the Epic tier, where a stats reroll leaves one to three stats.
	bool growing = false;
	if (in_tier > 0 && wanted > in_tier)
	{
		snprintf(text, sizeof(text), "%d targets, item has %d %s stat%s", wanted, in_tier, TierName(Target.tier), in_tier == 1 ? "" : "s");
		Text = text;
		growing = Target.tier == "E" && wanted <= EPIC_STATS;
		if (!growing)
			return 2;
	}
	if (match)
	{
		if (Met(Target, *match))
		{
			Text = match->locked ? "met [locked]" : "met";
			return 0;
		}
		if (match->locked)
		{
			Text = g_AutoLock ? "locked, will unlock" : "locked";
			return g_AutoLock ? 1 : 2;
		}
		snprintf(text, sizeof(text), "roll %g of %g", match->roll, MaxRoll(Target.tier, Target.stat));
		Text = text;
		return 1;
	}
	if (elsewhere)
	{
		// Moving a grafted stat would reroll it away; only another graft puts it in this tier.
		if (Grafted(elsewhere->tier, elsewhere->stat))
		{
			Text = std::string("grafted in ") + TierName(elsewhere->tier);
			Text += Grafted(Target.tier, Target.stat) ? std::string(", needs a graft in ") + TierName(Target.tier) : ", not moved";
			return 3;
		}
		Text = "already " + WithArticle(TierName(elsewhere->tier)) + " stat";
		if (!g_MoveTiers)
			return 2;
		// Wanted in both tiers: moving it would undo the other target.
		for (const ::Target& other : g_Targets)
		{
			if (other.stat != Target.stat || other.tier == Target.tier)
				continue;
			Text = "also " + WithArticle(TierName(other.tier)) + " target";
			return 2;
		}
		Text += elsewhere->locked ? ", will unlock and move" : ", will move";
		return 1;
	}
	// The game lists no outcomes for legendaries; go by the ones made for the gear slot and class.
	if (IsLegendaryTier(Target.tier))
	{
		bool listed = false;
		for (const auto& [ref, name] : SpecialStats("L"))
			if (ref == Target.stat) listed = true;
		if (!listed)
		{
			// Another slot's legendary gets here only by a graft.
			if (Grafted(Target.tier, Target.stat))
			{
				Text = "needs grafted effect";
				return 3;
			}
			Text = "not a legendary for this item";
			return 2;
		}
	}
	auto pool = g_Pools.find(Target.tier);
	if (pool != g_Pools.end() && !pool->second.empty() && !FindInPool(Target.tier, Target.stat))
	{
		Text = std::string("not a possible ") + TierName(Target.tier) + " stat";
		// What a tier can roll depends on the item level, so a pending level update decides first.
		int needed = 0;
		if (const StatInfo* stat = FindStat(Target.stat))
		{
			auto column = stat->columns.find("MIN_LEVEL");
			try { if (column != stat->columns.end()) needed = std::stoi(column->second); }
			catch (...) {}
		}
		if (needed > g_ItemLevel)
			Text = "needs item level " + std::to_string(needed);
		bool leveling = false;
		for (const ::Target& other : g_Targets)
			if (IsLevelTier(other.tier)) leveling = UpdateOffered();
		if (!leveling)
		{
			// At its level and still not offered: the slot never rolls it in this tier.
			if (needed > g_ItemLevel)
				return 2;
			Text = "needs grafted stat";
			return 3;
		}
		Text += needed > g_ItemLevel ? ", will update" : " at this level, checked again after the update";
		return 1;
	}
	if (in_tier == 0)
	{
		Text = std::string("no ") + TierName(Target.tier) + " stat on item";
		if (!g_AutoAdd || (AddRank(Target.tier) < 0 && !IsSpecialTier(Target.tier) && !IsLegendaryTier(Target.tier)))
			return 2;
		Text += ", will add";
		return 1;
	}
	if (growing)
	{
		Text += ", will reroll for more";
		return 1;
	}
	if (unlocked == 0)
	{
		Text = std::string("all ") + TierName(Target.tier) + " stats locked";
		if (!g_MoveTiers)
			return 2;
		Text += ", will unlock one";
		return 1;
	}
	int possible = 0;
	if (IsSpecialTier(Target.tier) || IsLegendaryTier(Target.tier))
		possible = static_cast<int>(SpecialStats(Target.tier).size()) - 1;
	if (pool != g_Pools.end())
		for (const PoolEntry& entry : pool->second) possible += entry.on_item ? 0 : 1;
	Text = "not on item yet";
	if (possible > 0)
		Text += ", 1 of " + std::to_string(possible) + " possible";
	return 1;
}

// Targets of the current run; g_Targets, or derived from the selected recipe.
static std::vector<Target> g_RunTargets;

static bool NeedsGraft(const Target& Target)
{
	std::string text;
	return TargetState(Target, text) == 3;
}

// The run wants more stats in the tier than the item has there.
static bool ShortTier(const std::string& Tier)
{
	int have = 0, wanted = 0;
	for (const Affix& affix : g_Item)
		if (affix.tier == Tier) have++;
	for (const Target& target : g_RunTargets)
		if (target.tier == Tier && !NeedsGraft(target)) wanted++;
	return have > 0 && wanted > have;
}

static int UnlockedIn(const std::string& Tier)
{
	int count = 0;
	for (const Affix& affix : g_Item)
		if (affix.tier == Tier && !affix.locked) count++;
	return count;
}

static double g_GoldAtStart = 0;
// Lock or unlock applied last step, checked on the next.
static std::string g_LockTier, g_LockStat;
static bool g_LockWanted = false;
// Tier an "Add" recipe was applied for last step, checked on the next, and how many stats it had.
static std::string g_AddTier;
static int g_AddCount = 0;
// Item level before an "Update Item" applied last step, 0 for none.
static int g_LevelBefore = 0;

static void Stop(const char* Reason)
{
	double gold = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
	Note("stopped after %d steps, %.0f goldus spent: %s", g_Attempts, g_GoldAtStart - gold, Reason);
	g_Status = Reason;
	g_Running = false;
}

// Recipe layout: [id, label, type (0 scores, 1 stats), materials, number, tier code, ...]
static bool g_AllScores = false;

// Tier "" finds the recipe with no tier code ("Reforge all Scores").
static int FindRecipe(CInstance* Ui, int Type, const std::string& Tier)
{
	std::string code = Lower(Tier);
	RValue recipes = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Ui), RValue("blacksmith_recipes") });
	if (!recipes.IsArray())
		return -1;

	std::vector<RValue> list = recipes.ToVector();
	for (size_t i = 0; i < list.size(); i++)
	{
		if (!list[i].IsArray())
			continue;
		std::vector<RValue> fields = list[i].ToVector();
		if (fields.size() < 5 || static_cast<int>(ToNumber(fields[2])) != Type)
			continue;
		std::string recipe_code = fields.size() > 5 && fields[5].IsString() ? fields[5].ToString() : "";
		if (recipe_code == code)
			return static_cast<int>(i);
	}
	return -1;
}

#ifdef SLORM_DEV
const char* SlormiteName(int Id)
{
	static const char* names[] = {
		"Incomplete Inferior Slormite", "Inferior Slormite Chunk", "Inferior Flawless Slormite",
		"Incomplete Modest Slormite", "Modest Slormite Chunk", "Modest Flawless Slormite",
		"Incomplete Greater Slormite", "Greater Slormite Chunk", "Greater Flawless Slormite",
		"Incomplete Superior Slormite", "Superior Slormite Chunk", "Superior Flawless Slormite",
		"Incomplete Ancestral Slormite", "Ancestral Slormite Chunk", "Ancestral Flawless Slormite",
	};
	return Id >= 1 && Id <= SLORMITE_COUNT ? names[Id - 1] : "unknown slormite";
}
#endif

const char* MaterialName(int Id)
{
	static const char* names[] = {
		"Normal Slormeline", "Magic Slormeline", "Rare Slormeline", "Epic Slormeline", "Legendary Slormeline",
		"Slormandrite of Fate", "Slormandrite of Negation", "Slormandrite of True Potential",
		"Slormandrite of Aptitude", "Slormandrite of Harmony", "Slormandrite of Virtue",
	};
	return Id >= 0 && Id < MATERIAL_COUNT ? names[Id] : "unknown material";
}

// With the third argument set the game only reports the count; the panel calls it this way every frame.
static double MaterialStock(CInstance* Self, CInstance* Other, int Id)
{
	RValue arguments[4] = { RValue(static_cast<double>(Id)), RValue(1.0), RValue(1.0), RValue("slormite") };
	RValue* pointers[4] = { &arguments[0], &arguments[1], &arguments[2], &arguments[3] };
	RValue count;
	g_CurrencySpend(Self, Other, count, 4, pointers);
	return ToNumber(count);
}

#ifdef SLORM_DEV
int g_GrantId = -1;
int g_GrantAmount = 0;
std::string g_GrantResult;

double g_SlormiteStock[SLORMITE_COUNT] = {};

// Stacks are items shaped [["trash", tier, id, kind, count, ...]]. Slormites are tier 1, ids 1-15, in slots 75-89.
static bool IsStack(const RValue& Slot, int Tier, int Id, RValue& Header)
{
	if (!Slot.IsArray() || ToNumber(g_Yytk->CallBuiltin("array_length", { Slot })) < 1)
		return false;
	Header = g_Yytk->CallBuiltin("array_get", { Slot, RValue(0.0) });
	if (!Header.IsArray())
		return false;
	std::vector<RValue> fields = Header.ToVector();
	return fields.size() >= 5 && fields[0].IsString() && fields[0].ToString() == "trash" && fields[3].IsString() && fields[3].ToString() == "slormite"
		&& ToNumber(fields[1]) == Tier && ToNumber(fields[2]) == Id;
}

// Hero whose inventory holds the stacks: the one with Normal Slormeline in slot 90.
static bool StackHero(RValue& Hero)
{
	RValue inventory = g_Yytk->CallBuiltin("variable_global_get", { RValue("inventory") });
	if (!inventory.IsArray())
		return false;
	for (const RValue& hero : inventory.ToVector())
	{
		if (!hero.IsArray() || ToNumber(g_Yytk->CallBuiltin("array_length", { hero })) <= 112)
			continue;
		RValue header;
		if (IsStack(g_Yytk->CallBuiltin("array_get", { hero, RValue(90.0) }), 0, 0, header))
		{
			Hero = hero;
			return true;
		}
	}
	return false;
}

static double SlormiteStock(int Id)
{
	RValue hero, header;
	if (!StackHero(hero) || !IsStack(g_Yytk->CallBuiltin("array_get", { hero, RValue(74.0 + Id) }), 1, Id, header))
		return 0;
	return ToNumber(g_Yytk->CallBuiltin("array_get", { header, RValue(4.0) }));
}

// Sets the count, creating the stack when the slot is empty.
static bool SetSlormite(int Id, double Count)
{
	RValue hero, header;
	if (!StackHero(hero))
		return false;
	RValue text(std::to_string(static_cast<long long>(Count)));
	RValue slot = g_Yytk->CallBuiltin("array_get", { hero, RValue(74.0 + Id) });
	if (IsStack(slot, 1, Id, header))
	{
		g_Yytk->CallBuiltin("array_set", { header, RValue(4.0), text });
		return true;
	}
	// Anything else in the slot is left alone.
	if (slot.IsArray() && ToNumber(g_Yytk->CallBuiltin("array_length", { slot })) > 0)
		return false;
	const std::string fields[8] = { "trash", "1", std::to_string(Id), "slormite", text.ToString(), "0", "0", "normal" };
	header = g_Yytk->CallBuiltin("array_create", { RValue(8.0) });
	for (int i = 0; i < 8; i++)
		g_Yytk->CallBuiltin("array_set", { header, RValue(static_cast<double>(i)), RValue(fields[i]) });
	RValue item = g_Yytk->CallBuiltin("array_create", { RValue(1.0) });
	g_Yytk->CallBuiltin("array_set", { item, RValue(0.0), header });
	g_Yytk->CallBuiltin("array_set", { hero, RValue(74.0 + Id), item });
	return true;
}

// Sets the count on a slormeline or slormandrite stack. Returns false when there is no stack.
static bool WriteStack(int Id, double Count)
{
	RValue hero, header;
	if (!StackHero(hero))
		return false;
	for (int s = 90; s <= 112; s++)
	{
		if (!IsStack(g_Yytk->CallBuiltin("array_get", { hero, RValue(static_cast<double>(s)) }), 0, Id, header))
			continue;
		g_Yytk->CallBuiltin("array_set", { header, RValue(4.0), RValue(std::to_string(static_cast<long long>(Count))) });
		return true;
	}
	return false;
}

static void Grant(CInstance* Self, CInstance* Other, int Id, int Amount)
{
	char text[160];
	// One past the materials is goldus.
	if (Id == MATERIAL_COUNT)
	{
		double gold = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
		g_Yytk->CallBuiltin("variable_global_set", { RValue("gold"), RValue(gold + Amount) });
		g_Gold = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
		snprintf(text, sizeof(text), "Goldus: %.0f -> %.0f", gold, g_Gold);
		g_GrantResult = text;
		Log("dev: %s", text);
		return;
	}
	// Past goldus come the slormites, ids 1-15.
	if (Id > MATERIAL_COUNT)
	{
		int slormite = Id - MATERIAL_COUNT;
		double before = SlormiteStock(slormite);
		bool written = SetSlormite(slormite, before + Amount);
		snprintf(text, sizeof(text), "%s: %.0f -> %.0f%s", SlormiteName(slormite), before, SlormiteStock(slormite), written ? "" : " (slot not usable)");
		g_GrantResult = text;
		Log("dev: %s", text);
		return;
	}
	double before = MaterialStock(Self, Other, Id);
	// A negative spend adds, if the game does not clamp it.
	RValue arguments[4] = { RValue(static_cast<double>(Id)), RValue(static_cast<double>(-Amount)), RValue(0.0), RValue("slormite") };
	RValue* pointers[4] = { &arguments[0], &arguments[1], &arguments[2], &arguments[3] };
	RValue result;
	g_CurrencySpend(Self, Other, result, 4, pointers);
	double after = MaterialStock(Self, Other, Id);
	const char* how = "negative spend";
	if (after != before + Amount)
	{
		how = WriteStack(Id, before + Amount) ? "stack write" : "no stack found";
		after = MaterialStock(Self, Other, Id);
	}
	snprintf(text, sizeof(text), "%s: %.0f -> %.0f (%s)", MaterialName(Id), before, after, how);
	g_GrantResult = text;
	Log("dev: %s", text);
}
#endif

// Cost spec: parts joined by '|', each "material" (one) or "material*count".
std::vector<std::pair<int, int>> ParseCost(const std::string& Spec)
{
	std::vector<std::pair<int, int>> parts;
	std::istringstream stream(Spec);
	std::string part;
	while (std::getline(stream, part, '|'))
	{
		try
		{
			size_t star = part.find('*');
			int id = std::stoi(part.substr(0, star));
			int count = star == std::string::npos ? 1 : std::stoi(part.substr(star + 1));
			if (id < 0 || id >= MATERIAL_COUNT || count < 1)
				return {};
			parts.emplace_back(id, count);
		}
		catch (...) { return {}; }
	}
	return parts;
}

// The goldus field is only accurate for the recipe selected in the game's panel, so it is a weak check.
static bool CanAfford(CInstance* Self, CInstance* Other, int Recipe, std::string& Why)
{
	RValue recipes = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("blacksmith_recipes") });
	if (!recipes.IsArray())
		return false;
	std::vector<RValue> list = recipes.ToVector();
	if (Recipe < 0 || static_cast<size_t>(Recipe) >= list.size() || !list[Recipe].IsArray())
		return false;
	std::vector<RValue> fields = list[Recipe].ToVector();
	if (fields.size() < 5)
		return false;

	std::string materials = fields[3].ToString();
	std::vector<std::pair<int, int>> cost = ParseCost(materials);
	if (cost.empty())
	{
		Why = "recipe cost format not understood: " + materials;
		return false;
	}
	for (const auto& [id, count] : cost)
	{
		double stock = MaterialStock(Self, Other, id);
		if (stock < count)
		{
			Why = std::string("out of ") + MaterialName(id);
			return false;
		}
		if (stock - count < g_MinStock[id])
		{
			Why = std::string(MaterialName(id)) + " limit reached (have " + std::to_string(static_cast<long long>(stock)) + ", keeping " + std::to_string(g_MinStock[id]) + ")";
			return false;
		}
	}

	double gold = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
	if (gold < ToNumber(fields[4]))
	{
		Why = "not enough goldus";
		return false;
	}
	return true;
}

// Lock and unlock share one recipe per stat: type 3, with the tier code, a label ending in the
// stat's name, and the stat's position as seventh field. Position alone is not trusted.
static int FindLockRecipe(CInstance* Ui, const Affix& Affix, bool Unlock, double& Position)
{
	RValue recipes = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Ui), RValue("blacksmith_recipes") });
	const StatInfo* stat = FindStat(Affix.stat);
	// A legendary has no stat row, and its tier has only the one lock.
	bool legendary = IsLegendaryTier(Affix.tier);
	if (!recipes.IsArray() || (!stat && !legendary))
		return -1;

	std::string ending = legendary ? "" : " " + stat->label + "}";
	std::string code(1, static_cast<char>(std::tolower(Affix.tier[0])));
	int found = -1;
	std::vector<RValue> list = recipes.ToVector();
	for (size_t i = 0; i < list.size(); i++)
	{
		if (!list[i].IsArray())
			continue;
		std::vector<RValue> fields = list[i].ToVector();
		if (fields.size() < 7 || static_cast<int>(ToNumber(fields[2])) != 3 || fields[5].ToString() != code)
			continue;
		std::string detail = fields[1].ToString();
		if (detail.size() < ending.size() || detail.compare(detail.size() - ending.size(), ending.size(), ending) != 0)
			continue;
		if ((fields[0].ToString().rfind("Unlock", 0) == 0) != Unlock)
			continue;
		// Two matches would mean the label is ambiguous; refuse.
		if (found >= 0)
			return -1;
		found = static_cast<int>(i);
		Position = ToNumber(fields[6]);
	}
	return found;
}

// No targets set: take the scores recipe selected in the panel and aim every stat it rerolls at max.
static bool TargetsFromSelectedRecipe(CInstance* Self, CInstance* Other)
{
	RValue recipes = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("blacksmith_recipes") });
	RValue selected = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("blacksmith_selected_recipe") });
	if (!recipes.IsArray() || !selected.IsNumberConvertible() || !ReadSlotItem(Self, Other))
	{
		Note("open the reforge panel with an item in the slot first");
		return false;
	}

	std::vector<RValue> list = recipes.ToVector();
	size_t index = static_cast<size_t>(selected.ToDouble());
	if (index >= list.size() || !list[index].IsArray())
		return false;
	std::vector<RValue> fields = list[index].ToVector();
	if (fields.size() < 5 || static_cast<int>(ToNumber(fields[2])) != 0)
	{
		Note("add a target, or select a 'Reforge Scores' recipe (selected: %s)", fields.empty() ? "?" : fields[0].ToString().c_str());
		return false;
	}

	std::string tier;
	if (fields.size() > 5 && fields[5].IsString())
		tier = std::string(1, static_cast<char>(std::toupper(fields[5].ToString()[0])));
	g_AllScores = tier.empty();

	for (const Affix& affix : g_Item)
	{
		if (affix.locked || (!tier.empty() && affix.tier != tier))
			continue;
		if (affix.tier != "N" && affix.tier != "D" && affix.tier != "M" && affix.tier != "R" && affix.tier != "E")
			continue;
		Target target;
		target.tier = affix.tier;
		target.stat = affix.stat;
		target.goal = Goal::MaxRoll;
		g_RunTargets.push_back(target);
	}
	Note("using selected recipe '%s'", fields[0].ToString().c_str());
	return !g_RunTargets.empty();
}

// What the overlay shows: item, recipes, stock.
static void Refresh(CInstance* Self, CInstance* Other)
{
	// The slot keeps its item when the menu closes, so also require Friedrich's reforge tab.
	RValue citizen = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("menu_citizen") });
	RValue panel = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("merchant_panel") });
	g_PanelOpen = citizen.IsString() && citizen.ToString() == "blacksmith" && ToNumber(panel) == 0;
	g_HasItem = g_PanelOpen && ReadSlotItem(Self, Other);
	g_Recipes.clear();
	if (!g_HasItem)
	{
		g_Item.clear();
		return;
	}

	RValue recipes = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("blacksmith_recipes") });
	if (recipes.IsArray())
	{
		for (const RValue& entry : recipes.ToVector())
		{
			if (!entry.IsArray())
				continue;
			std::vector<RValue> fields = entry.ToVector();
			if (fields.size() < 5)
				continue;
			Recipe recipe;
			recipe.label = fields[0].ToString();
			recipe.detail = fields[1].ToString();
			recipe.type = static_cast<int>(ToNumber(fields[2]));
			recipe.materials = fields[3].ToString();
			recipe.gold = ToNumber(fields[4]);
			if (fields.size() > 5 && fields[5].IsString())
				recipe.tier = fields[5].ToString();
			g_Recipes.push_back(recipe);
		}
	}

	bool score_reroll = false;
	for (const Recipe& recipe : g_Recipes)
		if (recipe.type == 0 && recipe.tier == "l") score_reroll = true;
	bool legendary = false;
	for (const Affix& affix : g_Item)
	{
		if (!IsLegendaryTier(affix.tier))
			continue;
		legendary = true;
		if (!affix.locked)
			g_FixedLegendary = score_reroll ? "" : affix.stat;
		else if (g_FixedLegendary != affix.stat)
			g_FixedLegendary.clear();
	}
	if (!legendary)
		g_FixedLegendary.clear();

	// Only ask about materials the listed recipes use.
	for (const Recipe& recipe : g_Recipes)
		for (const auto& [id, count] : ParseCost(recipe.materials))
			g_Stock[id] = MaterialStock(Self, Other, id);
	g_Gold = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
}

// Loads the active preset's targets when a different item lands in the reforge slot.
// The slot keeps its item while the menu is closed, so it is watched there too:
// reopening the panel on the same item leaves the targets alone.
static void AutoImport()
{
	// Hero and gear slot of the item last seen, empty for none.
	static std::string last;
	static bool known = false;
	static bool pending = false;

	RValue item;
	int owner = FindSlotItem(item);
	if (owner == -2)
		return;
	std::string now;
	if (owner >= 0)
	{
		std::vector<RValue> header = item.ToVector()[0].ToVector();
		now = std::to_string(owner) + ' ' + (header.size() > 1 ? header[1].ToString() : "");
	}
	// The item found in the slot at launch was there before; it does not count.
	if (known && now != last)
		pending = !now.empty();
	known = true;
	last = now;

	if (!g_AutoImport)
		pending = false;
	if (!pending || !g_HasItem)
		return;
	pending = false;
	if (g_ActivePreset < 0 || g_ActivePreset >= static_cast<int>(g_Presets.size()))
		return;
	const Preset& preset = g_Presets[g_ActivePreset];
	const PresetSlot* slot = BestPresetSlot(preset);
	if (!slot)
		return;
	ImportPreset(preset, *slot);
	SaveConfig();
	Note("imported the %s from %s", Lower(slot->label).c_str(), preset.name.c_str());
}

static void Start(CInstance* Self, CInstance* Other)
{
	g_AllScores = false;
	g_RunTargets = g_Targets;
	if (g_RunTargets.empty() && !TargetsFromSelectedRecipe(Self, Other))
	{
		g_Status = "nothing to do";
		return;
	}
	// Two targets for a tier that holds one stat could never both be met.
	for (size_t i = 0; i < g_RunTargets.size(); i++)
	{
		for (size_t j = i + 1; j < g_RunTargets.size(); j++)
		{
			if ((!IsSpecialTier(g_RunTargets[i].tier) && !IsLegendaryTier(g_RunTargets[i].tier)) || g_RunTargets[i].tier != g_RunTargets[j].tier)
				continue;
			g_Status = std::string("only one ") + TierName(g_RunTargets[i].tier) + " target is possible, remove the other";
			Note("%s", g_Status.c_str());
			return;
		}
	}
	g_GoldAtStart = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
	g_LockStat.clear();
	g_AddTier.clear();
	g_Running = true;
	g_Attempts = 0;
	g_Cooldown = 0;
	g_Status = "running";
	Note("started, %d targets, max %d reforges", static_cast<int>(g_RunTargets.size()), g_MaxAttempts);
	for (const Recipe& recipe : g_Recipes)
		Log("  recipe '%s' / '%s' type %d cost %s code '%s'", recipe.label.c_str(), recipe.detail.c_str(), recipe.type, recipe.materials.c_str(), recipe.tier.c_str());
}

static void Tick(CInstance* Self, CInstance* Other)
{
	std::lock_guard guard(g_Lock);

	bool key_down = (GetAsyncKeyState(g_Appearance.hotkey) & 0x8000) != 0;
	if (g_SuppressToggle)
	{
		// Wait for the newly bound key to be released.
		if (!key_down)
			g_SuppressToggle = false;
		key_down = true;
	}
	else if (key_down && !g_KeyWasDown && GetForegroundWindow() == GetActiveWindow())
	{
		(g_HasItem ? g_Visible : g_Manual) = !OverlayShown();
		Log("overlay %s", OverlayShown() ? "shown" : "hidden");
	}
	g_KeyWasDown = key_down;

	SyncSettings();
	if (g_WantSave)
	{
		g_WantSave = false;
		StoreConfig();
	}

	if (g_WantStop && g_Running)
		Stop("stopped by you");
	if (g_WantStart && !g_Running)
		Start(Self, Other);
	g_WantStart = g_WantStop = false;

#ifdef SLORM_DEV
	if (g_GrantId >= 0 && !g_Running)
		Grant(Self, Other, g_GrantId, g_GrantAmount);
	g_GrantId = -1;
	if (OverlayShown() && !g_Running)
	{
		for (int id = 0; id < MATERIAL_COUNT; id++)
			g_Stock[id] = MaterialStock(Self, Other, id);
		g_Gold = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("gold") }));
		for (int id = 1; id <= SLORMITE_COUNT; id++)
			g_SlormiteStock[id - 1] = SlormiteStock(id);
	}
#endif

	if (!g_Running)
	{
		// Also while hidden, so the hotkey knows whether an item is in the slot.
		if (--g_Refresh <= 0)
		{
			g_Refresh = 20;
			bool had_item = g_HasItem;
			Refresh(Self, Other);
			// Taking the item out closes the overlay, however it was opened.
			if (had_item && !g_HasItem)
				g_Manual = false;
			AutoImport();
		}
		return;
	}
	if (g_Cooldown > 0)
	{
		g_Cooldown--;
		return;
	}
	Refresh(Self, Other);
	if (!g_PanelOpen)
		return Stop("the reforge panel was closed");
	if (!g_HasItem)
		return Stop("no single item found in the reforge slot");

	for (const Affix& affix : g_Item)
		Log("  %s %s roll %g shown %g%s%s", affix.tier.c_str(), affix.stat.c_str(), affix.roll, affix.shown, affix.pure > 100 ? " pure" : "", affix.locked ? " locked" : "");

	// With auto-lock, every target stat is locked once it is met and unlocked while its roll
	// still needs work. While a tier is missing a target stat, the target stats already there are held too.
	int recipe = -1;
	const Affix* lock_affix = nullptr;
	bool unlock = false;
	double lock_position = 0;

	// A lock that did not take would otherwise be retried, and paid for, every step.
	if (!g_LockStat.empty())
	{
		bool done = false;
		for (const Affix& affix : g_Item)
			if (affix.tier == g_LockTier && affix.stat == g_LockStat && affix.locked == g_LockWanted) done = true;
		std::string stat = g_LockStat;
		g_LockStat.clear();
		if (!done)
			return Stop((StatName(stat) + ": the " + (g_LockWanted ? "lock" : "unlock") + " did not take effect").c_str());
	}
	if (!g_AddTier.empty())
	{
		int count = 0;
		for (const Affix& affix : g_Item)
			if (affix.tier == g_AddTier) count++;
		bool done = count > g_AddCount;
		std::string tier = g_AddTier;
		g_AddTier.clear();
		if (!done)
			return Stop((std::string("adding ") + TierName(tier) + " stats did not take effect").c_str());
	}
	if (g_LevelBefore > 0)
	{
		bool done = g_ItemLevel > g_LevelBefore;
		g_LevelBefore = 0;
		if (!done)
			return Stop("updating the item level did not take effect");
	}
	// Stop on an unreachable target before paying for a lock.
	for (const Target& target : g_RunTargets)
	{
		bool met = false;
		for (const Affix& affix : g_Item)
			if (affix.tier == target.tier && affix.stat == target.stat && Met(target, affix)) met = true;
		std::string problem;
		if (!met && TargetState(target, problem) == 2)
			return Stop((StatName(target.stat) + ": " + problem).c_str());
	}
	if (g_AutoLock && !g_AllScores)
	{
		for (const Affix& affix : g_Item)
		{
			if (lock_affix)
				break;
			// Reaper, mastery and attribute stats cannot be locked.
			if (IsSpecialTier(affix.tier))
				continue;
			const Target* own = nullptr;
			bool missing = false;
			for (const Target& target : g_RunTargets)
			{
				if (target.tier != affix.tier)
					continue;
				if (target.stat == affix.stat) own = &target;
				bool present = false;
				for (const Affix& other : g_Item)
					if (other.tier == target.tier && other.stat == target.stat) present = true;
				if (!present && !NeedsGraft(target)) missing = true;
			}
			// A tier with fewer stats than targets has to keep one stat free for the reroll that grows it.
			if (own && !affix.locked && ShortTier(affix.tier) && UnlockedIn(affix.tier) <= 1)
				continue;
			if (own && (missing || Met(*own, affix)) != affix.locked)
			{
				lock_affix = &affix;
				unlock = affix.locked;
			}
		}
	}

	// First unmet target decides the next reroll.
	const Target* pending = nullptr;
	int recipe_type = 0;
	// Tier the reroll hits; another tier than the target's when a stat is being moved out of it.
	std::string reroll_tier;
	bool evict = false;
	// The level comes before any reroll; it changes what the stats can roll.
	bool level_up = false;
	for (const Target& target : g_RunTargets)
	{
		if (lock_affix || !IsLevelTier(target.tier) || !UpdateOffered())
			continue;
		pending = &target;
		level_up = true;
		break;
	}
	for (const Target& target : g_RunTargets)
	{
		if (lock_affix || level_up)
			break;
		if (IsLevelTier(target.tier))
			continue;
		const Affix* match = nullptr;
		for (const Affix& affix : g_Item)
			if (affix.tier == target.tier && affix.stat == target.stat) match = &affix;

		if (match && Met(target, *match))
			continue;
		std::string problem;
		int state = TargetState(target, problem);
		if (state == 2)
			return Stop((StatName(target.stat) + ": " + problem).c_str());
		if (state == 3)
			continue;
		// Held locked while its tier still misses a target stat; that one goes first.
		if (match && match->locked)
			continue;
		pending = &target;
		// Keep the stat and reroll only its value where the game offers that.
		recipe_type = match && (!IsSpecialTier(target.tier) || FindRecipe(Self, 0, target.tier) >= 0) ? 0 : 1;
		bool in_tier = false;
		for (const Affix& affix : g_Item)
			if (affix.tier == target.tier) in_tier = true;
		if (!in_tier)
			recipe_type = 2;
		// The stat sits in another tier: reroll it out of there first, unlocking it if needed.
		for (const Affix& affix : g_Item)
		{
			if (match || !g_MoveTiers || affix.tier == target.tier || affix.stat != target.stat)
				continue;
			if (affix.locked)
			{
				lock_affix = &affix;
				unlock = true;
			}
			else
			{
				recipe_type = 1;
				reroll_tier = affix.tier;
				evict = true;
			}
		}
		// Every stat of the tier is locked: free one that is not a target, so the reroll has room.
		bool room = false;
		for (const Affix& affix : g_Item)
			if (affix.tier == target.tier && !affix.locked) room = true;
		for (const Affix& affix : g_Item)
		{
			if (match || !g_MoveTiers || room || evict || lock_affix || affix.tier != target.tier)
				continue;
			bool wanted = false;
			for (const Target& other : g_RunTargets)
				if (other.tier == affix.tier && other.stat == affix.stat) wanted = true;
			if (wanted)
				continue;
			lock_affix = &affix;
			unlock = true;
		}
		// A short tier that is fully locked cannot grow; free a stat even if it is a target.
		for (int pass = 0; pass < 2; pass++)
		{
			for (const Affix& affix : g_Item)
			{
				if (match || room || evict || lock_affix || affix.tier != target.tier || !ShortTier(target.tier))
					continue;
				// Stats that are not targets go first.
				bool wanted = false;
				for (const Target& other : g_RunTargets)
					if (other.tier == affix.tier && other.stat == affix.stat) wanted = true;
				if (wanted && pass == 0)
					continue;
				lock_affix = &affix;
				unlock = true;
			}
		}
		break;
	}
	if (!pending && !lock_affix)
	{
		int grafts = 0;
		for (const Target& target : g_RunTargets)
			if (NeedsGraft(target)) grafts++;
		if (grafts > 0)
			return Stop(("all other targets met, " + std::to_string(grafts) + (grafts == 1 ? " needs a graft" : " need a graft")).c_str());
		return Stop("all targets met");
	}
	if (g_Attempts >= g_MaxAttempts)
		return Stop("max_attempts reached");

	if (lock_affix)
	{
		recipe = FindLockRecipe(Self, *lock_affix, unlock, lock_position);
		if (recipe < 0)
			return Stop("lock recipe not offered for this stat");
	}

	// The game offers one "Add" at a time, so an earlier rarity may have to come first.
	std::string add_tier;
	if (level_up)
		recipe = FindRecipe(Self, 2, "");
	else if (!lock_affix && recipe_type == 2)
	{
		if (IsSpecialTier(pending->tier) || IsLegendaryTier(pending->tier))
		{
			recipe = FindRecipe(Self, 2, pending->tier);
			add_tier = pending->tier;
		}
		else for (const char* tier : { "M", "R", "E" })
		{
			if (recipe >= 0 || AddRank(tier) > AddRank(pending->tier))
				break;
			recipe = FindRecipe(Self, 2, tier);
			add_tier = tier;
		}
		if (recipe < 0)
			return Stop((std::string("no recipe offered to add ") + TierName(pending->tier) + " stats").c_str());
	}
	else if (!lock_affix)
	{
		if (!evict)
			reroll_tier = pending->tier;
		// Both reroll types hit every unlocked stat of the tier.
		for (const Affix& affix : g_Item)
		{
			if ((!g_AllScores && affix.tier != reroll_tier) || affix.locked)
				continue;
			if (affix.pure > 100 && !g_AllowPureLoss)
				return Stop("tier has a pure stat (allow rerolling pure stats to continue)");
		}
		recipe = FindRecipe(Self, recipe_type, g_AllScores ? "" : reroll_tier);
	}

	if (recipe < 0)
		return Stop("recipe not offered for this item");

	std::string why = "could not read the recipe cost";
	if (!CanAfford(Self, Other, recipe, why))
		return Stop(why.c_str());

	g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("blacksmith_selected_recipe"), RValue(recipe) });

	// For a lock or unlock the game passes the stat's position (the recipe's seventh field);
	// a reroll or an add takes no argument. A lock applied without it crashes the game.
	RValue result;
	RValue argument(lock_position);
	if (lock_affix)
	{
		g_LockTier = lock_affix->tier;
		g_LockStat = lock_affix->stat;
		g_LockWanted = !unlock;
	}
	g_AddTier = add_tier;
	g_AddCount = 0;
	for (const Affix& affix : g_Item)
		if (affix.tier == add_tier) g_AddCount++;
	g_LevelBefore = level_up ? g_ItemLevel : 0;
	RValue* arguments[1] = { &argument };
	Log("applying recipe %d%s", recipe, lock_affix ? " (lock)" : "");
	g_Apply(Self, Other, result, lock_affix ? 1 : 0, arguments);
	g_Attempts++;
	g_Cooldown = 3;

	RValue applied = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("blacksmith_last_applied_recipe") });
	if (!applied.IsArray())
		return Stop("the game did not apply the recipe");

	if (lock_affix)
		Note("%d: %s %s", g_Attempts, unlock ? "unlock" : "lock", StatName(lock_affix->stat).c_str());
	else if (level_up)
		Note("%d: update item level from %d", g_Attempts, g_ItemLevel);
	else if (!add_tier.empty())
		Note("%d: add %s stats for %s", g_Attempts, TierName(add_tier), StatName(pending->stat).c_str());
	else if (evict)
		Note("%d: reroll %s stats to free %s", g_Attempts, TierName(reroll_tier), StatName(pending->stat).c_str());
	else
		Note("%d: reroll %s %s for %s", g_Attempts, TierName(pending->tier), recipe_type ? "stats" : "scores", StatName(pending->stat).c_str());
}

// Runs once per frame, after the UI object's Step event.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
	if (code != step_code || !code)
	{
		if (step_code || !code || !code->GetName() || std::string_view(code->GetName()) != "gml_Object_obj_ui_next_gen_Step_0")
			return;
		step_code = code;
	}

	// Modules load in any order, so SlormAPI is asked for from the first step, not at load;
	// the swap chain for the overlay also only exists once the game is running.
	static bool tried = false;
	if (!g_Api)
	{
		if (tried)
			return;
		tried = true;
		if (!Register())
		{
			DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormReforger] SlormAPI is not loaded; the mod needs it");
			return;
		}
		Log("connected to SlormAPI, %d presets, overlay hook %s", static_cast<int>(g_Presets.size()), OverlayInstall(g_Yytk) ? "installed" : "FAILED");
	}

	// Input aimed at the overlay must not reach the game's UI.
	if (OverlayWantsMouse())
	{
		g_Yytk->CallBuiltin("mouse_clear", { RValue(1.0) });
		g_Yytk->CallBuiltin("mouse_clear", { RValue(2.0) });
	}
	if (OverlayWantsKeys())
		g_Yytk->CallBuiltin("io_clear", {});

	// Another mod may have run the event already; running it twice doubles every click.
	if (!Event.CalledOriginal()) Event.Call();
	Tick(self, other);
}

static PFUNC_YYGMLScript FindScript(const char* Name)
{
	CScript* script = nullptr;
	AurieStatus status = g_Yytk->GetNamedRoutinePointer(Name, reinterpret_cast<PVOID*>(&script));
	if (!AurieSuccess(status) || !script || !script->m_Functions)
		return nullptr;
	return script->m_Functions->m_ScriptFunction;
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_ConfigPath = ModulePath.parent_path() / "SlormReforger.txt";
	// Saved presets are read against the stat table, from the first step.
	LoadGameData();

	g_StatFromScore = FindScript("gml_Script_scr_loot_stat_from_score");
	g_Apply = FindScript("gml_Script_scr_blacksmith_recipe_apply");
	g_CurrencySpend = FindScript("gml_Script_scr_currency_spend");
	g_RollStats = FindScript("gml_Script_scr_loot_roll_stats");
	if (!g_StatFromScore || !g_Apply || !g_CurrencySpend)
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormReforger] game scripts not found");
		return AURIE_OBJECT_NOT_FOUND;
	}

	AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormReforger] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" SLORMREFORGER_VERSION " loaded, %d stats", static_cast<int>(g_Stats.size()));
	return AURIE_SUCCESS;
}
