// The panel beside Rick Manalan's Slormitizer, drawn each frame with the game's own sprites and fonts,
// and the same settings in Options > Mods through SlormAPI.
#include "Reslormitizer.hpp"
#include "Version.hpp"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <utility>
using namespace Aurie;
using namespace YYTK;

SlormAPIInterface* g_Api = nullptr;

// Positions are the game's 1920x1080 layout, scaled by the UI scale when drawn.
// The panel covers the character page of the inventory; it is drawn after the UI's draw event, so the
// hero and equipment the game draws each frame stay under it. The inventory beside it stays in use.
// Its frame lies on the page's own frame (the sprite's border is 4 in from its sides, 17 from its top
// and 6 from its bottom).
constexpr int CHARACTER_TAB = 0;
constexpr double COVER_X1 = 680, COVER_Y1 = 118, COVER_X2 = 1250, COVER_Y2 = 954;
constexpr double LEFT = COVER_X1 + 30, RIGHT = COVER_X2 - 30, ROW = 38, LINE = 22;
// A setting's selector, right-aligned in its row: arrows either side of the value.
constexpr double ARROW_W = 24, ARROW_H = 36, BOX_W = 220;
// The game's grey button, narrowed by drawing its two ends.
constexpr double BUTTON_SPRITE_W = 168, BUTTON_W = 156, BUTTON_H = 52;
// The switch sits in the gap left of the Slormitizer's item slot: the Ancestral Flawless Slormite in grey
// through the game's own shader, blue while the panel is open.
constexpr double SWITCH_X = 154, SWITCH_Y = 373, ICON = 48;
constexpr int SLORMITE_ICON = 205, OPEN_BLUE = 0xff9030;
// Materials' icons in spr_inventory_items, in the game's material order (Slormelines, then Fate to Virtue).
constexpr int MATERIAL_ICONS = 206;
constexpr double PLAN_ICON = 30;

static bool g_Shown = false;
// Layout to screen: the game scales its 1920x1080 layout to fit and centers it on other aspect ratios.
static double g_Scale = 1, g_OffsetX = 0, g_OffsetY = 0;
static double ScreenX(double X) { return g_OffsetX + X * g_Scale; }
static double ScreenY(double Y) { return g_OffsetY + Y * g_Scale; }

// The game draws tooltips from surfaces in the UI's draw event, before the panel. While the panel is up,
// those draws are held back and made again on top of it, or dropped when the mouse is on the panel, as
// the tooltip is then for the equipment under it.
static const char* SURFACE_DRAWS[] = { "draw_surface", "draw_surface_ext", "draw_surface_part", "draw_surface_part_ext", "draw_surface_stretched", "draw_surface_stretched_ext" };
constexpr int SURFACE_DRAW_COUNT = sizeof(SURFACE_DRAWS) / sizeof(SURFACE_DRAWS[0]);
static TRoutine g_SurfaceDraws[SURFACE_DRAW_COUNT] = {};
static bool g_Holding = false;
static std::vector<double> g_TooltipSurfaces;
struct HeldDraw
{
	int which;
	CInstance* self;
	std::vector<RValue> arguments;
	// The state the game drew it with: tooltips are drawn with their own blending.
	RValue matrix, state;
	double shader;
};
static std::vector<HeldDraw> g_Held;

// The game reads the mouse in more than one place, and other mods may run its step before this one
// does, so clearing the mouse is not enough: while the mouse is on the panel or the switch, the game's
// button checks see nothing pressed and the mouse is far off, so nothing under the panel is hovered
// either. The mod's own reads go through with g_OwnCheck set.
static const char* MOUSE_CHECKS[] = { "mouse_check_button", "mouse_check_button_pressed", "mouse_check_button_released",
	"device_mouse_check_button", "device_mouse_check_button_pressed", "device_mouse_check_button_released",
	"device_mouse_x_to_gui", "device_mouse_y_to_gui" };
constexpr int MOUSE_BUTTON_CHECKS = 6;
constexpr int MOUSE_CHECK_COUNT = sizeof(MOUSE_CHECKS) / sizeof(MOUSE_CHECKS[0]);
static TRoutine g_MouseChecks[MOUSE_CHECK_COUNT] = {};
static bool g_MouseOnPanel = false, g_OwnCheck = false;
static CInstance* g_Self = nullptr;
// The mouse in layout units, as of this frame's draw.
static double g_MouseX = 0, g_MouseY = 0;
static bool g_MouseDown = false;

// Where a click goes, as drawn last frame. The game never sees clicks on the panel: they would land on
// the equipment under it.
struct Hit
{
	std::string id, action;
	double x, y, w, h;
};
static std::vector<Hit> g_Hits;
static std::string g_Help;

