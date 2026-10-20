#include "Hud.hpp"
#include "Version.hpp"
#include <SlormAPI/SlormAPI_Interface.hpp>
#include <SlormAPI/Json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <functional>
#include <iterator>
#include <optional>
#include <unordered_set>
#include <sstream>
#include <string>
#include <vector>
using namespace Aurie;
using namespace YYTK;

static YYTKInterface* g_Yytk = nullptr;
// As shown on SlormAPI's MODS page, and the name of its settings and data file.
constexpr const char* MOD_NAME = "SlormHUD";
static SlormAPIInterface* g_Api = nullptr;
// The old settings file, imported once.
static fs::path g_ConfigPath;

std::recursive_mutex g_Lock;

Appearance g_Appearance;
bool g_SuppressToggle = false;
bool g_Visible = true;
bool g_Marks = true;
// The two switches as Options > Mods last showed them.
static bool g_SettingVisible = true;
static bool g_SettingMarks = true;
static bool g_WantSave = false;
std::string g_Layout;
// Sizes and shifts are as on a 2560-wide screen and scale with the game's UI from there.
// For the tooltip's pips the thickness is the pip's size.
const OutlineStyle OUTLINE_DEFAULTS[OUTLINE_COUNT] = { { 3, 0 }, { 3, -2 }, { 8.3f, -3.3f }, { 6, 0 }, { 12, 0 }, { 11.1f, -8.5f }, { 5.8f, 3.8f }, { 4, 0 }, { 3, 0 }, { 2.7f, 5.1f, 0, 4.1f }, { 12, 0 }, { 3, 0 }, { 4, 4.1f, 5.2f, 4.9f } };
OutlineStyle g_Outlines[OUTLINE_COUNT] = { { 3, 0 }, { 3, -2 }, { 8.3f, -3.3f }, { 6, 0 }, { 12, 0 }, { 11.1f, -8.5f }, { 5.8f, 3.8f }, { 4, 0 }, { 3, 0 }, { 2.7f, 5.1f, 0, 4.1f }, { 12, 0 }, { 3, 0 }, { 4, 4.1f, 5.2f, 4.9f } };

const LabelStyle LABEL_DEFAULTS[LABEL_COUNT] = { { SIDE_BOTTOM, 0, 0 }, { SIDE_BOTTOM, 0, -8 }, { SIDE_LEFT, -6, 0 }, { SIDE_BOTTOM, 0, 0 } };
LabelStyle g_Labels[LABEL_COUNT] = { { SIDE_BOTTOM, 0, 0 }, { SIDE_BOTTOM, 0, -8 }, { SIDE_LEFT, -6, 0 }, { SIDE_BOTTOM, 0, 0 } };

// None, match, partial, wrong, still to place, found by the Netherworld search.
const float MARK_DEFAULTS[MARK_COUNT][3] = { { 1, 1, 1 }, { 0.35f, 0.82f, 0.35f }, { 0.96f, 0.59f, 0.16f }, { 0.94f, 0.24f, 0.24f }, { 0.87f, 0.21f, 0.87f }, { 0.55f, 1, 0.2f } };
float g_MarkColors[MARK_COUNT][3] = { { 1, 1, 1 }, { 0.35f, 0.82f, 0.35f }, { 0.96f, 0.59f, 0.16f }, { 0.94f, 0.24f, 0.24f }, { 0.87f, 0.21f, 0.87f }, { 0.55f, 1, 0.2f } };

const char* OutlineName(int Kind)
{
	static const char* names[] = { "Legacies", "Stone sockets", "Round upgrades", "Square upgrades", "Skills", "Skill bar pickers", "Attributes", "Reapers", "Runes", "Gear slots", "Tooltip pips", "Tooltip special stats", "Ultimatums" };
	return Kind >= 0 && Kind < OUTLINE_COUNT ? names[Kind] : "";
}

Live g_Live;

bool g_NetherSearch = true;
bool g_NetherAll = false;
std::vector<NetherRule> g_NetherRules;

static bool g_KeyWasDown = false;
static bool g_MarksKeyWasDown = false;

template <typename... Args>
static void Log(const char* Format, Args... Arguments)
{
	DbgPrintEx(LOG_SEVERITY_INFO, (std::string("[SlormHUD] ") + Format).c_str(), Arguments...);
}

static void ResetConfig()
{
	g_Builds.clear();
	g_ActiveBuild = -1;
	g_Visible = true;
	g_Marks = true;
	std::copy(std::begin(OUTLINE_DEFAULTS), std::end(OUTLINE_DEFAULTS), std::begin(g_Outlines));
	memcpy(g_MarkColors, MARK_DEFAULTS, sizeof(g_MarkColors));
	std::copy(std::begin(LABEL_DEFAULTS), std::end(LABEL_DEFAULTS), std::begin(g_Labels));
	g_Appearance = Appearance();
	g_NetherSearch = true;
	g_NetherAll = false;
	g_NetherRules.clear();
	g_Layout.clear();
}

// Before 1.3.0 the settings were in SlormHUD.txt and the window layout in SlormHUD.layout.ini, next to the DLL.
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
		if (first == "build")
		{
			// build KEY NAME
			std::string key, name, error;
			words >> key;
			std::getline(words >> std::ws, name);
			Build build;
			if (ParseBuild(key, build, error))
			{
				if (!name.empty()) build.name = name;
				g_Builds.push_back(std::move(build));
			}
			continue;
		}
		if (first == "active_build") { words >> g_ActiveBuild; continue; }
		if (first == "visible") { int flag = 1; words >> flag; g_Visible = flag != 0; continue; }
		if (first == "outline")
		{
			// outline KIND WIDTH GROW X Y; older files stop after GROW
			int kind = -1;
			OutlineStyle style;
			if (words >> kind >> style.width >> style.grow && kind >= 0 && kind < OUTLINE_COUNT)
			{
				if (!(words >> style.x >> style.y)) style.x = style.y = 0;
				g_Outlines[kind] = style;
			}
			continue;
		}
		if (first == "label")
		{
			// label KIND SIDE X Y
			int kind = -1;
			LabelStyle style;
			if (words >> kind >> style.side >> style.x >> style.y && kind >= 0 && kind < LABEL_COUNT && style.side >= 0 && style.side < SIDE_COUNT)
				g_Labels[kind] = style;
			continue;
		}
		if (first == "mark_color")
		{
			// mark_color KIND RED GREEN BLUE
			int kind = -1;
			float color[3] = {};
			if (words >> kind >> color[0] >> color[1] >> color[2] && kind > 0 && kind < MARK_COUNT)
				std::copy(color, color + 3, g_MarkColors[kind]);
			continue;
		}
		if (first == "nether")
		{
			// nether SLOT COUNT
			std::string slot;
			NetherRule rule;
			words >> slot >> rule.count;
			rule.slot = -2;
			if (slot == "any") rule.slot = -1;
			for (int kind = 0; kind < SLOT_KINDS; kind++)
				if (slot == GEAR_SLOT_REFS[kind]) rule.slot = kind;
			if (rule.slot >= -1 && rule.count >= 1) g_NetherRules.push_back(rule);
			continue;
		}
		if (first == "nether_search") { int flag = 1; words >> flag; g_NetherSearch = flag != 0; continue; }
		if (first == "nether_all") { int flag = 0; words >> flag; g_NetherAll = flag != 0; continue; }
		if (first == "marks") { int flag = 1; words >> flag; g_Marks = flag != 0; continue; }
		if (first == "theme") { words >> g_Appearance.theme; continue; }
		if (first == "accent") { int on = 0; words >> on >> g_Appearance.accent[0] >> g_Appearance.accent[1] >> g_Appearance.accent[2]; g_Appearance.custom_accent = on != 0; continue; }
		if (first == "primary") { int on = 0; words >> on >> g_Appearance.primary[0] >> g_Appearance.primary[1] >> g_Appearance.primary[2]; g_Appearance.custom_primary = on != 0; continue; }
		if (first == "opacity") { words >> g_Appearance.opacity; continue; }
		if (first == "scale") { words >> g_Appearance.scale; continue; }
		if (first == "hotkey") { words >> g_Appearance.hotkey; if (g_Appearance.hotkey < 8 || g_Appearance.hotkey > 254) g_Appearance.hotkey = VK_F3; continue; }
		if (first == "marks_hotkey") { words >> g_Appearance.marks_hotkey; if (g_Appearance.marks_hotkey < 8 || g_Appearance.marks_hotkey > 254) g_Appearance.marks_hotkey = VK_F4; continue; }
	}
	std::ifstream layout(g_ConfigPath.parent_path() / "SlormHUD.layout.ini");
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

static int ValidKey(double Key, int Default)
{
	return Key >= 8 && Key <= 254 ? static_cast<int>(Key) : Default;
}

// Over the defaults: anything missing or out of range keeps its default.
static void ApplyConfig(const sj::Json& Config)
{
	if (const sj::Json* builds = Config.Find("builds"))
		for (const sj::Json& saved : builds->a)
		{
			Build build;
			std::string error;
			if (!ParseBuild(saved.GetStr("key"), build, error))
				continue;
			std::string name = saved.GetStr("name");
			if (!name.empty()) build.name = name;
			g_Builds.push_back(std::move(build));
		}
	g_ActiveBuild = static_cast<int>(Config.GetNum("active_build", g_ActiveBuild));
	if (const sj::Json* outlines = Config.Find("outlines"))
		for (int kind = 0; kind < OUTLINE_COUNT && kind < static_cast<int>(outlines->a.size()); kind++)
		{
			const sj::Json& saved = outlines->a[kind];
			OutlineStyle& style = g_Outlines[kind];
			style.width = static_cast<float>(saved.GetNum("width", style.width));
			style.grow = static_cast<float>(saved.GetNum("grow", style.grow));
			style.x = static_cast<float>(saved.GetNum("x", style.x));
			style.y = static_cast<float>(saved.GetNum("y", style.y));
		}
	if (const sj::Json* labels = Config.Find("labels"))
		for (int kind = 0; kind < LABEL_COUNT && kind < static_cast<int>(labels->a.size()); kind++)
		{
			const sj::Json& saved = labels->a[kind];
			LabelStyle& style = g_Labels[kind];
			int side = static_cast<int>(saved.GetNum("side", style.side));
			if (side >= 0 && side < SIDE_COUNT) style.side = side;
			style.x = static_cast<float>(saved.GetNum("x", style.x));
			style.y = static_cast<float>(saved.GetNum("y", style.y));
		}
	if (const sj::Json* colors = Config.Find("mark_colors"))
		for (int kind = 1; kind < MARK_COUNT && kind < static_cast<int>(colors->a.size()); kind++)
			ReadColor(&colors->a[kind], g_MarkColors[kind]);
	if (const sj::Json* rules = Config.Find("nether_rules"))
		for (const sj::Json& saved : rules->a)
		{
			std::string slot = saved.GetStr("slot");
			NetherRule rule;
			rule.count = static_cast<int>(saved.GetNum("count", 0));
			rule.slot = slot == "any" ? -1 : -2;
			for (int kind = 0; kind < SLOT_KINDS; kind++)
				if (slot == GEAR_SLOT_REFS[kind]) rule.slot = kind;
			if (rule.slot >= -1 && rule.count >= 1) g_NetherRules.push_back(rule);
		}
	g_NetherSearch = Config.GetBool("nether_search", g_NetherSearch);
	g_NetherAll = Config.GetBool("nether_all", g_NetherAll);
	g_Appearance.theme = static_cast<int>(Config.GetNum("theme", g_Appearance.theme));
	g_Appearance.custom_accent = Config.GetBool("custom_accent", false);
	ReadColor(Config.Find("accent"), g_Appearance.accent);
	g_Appearance.custom_primary = Config.GetBool("custom_primary", false);
	ReadColor(Config.Find("primary"), g_Appearance.primary);
	g_Appearance.opacity = static_cast<float>(Config.GetNum("opacity", g_Appearance.opacity));
	g_Appearance.scale = static_cast<float>(Config.GetNum("scale", g_Appearance.scale));
	g_Appearance.hotkey = ValidKey(Config.GetNum("hotkey", VK_F3), VK_F3);
	g_Appearance.marks_hotkey = ValidKey(Config.GetNum("marks_hotkey", VK_F4), VK_F4);
	g_Layout = Config.GetStr("layout");
	if (g_ActiveBuild < -1 || g_ActiveBuild >= static_cast<int>(g_Builds.size()))
		g_ActiveBuild = g_Builds.empty() ? -1 : 0;
}

