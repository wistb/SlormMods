#include "Reslormitizer.hpp"
#include "Version.hpp"
#include <imgui.h>
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


static bool DrawOptions()
{
	bool changed = false;
	ImGui::SeparatorText("Appearance");
	bool restyle = false;
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
	restyle |= ImGui::SliderFloat("Opacity", &g_Appearance.opacity, 0.3f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(160);
	// Applied on release: resizing the text under the cursor makes the slider jump.
	static float scale = 0;
	if (!ImGui::IsAnyItemActive() || scale == 0)
		scale = g_Appearance.scale;
	ImGui::SliderFloat("Text size", &scale, 0.75f, 2.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit())
	{
		g_Appearance.scale = scale;
		restyle = true;
	}
	if (ImGui::Button("Reset appearance"))
	{
		int hotkey = g_Appearance.hotkey;
		g_Appearance = Appearance();
		g_Appearance.hotkey = hotkey;
		scale = 0;
		restyle = true;
	}
	if (restyle)
		ApplyAppearance();

	ImGui::SeparatorText("Hotkey");
	static bool capturing = false;
	// A key held when capture starts must be released first, or it would be taken at once.
	static bool armed = false;
	if (capturing)
	{
		g_SuppressToggle = true;
		ImGui::Button("Press a key (Esc cancels)", { 220, 0 });
		int key = PressedKey();
		if (!armed)
			armed = key == 0;
		else if (key == VK_ESCAPE)
			capturing = false;
		else if (key)
		{
			g_Appearance.hotkey = key;
			capturing = false;
			changed = true;
		}
	}
	else
	{
		if (ImGui::Button(KeyName(g_Appearance.hotkey).c_str(), { 220, 0 }))
		{
			capturing = true;
			armed = false;
		}
		ImGui::SameLine();
		ImGui::TextUnformatted("shows or hides the overlay");
	}

	ImGui::SeparatorText("About");
	ImGui::TextUnformatted("SlormReslormitizer v" SLORMRESLORMITIZER_VERSION " by Crash");
	ImGui::TextLinkOpenURL("GitHub", "https://github.com/wistb/SlormMods/tree/main/mods/SlormReslormitizer");
	return changed || restyle;
}


static void Percent(const char* Label, int* Value)
{
	ImGui::SetNextItemWidth(110);
	if (ImGui::InputInt(Label, Value, 5, 25))
		*Value = std::clamp(*Value, 0, 100);
}

static bool DrawTargets()
{
	Settings before = g_Settings;
	ImGui::SeparatorText("Targets");
	Percent("Chance of success at least %", &g_Settings.chance);
	Percent("Critical Reinforce at least %", &g_Settings.critical);
	Percent("Double Reinforce at least %", &g_Settings.twice);
	Percent("Failure at most %", &g_Settings.failure);
	ImGui::SeparatorText("Slormite");
	ImGui::SetNextItemWidth(260);
	std::string current = g_Settings.slormite ? std::string(SlormiteName(g_Settings.slormite)) : "Whichever is in the Slormitizer";
	if (ImGui::BeginCombo("##slormite", current.c_str()))
	{
		if (ImGui::Selectable("Whichever is in the Slormitizer", g_Settings.slormite == 0))
			g_Settings.slormite = 0;
		for (int tier = SLORMITE_TIERS; tier >= 1; tier--)
		{
			char label[96];
			snprintf(label, sizeof(label), "%d. %s (%.0f)", tier, SlormiteName(tier), g_Machine.slormite_stock[tier]);
			if (ImGui::Selectable(label, g_Settings.slormite == tier))
				g_Settings.slormite = tier;
		}
		ImGui::EndCombo();
	}
	ImGui::Checkbox("Load more when the Slormitizer runs out", &g_Settings.reload);
	ImGui::Checkbox("Merge up when there are none left", &g_Settings.merge_up);
	ImGui::SetItemTooltip("Merges lower slormites, three into one, up to the one in use. Costs goldus, as on Rick's Merge Slormites tab.");
	ImGui::SameLine();
	if (ImGui::SmallButton("Merge up now"))
		g_WantMerge = true;
	ImGui::SetItemTooltip("Makes up to 100 of the slormite in use from the lower ones.");
	ImGui::SeparatorText("Repeat");
	ImGui::SetNextItemWidth(110);
	if (ImGui::InputInt("Stop at reinforce level", &g_Settings.until_level))
		g_Settings.until_level = std::clamp(g_Settings.until_level, 1, 10000);
	return memcmp(&before, &g_Settings, sizeof(Settings)) != 0;
}

static void OddsRow(const char* Name, const Odds& Odds)
{
	ImGui::TableNextRow();
	ImGui::TableNextColumn(); ImGui::TextUnformatted(Name);
	ImGui::TableNextColumn(); ImGui::Text("%.0f%%", std::clamp(Odds.chance, 0.0, 100.0));
	ImGui::TableNextColumn(); ImGui::Text("%.0f%%", std::clamp(Odds.critical, 0.0, 100.0));
	ImGui::TableNextColumn(); ImGui::Text("%.0f%%", std::clamp(Odds.twice, 0.0, 100.0));
	ImGui::TableNextColumn(); ImGui::Text("%.0f%%", std::clamp(Odds.failure, 0.0, 100.0));
}

static void DrawPlan()
{
	const Machine& m = g_Machine;
	ImGui::SeparatorText("Slormitizer");
	if (!m.open)
	{
		ImGui::TextUnformatted("Open the Slormitizer at Rick Manalan.");
		return;
	}
	if (m.has_item) ImGui::Text("Item +%d, potential %.0f%%", m.level, m.potential);
	else ImGui::TextUnformatted("No item");
	ImGui::SameLine();
	if (m.tier) ImGui::Text("| %s x %.0f", SlormiteName(m.tier), m.slormites);
	else ImGui::TextUnformatted("| no slormite");

	if (!g_Plan.possible)
	{
		ImGui::TextWrapped("%s", g_Plan.why.c_str());
		return;
	}
	if (ImGui::BeginTable("odds", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
	{
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("Success");
		ImGui::TableSetupColumn("Critical");
		ImGui::TableSetupColumn("Double");
		ImGui::TableSetupColumn("Failure");
		ImGui::TableHeadersRow();
		OddsRow("Without Slormandrites", m.base);
		OddsRow("With the plan", g_Plan.odds);
		OddsRow("Loaded now", m.shown);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Plan");
	bool any = false;
	for (int id = FATE; id < MATERIAL_COUNT; id++)
	{
		if (!g_Plan.count[id])
			continue;
		any = true;
		ImGui::Text("%d x %s (%.0f in stock)", g_Plan.count[id], MaterialName(id), m.stock[id]);
	}
	if (!any)
		ImGui::TextUnformatted("No Slormandrites needed.");
	ImGui::Text("Fee as loaded now: %.0f goldus", m.shown.cost);
}

static void DrawRun()
{
	ImGui::Separator();
	if (g_Running)
	{
		if (ImGui::Button("Stop", { 120, 0 }))
			g_WantStop = true;
		ImGui::SameLine();
		ImGui::TextUnformatted("running");
	}
	else
	{
		ImGui::BeginDisabled(!CanStart());
		if (ImGui::Button("Load", { 90, 0 }))
			g_WantLoad = true;
		ImGui::SetItemTooltip("Puts the plan's Slormandrites in the Slormitizer. Nothing is spent.");
		ImGui::SameLine();
		if (ImGui::Button("Reinforce once", { 130, 0 }))
			g_WantOnce = true;
		ImGui::SetItemTooltip("Loads the plan and activates the Slormitizer, if the game's numbers meet the targets.");
		ImGui::SameLine();
		if (ImGui::Button("Repeat", { 90, 0 }))
			g_WantRun = true;
		ImGui::SetItemTooltip("Keeps reinforcing until the level under Repeat, or until the targets can no longer be met.");
		ImGui::EndDisabled();
		ImGui::TextWrapped("%s", g_Status.c_str());
	}

	// Long lines wrap; the box never scrolls sideways.
	if (!g_Notes.empty() && ImGui::BeginChild("notes", { 0, 110 * g_Appearance.scale }, ImGuiChildFlags_Borders))
	{
		for (const std::string& note : g_Notes)
			ImGui::TextWrapped("%s", note.c_str());
		if (g_Running)
			ImGui::SetScrollHereY(1.0f);
	}
	if (!g_Notes.empty())
		ImGui::EndChild();
}

static void DrawWindow()
{
	std::lock_guard guard(g_Lock);
	ImGui::SetNextWindowPos({ 40, 40 }, ImGuiCond_FirstUseEver);
	// The id after ### keeps the saved position when the title changes.
	if (!ImGui::Begin("SlormReslormitizer v" SLORMRESLORMITIZER_VERSION "###SlormReslormitizer", g_Machine.open ? &g_Visible : &g_Manual, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::End();
		return;
	}

	bool changed = false;
	if (ImGui::BeginTabBar("tabs"))
	{
		if (ImGui::BeginTabItem("Reinforce"))
		{
			ImGui::BeginDisabled(g_Running);
			changed |= DrawTargets();
			ImGui::EndDisabled();
			DrawPlan();
			DrawRun();
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Options"))
		{
			changed |= DrawOptions();
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
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
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormReslormitizer] overlay ready");
	}

	if (!OverlayShown())
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
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormReslormitizer] back buffer %ux%u, window %ldx%ld", size.Width, size.Height, client.right, client.bottom);
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
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, "[SlormReslormitizer] %s", Text);
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

	if (g_Ready && OverlayShown())
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
	return g_Ready && OverlayShown() && ImGui::GetIO().WantCaptureMouse;
}

bool OverlayWantsKeys()
{
	return g_Ready && OverlayShown() && ImGui::GetIO().WantTextInput;
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
	Aurie::AurieStatus status = Aurie::MmCreateHook(Aurie::g_ArSelfModule, "SlormReslormitizer Present", table[8], HookedPresent, reinterpret_cast<PVOID*>(&g_OriginalPresent));
	if (!Aurie::AurieSuccess(status) || !g_OriginalPresent)
		return false;

	status = Aurie::MmCreateHook(Aurie::g_ArSelfModule, "SlormReslormitizer GetCursorPos", GetCursorPos, HookedCursor, reinterpret_cast<PVOID*>(&g_OriginalCursor));
	if (!Aurie::AurieSuccess(status))
		Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_WARNING, "[SlormReslormitizer] cursor hook failed: %s", Aurie::AurieStatusToString(status));
	return true;
}