// A setting kept by SlormAPI, as Options > Mods and the panel's selectors show it.
struct Number
{
	const char* key;
	int Settings::* field;
	const char* label;
	const char* help;
	int min, max, step;
};
static const Number NUMBERS[] = {
	{ "chance", &Settings::chance, "Chance of success at least", "Only reinforce at this chance of success or higher. None lets the plan choose the cheapest odds.", 0, 100, 5 },
	{ "critical", &Settings::critical, "Critical Reinforce at least", "Only reinforce at this chance of a Critical Reinforce or higher. None lets the plan choose.", 0, 100, 5 },
	{ "double", &Settings::twice, "Double Reinforce at least", "Only reinforce at this chance of a Double Reinforce or higher. None lets the plan choose.", 0, 100, 5 },
	{ "failure", &Settings::failure, "Failure at most", "Only reinforce at this chance of failure or lower. None lets the plan choose.", 0, 100, 5 },
	{ "until_level", &Settings::until_level, "Repeat stops at level", "Repeat stops once the item reaches this reinforce level. Hold Shift for steps of 10, Ctrl for 100.", 1, 999, 1 },
};
static const char* WHICHEVER = "Whichever is in the Slormitizer";
static const char* SLORMITE_HELP = "The slormite to reinforce with";
static const char* RELOAD_HELP = "Puts more of the slormite in use into the Slormitizer when it runs out";
static const char* MERGE_UP_HELP = "Merges lower slormites, three into one, up to the one in use; costs goldus";

sj::Json Command(const char* Name)
{
	sj::Json command = sj::Json::Object();
	command.Add("cmd", Name);
	return command;
}