static sj::Json ConfigJson()
{
	sj::Json config = sj::Json::Object();
	sj::Json builds = sj::Json::Array();
	for (const Build& build : g_Builds)
	{
		sj::Json saved = sj::Json::Object();
		saved.Add("key", build.key);
		saved.Add("name", build.name);
		builds.Push(std::move(saved));
	}
	config.Add("builds", std::move(builds));
	config.Add("active_build", g_ActiveBuild);
	sj::Json outlines = sj::Json::Array();
	for (const OutlineStyle& style : g_Outlines)
	{
		sj::Json saved = sj::Json::Object();
		saved.Add("width", Rounded(style.width));
		saved.Add("grow", Rounded(style.grow));
		saved.Add("x", Rounded(style.x));
		saved.Add("y", Rounded(style.y));
		outlines.Push(std::move(saved));
	}
	config.Add("outlines", std::move(outlines));
	sj::Json labels = sj::Json::Array();
	for (const LabelStyle& style : g_Labels)
	{
		sj::Json saved = sj::Json::Object();
		saved.Add("side", style.side);
		saved.Add("x", Rounded(style.x));
		saved.Add("y", Rounded(style.y));
		labels.Push(std::move(saved));
	}
	config.Add("labels", std::move(labels));
	// Indexed by Mark, None included.
	sj::Json colors = sj::Json::Array();
	for (const float* color : g_MarkColors)
		colors.Push(ColorJson(color));
	config.Add("mark_colors", std::move(colors));
	sj::Json rules = sj::Json::Array();
	for (const NetherRule& rule : g_NetherRules)
	{
		sj::Json saved = sj::Json::Object();
		saved.Add("slot", rule.slot < 0 ? "any" : GEAR_SLOT_REFS[rule.slot]);
		saved.Add("count", rule.count);
		rules.Push(std::move(saved));
	}
	config.Add("nether_rules", std::move(rules));
	config.Add("nether_search", g_NetherSearch);
	config.Add("nether_all", g_NetherAll);
	config.Add("theme", g_Appearance.theme);
	config.Add("custom_accent", g_Appearance.custom_accent);
	config.Add("accent", ColorJson(g_Appearance.accent));
	config.Add("custom_primary", g_Appearance.custom_primary);
	config.Add("primary", ColorJson(g_Appearance.primary));
	config.Add("opacity", Rounded(g_Appearance.opacity));
	config.Add("scale", Rounded(g_Appearance.scale));
	config.Add("hotkey", g_Appearance.hotkey);
	config.Add("marks_hotkey", g_Appearance.marks_hotkey);
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

// Everything but the two switches under Options > Mods lives in SlormAPI's data file for the mod.
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

// The saved settings, or the old files' the first time; true when they were imported.
static bool LoadConfig()
{
	ResetConfig();
	sj::Json command = Command("mod_data");
	command.Add("mod", MOD_NAME);
	std::optional<sj::Json> saved = Request(command);
	if (!saved)
		return false;
	if (saved->type == sj::Json::Obj)
	{
		ApplyConfig(*saved);
		return false;
	}
	if (!ImportOldConfig())
		return false;
	Log("imported %d builds from %s", static_cast<int>(g_Builds.size()), g_ConfigPath.string().c_str());
	StoreConfig();
	return true;
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
		std::string key = data.GetStr("key");
		if (key == "visible") g_Visible = g_SettingVisible = data.GetBool("value", g_Visible);
		else if (key == "marks") g_Marks = g_SettingMarks = data.GetBool("value", g_Marks);
	}
	catch (const std::exception& error)
	{
		Log("bad setting event: %s", error.what());
	}
}

// The hotkeys and the window change the two switches too; Options > Mods follows them.
static void SyncSettings()
{
	if (g_Visible != g_SettingVisible)
	{
		g_SettingVisible = g_Visible;
		SetSetting("visible", g_Visible);
	}
	if (g_Marks != g_SettingMarks)
	{
		g_SettingMarks = g_Marks;
		SetSetting("marks", g_Marks);
	}
}

static sj::Json Switch(const char* Key, const char* Label, const char* Help)
{
	sj::Json setting = sj::Json::Object();
	setting.Add("key", Key);
	setting.Add("label", Label);
	setting.Add("help", Help);
	setting.Add("type", "bool");
	setting.Add("default", true);
	return setting;
}

// Lists the mod on the MODS page, loads its settings and puts two switches under Options > Mods; false without SlormAPI.
static bool Register()
{
	AurieInterfaceBase* base = nullptr;
	if (!AurieSuccess(ObGetInterface(SLORMAPI_INTERFACE_NAME, base)) || !base)
		return false;
	g_Api = static_cast<SlormAPIInterface*>(base);
	std::lock_guard guard(g_Lock);

	sj::Json registration = Command("register_mod");
	registration.Add("name", MOD_NAME);
	registration.Add("file", "SlormHUD.dll");
	registration.Add("version", SLORMHUD_VERSION);
	registration.Add("author", "Crash");
	registration.Add("description", "Marks a slorm-planner build on the game's screens");
	sj::Json needs = sj::Json::Object();
	needs.Add("SlormAPI", "1.0.0");
	registration.Add("requires", std::move(needs));
	registration.Add("game", "1.0.2c");
	Request(registration);

	bool imported = LoadConfig();
	// SlormAPI's tab cuts a help line past about fifty characters.
	sj::Json settings = sj::Json::Array();
	settings.Push(Switch("visible", "Show the window", "Same as its hotkey, F3 unless changed"));
	settings.Push(Switch("marks", "Outline the build", "On the game's screens; F4 unless changed"));
	sj::Json command = Command("register_settings");
	command.Add("mod", MOD_NAME);
	command.Add("settings", std::move(settings));
	std::optional<sj::Json> values = Request(command);
	g_SettingVisible = values ? values->GetBool("visible", true) : g_Visible;
	g_SettingMarks = values ? values->GetBool("marks", true) : g_Marks;
	// Imported switches win once, and reach Options > Mods from the first step.
	if (!imported)
	{
		g_Visible = g_SettingVisible;
		g_Marks = g_SettingMarks;
	}
	if (!g_Api->Subscribe("setting_changed", SettingChanged, nullptr))
		Log("the switches under Options > Mods will not take effect until the game restarts");
	return true;
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

// One hero's row of a [hero][index] table, as numbers.
static std::vector<int> HeroRow(const RValue& Table, int Hero)
{
	std::vector<int> out;
	if (!Table.IsArray())
		return out;
	std::vector<RValue> heroes = Table.ToVector();
	if (Hero < 0 || Hero >= static_cast<int>(heroes.size()) || !heroes[Hero].IsArray())
		return out;
	for (const RValue& value : heroes[Hero].ToVector())
		out.push_back(static_cast<int>(ToNumber(value)));
	return out;
}

// An item is [header, line, ...]; the header holds its slot's name and its level.
// The game's own list of what a tier can roll on an item, as the blacksmith shows it.
static PFUNC_YYGMLScript g_RollStats = nullptr;
// The UI instance and its partner from the last step, to call game scripts with.
static CInstance* g_Self = nullptr;
static CInstance* g_Other = nullptr;
// Off at the title screen, where there is no character for the game's scripts to work on.
static bool g_InGame = false;

// Fills in the best roll of each stat on an item. The game is asked once per item and tier, then remembered.
static void ReadBestRolls(const RValue& Held, LiveItem& Item)
{
	if (!g_RollStats || !g_Self || !g_InGame || !Item.present)
		return;
	static std::map<std::string, double> known;
	for (const char* tier : { "N", "D", "M", "R", "E" })
	{
		bool any = false;
		for (const LiveAffix& affix : Item.affixes)
			if (affix.tier == tier && !affix.stat.empty()) any = true;
		if (!any)
			continue;
		// What can roll depends on the slot, the level and what the item already holds.
		std::string item_key = Item.slot + ' ' + std::to_string(Item.level) + ' ' + tier;
		for (const LiveAffix& affix : Item.affixes) item_key += ' ' + affix.tier + affix.stat;
		if (!known.count(item_key))
		{
			known[item_key] = 1;
			RValue arguments[3] = { Held, RValue(tier), RValue(1.0) };
			RValue* pointers[3] = { &arguments[0], &arguments[1], &arguments[2] };
			RValue list;
			g_RollStats(g_Self, g_Other, list, 3, pointers);
			std::string seen;
			if (list.IsArray())
				for (const RValue& row : list.ToVector())
				{
					// [lowest, highest, stat, ...]
					std::vector<RValue> fields = row.IsArray() ? row.ToVector() : std::vector<RValue>();
					if (fields.size() < 3 || !fields[2].IsString())
						continue;
					known[item_key + '|' + fields[2].ToString()] = ToNumber(fields[1]);
					for (const LiveAffix& affix : Item.affixes)
						if (affix.tier == tier && affix.stat == fields[2].ToString())
							seen += ' ' + affix.stat + " at " + std::to_string(static_cast<int>(affix.roll)) + " of " + std::to_string(static_cast<int>(ToNumber(fields[1])));
				}
#ifdef SLORM_DEV
			Log("best rolls, %s level %d tier %s:%s", Item.slot.c_str(), Item.level, tier, seen.empty() ? " none of the item's stats listed" : seen.c_str());
#endif
		}
		for (LiveAffix& affix : Item.affixes)
		{
			if (affix.tier != tier || affix.stat.empty())
				continue;
			auto found = known.find(item_key + '|' + affix.stat);
			if (found != known.end()) affix.best = found->second;
		}
	}
}

static LiveItem ReadItemPlain(const RValue& Held)
{
	LiveItem item;
	std::vector<RValue> parts = Held.IsArray() ? Held.ToVector() : std::vector<RValue>();
	if (parts.size() < 2 || !parts[0].IsArray())
		return item;
	item.present = true;
	std::vector<RValue> header = parts[0].ToVector();
	if (header.size() > 1 && header[1].IsString()) item.slot = Lower(header[1].ToString());
	if (header.size() > 2) item.level = static_cast<int>(ToNumber(header[2]));
	for (size_t i = 1; i < parts.size(); i++)
	{
		if (!parts[i].IsArray())
			continue;
		std::vector<RValue> fields = parts[i].ToVector();
		if (fields.size() < 2 || !fields[0].IsString())
			continue;
		LiveAffix affix;
		affix.tier = fields[0].ToString();
		// Stats name themselves; the rest go by number.
		if (fields[1].IsString() && affix.tier.size() == 1 && affix.tier != "L") affix.stat = fields[1].ToString();
		else affix.id = static_cast<int>(ToNumber(fields[1]));
		if (fields.size() > 2) affix.roll = ToNumber(fields[2]);
		if (fields.size() > 4 && !affix.stat.empty()) affix.pure = static_cast<int>(ToNumber(fields[4]));
		item.affixes.push_back(std::move(affix));
	}
	return item;
}

static LiveItem ReadItem(const RValue& Held)
{
	LiveItem item = ReadItemPlain(Held);
	ReadBestRolls(Held, item);
	return item;
}

static void ReadLive(CInstance* Self)
{
	auto global = [](const char* Name) { return g_Yytk->CallBuiltin("variable_global_get", { RValue(Name) }); };
	auto local = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };

	Live live;
	live.hero = static_cast<int>(ToNumber(global("current_hero")));
	RValue menu = local("menu_show");
	if (menu.IsString()) live.menu = menu.ToString();
	g_InGame = live.menu != "main" && !live.menu.empty();
	// While a screen is open the UI works on its own copies, so Undo can drop them.
	if (live.menu == "elements")
	{
		live.legacy_ranks = HeroRow(local("temp_elems"), live.hero);
		live.stones = HeroRow(local("temp_elems_equipped"), live.hero);
		live.realms = HeroRow(local("temp_elems_realms_equipped"), live.hero);
	}
	if (live.legacy_ranks.empty() || live.stones.empty() || live.realms.empty())
	{
		live.legacy_ranks = HeroRow(global("elements_rank"), live.hero);
		live.stones = HeroRow(global("elements_equipped"), live.hero);
		live.realms = HeroRow(global("elements_realms"), live.hero);
	}
	if (live.menu == "skills")
	{
		live.skill_ranks = HeroRow(local("temp_skills"), live.hero);
		live.skill_slots = HeroRow(local("temp_equipped"), live.hero);
	}
	if (live.skill_ranks.empty() || live.skill_slots.empty())
	{
		live.skill_ranks = HeroRow(global("skill_rank"), live.hero);
		live.skill_slots = HeroRow(global("skill_equipped"), live.hero);
	}
	live.activables = HeroRow(global("aura_array"), live.hero);
	live.attributes = HeroRow(global("traits"), live.hero);
	// Equipped items head the hero's inventory.
	RValue bags = global("inventory");
	if (bags.IsArray())
	{
		std::vector<RValue> heroes = bags.ToVector();
		if (live.hero >= 0 && live.hero < static_cast<int>(heroes.size()) && heroes[live.hero].IsArray())
			for (int slot = 0; slot < GEAR_SLOTS; slot++)
				live.gear.push_back(ReadItem(g_Yytk->CallBuiltin("array_get", { heroes[live.hero], RValue(static_cast<double>(slot)) })));
	}
#ifdef SLORM_DEV
	// Which slot each equipped item says it is, once, to check the order.
	static bool slots_logged = false;
	if (!slots_logged && !live.gear.empty())
	{
		slots_logged = true;
		std::string order;
		for (const LiveItem& item : live.gear) order += (item.present ? item.slot : "-") + " ";
		Log("gear slots in order: %s", order.c_str());
	}
#endif
	// Each ultimatum is [unlocked, level, equipped by the knight, by the huntress, by the mage].
	RValue ultimatums = global("ultimatums");
	if (ultimatums.IsArray() && live.hero >= 0 && live.hero < 3)
	{
		std::vector<RValue> list = ultimatums.ToVector();
		for (int id = 0; id < static_cast<int>(list.size()); id++)
		{
			std::vector<RValue> fields = list[id].IsArray() ? list[id].ToVector() : std::vector<RValue>();
			if (fields.size() < 5 || ToNumber(fields[2 + live.hero]) == 0)
				continue;
			live.ultimatum = id;
			live.ultimatum_level = static_cast<int>(ToNumber(fields[1]));
		}
	}
	RValue reapers = global("current_weapon"), runes = global("fast_check_rune_equipped");
	if (reapers.IsArray())
	{
		std::vector<RValue> held = reapers.ToVector();
		if (live.hero >= 0 && live.hero < static_cast<int>(held.size())) live.reaper = static_cast<int>(ToNumber(held[live.hero]));
	}
	if (runes.IsArray())
		for (const RValue& rune : runes.ToVector()) live.runes.push_back(static_cast<int>(ToNumber(rune)));
	// Each hero has a row of the map's places; a warlord's holds its reward legendaries.
	RValue places = global("warlords_data");
	if (places.IsArray())
	{
		std::vector<RValue> heroes = places.ToVector();
		if (live.hero >= 0 && live.hero < static_cast<int>(heroes.size()) && heroes[live.hero].IsArray())
			for (const auto& [id, lord] : Warlords())
			{
				RValue held = g_Yytk->CallBuiltin("array_get", { heroes[live.hero], RValue(static_cast<double>(id)) });
				std::vector<int>& rewards = live.nether_rewards[id];
				if (held.IsArray())
					for (const RValue& reward : held.ToVector()) rewards.push_back(static_cast<int>(ToNumber(reward)));
				else if (held.IsString())
				{
					// As the save writes them: "16.155.147".
					std::stringstream parts(held.ToString());
					for (std::string part; std::getline(parts, part, '.');)
						if (!part.empty() && isdigit(static_cast<unsigned char>(part[0]))) rewards.push_back(atoi(part.c_str()));
				}
			}
	}
	live.valid = !live.legacy_ranks.empty() && !live.stones.empty() && !live.realms.empty();
	g_Live = std::move(live);
}

