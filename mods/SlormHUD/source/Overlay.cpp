#include "Hud.hpp"
#include "Version.hpp"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <algorithm>
using namespace YYTK;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static bool g_Ready = false;
static ID3D11Device* g_Device = nullptr;
static ID3D11DeviceContext* g_Context = nullptr;
static std::string g_IniPath;
static HWND g_Window = nullptr;
static WNDPROC g_OriginalProc = nullptr;
// Latched once per frame so the game sees a steady answer.
static bool g_HideMouse = false;
using CursorFn = BOOL(WINAPI*)(LPPOINT);
static CursorFn g_OriginalCursor = nullptr;

static BOOL RealCursor(LPPOINT Point)
{
	return g_OriginalCursor ? g_OriginalCursor(Point) : GetCursorPos(Point);
}

// While the cursor is over the overlay, the game is told it is off screen, so nothing behind it hovers.
static BOOL WINAPI HookedCursor(LPPOINT Point)
{
	BOOL ok = g_OriginalCursor(Point);
	if (ok && Point && g_HideMouse)
		Point->x = Point->y = -20000;
	return ok;
}
static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);

void OverlaySetIniPath(const std::string& Path)
{
	g_IniPath = Path;
}

// Rarity colours as the game shows them.
static ImVec4 TierColor(const std::string& Code, const std::string& Stat = "")
{
	if (Code == "RP") return { 20 / 255.0f, 142 / 255.0f, 137 / 255.0f, 1.0f };
	if (Code == "MA") return { 206 / 255.0f, 70 / 255.0f, 7 / 255.0f, 1.0f };
	if (Code == "AT")
	{
		// The tier has its own colour; each attribute has another, in the game's order.
		static const ImVec4 traits[] = {
			{ 47 / 255.0f, 89 / 255.0f, 150 / 255.0f, 1.0f }, { 168 / 255.0f, 48 / 255.0f, 7 / 255.0f, 1.0f },
			{ 121 / 255.0f, 181 / 255.0f, 73 / 255.0f, 1.0f }, { 208 / 255.0f, 181 / 255.0f, 69 / 255.0f, 1.0f },
			{ 75 / 255.0f, 193 / 255.0f, 178 / 255.0f, 1.0f }, { 117 / 255.0f, 48 / 255.0f, 152 / 255.0f, 1.0f },
			{ 205 / 255.0f, 112 / 255.0f, 40 / 255.0f, 1.0f }, { 191 / 255.0f, 60 / 255.0f, 89 / 255.0f, 1.0f },
		};
		int id = Stat.size() == 4 ? Stat[3] - '0' : -1;
		return id >= 0 && id < 8 ? traits[id] : ImVec4{ 239 / 255.0f, 203 / 255.0f, 143 / 255.0f, 1.0f };
	}
	if (Code == "M") return { 46 / 255.0f, 135 / 255.0f, 38 / 255.0f, 1.0f };
	if (Code == "R") return { 23 / 255.0f, 106 / 255.0f, 177 / 255.0f, 1.0f };
	if (Code == "E") return { 198 / 255.0f, 141 / 255.0f, 32 / 255.0f, 1.0f };
	if (Code == "L") return { 206 / 255.0f, 70 / 255.0f, 7 / 255.0f, 1.0f };
	return { 137 / 255.0f, 137 / 255.0f, 137 / 255.0f, 1.0f };
}

static const ImVec4 PURE_COLOR = { 70 / 255.0f, 231 / 255.0f, 176 / 255.0f, 1.0f };
static const ImVec4 ERROR_COLOR = { 1.0f, 0.4f, 0.4f, 1.0f };

static ImVec4 AttributeColor(int Id)
{
	return TierColor("AT", "at_" + std::to_string(Id));
}

static const BuildSkill* FindSkill(const Build& Build, int Id)
{
	for (const BuildSkill& skill : Build.skills)
		if (skill.id == Id) return &skill;
	return nullptr;
}

static ImVec4 MarkColor(Mark Kind)
{
	if (Kind == Mark::None)
		return ImGui::GetStyleColorVec4(ImGuiCol_Text);
	const float* color = g_MarkColors[static_cast<int>(Kind)];
	return { color[0], color[1], color[2], 1.0f };
}

static void DrawSkill(const Build& Build, const char* Role, int Id, bool Live)
{
	if (Id < 0)
	{
		ImGui::TextDisabled("%s: none", Role);
		return;
	}
	const BuildSkill* skill = FindSkill(Build, Id);
	std::string title = std::string(Role) + ": " + SkillName(Build.hero, Id);
	Mark state = Live ? SkillMark(Build, Id) : Mark::None;
	if (state == Mark::Todo) title += " (not equipped)";
	else if (Live && SkillInWrongSlot(Build, Id)) title += " (in the other slot)";
	ImGui::PushStyleColor(ImGuiCol_Text, MarkColor(state));
	// The id after ### keeps the node open when its title changes.
	bool open = ImGui::TreeNodeEx((title + "###" + Role).c_str(), ImGuiTreeNodeFlags_DefaultOpen);
	ImGui::PopStyleColor();
	if (!open)
		return;
	if (!skill || skill->upgrades.empty())
		ImGui::TextDisabled("no upgrades");
	else
		for (const Ranked& upgrade : skill->upgrades)
		{
			std::string name = SkillName(Build.hero, upgrade.id);
			int rank = 0, wanted = 0;
			Mark mark = Live ? UpgradeMark(Build, upgrade.id, rank, wanted) : Mark::None;
			if (mark == Mark::None) ImGui::BulletText("%s, rank %d", name.c_str(), upgrade.rank);
			else if (mark == Mark::Match) ImGui::TextColored(MarkColor(Mark::Match), "%s: %d", name.c_str(), rank);
			else if (mark == Mark::Todo) ImGui::TextColored(MarkColor(Mark::Todo), "%s: not equipped, %d %s %d", name.c_str(), rank, rank < wanted ? "<" : rank > wanted ? ">" : "=", wanted);
			else ImGui::TextColored(MarkColor(mark), "%s: %d %s %d", name.c_str(), rank, rank < wanted ? "<" : ">", wanted);
		}
	ImGui::TreePop();
}