std::optional<sj::Json> Request(const sj::Json& Command)
{
	if (!g_Api)
		return std::nullopt;
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

static std::string Format(const char* Format, ...)
{
	char text[256];
	va_list arguments;
	va_start(arguments, Format);
	vsnprintf(text, sizeof text, Format, arguments);
	va_end(arguments);
	return text;
}

static std::string SlormiteChoice(int Tier)
{
	return Tier ? Format("%d. %s", Tier, SlormiteName(Tier)) : WHICHEVER;
}

// The name without the word Slormite, to fit the selector.
static std::string ShortSlormite(int Tier)
{
	std::string name = SlormiteName(Tier);
	for (const char* word : { " Slormite", "Slormite " })
		if (size_t at = name.find(word); at != std::string::npos)
			name.erase(at, strlen(word));
	return name;
}

static void ApplySetting(const std::string& Key, const sj::Json& Value)
{
	for (const Number& number : NUMBERS)
		if (Key == number.key && Value.type == sj::Json::Num)
			g_Settings.*number.field = std::clamp(static_cast<int>(Value.n), number.min, number.max);
	if (Key == "slormite" && Value.type == sj::Json::Str)
		for (int tier = 0; tier <= SLORMITE_TIERS; tier++)
			if (Value.s == SlormiteChoice(tier))
				g_Settings.slormite = tier;
	if (Value.type != sj::Json::Bool)
		return;
	if (Key == "reload") g_Settings.reload = Value.b;
	else if (Key == "merge_up") g_Settings.merge_up = Value.b;
}

static void Store(const char* Key, sj::Json Value)
{
	sj::Json command = Command("set_setting");
	command.Add("mod", MOD_NAME);
	command.Add("key", Key);
	command.Add("value", std::move(Value));
	if (std::optional<sj::Json> stored = Request(command))
		ApplySetting(Key, *stored);
}

static sj::Json Setting(const char* Key, const char* Label, const char* Help, const char* Type, sj::Json Default)
{
	sj::Json setting = sj::Json::Object();
	setting.Add("key", Key);
	setting.Add("label", Label);
	setting.Add("help", Help);
	setting.Add("type", Type);
	setting.Add("default", std::move(Default));
	return setting;
}

static void RegisterSettings()
{
	Settings defaults;
	sj::Json settings = sj::Json::Array();
	for (const Number& number : NUMBERS)
	{
		sj::Json setting = Setting(number.key, number.label, number.help, "number", defaults.*number.field);
		setting.Add("min", number.min);
		setting.Add("max", number.max);
		setting.Add("step", number.step);
		settings.Push(std::move(setting));
	}
	sj::Json slormite = Setting("slormite", "Slormite", SLORMITE_HELP, "choice", WHICHEVER);
	sj::Json tiers = sj::Json::Array();
	for (int tier = 0; tier <= SLORMITE_TIERS; tier++)
		tiers.Push(SlormiteChoice(tier));
	slormite.Add("options", std::move(tiers));
	settings.Push(std::move(slormite));
	settings.Push(Setting("reload", "Load more slormites", RELOAD_HELP, "bool", defaults.reload));
	settings.Push(Setting("merge_up", "Merge up when none are left", MERGE_UP_HELP, "bool", defaults.merge_up));

	sj::Json registration = Command("register_settings");
	registration.Add("mod", MOD_NAME);
	registration.Add("settings", std::move(settings));
	if (std::optional<sj::Json> values = Request(registration))
		for (const auto& [key, value] : values->o)
			ApplySetting(key, value);
}

static void OnSetting(const char* Event, const char* DataJson, void* User)
{
	try
	{
		sj::Json data = sj::Parse(DataJson ? DataJson : "");
		const sj::Json* value = data.Find("value");
		if (data.GetStr("mod") == MOD_NAME && value)
			ApplySetting(data.GetStr("key"), *value);
	}
	catch (const std::exception& error)
	{
		Log("%s event: %s", Event, error.what());
	}
}

template <int Which>
static void SurfaceDraw(RValue& Result, CInstance* Self, CInstance* Other, int Count, RValue* Arguments)
{
	if (g_Holding && Count > 0 && Arguments[0].IsNumberConvertible())
	{
		double surface = Arguments[0].ToDouble();
		// Only what goes to the screen; a tooltip drawn into another surface is being put together.
		bool to_screen = ToNumber(g_Yytk->CallBuiltin("surface_get_target", {})) < 0;
		// Only tooltips draw parts of surfaces to the screen; the pieces come and go as surfaces of their own.
		bool part = Which == 2 || Which == 3;
		if (to_screen && (part || std::find(g_TooltipSurfaces.begin(), g_TooltipSurfaces.end(), surface) != g_TooltipSurfaces.end()))
		{
			// With the mouse on the panel, a tooltip is for something under it.
			if (!g_MouseOnPanel)
				g_Held.push_back({ Which, Self, std::vector<RValue>(Arguments, Arguments + Count), g_Yytk->CallBuiltin("matrix_get", { RValue(0.0) }),
					g_Yytk->CallBuiltin("gpu_get_state", {}), ToNumber(g_Yytk->CallBuiltin("shader_current", {})) });
			return;
		}
	}
	g_SurfaceDraws[Which](Result, Self, Other, Count, Arguments);
}

template <int... Which>
static void HookSurfaceDraws(std::integer_sequence<int, Which...>)
{
	TRoutine detours[] = { SurfaceDraw<Which>... };
	for (int at = 0; at < SURFACE_DRAW_COUNT; at++)
	{
		TRoutine routine = nullptr;
		AurieStatus status = g_Yytk->GetNamedRoutinePointer(SURFACE_DRAWS[at], reinterpret_cast<PVOID*>(&routine));
		if (AurieSuccess(status) && routine)
			status = MmCreateHook(g_ArSelfModule, std::string("SlormReslormitizer ") + SURFACE_DRAWS[at], routine, detours[at], reinterpret_cast<PVOID*>(&g_SurfaceDraws[at]));
		if (!AurieSuccess(status))
			Log("cannot hook %s: tooltips stay under the panel", SURFACE_DRAWS[at]);
	}
}

template <int Which>
static void MouseCheck(RValue& Result, CInstance* Self, CInstance* Other, int Count, RValue* Arguments)
{
	if (g_MouseOnPanel && !g_OwnCheck)
	{
		Result = Which < MOUSE_BUTTON_CHECKS ? RValue(false) : RValue(-100000.0);
		return;
	}
	g_MouseChecks[Which](Result, Self, Other, Count, Arguments);
}

template <int... Which>
static void HookMouseChecks(std::integer_sequence<int, Which...>)
{
	TRoutine detours[] = { MouseCheck<Which>... };
	for (int at = 0; at < MOUSE_CHECK_COUNT; at++)
	{
		TRoutine routine = nullptr;
		AurieStatus status = g_Yytk->GetNamedRoutinePointer(MOUSE_CHECKS[at], reinterpret_cast<PVOID*>(&routine));
		if (AurieSuccess(status) && routine)
			status = MmCreateHook(g_ArSelfModule, std::string("SlormReslormitizer ") + MOUSE_CHECKS[at], routine, detours[at], reinterpret_cast<PVOID*>(&g_MouseChecks[at]));
		if (!AurieSuccess(status))
			Log("cannot hook %s: clicks on the panel may reach the game", MOUSE_CHECKS[at]);
	}
}

// One of the mod's own mouse button checks.
static bool Pressed(const char* Check)
{
	g_OwnCheck = true;
	bool down = g_Yytk->CallBuiltin(Check, { RValue(1.0) }).ToBoolean();
	g_OwnCheck = false;
	return down;
}

// The mouse in layout units.
static void Mouse(double& X, double& Y)
{
	g_OwnCheck = true;
	X = (ToNumber(g_Yytk->CallBuiltin("device_mouse_x_to_gui", { RValue(0.0) })) - g_OffsetX) / g_Scale;
	Y = (ToNumber(g_Yytk->CallBuiltin("device_mouse_y_to_gui", { RValue(0.0) })) - g_OffsetY) / g_Scale;
	g_OwnCheck = false;
}

bool PanelStart()
{
	AurieInterfaceBase* base = nullptr;
	if (!AurieSuccess(ObGetInterface(SLORMAPI_INTERFACE_NAME, base)) || !base)
		return false;
	g_Api = static_cast<SlormAPIInterface*>(base);

	sj::Json registration = Command("register_mod");
	registration.Add("name", MOD_NAME);
	registration.Add("version", SLORMRESLORMITIZER_VERSION);
	registration.Add("author", "Crash");
	registration.Add("description", "Operates Rick Manalan's Slormitizer: loads the cheapest Slormandrites for the odds you ask for, and reinforces");
	sj::Json needs = sj::Json::Object();
	needs.Add("SlormAPI", "1.0.0");
	registration.Add("requires", std::move(needs));
	registration.Add("game", "1.0.2c");
	Request(registration);

	RegisterSettings();
	g_Api->Subscribe("setting_changed", OnSetting, nullptr);
	HookSurfaceDraws(std::make_integer_sequence<int, SURFACE_DRAW_COUNT>());
	HookMouseChecks(std::make_integer_sequence<int, MOUSE_CHECK_COUNT>());
	return true;
}

static RValue Ui(CInstance* Self, const char* Name)
{
	return g_Yytk->CallBuiltin("variable_instance_get", { RValue(Self), RValue(Name) });
}

static bool AtRick(CInstance* Self)
{
	return g_Machine.open && ToText(Ui(Self, "menu_show")) == "inventory" && ToText(Ui(Self, "menu_citizen")) == "crafter";
}

static bool Covered(CInstance* Self)
{
	return g_Shown && AtRick(Self) && ToNumber(Ui(Self, "inventory_panel")) == CHARACTER_TAB;
}

static void Show(CInstance* Self, bool On)
{
	// The panel covers the character page, so that page has to be the one up.
	if (On && ToNumber(Ui(Self, "inventory_panel")) != CHARACTER_TAB)
	{
		g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("inventory_panel"), RValue(static_cast<double>(CHARACTER_TAB)) });
		g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("rebake_menu"), RValue(true) });
	}
	g_Shown = On;
}