static int At(const std::vector<int>& Values, int Index)
{
	return Index >= 0 && Index < static_cast<int>(Values.size()) ? Values[Index] : 0;
}

bool NetherMatch(const std::vector<int>& Rewards, std::string& Note)
{
	Note.clear();
	int counts[SLOT_KINDS] = {};
	for (int reward : Rewards)
	{
		int slot = LegendarySlot(reward);
		if (slot >= 0) counts[slot]++;
	}
	bool every = !g_NetherRules.empty();
	for (const NetherRule& rule : g_NetherRules)
	{
		bool met = false;
		for (int slot = 0; slot < SLOT_KINDS; slot++)
		{
			if ((rule.slot >= 0 && rule.slot != slot) || counts[slot] < rule.count)
				continue;
			met = true;
			std::string text = std::to_string(counts[slot]) + ' ' + GEAR_SLOT_NAMES[slot];
			if (Note.find(text) == std::string::npos) Note += (Note.empty() ? "" : ", ") + text;
		}
		if (!met) every = false;
	}
	return g_NetherAll ? every : !Note.empty();
}

Mark UpgradeMark(const Build& Build, int Id, int& Rank, int& Wanted)
{
	Wanted = -1;
	for (const BuildSkill& skill : Build.skills)
		for (const Ranked& upgrade : skill.upgrades)
			if (upgrade.id == Id) Wanted = upgrade.rank;
	Rank = At(g_Live.skill_ranks, Id);
	bool equipped = At(g_Live.skill_slots, Id) > 0;
	if (Wanted < 0)
	{
		// One the build leaves out only matters on a skill the build uses.
		int parent = UpgradeParent(Build.hero, Id);
		bool used = parent >= 0 && (parent == Build.support || parent == Build.primary || parent == Build.secondary);
		return equipped && used ? Mark::Wrong : Mark::None;
	}
	if (!equipped) return Mark::Todo;
	return Rank == Wanted ? Mark::Match : Rank == 0 ? Mark::Wrong : Mark::Partial;
}

Mark AttributeMark(const Build& Build, int Id, int& Rank, int& Wanted)
{
	Wanted = Id >= 0 && Id < ATTRIBUTE_COUNT ? Build.attributes[Id] : 0;
	Rank = At(g_Live.attributes, Id);
	if (Rank == Wanted) return Wanted ? Mark::Match : Mark::None;
	if (Rank > Wanted) return Mark::Wrong;
	return Rank ? Mark::Partial : Mark::Todo;
}

// An equipment slot's side on a 1920-wide screen.
constexpr double GEAR_SLOT_SIZE = 64;

// How much of what the build lists an item has: its stats in the build's tiers, its legendary and special stats.
static int SharedStats(const BuildItem& Wanted, const LiveItem& Item)
{
	int shared = 0;
	for (const BuildAffix& affix : Wanted.affixes)
		for (const LiveAffix& held : Item.affixes)
			if (held.stat == affix.stat && held.tier == affix.tier) shared++;
	// The legendary and the special stats tell two rings apart when their stats do not.
	for (const LiveAffix& held : Item.affixes)
	{
		if (held.tier == "L" && held.id == Wanted.legendary) shared += 3;
		if (held.tier == "RP" && held.id == Wanted.smith) shared++;
		if (held.tier == "MA" && held.id == Wanted.mastery) shared++;
		if (held.tier == "AT" && held.id == Wanted.attribute) shared++;
	}
	return shared;
}

// Either ring can go on either hand, so the build's two are paired with the worn two whichever way fits better.
static bool RingsCrossed()
{
	if (g_ActiveBuild < 0 || g_ActiveBuild >= static_cast<int>(g_Builds.size()))
		return false;
	const BuildItem* wanted[2] = {};
	for (const BuildItem& item : g_Builds[g_ActiveBuild].items)
		if (item.index == 6 || item.index == 7) wanted[item.index - 6] = &item;
	const LiveItem* worn[2] = {};
	int found = 0;
	for (const LiveItem& item : g_Live.gear)
		if (item.present && item.slot == "ring" && found < 2) worn[found++] = &item;
	if (!wanted[0] || !wanted[1] || found < 2)
		return false;
	return SharedStats(*wanted[0], *worn[1]) + SharedStats(*wanted[1], *worn[0]) > SharedStats(*wanted[0], *worn[0]) + SharedStats(*wanted[1], *worn[1]);
}

// Where the build's item is worn: the equipped item naming the same slot, the second such for the right ring.
// An empty slot names nothing, so it falls back to the build's own order. -1 when there is nowhere.
static int GearIndex(const BuildItem& Item)
{
	int skip = Item.index == 7 ? 1 : 0;
	if ((Item.index == 6 || Item.index == 7) && RingsCrossed()) skip = 1 - skip;
	for (int i = 0; i < static_cast<int>(g_Live.gear.size()); i++)
		if (g_Live.gear[i].present && g_Live.gear[i].slot == Item.slot && skip-- == 0) return i;
	bool free = Item.index >= 0 && Item.index < static_cast<int>(g_Live.gear.size()) && !g_Live.gear[Item.index].present;
	return free ? Item.index : -1;
}

static const LiveItem* LiveGear(const BuildItem& Item)
{
	int index = GearIndex(Item);
	return index >= 0 && g_Live.gear[index].present ? &g_Live.gear[index] : nullptr;
}

// The best roll a stat can have, for when the game has not said. Percent stats roll on a smaller scale on low-level items.
static double BestRoll(const std::string& Tier, const std::string& Stat, int Level)
{
	double base = Tier == "N" ? 100 : Tier == "E" ? 40 : 65;
	if (!Stat.ends_with("_percent") && !Stat.ends_with("_mult"))
		return base;
	int step = Level >= 52 ? 5 : Level >= 45 ? 4 : Level >= 35 ? 3 : Level >= 20 ? 2 : 1;
	return base * step / 5;
}

// The roll the build asks of a stat on an item of this level. The planner keeps rolls on the full scale,
// so a lower one is taken as a share of the best.
static double WantedRoll(const BuildAffix& Affix, int Level, double Known)
{
	double full = Affix.tier == "N" ? 100 : Affix.tier == "E" ? 40 : 65, best = Known > 0 ? Known : BestRoll(Affix.tier, Affix.stat, Level);
	return Affix.roll >= full ? best : std::round(best * Affix.roll / full);
}

static std::string Number(double Value)
{
	char text[32];
	snprintf(text, sizeof(text), "%g", Value);
	return text;
}

// One of an item's stats against what the build has for that slot: Match, Wrong when the build has no such stat,
// Partial in another tier, under the build's roll, or not pure where the build's is.
static Mark StatAgainst(const BuildItem& Wanted, const LiveItem& Item, const LiveAffix& Held, std::string& Note)
{
	Note.clear();
	const BuildAffix* other = nullptr;
	for (const BuildAffix& affix : Wanted.affixes)
	{
		if (affix.stat != Held.stat)
			continue;
		if (affix.tier != Held.tier)
		{
			other = &affix;
			continue;
		}
		// Any purity will do; how pure is not asked.
		if (affix.pure && Held.pure <= 100)
		{
			Note = "not pure";
			return Mark::Partial;
		}
		double need = WantedRoll(affix, Item.level, Held.best);
		if (Held.roll >= need)
			return Mark::Match;
		Note = "roll " + Number(Held.roll) + " of " + Number(need);
		return Mark::Partial;
	}
	if (!other)
		return Mark::Wrong;
	Note = "in another tier";
	return Mark::Partial;
}