static void DrawSkills(const Build& Build)
{
	bool live = g_Live.valid && g_Live.hero == Build.hero && !g_Live.skill_slots.empty();
	if (g_Live.valid && g_Live.hero != Build.hero)
		ImGui::TextDisabled("Playing a %s; this build is for a %s.", HeroName(g_Live.hero), HeroName(Build.hero));
	DrawSkill(Build, "Support", Build.support, live);
	DrawSkill(Build, "Primary", Build.primary, live);
	DrawSkill(Build, "Secondary", Build.secondary, live);

	static const char* keys[] = { "Activable 1", "Activable 2", "Activable 3", "Activable 4" };
	for (int i = 0; i < ACTIVABLE_COUNT; i++)
	{
		if (Build.activables[i] < 0) ImGui::TextDisabled("%s: none", keys[i]);
		else ImGui::Text("%s: %s", keys[i], ActivableName(Build.activables[i]).c_str());
	}
}

static void DrawReaper(const Build& Build)
{
	const BuildReaper& reaper = Build.reaper;
	bool live = g_Live.valid && g_Live.hero == Build.hero;
	std::string name = ReaperName(reaper.id, Build.hero, reaper.primordial);
	if (!live || g_Live.reaper < 0) ImGui::TextUnformatted(name.c_str());
	else if (g_Live.reaper == BuildReaperNumber(Build)) ImGui::TextColored(MarkColor(Mark::Match), "%s", name.c_str());
	else ImGui::TextColored(MarkColor(Mark::Todo), "%s (not equipped)", name.c_str());
	ImGui::Text("Level %d, affinity %d, mastery %d", reaper.level, reaper.affinity, reaper.mastery);
	static const char* kinds[] = { "Activation", "Effect", "Enhancement" };
	for (int i = 0; i < RUNE_COUNT; i++)
	{
		if (Build.runes[i].id < 0)
		{
			ImGui::TextDisabled("%s rune: none", kinds[i]);
			continue;
		}
		Mark mark = live ? RuneMark(Build, Build.runes[i].id) : Mark::None;
		std::string rune = RuneName(Build.runes[i].id) + ", level " + std::to_string(Build.runes[i].rank);
		if (mark == Mark::None) ImGui::BulletText("%s", rune.c_str());
		else ImGui::TextColored(MarkColor(mark), "%s%s", rune.c_str(), mark == Mark::Todo ? " (not equipped)" : "");
	}
	if (Build.ultimatum >= 0)
	{
		Mark mark = live ? UltimatumMark(Build, Build.ultimatum) : Mark::None;
		std::string text = UltimatumName(Build.ultimatum) + ", level " + std::to_string(Build.ultimatum_level);
		if (mark == Mark::Todo) text += " (not equipped)";
		else if (mark == Mark::Partial) text += " (at level " + std::to_string(g_Live.ultimatum_level) + ")";
		ImGui::TextColored(MarkColor(mark), "%s", text.c_str());
	}
}

static int At(const std::vector<int>& Values, int Index)
{
	return Index >= 0 && Index < static_cast<int>(Values.size()) ? Values[Index] : 0;
}

static void DrawLegacy(const Build& Build)
{
	std::vector<int> wanted = Build.nodes;
	if (Build.first_node >= 0) wanted.push_back(Build.first_node);
	bool live = g_Live.valid && g_Live.hero == Build.hero;
	if (g_Live.valid && !live)
		ImGui::TextDisabled("Playing a %s; this build is for a %s.", HeroName(g_Live.hero), HeroName(Build.hero));
	if (!live)
	{
		ImGui::TextDisabled("%d stones placed", static_cast<int>(wanted.size()));
		if (Build.legacies.empty())
			ImGui::TextDisabled("no legacies");
		for (const Ranked& legacy : Build.legacies)
			ImGui::BulletText("%s, rank %d", LegacyName(legacy.id).c_str(), legacy.rank);
		return;
	}

	int placed = 0, extra = 0;
	for (int stone : wanted)
		if (At(g_Live.stones, stone)) placed++;
	for (size_t i = 0; i < g_Live.stones.size(); i++)
		if (g_Live.stones[i] && std::find(wanted.begin(), wanted.end(), static_cast<int>(i)) == wanted.end()) extra++;
	bool stones_match = placed == static_cast<int>(wanted.size()) && !extra;
	std::string stones = std::to_string(placed) + " of " + std::to_string(wanted.size()) + " stones placed";
	if (extra) stones += ", " + std::to_string(extra) + " not in the build";
	ImGui::TextColored(stones_match ? MarkColor(Mark::Match) : extra ? MarkColor(Mark::Wrong) : MarkColor(Mark::Todo), "%s", stones.c_str());

	if (Build.legacies.empty())
		ImGui::TextDisabled("no legacies");
	for (const Ranked& legacy : Build.legacies)
	{
		bool active = At(g_Live.realms, LegacyRealm(legacy.id)) != 0;
		int rank = At(g_Live.legacy_ranks, legacy.id);
		std::string name = LegacyName(legacy.id);
		if (!active)
			ImGui::TextColored(MarkColor(Mark::Todo), "%s: not active, build has rank %d", name.c_str(), legacy.rank);
		else if (rank != legacy.rank)
			ImGui::TextColored(rank ? MarkColor(Mark::Partial) : MarkColor(Mark::Wrong), "%s: %d %s %d", name.c_str(), rank, rank < legacy.rank ? "<" : ">", legacy.rank);
		else
			ImGui::TextColored(MarkColor(Mark::Match), "%s: %d", name.c_str(), legacy.rank);
	}
}