// The stop level moves by 10 with Shift held and by 100 with Ctrl.
static int StepMultiplier(const Number& Setting)
{
	constexpr double SHIFT_KEY = 16, CONTROL_KEY = 17;
	if (Setting.field != &Settings::until_level)
		return 1;
	if (g_Yytk->CallBuiltin("keyboard_check", { RValue(CONTROL_KEY) }).ToBoolean())
		return 100;
	if (g_Yytk->CallBuiltin("keyboard_check", { RValue(SHIFT_KEY) }).ToBoolean())
		return 10;
	return 1;
}

static void Act(CInstance* Self, const std::string& Id, const std::string& Action)
{
	int direction = Action == "prev" ? -1 : Action == "next" ? 1 : 0;
	for (const Number& number : NUMBERS)
		if (Id == number.key && direction && !g_Running)
			Store(number.key, std::clamp(g_Settings.*number.field + direction * number.step * StepMultiplier(number), number.min, number.max));
	if (Id == "slormite" && direction && !g_Running)
		Store("slormite", SlormiteChoice((g_Settings.slormite + direction + SLORMITE_TIERS + 1) % (SLORMITE_TIERS + 1)));
	else if (Id == "reload" && !g_Running) Store("reload", !g_Settings.reload);
	else if (Id == "merge_up" && !g_Running) Store("merge_up", !g_Settings.merge_up);
	else if (Id == "load") g_WantLoad = true;
	else if (Id == "once") g_WantOnce = true;
	else if (Id == "repeat") g_WantRun = true;
	else if (Id == "stop") g_WantStop = true;
	else if (Id == "merge") g_WantMerge = true;
	else if (Id == "panel") Show(Self, !g_Shown);
	else if (Id == "close") Show(Self, false);
}

// Drawing, in layout units.

static RValue Asset(const char* Name)
{
	static std::unordered_map<std::string, double> known;
	auto found = known.find(Name);
	if (found == known.end())
		found = known.emplace(Name, ToNumber(g_Yytk->CallBuiltin("asset_get_index", { RValue(Name) }))).first;
	return RValue(found->second);
}

static void Sprite(const char* Name, int Frame, double X, double Y, double Scale = 1, int Color = 0xffffff, double Alpha = 1)
{
	double s = g_Scale;
	g_Yytk->CallBuiltin("draw_sprite_ext", { Asset(Name), RValue(static_cast<double>(Frame)), RValue(ScreenX(X)), RValue(ScreenY(Y)),
		RValue(Scale * s), RValue(Scale * s), RValue(0.0), RValue(static_cast<double>(Color)), RValue(Alpha) });
}

// Part of a sprite, in its own pixels, drawn at X, Y and stretched to W by H.
static void SpritePart(const char* Name, int Frame, double Left, double Top, double Width, double Height, double X, double Y, double W, double H)
{
	double s = g_Scale;
	g_Yytk->CallBuiltin("draw_sprite_part_ext", { Asset(Name), RValue(static_cast<double>(Frame)), RValue(Left), RValue(Top), RValue(Width), RValue(Height),
		RValue(ScreenX(X)), RValue(ScreenY(Y)), RValue(W / Width * s), RValue(H / Height * s), RValue(static_cast<double>(0xffffff)), RValue(1.0) });
}

static void Rect(double X1, double Y1, double X2, double Y2, int Color)
{
	g_Yytk->CallBuiltin("draw_set_color", { RValue(static_cast<double>(Color)) });
	g_Yytk->CallBuiltin("draw_rectangle", { RValue(ScreenX(X1)), RValue(ScreenY(Y1)), RValue(ScreenX(X2)), RValue(ScreenY(Y2)), RValue(false) });
	g_Yytk->CallBuiltin("draw_set_color", { RValue(static_cast<double>(0xffffff)) });
}

// The game's text: fonts and colors by name; it sizes the font for the UI scale itself.
static void Text(double X, double Y, const std::string& Content, const char* Font = "ft_standard_12", const char* Color = "white", int Align = 0)
{
	static PFUNC_YYGMLScript draw_text = FindScript("scr_draw_text");
	CallScript(draw_text, g_Self, { RValue(ScreenX(X)), RValue(ScreenY(Y)), RValue(Content), RValue(1.0), RValue(1.0), RValue(Font),
		RValue(static_cast<double>(Align)), RValue(1.0), RValue(0.0), RValue(1.0), RValue(Color), RValue("black"), RValue(1.0), RValue(false) });
}

// The size of text as the game draws it, in layout units: the game's own measures, which follow the
// interface font size option (a third argument would measure at the default size instead).
static double TextWidth(const std::string& Content, const char* Font)
{
	static PFUNC_YYGMLScript string_width = FindScript("scr_string_width");
	return ToNumber(CallScript(string_width, g_Self, { RValue(Content), RValue(Font) })) / g_Scale;
}

