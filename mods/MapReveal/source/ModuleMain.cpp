// The minimap and the large map show the whole map and its icons from the start of a floor,
// instead of what the hero has walked past.
#include <YYToolkit/YYTK_Shared.hpp>
#include <SlormAPI/SlormAPI_Interface.hpp>
#include <SlormAPI/Json.hpp>
#include "Version.hpp"
#include <algorithm>
#include <optional>
using namespace Aurie;
using namespace YYTK;

// How far around the hero the game shows icons, in the room's units; this covers any map.
constexpr double EVERYWHERE = 100000;
// The game uncovers the cells within this many of the hero's.
constexpr int REACH = 16;
// A sweep leaves nothing unseen; a few more are tried in case the game was not ready.
constexpr int SWEEPS = 5;
// GML's ds_type_grid.
constexpr double GRID_TYPE = 5;
// As shown on SlormAPI's MODS page and as the title of the mod's tab under Options > Mods.
constexpr const char* MOD_NAME = "Map Reveal";

static YYTKInterface* g_Yytk = nullptr;
static SlormAPIInterface* g_Api = nullptr;
// The switch under Options > Mods; always on without SlormAPI.
static bool g_Enabled = true;
static bool g_WasEnabled = true;
// What the game had the icon range at.
static double g_Fov = 0;
static PFUNC_YYGMLScript g_Maximap = nullptr;
// Set by a check that found cells the game has not uncovered.
static bool g_Sweep = false;
static double g_Grid = -1;
static int g_Sweeps = 0;
static ULONGLONG g_CheckedAt = 0;

template <typename... Args>
static void Log(const char* Format, Args... Arguments)
{
	DbgPrintEx(LOG_SEVERITY_INFO, (std::string("[MapReveal] ") + Format).c_str(), Arguments...);
}

// Numbers the game keeps as text count too; 0 for anything else.
static double ToNumber(const RValue& Value)
{
	if (Value.m_Kind == VALUE_STRING)
		return atof(Value.ToString().c_str());
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET || !Value.IsNumberConvertible())
		return 0;
	return Value.ToDouble();
}

static RValue Local(CInstance* Self, const char* Name)
{
	return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) });
}

static void Set(CInstance* Self, const char* Name, const RValue& Value)
{
	g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue(Name), Value });
}

static bool GridExists(const RValue& Grid)
{
	return ToNumber(g_Yytk->CallBuiltin("ds_exists", { Grid, RValue(GRID_TYPE) })) > 0;
}

static CInstance* FindInstance(const char* Object)
{
	RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue(Object) });
	if ((object.m_Kind != VALUE_REF && ToNumber(object) < 0) || ToNumber(g_Yytk->CallBuiltin("instance_number", { object })) < 1)
		return nullptr;
	RValue found = g_Yytk->CallBuiltin("instance_find", { object, RValue(0.0) });
	CInstance* instance = nullptr;
	if (!AurieSuccess(g_Yytk->GetInstanceObject(static_cast<int32_t>(found.ToDouble()), instance)))
		return nullptr;
	return instance;
}

// The game marks each cell of the map: 1 floor and -1 wall not yet seen, 100 and -100 once seen and painted.
static bool Holds(const RValue& Grid, double Left, double Top, double Right, double Bottom, std::initializer_list<double> Values)
{
	for (double value : Values)
		if (ToNumber(g_Yytk->CallBuiltin("ds_grid_value_exists", { Grid, RValue(Left), RValue(Top), RValue(Right), RValue(Bottom), RValue(value) })) > 0)
			return true;
	return false;
}

// The script that uncovers the map, whichever map is shown. It does so around the hero when its timer
// runs out, about once a second; so the hero is stood in turn all over the map with the timer run out.
static RValue& MaximapHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	CInstance* hero = g_Sweep ? FindInstance("obj_parent_hero") : nullptr;
	RValue grid(g_Grid);
	if (!hero || !GridExists(grid))
		return g_Maximap(Self, Other, Result, ArgumentCount, Arguments);
	g_Sweep = false;
	int width = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_width", { grid })));
	int height = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("ds_grid_height", { grid })));
	RValue room_width;
	double cell = width > 0 && AurieSuccess(g_Yytk->GetBuiltin("room_width", nullptr, NULL_INDEX, room_width)) ? ToNumber(room_width) / width : 16;
	RValue x = Local(hero, "x"), y = Local(hero, "y"), time = Local(Self, "maxi_time"), timer = Local(Self, "maxi_timer");
	for (int left = 0; left < width; left += REACH * 2)
		for (int top = 0; top < height; top += REACH * 2)
		{
			if (!Holds(grid, left, top, (std::min)(left + REACH * 2, width - 1), (std::min)(top + REACH * 2, height - 1), { 1, -1 }))
				continue;
			Set(hero, "x", RValue((left + REACH + 0.5) * cell));
			Set(hero, "y", RValue((top + REACH + 0.5) * cell));
			Set(Self, "maxi_time", timer);
			RValue unused;
			g_Maximap(Self, Other, unused, ArgumentCount, Arguments);
		}
	Set(hero, "x", x);
	Set(hero, "y", y);
	Set(Self, "maxi_time", time);
	return g_Maximap(Self, Other, Result, ArgumentCount, Arguments);
}