static void DrawAttributes(const Build& Build)
{
	bool live = g_Live.valid && g_Live.hero == Build.hero && !g_Live.attributes.empty();
	if (!ImGui::BeginTable("attributes", 2, ImGuiTableFlags_SizingFixedFit))
		return;
	for (int id = 0; id < ATTRIBUTE_COUNT; id++)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		ImGui::TextColored(AttributeColor(id), "%s", AttributeName(id).c_str());
		ImGui::TableNextColumn();
		int rank = 0, wanted = Build.attributes[id];
		Mark mark = live ? AttributeMark(Build, id, rank, wanted) : Mark::None;
		if (live && rank != wanted) ImGui::TextColored(MarkColor(mark), "%d %s %d", rank, rank < wanted ? "<" : ">", wanted);
		else if (wanted > 0) ImGui::TextColored(MarkColor(mark), "%d", wanted);
		else ImGui::TextDisabled("0");
	}
	ImGui::EndTable();
}

// Rolls are kept on the planner's scale; the best is 100 normal, 65 defense to rare, 40 epic.
static std::string RollText(const BuildAffix& Affix)
{
	int top = Affix.tier == "N" ? 100 : Affix.tier == "E" ? 40 : 65;
	if (Affix.roll >= top)
		return "max roll";
	return std::to_string(Affix.roll * 100 / top) + "% of max";
}