Mark GearStatMark(const BuildItem& Item, const BuildAffix& Affix, std::string& Note)
{
	Note.clear();
	const LiveItem* live = LiveGear(Item);
	if (!live)
		return Mark::Todo;
	const LiveAffix* elsewhere = nullptr;
	for (const LiveAffix& held : live->affixes)
	{
		if (held.stat != Affix.stat)
			continue;
		if (held.tier != Affix.tier)
		{
			elsewhere = &held;
			continue;
		}
		if (Affix.pure && held.pure <= 100)
		{
			Note = "not pure";
			return Mark::Partial;
		}
		double need = WantedRoll(Affix, live->level, held.best);
		if (held.roll >= need)
			return Mark::Match;
		Note = "at roll " + Number(held.roll) + " of " + Number(need);
		return Mark::Partial;
	}
	if (!elsewhere)
		return Mark::Todo;
	Note = "in tier " + elsewhere->tier;
	return Mark::Partial;
}

Mark GearSpecialMark(const BuildItem& Item, const char* Tier, int Id, int Value, std::string& Note)
{
	Note.clear();
	const LiveItem* live = LiveGear(Item);
	if (live)
		for (const LiveAffix& held : live->affixes)
			if (held.tier == Tier && held.id == Id)
			{
				// A legendary with no range, or one whose roll shows the same values, has nothing to gain.
				bool fixed = std::string_view(Tier) == "L" && (!LegendaryHasRange(Id) || !LegendaryGains(Id, held.roll, Value));
				if (fixed || held.roll >= Value)
					return Mark::Match;
				Note = "at " + Number(held.roll) + " of " + std::to_string(Value);
				return Mark::Partial;
			}
	return Mark::Todo;
}

Mark GearMark(const BuildItem& Item)
{
	if (!LiveGear(Item))
		return Mark::Todo;
	int wanted = 0, found = 0, started = 0;
	std::string note;
	auto count = [&](Mark Kind) { wanted++; if (Kind == Mark::Match) found++; if (Kind != Mark::Todo) started++; };
	for (const BuildAffix& affix : Item.affixes) count(GearStatMark(Item, affix, note));
	if (Item.smith >= 0) count(GearSpecialMark(Item, "RP", Item.smith, Item.smith_value, note));
	if (Item.mastery >= 0) count(GearSpecialMark(Item, "MA", Item.mastery, Item.mastery_value, note));
	if (Item.attribute >= 0) count(GearSpecialMark(Item, "AT", Item.attribute, Item.attribute_value, note));
	if (Item.legendary >= 0) count(GearSpecialMark(Item, "L", Item.legendary, Item.legendary_value, note));
	return found == wanted ? Mark::Match : started ? Mark::Partial : Mark::Todo;
}

Mark UltimatumMark(const Build& Build, int Id)
{
	if (Build.ultimatum < 0)
		return Mark::None;
	bool equipped = Id == g_Live.ultimatum;
	if (Id != Build.ultimatum)
		return equipped ? Mark::Wrong : Mark::None;
	if (!equipped)
		return Mark::Todo;
	return g_Live.ultimatum_level >= Build.ultimatum_level ? Mark::Match : Mark::Partial;
}

Mark RuneMark(const Build& Build, int Id)
{
	if (Id < 0 || Id >= static_cast<int>(g_Live.runes.size()) || g_Live.runes[Id] < 0)
		return Mark::None;
	bool wanted = false;
	for (const Ranked& rune : Build.runes)
		if (rune.id == Id) wanted = true;
	bool equipped = g_Live.runes[Id] > 0;
	if (wanted) return equipped ? Mark::Match : Mark::Todo;
	return equipped ? Mark::Wrong : Mark::None;
}

bool SkillInWrongSlot(const Build& Build, int Id)
{
	int slot = At(g_Live.skill_slots, Id);
	if (Id == Build.primary) return slot > 0 && slot != SLOT_PRIMARY;
	if (Id == Build.secondary) return slot > 0 && slot != SLOT_SECONDARY;
	return false;
}

Mark SkillMark(const Build& Build, int Id)
{
	bool wanted = Id == Build.support || Id == Build.primary || Id == Build.secondary;
	bool equipped = At(g_Live.skill_slots, Id) > 0;
	if (!wanted)
		return equipped ? Mark::Wrong : Mark::None;
	if (!equipped)
		return Mark::Todo;
	if (SkillInWrongSlot(Build, Id))
		return Mark::Partial;
	int rank = 0, target = 0;
	for (int id = 0; id < static_cast<int>(g_Live.skill_slots.size()); id++)
	{
		if (UpgradeParent(Build.hero, id) != Id)
			continue;
		Mark mark = UpgradeMark(Build, id, rank, target);
		if (mark != Mark::None && mark != Mark::Match) return Mark::Partial;
	}
	return Mark::Match;
}

// GameMaker colours are blue, green, red from the high byte down.
static double MarkColor(Mark Kind)
{
	const float* color = g_MarkColors[static_cast<int>(Kind)];
	auto byte = [](float Part) { return static_cast<int>(std::clamp(Part, 0.0f, 1.0f) * 255.0f + 0.5f); };
	return static_cast<double>(byte(color[0]) | (byte(color[1]) << 8) | (byte(color[2]) << 16));
}

// The game's UI scale against a 2560-wide screen's, which the outline and text settings are given for.
static double g_Unit = 1;

// Set while the inventory screen shows an item's or a reaper's tooltip.
static bool g_OnlyHovered = false;
static double g_MouseX = 0, g_MouseY = 0;
// Where the open menu rests on screen. A menu's areas are measured from its own corner, and on a
// screen that is not 16:9 the game centres the menu, so its corner is not the screen's.
static double g_MenuX = 0, g_MenuY = 0;

// A ring around the middle of a box, or a frame around the box for the square kinds.
static void Outline(double Left, double Top, double Right, double Bottom, Mark Kind, OutlineKind Shape)
{
	const OutlineStyle& style = g_Outlines[Shape];
	// With a tooltip up, only what the mouse is on keeps its outline; the tooltip would sit under the rest.
	if (g_OnlyHovered && (g_MouseX < Left || g_MouseX > Right || g_MouseY < Top || g_MouseY > Bottom))
		return;
	double width = std::clamp(static_cast<double>(style.width), 1.0, 20.0) * g_Unit, grow = style.grow * g_Unit;
	Left += style.x * g_Unit;
	Right += style.x * g_Unit;
	Top += style.y * g_Unit;
	Bottom += style.y * g_Unit;
	if (Shape == OUTLINE_UPGRADE || Shape == OUTLINE_SKILL || Shape == OUTLINE_BAR || Shape == OUTLINE_REAPER || Shape == OUTLINE_GEAR || Shape == OUTLINE_ULTIMATUM)
	{
		g_Yytk->CallBuiltin("draw_set_color", { RValue(MarkColor(Kind)) });
		double l = Left - grow, t = Top - grow, r = Right + grow, b = Bottom + grow;
		auto bar = [](double X1, double Y1, double X2, double Y2)
		{
			// A filled rectangle takes in its far corner, so stop one short.
			g_Yytk->CallBuiltin("draw_rectangle", { RValue(X1), RValue(Y1), RValue(X2 - 1), RValue(Y2 - 1), RValue(false) });
		};
		bar(l - width, t - width, r + width, t);
		bar(l - width, b, r + width, b + width);
		bar(l - width, t, l, b);
		bar(r, t, r + width, b);
		return;
	}
	g_Yytk->CallBuiltin("draw_set_color", { RValue(MarkColor(Kind)) });
	// The game's shapes land most of a pixel right and down of its sprites.
	constexpr double nudge = 0.75;
	double x = (Left + Right) / 2 - nudge, y = (Top + Bottom) / 2 - nudge, radius = (std::max)(1.0, (Right - Left) / 2 + grow);
	// Drawn as a strip of triangles: the game's own circle lands off the centre.
	// Four steps make a square stood on its corner, as the attribute icons are.
	bool diamond = Shape == OUTLINE_ATTRIBUTE || Shape == OUTLINE_SPECIAL;
	int steps = diamond ? 4 : 40;
	// Corner to corner the line is wider than it is across.
	double outer = radius + (diamond ? width * 1.41421356 : width);
	g_Yytk->CallBuiltin("draw_primitive_begin", { RValue(5.0) });
	for (int i = 0; i <= steps; i++)
	{
		double angle = i * 6.283185307179586 / steps, across = cos(angle), down = sin(angle);
		g_Yytk->CallBuiltin("draw_vertex", { RValue(x + across * radius), RValue(y + down * radius) });
		g_Yytk->CallBuiltin("draw_vertex", { RValue(x + across * outer), RValue(y + down * outer) });
	}
	g_Yytk->CallBuiltin("draw_primitive_end", {});
}

// Text beside a box, edged in black so it reads over the game's art. It sits clear of the box's outline; OUTLINE_COUNT for a box with none.
static void Label(double Left, double Top, double Right, double Bottom, const std::string& Text, Mark Kind, LabelKind Where, OutlineKind Shape)
{
	const LabelStyle& style = g_Labels[Where];
	double reach = (Shape == OUTLINE_COUNT ? 4 : g_Outlines[Shape].width + (std::max)(0.0f, g_Outlines[Shape].grow) + 4) * g_Unit;
	double X = (Left + Right) / 2, Y = (Top + Bottom) / 2;
	// GameMaker aligns 0 from the left or top, 1 centred, 2 from the right or bottom.
	double across = 1, down = 1;
	switch (style.side)
	{
	case SIDE_TOP: Y = Top - reach; down = 2; break;
	case SIDE_LEFT: X = Left - reach; across = 2; break;
	case SIDE_RIGHT: X = Right + reach; across = 0; break;
	default: Y = Bottom + reach; down = 0; break;
	}
	X += style.x * g_Unit;
	Y += style.y * g_Unit;
	g_Yytk->CallBuiltin("draw_set_halign", { RValue(across) });
	g_Yytk->CallBuiltin("draw_set_valign", { RValue(down) });
	static const int edge[][2] = { { -2, 0 }, { 2, 0 }, { 0, -2 }, { 0, 2 }, { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } };
	g_Yytk->CallBuiltin("draw_set_color", { RValue(0.0) });
	for (const auto& offset : edge)
		g_Yytk->CallBuiltin("draw_text", { RValue(X + offset[0]), RValue(Y + offset[1]), RValue(Text.c_str()) });
	g_Yytk->CallBuiltin("draw_set_color", { RValue(MarkColor(Kind)) });
	g_Yytk->CallBuiltin("draw_text", { RValue(X), RValue(Y), RValue(Text.c_str()) });
}

// Cuts drawing off at a panel's edge, and gives back what to hand Unclip. The scissor is in window pixels, the panel in GUI units.
static RValue Clip(double Left, double Top, double Right, double Bottom)
{
	RValue old_scissor = g_Yytk->CallBuiltin("gpu_get_scissor", {});
	double scale_x = ToNumber(g_Yytk->CallBuiltin("window_get_width", {})) / (std::max)(1.0, ToNumber(g_Yytk->CallBuiltin("display_get_gui_width", {})));
	double scale_y = ToNumber(g_Yytk->CallBuiltin("window_get_height", {})) / (std::max)(1.0, ToNumber(g_Yytk->CallBuiltin("display_get_gui_height", {})));
	g_Yytk->CallBuiltin("gpu_set_scissor", { RValue((Left + g_MenuX) * scale_x), RValue((Top + g_MenuY) * scale_y), RValue((Right - Left) * scale_x), RValue((Bottom - Top) * scale_y) });
	return old_scissor;
}

static void Unclip(const RValue& Old)
{
	if (!Old.IsStruct())
		return;
	auto part = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_struct_get", { Old, RValue(Name) }); };
	g_Yytk->CallBuiltin("gpu_set_scissor", { part("x"), part("y"), part("w"), part("h") });
}

