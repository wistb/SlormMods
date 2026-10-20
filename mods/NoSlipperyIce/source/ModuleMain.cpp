// Slippery Ice never takes effect: the game is told the cataclysm is not active, and it is
// taken off the run's lists and the Netherworld's table so it is not shown either.
#include <YYToolkit/YYTK_Shared.hpp>
#include <SlormAPI/SlormAPI_Interface.hpp>
#include "Version.hpp"
using namespace Aurie;
using namespace YYTK;

// The cataclysm's number in the game's table.
constexpr double SLIPPERY_ICE = 31;

static YYTKInterface* g_Yytk = nullptr;
static PFUNC_YYGMLScript g_CheckActive = nullptr;
static PFUNC_YYGMLScript g_LoadTables = nullptr;
static PFUNC_YYGMLScript g_Launcher = nullptr;
// The cataclysm as the Netherworld's table names it.
constexpr const char* NAME = "Slippery Ice";

template <typename... Args>
static void Log(const char* Format, Args... Arguments)
{
	DbgPrintEx(LOG_SEVERITY_INFO, (std::string("[NoSlipperyIce] ") + Format).c_str(), Arguments...);
}

// Numbers the game keeps as text count too.
static bool IsSlipperyIce(const RValue& Value)
{
	if (Value.m_Kind == VALUE_STRING)
		return atof(Value.ToString().c_str()) == SLIPPERY_ICE;
	if (Value.m_Kind == VALUE_UNDEFINED || Value.m_Kind == VALUE_UNSET || !Value.IsNumberConvertible())
		return false;
	return Value.ToDouble() == SLIPPERY_ICE;
}

// The run keeps "Cataclysms" as a list of numbers and "Temp Cataclysms" as rows starting with one.
static void Strip(const char* Key)
{
	RValue run_data = g_Yytk->CallBuiltin("variable_global_get", { RValue("run_data") });
	RValue list = g_Yytk->CallBuiltin("ds_map_find_value", { run_data, RValue(Key) });
	if (!g_Yytk->CallBuiltin("is_array", { list }).ToBoolean())
		return;
	int count = static_cast<int>(g_Yytk->CallBuiltin("array_length", { list }).ToDouble());
	for (int at = count - 1; at >= 0; at--)
	{
		RValue entry = g_Yytk->CallBuiltin("array_get", { list, RValue(static_cast<double>(at)) });
		if (g_Yytk->CallBuiltin("is_array", { entry }).ToBoolean())
		{
			if (g_Yytk->CallBuiltin("array_length", { entry }).ToDouble() < 1)
				continue;
			entry = g_Yytk->CallBuiltin("array_get", { entry, RValue(0.0) });
		}
		if (!IsSlipperyIce(entry))
			continue;
		g_Yytk->CallBuiltin("array_delete", { list, RValue(static_cast<double>(at)), RValue(1.0) });
		Log("removed Slippery Ice from the run's %s", Key);
	}
}

static RValue Field(const RValue& Row, int At)
{
	return g_Yytk->CallBuiltin("array_get", { Row, RValue(static_cast<double>(At)) });
}

// A row of the Netherworld's table names a place's cataclysm in field 1 and a warlord's in
// field 2, as "A, B, C". The map's tooltip and the run both read them from there.
static void PatchWarlords()
{
	static bool done = false;
	if (done)
		return;
	RValue table = g_Yytk->CallBuiltin("variable_global_get", { RValue("warlords") });
	if (!g_Yytk->CallBuiltin("is_array", { table }).ToBoolean())
		return;
	int count = static_cast<int>(g_Yytk->CallBuiltin("array_length", { table }).ToDouble());
	if (count < 1)
		return;
	done = true;

	for (int at = 0; at < count; at++)
	{
		RValue row = Field(table, at);
		if (!g_Yytk->CallBuiltin("is_array", { row }).ToBoolean() || g_Yytk->CallBuiltin("array_length", { row }).ToDouble() < 3)
			continue;
		for (int field = 1; field <= 2; field++)
		{
			RValue value = Field(row, field);
			if (value.m_Kind != VALUE_STRING)
				continue;
			std::string text = value.ToString(), kept;
			bool found = false;
			for (size_t from = 0; from <= text.size();)
			{
				size_t to = text.find(", ", from);
				if (to == std::string::npos) to = text.size();
				std::string name = text.substr(from, to - from);
				if (name == NAME) found = true;
				else kept += (kept.empty() ? "" : ", ") + name;
				from = to + 2;
			}
			if (!found)
				continue;
			g_Yytk->CallBuiltin("array_set", { row, RValue(static_cast<double>(field)), RValue(kept) });
			// Read back through the table: a write to a copy would not show there.
			bool stuck = Field(Field(g_Yytk->CallBuiltin("variable_global_get", { RValue("warlords") }), at), field).ToString() == kept;
			Log("Netherworld row %d field %d: \"%s\" -> \"%s\"%s", at, field, text.c_str(), kept.c_str(), stuck ? "" : " (FAILED, the table kept the old text)");
		}
	}
}