static double TextHeight(const char* Font)
{
	static PFUNC_YYGMLScript string_height = FindScript("scr_string_height");
	return ToNumber(CallScript(string_height, g_Self, { RValue("Ag"), RValue(Font) })) / g_Scale;
}

// Text broken at spaces into lines no wider than Width.
static std::vector<std::string> Wrap(const std::string& Content, const char* Font, double Width)
{
	std::vector<std::string> lines;
	std::string line, word;
	std::istringstream words(Content);
	while (words >> word)
	{
		std::string longer = line.empty() ? word : line + " " + word;
		if (!line.empty() && TextWidth(longer, Font) > Width)
		{
			lines.push_back(line);
			line = word;
		}
		else
			line = longer;
	}
	if (!line.empty())
		lines.push_back(line);
	return lines;
}

// Wrapped text from Y down; the height it took.
static double Paragraph(double X, double Y, const std::string& Content, double Width, const char* Font = "ft_standard_12", const char* Color = "white")
{
	std::vector<std::string> lines = Wrap(Content, Font, Width);
	double Line = TextHeight(Font) + 2;
	for (size_t at = 0; at < lines.size(); at++)
		Text(X, Y + Line * at, lines[at], Font, Color);
	return Line * lines.size();
}

// Registers a clickable area; true while the mouse is over it.
static bool Area(const char* Id, const char* Action, double X, double Y, double W, double H, const char* Help)
{
	g_Hits.push_back({ Id, Action, X, Y, W, H });
	bool over = g_MouseX >= X && g_MouseX < X + W && g_MouseY >= Y && g_MouseY < Y + H;
	if (over && Help)
		g_Help = Help;
	return over;
}

// The frame of a game button: normal, hovered, pressed.
static int Frame(bool Over)
{
	return Over ? (g_MouseDown ? 2 : 1) : 0;
}

static void Button(const char* Id, double X, double Y, const char* Label, const char* Help)
{
	int frame = Frame(Area(Id, "click", X, Y, BUTTON_W, BUTTON_H, Help));
	double half = BUTTON_W / 2;
	SpritePart("spr_menu_grey_button", frame, 0, 0, half, BUTTON_H, X, Y, half, BUTTON_H);
	SpritePart("spr_menu_grey_button", frame, BUTTON_SPRITE_W - half, 0, half, BUTTON_H, X + half, Y, half, BUTTON_H);
	Text(X + half, Y + BUTTON_H / 2, Label, "ft_standard_12", "white", 1);
}

static void Selector(const char* Id, double Y, const char* Label, const std::string& Value, const char* Help)
{
	Text(LEFT, Y + ROW / 2, Label);
	double right = RIGHT - ARROW_W, box = right - 4 - BOX_W, left = box - 4 - ARROW_W, top = Y + (ROW - ARROW_H) / 2;
	Rect(box, top, box + BOX_W, top + ARROW_H, 0x020202);
	Rect(box + 3, top + 3, box + BOX_W - 3, top + ARROW_H - 3, 0x101010);
	Text(box + BOX_W / 2, Y + ROW / 2, Value, "ft_standard_12", "grey", 1);
	Sprite("spr_selector_btn_l_v2", Frame(Area(Id, "prev", left, top, ARROW_W, ARROW_H, Help)), left, top);
	Sprite("spr_selector_btn_r_v2", Frame(Area(Id, "next", right, top, ARROW_W, ARROW_H, Help)), right, top);
}

static void Toggle(const char* Id, double Y, const char* Label, bool On, const char* Help)
{
	Text(LEFT, Y + ROW / 2, Label);
	// The diamond's sprite has its origin 4 in; the cross is centered.
	double x = RIGHT - 40, top = Y + (ROW - 36) / 2;
	Sprite("spr_radio_btn_v3", Area(Id, "toggle", x, top, 36, 36, Help) ? 1 : 0, x, top);
	if (On)
		Sprite("spr_radio_cross_v2", 0, x + 18, top + 18);
}

static void GreyShader(double Brightness)
{
	RValue shader = Asset("shd_bright_contrast");
	g_Yytk->CallBuiltin("shader_set", { shader });
	auto uniform = [&](const char* Name, double Value)
	{
		RValue handle = g_Yytk->CallBuiltin("shader_get_uniform", { shader, RValue(Name) });
		g_Yytk->CallBuiltin("shader_set_uniform_f", { handle, RValue(Value) });
	};
	uniform("fade", 1);
	uniform("brightness_amount", Brightness);
	uniform("contrast_amount", 0);
}

static void Fog(bool On, int Color)
{
	g_Yytk->CallBuiltin("gpu_set_fog", { RValue(On), RValue(static_cast<double>(Color)), RValue(0.0), RValue(1.0) });
}