// Each step of the game's UI object.
static void Tick(CInstance* Self)
{
	if (!g_Enabled)
	{
		// Switched off: the icon range goes back; what the map shows already stays until the next floor.
		if (g_WasEnabled)
		{
			g_Sweep = false;
			if (g_Fov > 0 && ToNumber(Local(Self, "minimap_fov")) >= EVERYWHERE)
				Set(Self, "minimap_fov", RValue(g_Fov));
		}
		g_WasEnabled = false;
		return;
	}
	g_WasEnabled = true;
	// Icons are shown for what lies within this of the hero; the game sets it anew on each map.
	double fov = ToNumber(Local(Self, "minimap_fov"));
	if (fov > 0 && fov < EVERYWHERE)
	{
		g_Fov = fov;
		Set(Self, "minimap_fov", RValue(EVERYWHERE));
	}
	ULONGLONG now = GetTickCount64();
	if (now - g_CheckedAt < 500)
		return;
	g_CheckedAt = now;
	RValue grid = Local(Self, "grid_minimap");
	if (!GridExists(grid))
		return;
	g_Grid = grid.ToDouble();
	double right = ToNumber(g_Yytk->CallBuiltin("ds_grid_width", { grid })) - 1, bottom = ToNumber(g_Yytk->CallBuiltin("ds_grid_height", { grid })) - 1;
	// Nothing seen yet: a new map.
	if (!Holds(grid, 0, 0, right, bottom, { 100, -100 }))
		g_Sweeps = 0;
	if (!g_Sweep && g_Sweeps < SWEEPS && Holds(grid, 0, 0, right, bottom, { 1, -1 }))
	{
		g_Sweep = true;
		g_Sweeps++;
		Log("sweep %d", g_Sweeps);
	}
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

// Once per frame on the game thread, after the player moved a switch under Options > Mods.
static void SettingChanged(const char* Event, const char* DataJson, void* User)
{
	try
	{
		sj::Json data = sj::Parse(DataJson ? DataJson : "");
		if (data.GetStr("mod") != MOD_NAME || data.GetStr("key") != "enabled")
			return;
		g_Enabled = data.GetBool("value", true);
		Log("switched %s", g_Enabled ? "on" : "off");
	}
	catch (const std::exception& error)
	{
		Log("bad setting event: %s", error.what());
	}
}

// With SlormAPI installed the mod is listed on the game's MODS page and has a switch under
// Options > Mods; it works without it.
static void Register()
{
	AurieInterfaceBase* base = nullptr;
	if (!AurieSuccess(ObGetInterface(SLORMAPI_INTERFACE_NAME, base)) || !base)
		return;
	g_Api = static_cast<SlormAPIInterface*>(base);

	sj::Json registration = Command("register_mod");
	registration.Add("name", MOD_NAME);
	registration.Add("file", "MapReveal.dll");
	registration.Add("version", MAPREVEAL_VERSION);
	registration.Add("author", "Crash");
	registration.Add("description", "Shows the whole map and its icons from the start of a floor");
	sj::Json needs = sj::Json::Object();
	needs.Add("SlormAPI", "1.0.0");
	registration.Add("requires", std::move(needs));
	registration.Add("game", "1.0.2c");
	if (Request(registration))
		Log("registered with SlormAPI");

	sj::Json setting = sj::Json::Object();
	setting.Add("key", "enabled");
	setting.Add("label", "Reveal the map");
	// SlormAPI's tab cuts a help line past about fifty characters.
	setting.Add("help", "The whole floor and its icons, on both maps");
	setting.Add("type", "bool");
	setting.Add("default", true);
	sj::Json settings = sj::Json::Array();
	settings.a.push_back(std::move(setting));
	sj::Json command = Command("register_settings");
	command.Add("mod", MOD_NAME);
	command.Add("settings", std::move(settings));
	if (std::optional<sj::Json> values = Request(command))
		g_Enabled = values->GetBool("enabled", true);
	if (!g_Api->Subscribe("setting_changed", SettingChanged, nullptr))
		Log("the switch under Options > Mods will not take effect until the game restarts");
}

// Runs once per frame, after the UI object's Step event.
static void CodeCallback(FWCodeEvent& Event)
{
	// This runs for every event of every object, so compare by pointer once the event is known.
	static CCode* step_code = nullptr;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
	if (!code)
		return;
	if (code != step_code)
	{
		if (step_code || !code->GetName() || std::string_view(code->GetName()) != "gml_Object_obj_ui_next_gen_Step_0")
			return;
		step_code = code;
		// Modules load in any order, so SlormAPI is looked for once the game runs.
		Register();
	}
	// Another mod may have run the event already; running it twice doubles every click.
	if (!Event.CalledOriginal()) Event.Call();
	Tick(self);
}

static AurieStatus Hook(AurieModule* Module, const char* Name, PVOID Replacement, PFUNC_YYGMLScript* Original)
{
	CScript* script = nullptr;
	AurieStatus status = g_Yytk->GetNamedRoutinePointer((std::string("gml_Script_") + Name).c_str(), reinterpret_cast<PVOID*>(&script));
	if (!AurieSuccess(status))
		return status;
	if (!script || !script->m_Functions || !script->m_Functions->m_ScriptFunction)
		return AURIE_OBJECT_NOT_FOUND;
	return MmCreateHook(Module, std::string("MapReveal ") + Name, script->m_Functions->m_ScriptFunction, Replacement, reinterpret_cast<PVOID*>(Original));
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	AurieStatus status = Hook(Module, "scr_ui_maximap", MaximapHook, &g_Maximap);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] the game's map script was not hooked: %s", AurieStatusToString(status));
		return status;
	}
	status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[MapReveal] callback failed: %s", AurieStatusToString(status));
		return status;
	}

	Log("v" MAPREVEAL_VERSION " loaded");
	return AURIE_SUCCESS;
}