static RValue& LoadTablesHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	RValue& result = g_LoadTables(Self, Other, Result, ArgumentCount, Arguments);
	PatchWarlords();
	return result;
}

// Draws the expedition launcher, the Netherworld's map included.
static RValue& LauncherHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	PatchWarlords();
	return g_Launcher(Self, Other, Result, ArgumentCount, Arguments);
}

// The hero's slide and the ice on the floor both ask this each step.
static RValue& CheckActiveHook(CInstance* Self, CInstance* Other, RValue& Result, int ArgumentCount, RValue** Arguments)
{
	if (ArgumentCount < 1 || !Arguments[0] || !IsSlipperyIce(*Arguments[0]))
		return g_CheckActive(Self, Other, Result, ArgumentCount, Arguments);

	PatchWarlords();
	Strip("Cataclysms");
	Strip("Temp Cataclysms");
	Result = RValue(0.0);
	return Result;
}

// With SlormAPI installed the mod is listed on the game's MODS page; it works without it.
static void CodeCallback(FWCodeEvent& Event)
{
	static bool asked = false;
	if (asked)
		return;
	auto& [self, other, code, argument_count, arguments] = Event.Arguments();
	if (!code || !code->GetName() || std::string_view(code->GetName()) != "gml_Object_obj_ui_next_gen_Step_0")
		return;
	// Modules load in any order, so SlormAPI is looked for once the game runs.
	asked = true;
	AurieInterfaceBase* base = nullptr;
	if (!AurieSuccess(ObGetInterface(SLORMAPI_INTERFACE_NAME, base)) || !base)
		return;
	const char* response = static_cast<SlormAPIInterface*>(base)->Request(R"({"cmd":"register_mod","name":"NoSlipperyIce","version":")" NOSLIPPERYICE_VERSION R"(","author":"Crash","description":"Removes the Slippery Ice cataclysm","requires":{"SlormAPI":"1.0.0"}})");
	Log("registered with SlormAPI: %s", response);
}

static AurieStatus Hook(AurieModule* Module, const char* Name, PVOID Replacement, PFUNC_YYGMLScript* Original)
{
	CScript* script = nullptr;
	AurieStatus status = g_Yytk->GetNamedRoutinePointer((std::string("gml_Script_") + Name).c_str(), reinterpret_cast<PVOID*>(&script));
	if (!AurieSuccess(status))
		return status;
	if (!script || !script->m_Functions || !script->m_Functions->m_ScriptFunction)
		return AURIE_OBJECT_NOT_FOUND;
	return MmCreateHook(Module, std::string("NoSlipperyIce ") + Name, script->m_Functions->m_ScriptFunction, Replacement, reinterpret_cast<PVOID*>(Original));
}

EXPORTED AurieStatus ModuleInitialize(
	IN AurieModule* Module,
	IN const fs::path& ModulePath
)
{
	g_Yytk = YYTK::GetInterface();
	if (!g_Yytk)
		return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;

	AurieStatus status = Hook(Module, "scr_cataclysm_check_if_active", CheckActiveHook, &g_CheckActive);
	if (!AurieSuccess(status))
	{
		DbgPrintEx(LOG_SEVERITY_ERROR, "[NoSlipperyIce] the game's cataclysm check was not hooked: %s", AurieStatusToString(status));
		return status;
	}
	// Without these the ice is still gone; only the Netherworld's map would go on naming it.
	if (!AurieSuccess(Hook(Module, "scr_load_tables", LoadTablesHook, &g_LoadTables)) | !AurieSuccess(Hook(Module, "scr_ui_menus_launcher", LauncherHook, &g_Launcher)))
		Log("a table hook failed, the Netherworld's map may still name Slippery Ice");
	if (!AurieSuccess(g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, CodeCallback, 0)))
		Log("the step callback failed, the mod is not listed on SlormAPI's MODS page");

	Log("v" NOSLIPPERYICE_VERSION " loaded");
	return AURIE_SUCCESS;
}