// The slormite icon centered on X, Y: grey, or blue, with a shadow under it.
static void Slormite(double X, double Y, double Size, bool Blue, double Brightness, double Depth)
{
	double scale = Size / 64, x = X - Size / 2, y = Y - Size / 2;
	if (Depth > 0)
	{
		Fog(true, 0);
		Sprite("spr_inventory_items", SLORMITE_ICON, x + Depth * 0.6, y + Depth, scale, 0xffffff, 0.5);
		Fog(false, 0);
	}
	GreyShader(Brightness);
	Sprite("spr_inventory_items", SLORMITE_ICON, x, y, scale);
	g_Yytk->CallBuiltin("shader_reset", {});
	if (Blue)
	{
		// Flat blue over the grey keeps the facets.
		Fog(true, OPEN_BLUE);
		Sprite("spr_inventory_items", SLORMITE_ICON, x, y, scale, 0xffffff, 0.55);
		Fog(false, 0);
	}
}

static void DrawSwitch()
{
	bool over = Area("panel", "click", SWITCH_X, SWITCH_Y, ICON, ICON, g_Shown ? "Close SlormReslormitizer" : "SlormReslormitizer options");
	bool down = over && g_MouseDown;
	// Raised with a shadow; larger and brighter under the mouse; pressed into the panel when held.
	double size = over && !down ? ICON * 1.1 : ICON;
	double sink = down ? 2 : 0;
	Slormite(SWITCH_X + ICON / 2, SWITCH_Y + ICON / 2 + sink, size, g_Shown, over ? 0.15 : 0, down ? 2 : 5);
}

// The ornate frame of the character page's weapon box, stretched over the panel: its corners, plain
// strips of its edges and inside, and the skull ornament on top. In the sprite's pixels (419 by 304).
static void DrawFrame()
{
	const char* box = "spr_weapon_box_inventory_v2";
	constexpr double W = 419, H = 304, CW = 80, CH = 84;
	double x1 = COVER_X1, y1 = COVER_Y1, x2 = COVER_X2, y2 = COVER_Y2, w = x2 - x1, h = y2 - y1;
	Rect(x1 + 4, y1 + 17, x2 - 4, y2 - 6, 0x0b0b0b);
	SpritePart(box, 0, 100, 100, 10, 10, x1 + CW, y1 + CH, w - CW * 2, h - CH * 2);
	SpritePart(box, 0, 72, 0, 8, CH, x1 + CW, y1, w - CW * 2, CH);
	SpritePart(box, 0, 150, H - CH, 10, CH, x1 + CW, y2 - CH, w - CW * 2, CH);
	SpritePart(box, 0, 0, CH, CW, 10, x1, y1 + CH, CW, h - CH * 2);
	SpritePart(box, 0, W - CW, CH, CW, 10, x2 - CW, y1 + CH, CW, h - CH * 2);
	SpritePart(box, 0, 0, 0, CW, CH, x1, y1, CW, CH);
	SpritePart(box, 0, W - CW, 0, CW, CH, x2 - CW, y1, CW, CH);
	SpritePart(box, 0, 0, H - CH, CW, CH, x1, y2 - CH, CW, CH);
	SpritePart(box, 0, W - CW, H - CH, CW, CH, x2 - CW, y2 - CH, CW, CH);
	constexpr double ORNAMENT_LEFT = 82, ORNAMENT_W = 255, ORNAMENT_H = 60;
	SpritePart(box, 0, ORNAMENT_LEFT, 0, ORNAMENT_W, ORNAMENT_H, (x1 + x2 - ORNAMENT_W) / 2, y1, ORNAMENT_W, ORNAMENT_H);
}

static std::string Percent(double Value)
{
	return Format("%.0f%%", std::clamp(Value, 0.0, 100.0));
}

static void OddsRow(double Y, const char* Name, const Odds& Odds, const char* Color)
{
	Text(LEFT, Y, Name, "ft_standard_12", Color);
	const double values[] = { Odds.chance, Odds.critical, Odds.twice, Odds.failure };
	for (int column = 0; column < 4; column++)
		Text(LEFT + 250 + 70 * column, Y, Percent(values[column]), "ft_standard_12", Color, 2);
}