// Outlines the ancestral legacy map against the build. The UI keeps a list of what can be
// clicked this frame, with screen rectangles: mba for legacies, mbas for stone slots.
static void MarkLegacies(CInstance* Self, const Build& Build)
{
	auto get = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
	RValue legacies = get("mba"), slots = get("mbas");
	if (!legacies.IsArray() || !slots.IsArray())
		return;
	// The map is clipped to its panel; the lists are not.
	double left = ToNumber(get("tree_x")), top = ToNumber(get("tree_y"));
	double right = left + ToNumber(get("default_tree_w")), bottom = top + ToNumber(get("default_tree_h"));
	auto inside = [&](double X1, double Y1, double X2, double Y2) { return X2 >= left && X1 <= right && Y2 >= top && Y1 <= bottom; };

	// The lists stand still while the map is dragged, so shift them by how far it has moved since they last changed.
	static double seen_mark = 0, seen_x = 0, seen_y = 0;
	double pan_x = ToNumber(get("tree_off_x")), pan_y = ToNumber(get("tree_off_y")), zoom = ToNumber(get("tree_zoom"));
	std::vector<RValue> slot_list = slots.ToVector();
	double mark = static_cast<double>(slot_list.size());
	if (!slot_list.empty() && slot_list[0].IsArray())
		for (const RValue& field : slot_list[0].ToVector()) mark = mark * 31 + ToNumber(field);
	if (mark != seen_mark)
	{
		seen_mark = mark;
		seen_x = pan_x;
		seen_y = pan_y;
	}
	double shift_x = (seen_x - pan_x) * zoom, shift_y = (seen_y - pan_y) * zoom;

	RValue old_scissor = Clip(left, top, right, bottom);

	for (const RValue& entry : legacies.ToVector())
	{
		if (!entry.IsArray())
			continue;
		// [name, ?, x, y, width, height, ?, sprite, legacy id]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 9 || !fields[0].IsString() || fields[0].ToString() != "Elements")
			continue;
		int id = static_cast<int>(ToNumber(fields[8]));
		double x = ToNumber(fields[2]) + shift_x, y = ToNumber(fields[3]) + shift_y, width = ToNumber(fields[4]), height = ToNumber(fields[5]);
		if (!inside(x, y, x + width, y + height))
			continue;
		const Ranked* wanted = nullptr;
		for (const Ranked& legacy : Build.legacies)
			if (legacy.id == id) wanted = &legacy;
		bool active = At(g_Live.realms, LegacyRealm(id)) != 0;
		int rank = At(g_Live.legacy_ranks, id);
		Mark mark = Mark::None;
		if (wanted) mark = !active ? Mark::Todo : rank == 0 ? Mark::Wrong : rank == wanted->rank ? Mark::Match : Mark::Partial;
		else if (active) mark = Mark::Wrong;
		if (mark == Mark::None)
			continue;
		Outline(x, y, x + width, y + height, mark, OUTLINE_LEGACY);
		if (!wanted)
			continue;
		// Ranks put in against the build's: "2 < 10", or the rank alone once it matches.
		std::string label = std::to_string(rank);
		if (rank != wanted->rank)
			label += (rank < wanted->rank ? " < " : " > ") + std::to_string(wanted->rank);
		Label(x, y, x + width, y + height, label, mark, LABEL_LEGACY, OUTLINE_LEGACY);
	}

	for (const RValue& entry : slot_list)
	{
		if (!entry.IsArray())
			continue;
		// [left, right, top, bottom, slot id, ...]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 5)
			continue;
		double x1 = ToNumber(fields[0]) + shift_x, x2 = ToNumber(fields[1]) + shift_x, y1 = ToNumber(fields[2]) + shift_y, y2 = ToNumber(fields[3]) + shift_y;
		int id = static_cast<int>(ToNumber(fields[4]));
		if (!inside(x1, y1, x2, y2))
			continue;
		bool wanted = id == Build.first_node || std::find(Build.nodes.begin(), Build.nodes.end(), id) != Build.nodes.end();
		bool placed = At(g_Live.stones, id) != 0;
		if (wanted || placed)
			Outline(x1, y1, x2, y2, !wanted ? Mark::Wrong : placed ? Mark::Match : Mark::Todo, OUTLINE_SOCKET);
	}

	Unclip(old_scissor);
}

// Half a warlord's icon at full zoom, on a 1920-wide screen.
constexpr double NETHER_ICON = 32;

// The Netherworld map: under each warlord whose possible rewards meet the search, what it met.
static void MarkNether(CInstance* Self)
{
	auto get = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
	// The launcher keeps the map while its other pages show; only the Netherworld page lists warlords.
	bool listed = false;
	RValue areas = get("mba");
	if (areas.IsArray())
		for (const RValue& entry : areas.ToVector())
		{
			RValue name = entry.IsArray() ? g_Yytk->CallBuiltin("array_get", { entry, RValue(0.0) }) : RValue();
			if (name.IsString() && name.ToString() == "Warlords")
			{
				listed = true;
				break;
			}
		}
	if (!listed)
		return;
	double scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	if (scale <= 0)
		scale = 1;
	// The picture's corner sits elsewhere once the map is enlarged.
	bool large = ToNumber(get("nethermap_size")) != 0;
	double origin_x = ToNumber(get(large ? "nethermap_x" : "nethermap_small_x")), origin_y = ToNumber(get(large ? "nethermap_y" : "nethermap_small_y"));
	double left = ToNumber(get("nethermap_x")), top = ToNumber(get("nethermap_y"));
	double width = ToNumber(get("default_nethermap_w")), height = ToNumber(get("default_nethermap_h"));
	double pan_x = ToNumber(get("nethermap_off_x")), pan_y = ToNumber(get("nethermap_off_y")), zoom = ToNumber(get("nethermap_zoom"));
	if (width <= 0 || height <= 0 || zoom <= 0)
		return;

	RValue old_scissor = Clip(left, top, left + width, top + height);
	for (const auto& [id, rewards] : g_Live.nether_rewards)
	{
		std::string note;
		auto lord = Warlords().find(id);
		if (lord == Warlords().end() || !NetherMatch(rewards, note))
			continue;
		double x = origin_x + (lord->second.x * scale - pan_x) * zoom, y = origin_y + (lord->second.y * scale - pan_y) * zoom;
		double half = NETHER_ICON * scale * zoom;
		if (x + half < left || x - half > left + width || y + half < top || y - half > top + height)
			continue;
		Label(x - half, y - half, x + half, y + half, note, Mark::Search, LABEL_NETHER, OUTLINE_COUNT);
	}
	Unclip(old_scissor);
}

// Outlines the skill screen: the row of skills, the specializations, and the grid of upgrades.
static void MarkSkills(CInstance* Self, const Build& Build)
{
	RValue areas = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("mba") });
	if (!areas.IsArray())
		return;
	for (const RValue& entry : areas.ToVector())
	{
		if (!entry.IsArray())
			continue;
		// [name, ?, x, y, width, height, ?, sprite, id]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 9 || !fields[0].IsString())
			continue;
		std::string kind = fields[0].ToString();
		int id = static_cast<int>(ToNumber(fields[8]));
		double x = ToNumber(fields[2]), y = ToNumber(fields[3]), width = ToNumber(fields[4]), height = ToNumber(fields[5]);
		if (kind == "Upgrade")
		{
			int rank = 0, wanted = 0;
			Mark mark = UpgradeMark(Build, id, rank, wanted);
			if (mark == Mark::None)
				continue;
			bool square = UpgradeIsSquare(Build.hero, id);
			Outline(x, y, x + width, y + height, mark, square ? OUTLINE_UPGRADE : OUTLINE_PASSIVE);
			// The game shows the rank itself; say what the build has when they differ.
			if (wanted >= 0 && rank != wanted)
				Label(x, y, x + width, y + height, std::to_string(rank) + (rank < wanted ? "<" : ">") + std::to_string(wanted), mark, LABEL_UPGRADE, square ? OUTLINE_UPGRADE : OUTLINE_PASSIVE);
		}
		else if (kind == "Choose_Skill")
		{
			Mark mark = SkillMark(Build, id);
			if (mark != Mark::None)
				Outline(x, y, x + width, y + height, mark, OUTLINE_SKILL);
		}
		// A specialization is listed only while another is taken; each brings its own support skill.
		else if (kind == "Choose_Spec" && id == Build.support)
			Outline(x, y, x + width, y + height, Mark::Todo, OUTLINE_SKILL);
	}
}

// The inventory screen's fixed layouts are measured from its page tabs, which the game does list:
// three down the middle, named inv_0 to inv_2, the top one at 616,136 on a 1920-wide screen.
// Shift comes back as how far the screen sits from there, so the layouts hold where the panel is placed differently.
static void InventoryShift(CInstance* Self, double Scale, double& X, double& Y)
{
	X = Y = 0;
	RValue slots = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("mbas") });
	if (!slots.IsArray())
		return;
	for (const RValue& entry : slots.ToVector())
	{
		if (!entry.IsArray())
			continue;
		// [left, right, top, bottom, name, kind, ...]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 6 || !fields[4].IsString() || !fields[5].IsString() || fields[5].ToString() != "panel_switcher")
			continue;
		std::string name = fields[4].ToString();
		if (name.size() != 5 || name.compare(0, 4, "inv_") != 0 || name[4] < '0' || name[4] > '2')
			continue;
		X = ToNumber(fields[0]) - 616 * Scale;
		Y = ToNumber(fields[2]) - (136 + 272 * (name[4] - '0')) * Scale;
		return;
	}
}

// The attributes on the inventory screen: an outline on each icon, and a tick on its bar where the build stops.
// The game lists the panel's areas only while there is a point to spend, so the layout is kept from a
// 1920-wide screen and corrected by the areas whenever they are there.
static void MarkAttributes(CInstance* Self, const Build& Build)
{
	// The inventory screen has three pages: gear, reapers, attributes.
	RValue page = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("inventory_panel") });
	if (static_cast<int>(ToNumber(page)) != 2)
		return;
	double scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	if (scale <= 0)
		scale = 1;
	struct Row { double x, y, size, bar_left, bar_top, bar_height; };
	double shift_x = 0, shift_y = 0;
	InventoryShift(Self, scale, shift_x, shift_y);
	Row rows[ATTRIBUTE_COUNT];
	for (int id = 0; id < ATTRIBUTE_COUNT; id++)
		rows[id] = { shift_x + (id % 2 ? 774 : 790) * scale, shift_y + (359 + 60 * id) * scale, 36 * scale, shift_x + 838 * scale, shift_y + (367 + 60 * id) * scale, 20 * scale };

	RValue areas = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("mba") });
	if (areas.IsArray())
	{
		bool bar_seen[ATTRIBUTE_COUNT] = {};
		for (const RValue& entry : areas.ToVector())
		{
			if (!entry.IsArray())
				continue;
			// [name, ?, x, y, width, height, ?, sprite, "attribute|direction" or "attribute|direction|kind"]
			std::vector<RValue> fields = entry.ToVector();
			if (fields.size() < 9 || !fields[0].IsString() || fields[0].ToString() != "character_plus_trait" || !fields[8].IsString())
				continue;
			std::string tag = fields[8].ToString();
			int id = atoi(tag.c_str());
			if (id < 0 || id >= ATTRIBUTE_COUNT)
				continue;
			double x = ToNumber(fields[2]), y = ToNumber(fields[3]), width = ToNumber(fields[4]), height = ToNumber(fields[5]);
			if (tag.size() > 4 && tag.compare(tag.size() - 4, 4, "|big") == 0)
			{
				rows[id].x = x;
				rows[id].y = y;
				rows[id].size = width;
			}
			// The bar comes as up to two areas, the points in and the points left; it starts at the leftmost.
			else if (std::count(tag.begin(), tag.end(), '|') == 1 && (!bar_seen[id] || x < rows[id].bar_left))
			{
				bar_seen[id] = true;
				rows[id].bar_left = x;
				rows[id].bar_top = y;
				rows[id].bar_height = height;
			}
		}
	}

	for (int id = 0; id < ATTRIBUTE_COUNT; id++)
	{
		const Row& row = rows[id];
		int rank = 0, wanted = 0;
		Mark mark = AttributeMark(Build, id, rank, wanted);
		if (mark == Mark::None)
			continue;
		Outline(row.x, row.y, row.x + row.size, row.y + row.size, mark, OUTLINE_ATTRIBUTE);
		if (rank == wanted)
			continue;
		Label(row.x, row.y, row.x + row.size, row.y + row.size, std::to_string(rank) + (rank < wanted ? "<" : ">") + std::to_string(wanted), mark, LABEL_ATTRIBUTE, OUTLINE_ATTRIBUTE);
		// The bar shows the points put in, then the ones gear adds, so the build's end lies that much further along.
		int bonus = 0;
		for (const LiveItem& item : g_Live.gear)
			for (const LiveAffix& affix : item.affixes)
				if (affix.tier == "AT" && affix.id == id) bonus += static_cast<int>(affix.roll);
		// The first point starts 32 in, and each is 8 wide.
		double at = row.bar_left + (32 + 8 * (wanted + bonus)) * scale;
		g_Yytk->CallBuiltin("draw_set_color", { RValue(MarkColor(mark)) });
		g_Yytk->CallBuiltin("draw_rectangle", { RValue(at - 2 * scale), RValue(row.bar_top - 4 * scale), RValue(at + 2 * scale - 1), RValue(row.bar_top + row.bar_height + 4 * scale - 1), RValue(false) });
	}
}