static void DrawGear(const Build& Build)
{
	bool live = g_Live.valid && g_Live.hero == Build.hero && !g_Live.gear.empty();
	if (Build.items.empty())
		ImGui::TextDisabled("no gear");
	// Whether the item has the line, after the line itself.
	auto status = [&](Mark Kind, const std::string& Other)
	{
		if (!live)
			return;
		ImGui::SameLine();
		if (Kind == Mark::Match) ImGui::TextColored(MarkColor(Kind), "on the item");
		else if (Kind == Mark::Partial) ImGui::TextColored(MarkColor(Kind), "on the item %s", Other.c_str());
		else ImGui::TextColored(MarkColor(Mark::Todo), "missing");
	};
	for (size_t i = 0; i < Build.items.size(); i++)
	{
		const BuildItem& item = Build.items[i];
		ImGui::PushID(static_cast<int>(i));
		std::string title = item.label + ", level " + std::to_string(item.level);
		if (item.reinforcement > 0) title += " +" + std::to_string(item.reinforcement);
		if (item.legendary >= 0) title += ", " + LegendaryName(item.legendary);
		if (live)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, MarkColor(GearMark(item)));
		}
		// The id after ### keeps the node open when its title changes.
		bool open = ImGui::TreeNode((title + "###item").c_str());
		if (live)
			ImGui::PopStyleColor();
		if (open)
		{
			std::string other;
			for (const BuildAffix& affix : item.affixes)
			{
				ImGui::TextColored(affix.pure ? PURE_COLOR : TierColor(affix.tier), "%s", StatName(affix.stat).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("%s%s", RollText(affix).c_str(), affix.pure ? "," : "");
				if (affix.pure)
				{
					ImGui::SameLine();
					ImGui::TextColored(PURE_COLOR, "pure");
				}
				Mark mark = live ? GearStatMark(item, affix, other) : Mark::None;
				status(mark, other);
			}
			if (item.smith >= 0)
			{
				ImGui::TextColored(TierColor("RP"), "%s Affinity +%d", SmithName(item.smith).c_str(), item.smith_value);
				status(live ? GearSpecialMark(item, "RP", item.smith, item.smith_value, other) : Mark::None, other);
			}
			if (item.mastery >= 0)
			{
				ImGui::TextColored(TierColor("MA"), "%s Mastery +%d", SkillName(Build.hero, item.mastery).c_str(), item.mastery_value);
				status(live ? GearSpecialMark(item, "MA", item.mastery, item.mastery_value, other) : Mark::None, other);
			}
			if (item.attribute >= 0)
			{
				ImGui::TextColored(AttributeColor(item.attribute), "%s +%d", AttributeName(item.attribute).c_str(), item.attribute_value);
				status(live ? GearSpecialMark(item, "AT", item.attribute, item.attribute_value, other) : Mark::None, other);
			}
			if (item.legendary >= 0)
			{
				if (LegendaryHasRange(item.legendary)) ImGui::TextColored(TierColor("L"), "%s, %d%%", LegendaryName(item.legendary).c_str(), item.legendary_value);
				else ImGui::TextColored(TierColor("L"), "%s", LegendaryName(item.legendary).c_str());
				status(live ? GearSpecialMark(item, "L", item.legendary, item.legendary_value, other) : Mark::None, other);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

static void DrawBuild()
{
	if (g_ActiveBuild < 0 || g_ActiveBuild >= static_cast<int>(g_Builds.size()))
	{
		ImGui::TextDisabled("No build selected. Import one on the Imports tab.");
		return;
	}
	const Build& build = g_Builds[g_ActiveBuild];
	ImGui::Text("%s", build.name.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("%s, level %d", HeroName(build.hero), build.level);

	if (ImGui::CollapsingHeader("Skills", ImGuiTreeNodeFlags_DefaultOpen)) DrawSkills(build);
	if (ImGui::CollapsingHeader("Reaper")) DrawReaper(build);
	if (ImGui::CollapsingHeader("Ancestral legacy")) DrawLegacy(build);
	if (ImGui::CollapsingHeader("Attributes")) DrawAttributes(build);
	if (ImGui::CollapsingHeader("Gear")) DrawGear(build);
}

static bool DrawBuilds()
{
	bool changed = false;
	static char link[2048] = "";
	static char name[64] = "";
	static std::string error;

	ImGui::SeparatorText("Import a build");
	ImGui::TextDisabled("Paste a slorm-planner share link.");
	ImGui::SetNextItemWidth(360);
	ImGui::InputTextWithHint("##link", "https://cayrac.github.io/slorm-planner/view/build/...", link, sizeof(link));
	ImGui::SameLine();
	if (ImGui::Button("Paste"))
	{
		const char* clipboard = ImGui::GetClipboardText();
		snprintf(link, sizeof(link), "%s", clipboard ? clipboard : "");
	}
	ImGui::SetNextItemWidth(200);
	ImGui::InputTextWithHint("##name", "name (optional)", name, sizeof(name));
	ImGui::SameLine();
	ImGui::BeginDisabled(!link[0]);
	if (ImGui::Button("Import"))
	{
		Build build;
		if (ParseBuild(link, build, error))
		{
			if (name[0]) build.name = name;
			g_Builds.push_back(std::move(build));
			g_ActiveBuild = static_cast<int>(g_Builds.size()) - 1;
			link[0] = name[0] = 0;
			error.clear();
			changed = true;
		}
	}
	ImGui::EndDisabled();
	if (!error.empty())
		ImGui::TextColored(ERROR_COLOR, "%s", error.c_str());

	ImGui::SeparatorText("Builds");
	if (g_Builds.empty())
		ImGui::TextDisabled("None yet.");
	for (size_t i = 0; i < g_Builds.size(); i++)
	{
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::SmallButton("x"))
		{
			g_Builds.erase(g_Builds.begin() + i);
			if (g_ActiveBuild == static_cast<int>(i)) g_ActiveBuild = g_Builds.empty() ? -1 : 0;
			else if (g_ActiveBuild > static_cast<int>(i)) g_ActiveBuild--;
			changed = true;
			ImGui::PopID();
			break;
		}
		ImGui::SameLine();
		if (ImGui::RadioButton(g_Builds[i].name.c_str(), g_ActiveBuild == static_cast<int>(i)))
		{
			g_ActiveBuild = static_cast<int>(i);
			changed = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%s, level %d, %d items", HeroName(g_Builds[i].hero), g_Builds[i].level, static_cast<int>(g_Builds[i].items.size()));
		ImGui::PopID();
	}
	return changed;
}

static std::string KeyName(int Key)
{
	if (Key >= VK_F1 && Key <= VK_F24)
		return "F" + std::to_string(Key - VK_F1 + 1);
	UINT scan = MapVirtualKeyW(Key, MAPVK_VK_TO_VSC);
	// Navigation keys share scan codes with the numpad unless flagged as extended.
	bool extended = Key == VK_INSERT || Key == VK_DELETE || Key == VK_HOME || Key == VK_END || Key == VK_PRIOR || Key == VK_NEXT
		|| Key == VK_LEFT || Key == VK_RIGHT || Key == VK_UP || Key == VK_DOWN || Key == VK_DIVIDE || Key == VK_RCONTROL || Key == VK_RMENU;
	char name[64] = {};
	if (scan && GetKeyNameTextA(static_cast<LONG>((scan << 16) | (extended ? 1 << 24 : 0)), name, sizeof(name)) > 0)
		return name;
	return "key " + std::to_string(Key);
}

// Returns a key that is down, for rebinding. Mouse buttons are skipped.
static int PressedKey()
{
	for (int key = 8; key <= 254; key++)
	{
		if (key == VK_LBUTTON || key == VK_RBUTTON || key == VK_MBUTTON || key == VK_XBUTTON1 || key == VK_XBUTTON2)
			continue;
		// Skip the side-neutral modifier codes; the left/right ones report the same press.
		if (key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU)
			continue;
		if (GetAsyncKeyState(key) & 0x8000)
			return key;
	}
	return 0;
}


// About links. An empty address shows the name greyed out until there is a page to point at.
static const char* LINK_AUTHOR = "https://discord.com/users/187106829984202761";
static const char* LINK_NEXUS = "https://www.nexusmods.com/theslormancer/mods/3";
static const char* LINK_GITHUB = "https://github.com/wistb/SlormMods/tree/main/mods/SlormHUD";

static void Link(const char* Label, const char* Url)
{
	if (Url[0])
	{
		ImGui::TextLinkOpenURL(Label, Url);
		return;
	}
	ImGui::BeginDisabled();
	ImGui::TextLink(Label);
	ImGui::EndDisabled();
}

static const char* THEME_NAMES[] = { "Dark", "Light", "Classic", "Slormancer" };

// Colours every preset derives from one accent.
static void SetAccent(ImVec4 accent)
{
	ImVec4* colors = ImGui::GetStyle().Colors;
	auto with = [&](float alpha) { return ImVec4(accent.x, accent.y, accent.z, alpha); };
	auto dim = [&](float factor) { return ImVec4(accent.x * factor, accent.y * factor, accent.z * factor, 1.0f); };
	colors[ImGuiCol_Button] = with(0.45f);
	colors[ImGuiCol_ButtonHovered] = with(0.75f);
	colors[ImGuiCol_ButtonActive] = with(1.0f);
	colors[ImGuiCol_Header] = with(0.35f);
	colors[ImGuiCol_HeaderHovered] = with(0.65f);
	colors[ImGuiCol_HeaderActive] = with(0.9f);
	colors[ImGuiCol_FrameBgHovered] = with(0.3f);
	colors[ImGuiCol_FrameBgActive] = with(0.5f);
	colors[ImGuiCol_CheckMark] = with(1.0f);
	colors[ImGuiCol_SliderGrab] = with(0.8f);
	colors[ImGuiCol_SliderGrabActive] = with(1.0f);
	colors[ImGuiCol_Tab] = dim(0.4f);
	colors[ImGuiCol_TabHovered] = with(0.8f);
	colors[ImGuiCol_TabSelected] = dim(0.7f);
	colors[ImGuiCol_TitleBgActive] = dim(0.45f);
	colors[ImGuiCol_SeparatorHovered] = with(0.7f);
	colors[ImGuiCol_SeparatorActive] = with(1.0f);
	colors[ImGuiCol_TextSelectedBg] = with(0.4f);
}

// Bars and fields: title bar, unselected tabs, input fields, table headers.
static void SetPrimary(ImVec4 primary)
{
	ImVec4* colors = ImGui::GetStyle().Colors;
	auto shade = [&](float factor, float alpha = 1.0f)
	{
		return ImVec4((std::min)(primary.x * factor, 1.0f), (std::min)(primary.y * factor, 1.0f), (std::min)(primary.z * factor, 1.0f), alpha);
	};
	colors[ImGuiCol_TitleBg] = shade(0.7f);
	colors[ImGuiCol_TitleBgActive] = shade(1.0f);
	colors[ImGuiCol_TitleBgCollapsed] = shade(0.7f, 0.8f);
	colors[ImGuiCol_Tab] = shade(0.7f);
	colors[ImGuiCol_TableHeaderBg] = shade(0.8f);
	colors[ImGuiCol_FrameBg] = shade(0.8f);
	colors[ImGuiCol_FrameBgHovered] = shade(1.15f);
	colors[ImGuiCol_FrameBgActive] = shade(1.4f);
	colors[ImGuiCol_ScrollbarGrab] = shade(1.0f);
	colors[ImGuiCol_ScrollbarGrabHovered] = shade(1.25f);
	colors[ImGuiCol_ScrollbarGrabActive] = shade(1.5f);
}

static void ApplyAppearance()
{
	Appearance& look = g_Appearance;
	look.theme = std::clamp(look.theme, 0, 3);
	look.opacity = std::clamp(look.opacity, 0.3f, 1.0f);
	look.scale = std::clamp(look.scale, 0.75f, 2.0f);

	ImGuiStyle& style = ImGui::GetStyle();
	style = ImGuiStyle();
	style.TabRounding = 0;
	if (look.theme == 1) ImGui::StyleColorsLight();
	else if (look.theme == 2) ImGui::StyleColorsClassic();
	else ImGui::StyleColorsDark();

	ImVec4* colors = style.Colors;
	if (look.theme == 3)
	{
		// The game's panels: near-black brown with dull gold edges.
		style.WindowRounding = 0;
		style.FrameRounding = 0;
		style.WindowBorderSize = 2;
		colors[ImGuiCol_WindowBg] = { 0.07f, 0.06f, 0.05f, 1.0f };
		colors[ImGuiCol_PopupBg] = { 0.09f, 0.08f, 0.07f, 1.0f };
		colors[ImGuiCol_ChildBg] = { 0.05f, 0.045f, 0.04f, 1.0f };
		colors[ImGuiCol_Border] = { 0.36f, 0.32f, 0.24f, 1.0f };
		colors[ImGuiCol_Separator] = { 0.36f, 0.32f, 0.24f, 1.0f };
		colors[ImGuiCol_FrameBg] = { 0.15f, 0.13f, 0.11f, 1.0f };
		colors[ImGuiCol_TitleBg] = { 0.10f, 0.09f, 0.07f, 1.0f };
		colors[ImGuiCol_TitleBgCollapsed] = { 0.10f, 0.09f, 0.07f, 0.8f };
		colors[ImGuiCol_TableHeaderBg] = { 0.14f, 0.12f, 0.10f, 1.0f };
		colors[ImGuiCol_TableRowBgAlt] = { 1.0f, 0.95f, 0.8f, 0.04f };
		colors[ImGuiCol_Text] = { 0.92f, 0.89f, 0.80f, 1.0f };
		colors[ImGuiCol_TextDisabled] = { 0.54f, 0.52f, 0.46f, 1.0f };
		SetAccent({ 0.78f, 0.62f, 0.25f, 1.0f });
	}
	if (look.custom_accent)
		SetAccent({ look.accent[0], look.accent[1], look.accent[2], 1.0f });
	if (look.custom_primary)
		SetPrimary({ look.primary[0], look.primary[1], look.primary[2], 1.0f });

	colors[ImGuiCol_WindowBg].w = look.opacity;
	colors[ImGuiCol_PopupBg].w = (std::max)(look.opacity, 0.9f);
	ImGui::GetIO().FontGlobalScale = look.scale;
}


// A slider that goes back to its default on a right click or a double click.
static bool Slider(const char* Label, float* Value, float Low, float High, const char* Format, float Default)
{
	bool changed = ImGui::SliderFloat(Label, Value, Low, High, Format);
	if (ImGui::IsItemHovered() && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)))
	{
		// Let go of the slider, or the held button drags it straight back.
		ImGui::ClearActiveID();
		changed |= *Value != Default;
		*Value = Default;
	}
	return changed;
}

static bool DrawOptions()
{
	bool changed = false;
	ImGui::SeparatorText("Appearance");
	bool restyle = false;
	// Applied on release: resizing the text under the cursor makes the slider jump.
	static float scale = 0;
	if (ImGui::BeginTabBar("appearance"))
	{
		if (ImGui::BeginTabItem("Window"))
		{
			ImGui::SetNextItemWidth(160);
			restyle |= ImGui::Combo("Theme", &g_Appearance.theme, THEME_NAMES, 4);
			restyle |= ImGui::Checkbox("Custom accent", &g_Appearance.custom_accent);
			if (g_Appearance.custom_accent)
			{
				ImGui::SameLine();
				restyle |= ImGui::ColorEdit3("##accent", g_Appearance.accent, ImGuiColorEditFlags_NoInputs);
			}
			restyle |= ImGui::Checkbox("Custom primary", &g_Appearance.custom_primary);
			if (g_Appearance.custom_primary)
			{
				ImGui::SameLine();
				restyle |= ImGui::ColorEdit3("##primary", g_Appearance.primary, ImGuiColorEditFlags_NoInputs);
			}
			ImGui::SetNextItemWidth(160);
			restyle |= Slider("Opacity", &g_Appearance.opacity, 0.3f, 1.0f, "%.2f", Appearance().opacity);
			ImGui::SetNextItemWidth(160);
			if (!ImGui::IsAnyItemActive() || scale == 0)
				scale = g_Appearance.scale;
			bool reset_scale = Slider("Text size", &scale, 0.75f, 2.0f, "%.2f", Appearance().scale) && !ImGui::IsItemActive();
			if (ImGui::IsItemDeactivatedAfterEdit() || reset_scale)
			{
				g_Appearance.scale = scale;
				restyle = true;
			}
			if (ImGui::Button("Reset window"))
			{
				int hotkey = g_Appearance.hotkey, marks_hotkey = g_Appearance.marks_hotkey;
				g_Appearance = Appearance();
				g_Appearance.hotkey = hotkey;
				g_Appearance.marks_hotkey = marks_hotkey;
				scale = 0;
				restyle = true;
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Outlines"))
		{
			changed |= ImGui::Checkbox("Outline the build on the game's screens", &g_Marks);
			ImGui::TextDisabled("Thickness of each line, how far outside the icon it sits, and how far to move it.");
			for (int kind = 0; kind < OUTLINE_COUNT; kind++)
			{
				ImGui::PushID(kind);
				ImGui::SetNextItemWidth(110);
				changed |= Slider("##width", &g_Outlines[kind].width, 1, 12, "thick %.1f", OUTLINE_DEFAULTS[kind].width);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(110);
				changed |= Slider("##grow", &g_Outlines[kind].grow, -12, 12, "size %.1f", OUTLINE_DEFAULTS[kind].grow);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				changed |= Slider("##x", &g_Outlines[kind].x, -12, 12, "x %.1f", OUTLINE_DEFAULTS[kind].x);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80);
				changed |= Slider("##y", &g_Outlines[kind].y, -12, 12, "y %.1f", OUTLINE_DEFAULTS[kind].y);
				ImGui::SameLine();
				ImGui::TextUnformatted(OutlineName(kind));
				ImGui::PopID();
			}
			if (ImGui::Button("Reset outlines"))
			{
				std::copy(OUTLINE_DEFAULTS, OUTLINE_DEFAULTS + OUTLINE_COUNT, g_Outlines);
				changed = true;
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Rank text"))
		{
			ImGui::TextDisabled("Where the rank text sits, and how far to move it from there.");
			static const char* label_names[] = { "Legacy ranks", "Upgrade ranks", "Attribute points", "Netherworld rewards" };
			static const char* sides[] = { "Bottom", "Top", "Left", "Right" };
			for (int kind = 0; kind < LABEL_COUNT; kind++)
			{
				ImGui::PushID(kind);
				ImGui::SetNextItemWidth(90);
				changed |= ImGui::Combo("##side", &g_Labels[kind].side, sides, SIDE_COUNT);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90);
				changed |= Slider("##x", &g_Labels[kind].x, -60, 60, "x %.0f", LABEL_DEFAULTS[kind].x);
				ImGui::SameLine();
				ImGui::SetNextItemWidth(90);
				changed |= Slider("##y", &g_Labels[kind].y, -60, 60, "y %.0f", LABEL_DEFAULTS[kind].y);
				ImGui::SameLine();
				ImGui::TextUnformatted(label_names[kind]);
				ImGui::PopID();
			}
			if (ImGui::Button("Reset rank text"))
			{
				std::copy(LABEL_DEFAULTS, LABEL_DEFAULTS + LABEL_COUNT, g_Labels);
				changed = true;
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Colors"))
		{
			ImGui::TextDisabled("Used by the outlines, the text and the Build and Nether tabs.");
			static const char* states[] = { "", "Matches the build", "Some ranks in", "Not in the build, or no ranks", "Still to place", "Found by the Netherworld search" };
			for (int kind = 1; kind < MARK_COUNT; kind++)
				changed |= ImGui::ColorEdit3(states[kind], g_MarkColors[kind], ImGuiColorEditFlags_NoInputs);
			if (ImGui::Button("Reset colors"))
			{
				memcpy(g_MarkColors, MARK_DEFAULTS, sizeof(g_MarkColors));
				changed = true;
			}
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	if (restyle)
		ApplyAppearance();

	ImGui::SeparatorText("Hotkeys");
	// Which key is being rebound, -1 for none.
	static int capturing = -1;
	// A key held when capture starts must be released first, or it would be taken at once.
	static bool armed = false;
	int* keys[] = { &g_Appearance.hotkey, &g_Appearance.marks_hotkey };
	static const char* uses[] = { "shows or hides the overlay", "shows or hides the outlines" };
	for (int i = 0; i < 2; i++)
	{
		ImGui::PushID(i);
		if (capturing == i)
		{
			g_SuppressToggle = true;
			ImGui::Button("Press a key (Esc cancels)", { 220, 0 });
			int key = PressedKey();
			if (!armed)
				armed = key == 0;
			else if (key == VK_ESCAPE)
				capturing = -1;
			else if (key)
			{
				*keys[i] = key;
				capturing = -1;
				changed = true;
			}
		}
		else if (ImGui::Button(KeyName(*keys[i]).c_str(), { 220, 0 }))
		{
			capturing = i;
			armed = false;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(uses[i]);
		ImGui::PopID();
	}

	ImGui::SeparatorText("About");
	ImGui::TextUnformatted("SlormHUD v" SLORMHUD_VERSION " by");
	ImGui::SameLine();
	Link("Crash", LINK_AUTHOR);
	Link("Nexus Mods", LINK_NEXUS);
	ImGui::SameLine();
	Link("GitHub", LINK_GITHUB);
	return changed || restyle;
}

#ifdef SLORM_DEV
static void DrawDev()
{
	ImGui::TextDisabled("Dev build. Writes the game's variables to mods/Aurie/SlormHUD.dump.txt.");
	ImGui::TextDisabled("F9 dumps too, without moving the mouse.");
	ImGuiIO& io = ImGui::GetIO();
	ImGui::Text("Overlay takes: mouse %s, keys %s", io.WantCaptureMouse ? "yes" : "no", io.WantTextInput ? "yes" : "no");
	static char note[64] = "";
	ImGui::SetNextItemWidth(260);
	ImGui::InputTextWithHint("##note", "what is on screen", note, sizeof(note));
	ImGui::SameLine();
	if (ImGui::Button("Dump"))
	{
		g_DumpNote = note;
		g_WantDump = true;
	}
	static const char* quick[] = { "legacy screen open", "legacy screen closed", "after placing a stone", "after ranking a legacy" };
	for (const char* label : quick)
		if (ImGui::Button(label))
		{
			g_DumpNote = label;
			g_WantDump = true;
		}
	if (!g_DumpResult.empty())
		ImGui::TextUnformatted(g_DumpResult.c_str());
}
#endif

// The Netherworld search: its rules, then the warlords that meet them now.
static bool DrawNether()
{
	bool changed = ImGui::Checkbox("Label warlords on the Netherworld map", &g_NetherSearch);
	ImGui::TextDisabled("A warlord is labeled when its possible rewards hold enough legendaries of one slot.");
	for (size_t i = 0; i < g_NetherRules.size(); i++)
	{
		NetherRule& rule = g_NetherRules[i];
		ImGui::PushID(static_cast<int>(i));
		if (ImGui::SmallButton("x"))
		{
			g_NetherRules.erase(g_NetherRules.begin() + i);
			ImGui::PopID();
			changed = true;
			break;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("at least");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(90);
		changed |= ImGui::SliderInt("##count", &rule.count, 1, 7);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110);
		// "Any slot" heads the list.
		static const char* choices[SLOT_KINDS + 1] = { "Any slot" };
		std::copy(GEAR_SLOT_NAMES, GEAR_SLOT_NAMES + SLOT_KINDS, choices + 1);
		int choice = rule.slot + 1;
		if (ImGui::Combo("##slot", &choice, choices, SLOT_KINDS + 1))
		{
			rule.slot = choice - 1;
			changed = true;
		}
		ImGui::PopID();
	}
	if (ImGui::Button("Add a rule"))
	{
		g_NetherRules.push_back({});
		changed = true;
	}
	if (g_NetherRules.size() > 1)
	{
		ImGui::SameLine();
		changed |= ImGui::Checkbox("Must meet every rule", &g_NetherAll);
	}

	ImGui::SeparatorText("Found");
	if (g_NetherRules.empty())
		ImGui::TextDisabled("Add a rule to search.");
	else if (g_Live.nether_rewards.empty())
		ImGui::TextDisabled("Load a character to search its Netherworld.");
	else
	{
		int found = 0;
		for (const auto& [id, rewards] : g_Live.nether_rewards)
		{
			std::string note;
			auto lord = Warlords().find(id);
			if (lord == Warlords().end() || !NetherMatch(rewards, note))
				continue;
			found++;
			ImGui::TextColored(MarkColor(Mark::Search), "%s", lord->second.name.c_str());
			ImGui::SameLine();
			ImGui::TextDisabled("%s", note.c_str());
			if (ImGui::IsItemHovered())
			{
				std::string list;
				for (int reward : rewards)
				{
					int slot = LegendarySlot(reward);
					list += (list.empty() ? "" : "\n") + LegendaryName(reward) + (slot >= 0 ? std::string(" (") + GEAR_SLOT_NAMES[slot] + ")" : "");
				}
				ImGui::SetTooltip("%s", list.c_str());
			}
		}
		if (!found)
			ImGui::TextDisabled("No warlord meets the search.");
	}
	return changed;
}

static void DrawWindow()
{
	std::lock_guard guard(g_Lock);
	bool open = true;
	ImGui::SetNextWindowPos({ 40, 40 }, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSizeConstraints({ 0, 0 }, { FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.8f });
	// The id after ### keeps the saved position when the title changes.
	if (!ImGui::Begin("SlormHUD v" SLORMHUD_VERSION "###SlormHUD", &open, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	bool changed = false;
	if (ImGui::BeginTabBar("tabs"))
	{
		if (ImGui::BeginTabItem("Build"))
		{
			DrawBuild();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Imports"))
		{
			changed |= DrawBuilds();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Nether"))
		{
			changed |= DrawNether();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Options"))
		{
			changed |= DrawOptions();
			ImGui::EndTabItem();
		}
#ifdef SLORM_DEV
		if (ImGui::BeginTabItem("Dev"))
		{
			DrawDev();
			ImGui::EndTabItem();
		}
#endif
		ImGui::EndTabBar();
	}

	ImGui::End();
	if (!open)
	{
		g_Visible = false;
		changed = true;
	}
	if (changed)
		SaveConfig();
}

static void Frame(IDXGISwapChain* swapchain, UINT flags)
{
	if (!swapchain || (flags & DXGI_PRESENT_TEST))
		return;

	if (!g_Ready)
	{
		DXGI_SWAP_CHAIN_DESC description = {};
		if (FAILED(swapchain->GetDevice(IID_PPV_ARGS(&g_Device))) || FAILED(swapchain->GetDesc(&description)))
			return;
		g_Device->GetImmediateContext(&g_Context);

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = g_IniPath.empty() ? nullptr : g_IniPath.c_str();
		ApplyAppearance();
		g_Window = description.OutputWindow;
		ImGui_ImplWin32_Init(g_Window);
		g_OriginalProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProc)));
		ImGui_ImplDX11_Init(g_Device, g_Context);
		g_Ready = true;
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormHUD] overlay ready");
	}

	if (!g_Visible)
	{
		g_HideMouse = false;
		return;
	}

	ID3D11Texture2D* buffer = nullptr;
	ID3D11RenderTargetView* view = nullptr;
	if (FAILED(swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer))))
		return;
	D3D11_TEXTURE2D_DESC size = {};
	buffer->GetDesc(&size);
	HRESULT created = g_Device->CreateRenderTargetView(buffer, nullptr, &view);
	buffer->Release();
	if (FAILED(created))
		return;

	g_HideMouse = false;
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	// The mouse is polled: window messages do not reliably carry it in this game.
	// The back buffer can be a different size from the window, so scale to it.
	ImGuiIO& io = ImGui::GetIO();
	RECT client = {};
	POINT cursor = {};
	GetClientRect(g_Window, &client);
	RealCursor(&cursor);
	ScreenToClient(g_Window, &cursor);
	io.DisplaySize = { static_cast<float>(size.Width), static_cast<float>(size.Height) };
	static bool logged = false;
	if (!logged)
	{
		logged = true;
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormHUD] back buffer %ux%u, window %ldx%ld", size.Width, size.Height, client.right, client.bottom);
	}
	if (client.right > 0 && client.bottom > 0 && GetForegroundWindow() == g_Window)
	{
		io.AddMousePosEvent(cursor.x * static_cast<float>(size.Width) / client.right, cursor.y * static_cast<float>(size.Height) / client.bottom);
		io.AddMouseButtonEvent(0, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
		io.AddMouseButtonEvent(1, (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
	}
	ImGui::NewFrame();
	// The game draws its own cursor under the overlay.
	ImGui::GetIO().MouseDrawCursor = ImGui::GetIO().WantCaptureMouse;
	DrawWindow();
	ImGui::Render();
	g_HideMouse = io.WantCaptureMouse;

	ID3D11RenderTargetView* old_view = nullptr;
	ID3D11DepthStencilView* old_depth = nullptr;
	g_Context->OMGetRenderTargets(1, &old_view, &old_depth);
	g_Context->OMSetRenderTargets(1, &view, nullptr);
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	g_Context->OMSetRenderTargets(1, &old_view, old_depth);
	if (old_view) old_view->Release();
	if (old_depth) old_depth->Release();
	view->Release();
}

static void LogOnce(bool& Done, const char* Text)
{
	if (Done)
		return;
	Done = true;
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormHUD] %s", Text);
}

static LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
	static bool saw_any = false, saw_mouse = false, saw_raw = false, saw_char = false;
	bool mouse = Message >= WM_MOUSEFIRST && Message <= WM_MOUSELAST;
	bool keyboard = Message == WM_KEYDOWN || Message == WM_KEYUP || Message == WM_CHAR;
	LogOnce(saw_any, "window messages seen");
	if (mouse) LogOnce(saw_mouse, "mouse messages seen");
	if (Message == WM_INPUT) LogOnce(saw_raw, "raw input seen");
	if (Message == WM_CHAR) LogOnce(saw_char, "char messages seen");

	if (g_Ready && g_Visible)
	{
		// Mouse position and buttons are polled per frame; wheel and keys come from here.
		if (!mouse || Message == WM_MOUSEWHEEL)
			ImGui_ImplWin32_WndProcHandler(Window, Message, WParam, LParam);

		ImGuiIO& io = ImGui::GetIO();
		// Without WM_CHAR, type digits from key presses.
		if (Message == WM_KEYDOWN && io.WantTextInput && !saw_char)
		{
			if (WParam >= '0' && WParam <= '9') io.AddInputCharacter(static_cast<unsigned>(WParam));
			else if (WParam >= VK_NUMPAD0 && WParam <= VK_NUMPAD9) io.AddInputCharacter(static_cast<unsigned>('0' + WParam - VK_NUMPAD0));
			else if (WParam == VK_OEM_PERIOD || WParam == VK_DECIMAL) io.AddInputCharacter('.');
			else if (WParam == VK_OEM_MINUS || WParam == VK_SUBTRACT) io.AddInputCharacter('-');
		}

		// Keep clicks and typing on the overlay away from the game.
		if ((mouse && io.WantCaptureMouse) || (keyboard && io.WantTextInput))
			return 0;
	}
	return CallWindowProcW(g_OriginalProc, Window, Message, WParam, LParam);
}

bool OverlayWantsMouse()
{
	return g_Ready && g_Visible && ImGui::GetIO().WantCaptureMouse;
}

bool OverlayWantsKeys()
{
	return g_Ready && g_Visible && ImGui::GetIO().WantTextInput;
}

using PresentFn = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
static PresentFn g_OriginalPresent = nullptr;

static HRESULT WINAPI HookedPresent(IDXGISwapChain* Swapchain, UINT Sync, UINT Flags)
{
	Frame(Swapchain, Flags);
	return g_OriginalPresent(Swapchain, Sync, Flags);
}

// YYToolkit 5.0.0c never installs its own Present hook, so EVENT_FRAME does not fire.
bool OverlayInstall(YYTKInterface* Yytk)
{
	RValue info = Yytk->CallBuiltin("os_get_info", {});
	RValue chain = Yytk->CallBuiltin("ds_map_find_value", { info, RValue("video_d3d11_swapchain") });
	Yytk->CallBuiltin("ds_map_destroy", { info });

	IDXGISwapChain* swapchain = static_cast<IDXGISwapChain*>(chain.m_Pointer);
	if (!swapchain)
		return false;
	PVOID* table = *reinterpret_cast<PVOID**>(swapchain);
	Aurie::AurieStatus status = Aurie::MmCreateHook(Aurie::g_ArSelfModule, "SlormHUD Present", table[8], HookedPresent, reinterpret_cast<PVOID*>(&g_OriginalPresent));
	if (!Aurie::AurieSuccess(status) || !g_OriginalPresent)
		return false;

	status = Aurie::MmCreateHook(Aurie::g_ArSelfModule, "SlormHUD GetCursorPos", GetCursorPos, HookedCursor, reinterpret_cast<PVOID*>(&g_OriginalCursor));
	if (!Aurie::AurieSuccess(status))
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_WARNING, "[SlormHUD] cursor hook failed: %s", Aurie::AurieStatusToString(status));
	return true;
}