static void DrawPanel()
{
	const Machine& m = g_Machine;
	DrawFrame();

	// Under the frame's ornament.
	double y = COVER_Y1 + 58;
	Slormite(LEFT + 15, y + 15, 30, true, 0, 0);
	Text(LEFT + 38, y + 15, "SlormReslormitizer", "ft_standard_16");
	double close_x = COVER_X2 - 22 - 52, close_y = COVER_Y1 + 20;
	Sprite("spr_menu_escape_button", Frame(Area("close", "click", close_x, close_y, 52, 52, "Back to the character")), close_x, close_y);
	y += 40;

	for (int at = 0; at < 4; at++, y += ROW)
	{
		// At its default a floor asks nothing: 0% for the chances, 100% for failure.
		int value = g_Settings.*NUMBERS[at].field;
		bool none = NUMBERS[at].field == &Settings::failure ? value >= 100 : value <= 0;
		Selector(NUMBERS[at].key, y, NUMBERS[at].label, none ? std::string("None") : Format("%d%%", value), NUMBERS[at].help);
	}
	std::string slormite = g_Settings.slormite ? Format("%s (%.0f)", ShortSlormite(g_Settings.slormite).c_str(), m.slormite_stock[g_Settings.slormite]) : std::string("In the Slormitizer");
	Selector("slormite", y, "Slormite", slormite, SLORMITE_HELP);
	y += ROW;
	Selector("until_level", y, NUMBERS[4].label, Format("+%d", g_Settings.until_level), NUMBERS[4].help);
	y += ROW;
	Toggle("reload", y, "Load more slormites", g_Settings.reload, RELOAD_HELP);
	y += ROW;
	Toggle("merge_up", y, "Merge up when none are left", g_Settings.merge_up, MERGE_UP_HELP);
	y += ROW + 8;

	if (m.has_item) Text(LEFT, y, Format("Item +%d, potential %.0f%%", m.level, m.potential));
	else Text(LEFT, y, "No item in the Slormitizer", "ft_standard_12", "grey");
	Text(RIGHT, y, m.tier ? Format("%s x %.0f", ShortSlormite(m.tier).c_str(), m.slormites) : std::string("No slormite"), "ft_standard_12", m.tier ? "white" : "grey", 2);
	y += LINE + 4;

	if (g_Plan.possible)
	{
		const char* heads[] = { "Success", "Critical", "Double", "Failure" };
		for (int column = 0; column < 4; column++)
			Text(LEFT + 250 + 70 * column, y, heads[column], "ft_standard_11", "grey", 2);
		y += LINE;
		OddsRow(y, "Without Slormandrites", m.base, "grey");
		OddsRow(y + LINE, "With plan", g_Plan.odds, "item_highlight");
		OddsRow(y + LINE * 2, "Loaded now", m.shown, "white");
		y += LINE * 3 + 6;
		// Each Slormandrite as its inventory icon and count; its name and stock under the mouse.
		bool any = std::any_of(std::begin(g_Plan.count) + FATE, std::end(g_Plan.count), [](int count) { return count > 0; });
		if (!any)
			Text(LEFT, y, "No Slormandrites needed");
		else
		{
			Text(LEFT, y, "Plan:");
			double x = LEFT + TextWidth("Plan:", "ft_standard_12") + 12;
			for (int id = FATE; id < MATERIAL_COUNT; id++)
			{
				if (!g_Plan.count[id])
					continue;
				Area("plan", "none", x, y - PLAN_ICON / 2, PLAN_ICON, PLAN_ICON, Format("%s: %d to load, %.0f in stock", MaterialName(id), g_Plan.count[id], m.stock[id]).c_str());
				Sprite("spr_inventory_items", MATERIAL_ICONS + id, x, y - PLAN_ICON / 2, PLAN_ICON / 64);
				std::string count = Format("%d", g_Plan.count[id]);
				Text(x + PLAN_ICON + 4, y, count);
				x += PLAN_ICON + 4 + TextWidth(count, "ft_standard_12") + 16;
			}
		}
		y += 4;
		Text(LEFT, y + LINE - 2, Format("Fee as loaded now: %.0f goldus", m.shown.cost), "ft_standard_11", "grey");
	}
	else
		Paragraph(LEFT, y, g_Plan.why, RIGHT - LEFT, "ft_standard_12", "grey");

	y = COVER_Y2 - 204;
	if (g_Running)
		Button("stop", LEFT, y, "Stop", "Stops after the current step");
	else if (CanStart())
	{
		double gap = (RIGHT - LEFT - BUTTON_W * 3) / 2;
		Button("load", LEFT, y, "Load", "Puts the plan's Slormandrites in the Slormitizer; nothing is spent");
		Button("once", LEFT + BUTTON_W + gap, y, "Reinforce once", "Loads the plan and activates the Slormitizer, if the game's numbers meet the targets");
		Button("repeat", LEFT + (BUTTON_W + gap) * 2, y, "Repeat", "Keeps reinforcing until the level set above, or until the targets can no longer be met");
	}
	y += BUTTON_H + 8;
	if (!g_Running)
		Button("merge", LEFT, y, "Merge up now", "Makes up to 100 of the slormite in use from the lower ones");
	// Beside the button, centered on it when it fits on one line.
	double status_x = LEFT + BUTTON_W + 16;
	std::vector<std::string> status = Wrap(g_Status, "ft_standard_12", RIGHT - status_x);
	for (size_t at = 0; at < status.size(); at++)
		Text(status_x, y + BUTTON_H / 2 + (TextHeight("ft_standard_12") + 2) * (at - (status.size() - 1) / 2.0), status[at], "ft_standard_12", "grey");
	y += BUTTON_H + 14;

	// The last two things that happened, a repeat shown once with its count; clear of the frame's lower
	// corners.
	std::vector<std::string> recent;
	for (size_t at = g_Notes.size(); at > 0 && recent.size() <= 2;)
	{
		size_t repeats = 1;
		for (at--; at > 0 && g_Notes[at - 1] == g_Notes[at]; at--)
			repeats++;
		recent.insert(recent.begin(), repeats > 1 ? Format("%s (x%zu)", g_Notes[at].c_str(), repeats) : g_Notes[at]);
	}
	if (recent.size() > 2)
		recent.erase(recent.begin());
	double notes_x = COVER_X1 + 84;
	for (const std::string& note : recent)
		y += Paragraph(notes_x, y, note, RIGHT - 40 - notes_x, "ft_standard_11", "grey");
}