// The equipment slots on the inventory screen's gear page. The game lists no areas for them,
// so they are placed as measured on a 1920-wide screen.
static void MarkGear(CInstance* Self, const Build& Build)
{
	RValue page = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("inventory_panel") });
	if (static_cast<int>(ToNumber(page)) != 0)
		return;
	double scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	if (scale <= 0)
		scale = 1;
	double shift_x = 0, shift_y = 0;
	InventoryShift(Self, scale, shift_x, shift_y);
	static const double corners[GEAR_SLOTS][2] = {
		{ 884, 188 }, { 1112, 260 }, { 756, 260 }, { 756, 340 }, { 756, 420 }, { 934, 540 },
		{ 848, 508 }, { 1020, 508 }, { 984, 188 }, { 1112, 420 }, { 1112, 340 },
	};
	for (const BuildItem& item : Build.items)
	{
		int index = GearIndex(item);
		if (index < 0 || index >= GEAR_SLOTS)
			continue;
		double x = shift_x + corners[index][0] * scale, y = shift_y + corners[index][1] * scale;
		Outline(x, y, x + GEAR_SLOT_SIZE * scale, y + GEAR_SLOT_SIZE * scale, GearMark(item), OUTLINE_GEAR);
	}
}

#ifdef SLORM_DEV
static std::string Show(const RValue& Value, int Depth, size_t Limit);
#endif

// The game's colour for pure stats, as it packs colours: blue, green, red.
constexpr double PURE_COLOR = 70 + 231 * 256 + 176 * 65536;

// A solid square with a thin black edge. One about a graft is in the pure colour below its diagonal.
static void Pip(double X, double Y, Mark Kind, bool Grafted = false)
{
	const OutlineStyle& style = g_Outlines[OUTLINE_PIP];
	double half = std::clamp(static_cast<double>(style.width), 2.0, 40.0) / 2 * g_Unit;
	X += style.x * g_Unit;
	Y += style.y * g_Unit;
	g_Yytk->CallBuiltin("draw_set_color", { RValue(0.0) });
	g_Yytk->CallBuiltin("draw_rectangle", { RValue(X - half - 2), RValue(Y - half - 2), RValue(X + half + 1), RValue(Y + half + 1), RValue(false) });
	g_Yytk->CallBuiltin("draw_set_color", { RValue(MarkColor(Kind)) });
	g_Yytk->CallBuiltin("draw_rectangle", { RValue(X - half), RValue(Y - half), RValue(X + half - 1), RValue(Y + half - 1), RValue(false) });
	if (!Grafted)
		return;
	g_Yytk->CallBuiltin("draw_set_color", { RValue(PURE_COLOR) });
	g_Yytk->CallBuiltin("draw_triangle", { RValue(X + half), RValue(Y - half), RValue(X + half), RValue(Y + half), RValue(X - half), RValue(Y + half), RValue(false) });
}

// An item's tooltip: a pip on the bar beside each stat, for whether the build has that stat on that slot.
// The game keeps where the tooltip is and how far down each line sits.
static void MarkTooltip(CInstance* Self, const Build& Build)
{
	auto get = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
	RValue type = get("tooltip_type"), lines = get("subtooltip_snapping"), panels = get("subtooltip_surfs");
	if (ToNumber(get("show_tooltip")) == 0 || !type.IsString() || type.ToString() != "item" || !lines.IsArray() || !panels.IsArray())
		return;
	std::vector<RValue> panel_list = panels.ToVector();
	if (panel_list.empty() || !panel_list[0].IsArray())
		return;
	// [surface, x, y, ...]
	std::vector<RValue> panel = panel_list[0].ToVector();
	if (panel.size() < 3)
		return;
	double scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	if (scale <= 0)
		scale = 1;
	// On a 1920-wide screen the bar runs down the panel 102.75 in from its left, and a line's middle is just above where it starts.
	double bar = ToNumber(panel[1]) + 102.75 * scale, top = ToNumber(panel[2]) - 0.6 * scale;
	// A tall tooltip scrolls inside its panel: [surface, x, y, height, width, scrolled].
	double scrolled = panel.size() > 5 ? ToNumber(panel[5]) : 0;
	if (scrolled == 0)
		scrolled = ToNumber(get("subtooltip_surfs_scroll"));
	// What shows is the full height less how far it can scroll.
	double panel_top = ToNumber(panel[2]), panel_bottom = panel_top + (panel.size() > 4 ? ToNumber(panel[3]) - ToNumber(panel[4]) : 2000.0);
	top -= scrolled;
#ifdef SLORM_DEV
	// The panel's numbers, logged as they change, to learn how scrolling shows in them.
	static std::string logged_panel;
	std::string now = Show(panels, 2, 8) + " scroll " + Show(get("subtooltip_surfs_scroll"), 1, 8) + " lines " + Show(lines, 2, 20);
	if (now != logged_panel)
	{
		logged_panel = now;
		Log("tooltip panel %s", now.c_str());
	}
#endif

	// The item the tooltip is for, by its place in the hero's inventory.
	RValue bags = g_Yytk->CallBuiltin("variable_global_get", { RValue("inventory") });
	if (!bags.IsArray())
		return;
	std::vector<RValue> heroes = bags.ToVector();
	if (g_Live.hero < 0 || g_Live.hero >= static_cast<int>(heroes.size()) || !heroes[g_Live.hero].IsArray())
		return;
	// Most tooltips name a place in the inventory; some carry the item itself.
	RValue about = get("tooltip_value");
	if (!about.IsArray() && !about.IsNumberConvertible())
		return;
	LiveItem held = ReadItem(about.IsArray() ? about : g_Yytk->CallBuiltin("array_get", { heroes[g_Live.hero], about }));
	if (!held.present)
		return;

	// The build's item for that slot; of the two rings, the one this item is closer to.
	std::string note;
	const BuildItem* wanted = nullptr;
	int best = -1;
	// A worn item goes with the build item its slot's frame goes with.
	int place = about.IsArray() ? -1 : static_cast<int>(ToNumber(about));
	for (const BuildItem& item : Build.items)
		if (place >= 0 && place < GEAR_SLOTS && GearIndex(item) == place)
		{
			wanted = &item;
			best = 1000;
		}
	for (const BuildItem& item : Build.items)
	{
		if (item.slot != held.slot || best == 1000)
			continue;
		int shared = 0;
		// The legendary and the special stats say more about which ring is meant than any one stat.
		for (const LiveAffix& affix : held.affixes)
		{
			if (affix.tier == "L" && affix.id == item.legendary) shared += 6;
			if (affix.tier == "RP" && affix.id == item.smith) shared += 2;
			if (affix.tier == "MA" && affix.id == item.mastery) shared += 2;
			if (affix.tier == "AT" && affix.id == item.attribute) shared += 2;
		}
		for (const LiveAffix& affix : held.affixes)
			if (!affix.stat.empty())
			{
				// A stat in its tier counts for more than one in another tier.
				Mark mark = StatAgainst(item, held, affix, note);
				shared += mark == Mark::Wrong ? 0 : mark == Mark::Partial && note == "in another tier" ? 1 : 2;
			}
		if (shared > best)
		{
			best = shared;
			wanted = &item;
		}
	}
	if (!wanted)
		return;

	// The tooltip groups lines by tier, and lists a tier's stats by name, whatever order the item holds them in.
	// Only the stat tiers are placed reliably.
	std::vector<RValue> line_list = lines.ToVector();
	std::vector<bool> used(line_list.size(), false);
	std::vector<const LiveAffix*> listed;
	for (const LiveAffix& affix : held.affixes)
		if (!affix.stat.empty()) listed.push_back(&affix);
	std::stable_sort(listed.begin(), listed.end(), [](const LiveAffix* A, const LiveAffix* B) { return StatName(A->stat) < StatName(B->stat); });
	// Pure stats the build has in a tier that the item lacks there: each is a graft still to make.
	std::map<std::string, int> grafts;
	for (const BuildAffix& affix : wanted->affixes)
	{
		bool there = false;
		for (const LiveAffix& other : held.affixes)
			if (other.tier == affix.tier && other.stat == affix.stat) there = true;
		if (affix.pure && !there) grafts[affix.tier]++;
	}
	for (const LiveAffix* entry : listed)
	{
		const LiveAffix& affix = *entry;
		for (size_t line = 0; line < line_list.size(); line++)
		{
			if (used[line] || !line_list[line].IsArray())
				continue;
			std::vector<RValue> fields = line_list[line].ToVector();
			if (fields.size() < 2 || !fields[0].IsString() || fields[0].ToString() != affix.tier)
				continue;
			used[line] = true;
			// A line scrolled out of the panel has no pip.
			double y = top + ToNumber(fields[1]);
			Mark mark = StatAgainst(*wanted, held, affix, note);
			bool grafted = false;
			bool build_pure = false;
			for (const BuildAffix& other : wanted->affixes)
				if (other.stat == affix.stat && other.pure) build_pure = true;
			// Off: the build's pure stat without its purity or in another tier, or a pure stat the build has not.
			if (mark == Mark::Partial && (note == "not pure" || (note == "in another tier" && build_pure))) grafted = true;
			else if (mark == Mark::Wrong && affix.pure > 100) grafted = true;
			// A stat the build has no use for, in a tier still short of a graft, is where the graft goes.
			else if (mark == Mark::Wrong && grafts[affix.tier] > 0)
			{
				grafts[affix.tier]--;
				mark = Mark::Todo;
				grafted = true;
			}
			if (y >= panel_top + 8 * scale && y <= panel_bottom)
				Pip(bar, y, mark, grafted);
			break;
		}
	}

	// The reaper smith, mastery and attribute each have a row lower down, with a diamond icon left of the bar.
	for (const LiveAffix& affix : held.affixes)
	{
		int build_id = affix.tier == "RP" ? wanted->smith : affix.tier == "MA" ? wanted->mastery : affix.tier == "AT" ? wanted->attribute : -2;
		if (build_id == -2)
			continue;
		int build_value = affix.tier == "RP" ? wanted->smith_value : affix.tier == "MA" ? wanted->mastery_value : wanted->attribute_value;
		// One the build leaves empty costs nothing, so it is not marked as wrong.
		Mark mark = build_id < 0 ? Mark::Todo : affix.id != build_id ? Mark::Wrong : affix.roll >= build_value ? Mark::Match : Mark::Partial;
		for (const RValue& entry : line_list)
		{
			std::vector<RValue> fields = entry.IsArray() ? entry.ToVector() : std::vector<RValue>();
			if (fields.size() < 2 || !fields[0].IsString() || fields[0].ToString() != affix.tier)
				continue;
			// The icon's middle sits 78 left of the bar and 23 below where a stat's pip would, on a 1920-wide screen.
			double x = bar - 78 * scale, y = top + ToNumber(fields[1]) + 23 * scale, half = 21 * scale;
			if (y - half >= panel_top && y + half <= panel_bottom)
				Outline(x - half, y - half, x + half, y + half, mark, OUTLINE_SPECIAL);
			break;
		}
	}
}

// Reapers and runes, wherever the inventory screen lists them: the reaper page, the gear page's
// equipped reaper and runes, and the rune list at the reaper forge.
static void MarkReaper(CInstance* Self, const Build& Build)
{
	auto get = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
	int wanted = BuildReaperNumber(Build);
	double scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	if (scale <= 0)
		scale = 1;
	RValue slots = get("mbas"), areas = get("mba");
	// An owned reaper is listed as an area; the build's may not be.
	bool listed = false;
	if (slots.IsArray())
		for (const RValue& entry : slots.ToVector())
		{
			if (!entry.IsArray())
				continue;
			// [left, right, top, bottom, reaper, kind, ...]
			std::vector<RValue> fields = entry.ToVector();
			if (fields.size() < 6 || !fields[5].IsString())
				continue;
			std::string kind = fields[5].ToString();
			if (kind != "weapon_select" && kind != "weapon_switcher")
				continue;
			int id = static_cast<int>(ToNumber(fields[4]));
			// The list shows a reaper once, plain or primordial.
			bool build = id == wanted || (kind == "weapon_select" && id % REAPER_COUNT == Build.reaper.id);
			if (build && kind == "weapon_select") listed = true;
			Mark mark = build ? (g_Live.reaper == wanted ? Mark::Match : Mark::Todo) : id == g_Live.reaper ? Mark::Wrong : Mark::None;
			if (mark == Mark::None)
				continue;
			// The gear page's slot is drawn a little above the area that takes its clicks.
			double lift = kind == "weapon_switcher" ? 8 * scale : 0;
			Outline(ToNumber(fields[0]), ToNumber(fields[2]) - lift, ToNumber(fields[1]), ToNumber(fields[3]) - lift, mark, OUTLINE_REAPER);
		}
	// The reaper page lays every reaper out in a fixed grid, six across and four down, owned or not.
	// One not owned yet is blacked out, drawn smaller, and not listed, so it is placed from the grid.
	int order = ReaperOrder(Build.reaper.id), page = static_cast<int>(ToNumber(get("weapon_page"))) - 1;
	bool on_list = static_cast<int>(ToNumber(get("inventory_panel"))) == 1;
	if (on_list && !listed && order >= 0 && order / 24 == page)
	{
		int cell = order % 24;
		double shift_x = 0, shift_y = 0;
		InventoryShift(Self, scale, shift_x, shift_y);
		double x = shift_x + (880 + 136 * (cell % 6)) * scale, y = shift_y + (344 + 136 * (cell / 6)) * scale;
		Outline(x, y, x + 112 * scale, y + 112 * scale, Mark::Todo, OUTLINE_REAPER);
	}
	if (areas.IsArray())
		for (const RValue& entry : areas.ToVector())
		{
			if (!entry.IsArray())
				continue;
			// [name, ?, x, y, width, height, ?, sprite, rune]
			std::vector<RValue> fields = entry.ToVector();
			if (fields.size() < 9 || !fields[0].IsString())
				continue;
			std::string kind = fields[0].ToString();
			double x = ToNumber(fields[2]), y = ToNumber(fields[3]);
			// The mage's list of ultimatums, and the equipped one's slot on the gear page.
			if (kind == "Ultimatum Equip" || kind == "Ultimatum Open")
			{
				Mark mark = UltimatumMark(Build, kind == "Ultimatum Open" ? g_Live.ultimatum : static_cast<int>(ToNumber(fields[8])));
				if (mark != Mark::None)
					Outline(x, y, x + ToNumber(fields[4]), y + ToNumber(fields[5]), mark, OUTLINE_ULTIMATUM);
				continue;
			}
			if (kind != "Equip Rune" && kind != "Open Rune")
				continue;
			Mark mark = RuneMark(Build, static_cast<int>(ToNumber(fields[8])));
			if (mark != Mark::None)
				Outline(x, y, x + ToNumber(fields[4]), y + ToNumber(fields[5]), mark, OUTLINE_RUNE);
		}
}

// The skill occupying one of the bar's skill slots, -1 for none. The support slot takes whichever support skill is equipped.
static int SkillInSlot(const Build& Build, int Slot)
{
	for (int id = 0; id < static_cast<int>(g_Live.skill_slots.size()); id++)
	{
		if (UpgradeParent(Build.hero, id) >= 0 || g_Live.skill_slots[id] <= 0)
			continue;
		bool support = id < SUPPORT_SKILLS;
		if (Slot == 0 ? support : !support && g_Live.skill_slots[id] == Slot) return id;
	}
	return -1;
}

// The list that opens above a slot of the skill bar to change what it holds.
static void MarkBar(CInstance* Self, const Build& Build)
{
	auto get = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
	int primary = SkillInSlot(Build, SLOT_PRIMARY), secondary = SkillInSlot(Build, SLOT_SECONDARY), support = SkillInSlot(Build, 0);

	double scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	if (scale <= 0)
		scale = 1;

	// The list is only there while a slot is being changed.
	RValue rows = get("act_array"), type = get("panel_type"), value = get("panel_val");
	if (!rows.IsArray() || !type.IsString() || type.ToString().empty())
		return;
	int wanted = -1, held = -1;
	if (type.ToString() == "aura")
	{
		int index = static_cast<int>(ToNumber(value));
		if (index < 0 || index >= ACTIVABLE_COUNT)
			return;
		wanted = Build.activables[index];
		held = index < static_cast<int>(g_Live.activables.size()) ? g_Live.activables[index] : -1;
	}
	else if (value.IsString())
	{
		std::string which = value.ToString();
		if (which == "primary_skill") { wanted = Build.primary; held = primary; }
		else if (which == "secondary_skill") { wanted = Build.secondary; held = secondary; }
		else if (which == "support_skill") { wanted = Build.support; held = support; }
		else return;
	}
	for (const RValue& entry : rows.ToVector())
	{
		if (!entry.IsArray())
			continue;
		// [left, top, right, bottom, "auras" or 0, ?, sprite, skill id, activable id or side, ...]
		std::vector<RValue> fields = entry.ToVector();
		if (fields.size() < 9)
			continue;
		bool aura = fields[4].IsString();
		int id = static_cast<int>(ToNumber(fields[aura ? 8 : 7]));
		if (id != wanted && id != held)
			continue;
		// The icon is a square at the row's left end.
		double left = ToNumber(fields[0]), top = ToNumber(fields[1]), bottom = ToNumber(fields[3]), inset = 6 * scale;
		Mark mark = id != wanted ? Mark::Wrong : id == held ? Mark::Match : Mark::Todo;
		Outline(left + inset, top + inset, left + (bottom - top) - inset, bottom - inset, mark, OUTLINE_BAR);
	}
}

// Runs after the UI has drawn.
static void DrawMarks(CInstance* Self)
{
	std::lock_guard guard(g_Lock);
	if (!g_Marks)
		return;
	// The Netherworld search needs no build.
	bool has_build = g_Live.valid && g_ActiveBuild >= 0 && g_ActiveBuild < static_cast<int>(g_Builds.size()) && g_Builds[g_ActiveBuild].hero == g_Live.hero;
	bool nether = g_NetherSearch && !g_NetherRules.empty();
	if (!has_build && !nether)
		return;
	static const Build no_build;
	const Build& build = has_build ? g_Builds[g_ActiveBuild] : no_build;
	RValue menu = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("menu_show") });
	RValue position = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("menu_position") });
	if (!menu.IsString() || !position.IsString())
		return;
	// The skill bar's pickers open with no menu up; a menu is marked once it has slid into place.
	bool bar = menu.ToString() == "none";
	if (!bar && position.ToString() != "open")
		return;
	// A menu slides in and out; nothing is where it will be until it rests. Where it rests depends on the
	// screen's shape, so rest is the position holding still from one frame to the next.
	auto offset = [&](const char* Name) { return ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) })); };
	static double last_x = 0, last_y = 0;
	double menu_x = offset("menu_x_pos"), menu_y = offset("menu_y_pos");
	bool moving = std::abs(menu_x - last_x) > 0.01 || std::abs(menu_y - last_y) > 0.01;
	last_x = menu_x;
	last_y = menu_y;
#ifdef SLORM_DEV
	// The screen's numbers on each menu, to see what changes with its shape.
	static std::string logged_screen;
	std::string screen = menu.ToString() + " at rest";
	if (!moving && screen != logged_screen)
	{
		logged_screen = screen;
		auto number = [](const char* Name) { return ToNumber(g_Yytk->CallBuiltin(Name, {})); };
		Log("screen on %s: gui %.0fx%.0f, window %.0fx%.0f, ui_scale %.4f, menu at %.2f,%.2f, target %.2f, tree at %.2f,%.2f",
			menu.ToString().c_str(), number("display_get_gui_width"), number("display_get_gui_height"), number("window_get_width"), number("window_get_height"),
			ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") })), menu_x, menu_y, offset("menu_y_target"), offset("tree_x"), offset("tree_y"));
	}
#endif
	if (!bar && moving)
		return;
	double ui_scale = ToNumber(g_Yytk->CallBuiltin("variable_global_get", { RValue("ui_scale") }));
	g_Unit = ui_scale > 0 ? ui_scale * 0.75 : 1;
	RValue old_color = g_Yytk->CallBuiltin("draw_get_color", {}), old_alpha = g_Yytk->CallBuiltin("draw_get_alpha", {});
	RValue old_halign = g_Yytk->CallBuiltin("draw_get_halign", {}), old_valign = g_Yytk->CallBuiltin("draw_get_valign", {});
	g_Yytk->CallBuiltin("draw_set_alpha", { RValue(1.0) });
	// Draw in the menu's own space: everything shifts by where the menu rests.
	g_MenuX = bar ? 0 : menu_x;
	g_MenuY = bar ? 0 : menu_y;
	RValue old_world = g_Yytk->CallBuiltin("matrix_get", { RValue(2.0) });
	auto shift_world = [&](bool On)
	{
		if (!On)
			g_Yytk->CallBuiltin("matrix_set", { RValue(2.0), old_world });
		else
			g_Yytk->CallBuiltin("matrix_set", { RValue(2.0), g_Yytk->CallBuiltin("matrix_build", { RValue(g_MenuX), RValue(g_MenuY), RValue(0.0), RValue(0.0), RValue(0.0), RValue(0.0), RValue(1.0), RValue(1.0), RValue(1.0) }) });
	};
	shift_world(true);
	if (bar)
	{
		if (has_build) MarkBar(Self, build);
	}
	else if (has_build && (menu.ToString() == "elements" || menu.ToString() == "skills"))
	{
		if (menu.ToString() == "elements") MarkLegacies(Self, build);
		else MarkSkills(Self, build);
		// The game keeps the hover tooltip on a screen-sized surface it has already drawn; drawing it
		// again puts it back over the outlines.
		RValue shown = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("show_tooltip") });
		RValue tip = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("tooltip_surf") });
		if (ToNumber(shown) != 0 && ToNumber(g_Yytk->CallBuiltin("surface_exists", { tip })) != 0)
		{
			// The tooltip's surface covers the screen, not the menu.
			shift_world(false);
			g_Yytk->CallBuiltin("draw_surface", { tip, RValue(0.0), RValue(0.0) });
		}
	}
	else if (has_build && menu.ToString() == "inventory")
	{
		RValue shown = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("show_tooltip") });
		RValue type = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("tooltip_type") });
		// Items, reapers, runes and ultimatums open a large tooltip; the small help ones do not cover much.
		RValue about = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("tooltip_value") });
		std::string kind = type.IsString() ? type.ToString() : "", subject = about.IsString() ? about.ToString() : "";
		bool large = kind == "item" || kind == "weapon" || (kind == "super_help" && (subject.rfind("rune|", 0) == 0 || subject.rfind("ultimatum|", 0) == 0));
		g_OnlyHovered = ToNumber(shown) != 0 && large;
		g_MouseX = ToNumber(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { RValue(0.0) })) - g_MenuX;
		g_MouseY = ToNumber(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { RValue(0.0) })) - g_MenuY;
		MarkAttributes(Self, build);
		MarkReaper(Self, build);
		MarkGear(Self, build);
		g_OnlyHovered = false;
		MarkTooltip(Self, build);
	}
	else if (nether)
		MarkNether(Self);
	shift_world(false);
	g_Yytk->CallBuiltin("draw_set_halign", { old_halign });
	g_Yytk->CallBuiltin("draw_set_valign", { old_valign });
	g_Yytk->CallBuiltin("draw_set_color", { old_color });
	g_Yytk->CallBuiltin("draw_set_alpha", { old_alpha });
}