// What the control under the mouse does, in a box by the cursor, kept on the screen.
static void DrawHelp()
{
	if (g_Help.empty())
		return;
	double width = TextWidth(g_Help, "ft_standard_11") + 24, height = TextHeight("ft_standard_11") + 12;
	double right = (ToNumber(g_Yytk->CallBuiltin("display_get_gui_width", {})) - g_OffsetX) / g_Scale;
	double bottom = (ToNumber(g_Yytk->CallBuiltin("display_get_gui_height", {})) - g_OffsetY) / g_Scale;
	double x = g_MouseX + 18, y = g_MouseY + 22;
	double left = -g_OffsetX / g_Scale, top = -g_OffsetY / g_Scale;
	if (x + width > right) x = g_MouseX - 8 - width;
	if (y + height > bottom) y = g_MouseY - 8 - height;
	x = (std::max)(x, left + 4);
	y = (std::max)(y, top + 4);
	Rect(x - 2, y - 2, x + width + 2, y + height + 2, 0x000000);
	Rect(x, y, x + width, y + height, 0x333a3b);
	Rect(x + 2, y + 2, x + width - 2, y + height - 2, 0x101010);
	Text(x + 12, y + height / 2, g_Help, "ft_standard_11", "white");
}

// The panel or the switch is under the mouse at MouseX, MouseY (layout units); the hit, if any.
static const Hit* UnderMouse(CInstance* Self, double MouseX, double MouseY)
{
	const Hit* hit = nullptr;
	for (const Hit& area : g_Hits)
		if (MouseX >= area.x && MouseX < area.x + area.w && MouseY >= area.y && MouseY < area.y + area.h)
			hit = &area;
	bool on_panel = Covered(Self) && MouseX >= COVER_X1 && MouseX <= COVER_X2 && MouseY >= COVER_Y1 && MouseY <= COVER_Y2;
	g_MouseOnPanel = hit || on_panel;
	return hit;
}

// From the end of the step to the panel: other mods may run the UI's draw before this one does.
static void DropHeld()
{
	for (HeldDraw& held : g_Held)
		g_Yytk->CallBuiltin("ds_map_destroy", { held.state });
	g_Held.clear();
}

static void StartHolding(CInstance* Self)
{
	DropHeld();
	g_Holding = g_Api && Covered(Self);
	if (!g_Holding)
		return;
	g_TooltipSurfaces.clear();
	for (const char* name : { "tooltip_surf", "tooltip_craft_surf" })
		if (double surface = ToNumber(Ui(Self, name)); surface > 0)
			g_TooltipSurfaces.push_back(surface);
}

void PanelDraw(CInstance* Self)
{
	g_Holding = false;
	g_Hits.clear();
	g_Help.clear();
	bool at_rick = g_Api && AtRick(Self);
	if (at_rick)
	{
		g_Self = Self;
		double scale = ToNumber(Ui(Self, "ui_scale"));
		g_Scale = scale > 0 ? scale : 1;
		g_OffsetX = (ToNumber(g_Yytk->CallBuiltin("display_get_gui_width", {})) - 1920 * g_Scale) / 2;
		g_OffsetY = (ToNumber(g_Yytk->CallBuiltin("display_get_gui_height", {})) - 1080 * g_Scale) / 2;
		Mouse(g_MouseX, g_MouseY);
		g_MouseDown = Pressed("mouse_check_button");
		DrawSwitch();
		if (Covered(Self))
			DrawPanel();
		UnderMouse(Self, g_MouseX, g_MouseY);
		DrawHelp();
	}
	else
		g_MouseOnPanel = false;

	for (HeldDraw& held : g_Held)
	{
		RValue matrix = g_Yytk->CallBuiltin("matrix_get", { RValue(0.0) });
		RValue state = g_Yytk->CallBuiltin("gpu_get_state", {});
		double shader = ToNumber(g_Yytk->CallBuiltin("shader_current", {}));
		g_Yytk->CallBuiltin("matrix_set", { RValue(0.0), held.matrix });
		g_Yytk->CallBuiltin("gpu_set_state", { held.state });
		if (held.shader >= 0) g_Yytk->CallBuiltin("shader_set", { RValue(held.shader) });
		RValue result;
		g_SurfaceDraws[held.which](result, held.self, held.self, static_cast<int>(held.arguments.size()), held.arguments.data());
		if (shader >= 0) g_Yytk->CallBuiltin("shader_set", { RValue(shader) });
		else g_Yytk->CallBuiltin("shader_reset", {});
		g_Yytk->CallBuiltin("gpu_set_state", { state });
		g_Yytk->CallBuiltin("matrix_set", { RValue(0.0), matrix });
		g_Yytk->CallBuiltin("ds_map_destroy", { state });
	}
	DropHeld();
}

void PanelInput(CInstance* Self)
{
	if (!g_Api || g_Hits.empty() || !AtRick(Self))
	{
		g_MouseOnPanel = false;
		return;
	}
	double mx, my;
	Mouse(mx, my);
	const Hit* hit = UnderMouse(Self, mx, my);
	if (hit && Pressed("mouse_check_button_pressed"))
		Act(Self, hit->id, hit->action);
}

void PanelTick(CInstance* Self)
{
	if (g_Shown && !g_Machine.open)
		g_Shown = false;
	// Leaving the character page by the game's own tabs closes the panel.
	if (g_Shown && ToNumber(Ui(Self, "inventory_panel")) != CHARACTER_TAB)
		g_Shown = false;
	// The game has decided its tooltip for this frame; nothing under the panel gets one.
	if (g_MouseOnPanel)
		g_Yytk->CallBuiltin("variable_instance_set", { RValue(Self), RValue("show_tooltip"), RValue(false) });
	StartHolding(Self);
}