#ifdef SLORM_DEV
bool g_WantDump = false;
std::string g_DumpNote;
std::string g_DumpResult;
static int g_DumpCount = 0;

// Names that get printed in full: the ancestral legacy screen and what it reads.
static bool Interesting(const std::string& Name)
{
	static const char* words[] = { "ele", "ancest", "gem", "realm", "stone", "legac", "aura", "hero", "class", "menu", "tree", "skill", "trait", "mba", "door", "coord", "equip", "temp", "attrib", "stat", "act_", "panel", "activ", "tooltip", "ultim" };
	std::string name = Lower(Name);
	if (name == "mb")
		return true;
	for (const char* word : words)
		if (name.find(word) != std::string::npos) return true;
	return false;
}

static std::string Show(const RValue& Value, int Depth, size_t Limit)
{
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET) return "undefined";
	if (Value.IsString())
	{
		std::string text = Value.ToString();
		if (text.size() > 80) text = text.substr(0, 80) + "...";
		return '"' + text + '"';
	}
	if (Value.IsArray())
	{
		std::vector<RValue> parts = Value.ToVector();
		std::string out = "[" + std::to_string(parts.size()) + "]";
		if (Depth <= 0)
			return out;
		out += " {";
		for (size_t i = 0; i < parts.size() && i < Limit; i++)
			out += (i ? ", " : " ") + Show(parts[i], Depth - 1, Limit);
		if (parts.size() > Limit) out += ", ...";
		return out + " }";
	}
	if (Value.IsStruct()) return "struct";
	if (Value.m_Kind == VALUE_PTR) return "pointer";
	if (Value.m_Kind == VALUE_REF) return "ref " + std::to_string(Value.ToDouble());
	if (Value.IsNumberConvertible())
	{
		char text[32];
		double number = Value.ToDouble();
		snprintf(text, sizeof(text), number == static_cast<long long>(number) ? "%.0f" : "%g", number);
		return text;
	}
	return "kind " + std::to_string(Value.m_Kind);
}

static void DumpNames(std::ofstream& File, const RValue& Names, const std::function<RValue(const std::string&)>& Get)
{
	if (!Names.IsArray())
	{
		File << "(no names)\n";
		return;
	}
	std::vector<std::string> names;
	for (const RValue& name : Names.ToVector())
		if (name.IsString()) names.push_back(name.ToString());
	std::sort(names.begin(), names.end());
	for (const std::string& name : names)
	{
		bool full = Interesting(name);
		File << name << " = " << Show(Get(name), full ? 3 : 1, full ? 400 : 6) << '\n';
	}
}

static void Dump(CInstance* Self)
{
	std::ofstream file(g_ConfigPath.parent_path() / "SlormHUD.dump.txt", g_DumpCount ? std::ios::app : std::ios::trunc);
	g_DumpCount++;
	file << "===== dump " << g_DumpCount << ": " << g_DumpNote << " =====\n";
	file << "--- globals ---\n";
	// -5 is GML's "global".
	DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(-5.0) }),
		[](const std::string& Name) { return g_Yytk->CallBuiltin("variable_global_get", { RValue(Name.c_str()) }); });
	file << "--- obj_ui_next_gen ---\n";
	DumpNames(file, g_Yytk->CallBuiltin("variable_instance_get_names", { RValue(Self) }),
		[&](const std::string& Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name.c_str()) }); });
	RValue width = g_Yytk->CallBuiltin("display_get_gui_width", {}), height = g_Yytk->CallBuiltin("display_get_gui_height", {});
	file << "--- gui " << Show(width, 0, 0) << " x " << Show(height, 0, 0) << " ---\n";
	file << "mouse_gui = " << Show(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { RValue(0.0) }), 0, 0) << ", "
		<< Show(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { RValue(0.0) }), 0, 0) << '\n';
	RValue tip = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("tooltip_surf") });
	if (ToNumber(g_Yytk->CallBuiltin("surface_exists", { tip })) != 0)
		file << "tooltip_surf_size = " << Show(g_Yytk->CallBuiltin("surface_get_width", { tip }), 0, 0) << ", "
			<< Show(g_Yytk->CallBuiltin("surface_get_height", { tip }), 0, 0) << '\n';
	RValue surface = g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue("tree_surf") });
	if (ToNumber(g_Yytk->CallBuiltin("surface_exists", { surface })) != 0)
		file << "tree_surf_size = " << Show(g_Yytk->CallBuiltin("surface_get_width", { surface }), 0, 0) << ", "
			<< Show(g_Yytk->CallBuiltin("surface_get_height", { surface }), 0, 0) << '\n';
	g_DumpResult = "dump " + std::to_string(g_DumpCount) + " written";
	Log("%s", g_DumpResult.c_str());
}
#endif

static void Tick(CInstance* Self, CInstance* Other)
{
	g_Self = Self;
	g_Other = Other;
	std::lock_guard guard(g_Lock);

	bool key_down = (GetAsyncKeyState(g_Appearance.hotkey) & 0x8000) != 0;
	bool marks_key_down = (GetAsyncKeyState(g_Appearance.marks_hotkey) & 0x8000) != 0;
	bool focused = GetForegroundWindow() == GetActiveWindow();
	if (g_SuppressToggle)
	{
		// Wait for the newly bound key to be released.
		if (!key_down && !marks_key_down)
			g_SuppressToggle = false;
		key_down = marks_key_down = true;
	}
	else if (focused && key_down && !g_KeyWasDown)
	{
		g_Visible = !g_Visible;
	}
	else if (focused && marks_key_down && !g_MarksKeyWasDown)
	{
		g_Marks = !g_Marks;
	}
	g_KeyWasDown = key_down;
	g_MarksKeyWasDown = marks_key_down;

	// A few times a second is plenty for a comparison.
	static int wait = 0;
	if (--wait <= 0)
	{
		wait = 4;
		ReadLive(Self);
	}

	SyncSettings();
	if (g_WantSave)
	{
		g_WantSave = false;
		StoreConfig();
	}

#ifdef SLORM_DEV
	// What the mouse is over on the inventory screen, logged as it changes: the slots are not listed anywhere.
	if (g_Live.menu == "inventory")
	{
		auto local = [&](const char* Name) { return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) }); };
		std::string over = Show(local("mouse_above_x"), 0, 0) + "," + Show(local("mouse_above_y"), 0, 0) + " tooltip " + Show(local("tooltip_type"), 1, 8)
			+ " " + Show(local("tooltip_value"), 1, 8) + " at " + Show(local("tooltip_xx"), 0, 0) + "," + Show(local("tooltip_yy"), 0, 0);
		static std::string last_over;
		if (over != last_over)
		{
			last_over = over;
			Log("hover %s", over.c_str());
		}
	}
	static bool dump_key_was_down = false;
	bool dump_key = (GetAsyncKeyState(DUMP_KEY) & 0x8000) != 0;
	if (dump_key && !dump_key_was_down)
	{
		g_DumpNote = "key";
		g_WantDump = true;
	}
	dump_key_was_down = dump_key;
	if (g_WantDump)
	{
		g_WantDump = false;
		Dump(Self);
	}
#endif
}

// Runs for the UI object's Step event, and for its Draw GUI event to outline the build.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
#ifdef SLORM_DEV
	// Each event of the UI objects, logged once, to find the ones that draw.
	static std::unordered_set<CCode*> seen;
	if (code && seen.insert(code).second && code->GetName() && strstr(code->GetName(), "_ui_"))
		Log("event %s", code->GetName());
#endif
	static CCode* draw_code = nullptr;
	if (code && code == draw_code)
	{
		// Another mod may have run the event already; running it twice doubles every click.
		if (!Event.CalledOriginal()) Event.Call();
		if (g_Api) DrawMarks(self);
		return;
	}
	if (!draw_code && code && code->GetName() && std::string_view(code->GetName()) == "gml_Object_obj_ui_next_gen_Draw_64")
	{
		draw_code = code;
		return;
	}
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
			DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormHUD] SlormAPI is not loaded; the mod needs it");
			return;
		}
		Log("connected to SlormAPI, %d builds, overlay hook %s", static_cast<int>(g_Builds.size()), OverlayInstall(g_Yytk) ? "installed" : "FAILED");
	}

#ifdef SLORM_DEV
	// When the overlay takes input from the game, and when it lets go.
	static bool took_mouse = false, took_keys = false;
	if (OverlayWantsMouse() != took_mouse || OverlayWantsKeys() != took_keys)
	{
		took_mouse = OverlayWantsMouse();
		took_keys = OverlayWantsKeys();
		Log("input: mouse %s, keys %s", took_mouse ? "taken" : "free", took_keys ? "taken" : "free");
	}
	// What the game sees on each click.
	static bool was_down = false;
	bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (down != was_down)
	{
		was_down = down;
		auto number = [](const char* Name, std::vector<RValue> Arguments) { return g_Yytk->CallBuiltin(Name, Arguments).ToDouble(); };
		Log("click %s: overlay mouse %d keys %d visible %d, game held %.0f pressed %.0f released %.0f at %.0f,%.0f",
			down ? "down" : "up", OverlayWantsMouse() ? 1 : 0, OverlayWantsKeys() ? 1 : 0, g_Visible ? 1 : 0,
			number("mouse_check_button", { RValue(1.0) }), number("mouse_check_button_pressed", { RValue(1.0) }),
			number("mouse_check_button_released", { RValue(1.0) }),
			number("device_mouse_x_to_gui", { RValue(0.0) }), number("device_mouse_y_to_gui", { RValue(0.0) }));
	}
#endif
	// Input aimed at the overlay must not reach the game's UI.
	if (OverlayWantsMouse())
	{
		g_Yytk->CallBuiltin("mouse_clear", { RValue(1.0) });
		g_Yytk->CallBuiltin("mouse_clear", { RValue(2.0) });
	}
	if (OverlayWantsKeys())
		g_Yytk->CallBuiltin("io_clear", {});

	if (!Event.CalledOriginal()) Event.Call();
	Tick(self, other);
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	g_ConfigPath = ModulePath.parent_path() / "SlormHUD.txt";
	// Saved builds are read against the stat table, from the first step.
	LoadGameData();

	// Without it, best rolls fall back to a rule of thumb.
	CScript* roll_stats = nullptr;
	if (AurieSuccess(g_Yytk->GetNamedRoutinePointer("gml_Script_scr_loot_roll_stats", reinterpret_cast<PVOID*>(&roll_stats))) && roll_stats && roll_stats->m_Functions)
		g_RollStats = roll_stats->m_Functions->m_ScriptFunction;

	AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormHUD] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" SLORMHUD_VERSION " loaded, %d stats", static_cast<int>(g_Stats.size()));
	return AURIE_SUCCESS;
}
