// Marks things of the game on screen: where they stand, and which way they lie when out of view.
#include "../Suite.hpp"
#include <imgui.h>
#include <algorithm>
#include <cmath>
using namespace Aurie;
using namespace YYTK;

namespace
{
	// A place in the room and where it falls on screen, as fractions of the screen.
	struct Mark
	{
		double x = 0, y = 0;
		float across = 0, down = 0;
		double distance = 0;
		// Which of its kinds' colors it takes.
		int kind = 0;
		// 0 one wave, 1 two, 2 three, 3 chaos.
		int tier = 0;
		// One the player wants to stay away from.
		bool avoid = false;
	};

	// The part of the room in view, and the part of the window it is drawn to.
	struct View
	{
		double x = 0, y = 0, w = 0, h = 0;
		double left = 0, top = 0, right = 0, bottom = 0;
		double window_w = 0, window_h = 0;

		bool Read()
		{
			RValue camera = g_Yytk->CallBuiltin("view_get_camera", { RValue(0.0) });
			x = ToNumber(g_Yytk->CallBuiltin("camera_get_view_x", { camera }));
			y = ToNumber(g_Yytk->CallBuiltin("camera_get_view_y", { camera }));
			w = ToNumber(g_Yytk->CallBuiltin("camera_get_view_width", { camera }));
			h = ToNumber(g_Yytk->CallBuiltin("camera_get_view_height", { camera }));
			window_w = ToNumber(g_Yytk->CallBuiltin("window_get_width", {}));
			window_h = ToNumber(g_Yytk->CallBuiltin("window_get_height", {}));
			RValue place = g_Yytk->CallBuiltin("application_get_position", {});
			std::vector<RValue> edges = place.IsArray() ? place.ToVector() : std::vector<RValue>();
			if (edges.size() >= 4)
			{
				left = ToNumber(edges[0]);
				top = ToNumber(edges[1]);
				right = ToNumber(edges[2]);
				bottom = ToNumber(edges[3]);
			}
			if (right <= left || bottom <= top)
			{
				left = top = 0;
				right = window_w;
				bottom = window_h;
			}
			return w > 0 && h > 0 && window_w > 0 && window_h > 0;
		}

		void Place(Mark& At) const
		{
			At.across = static_cast<float>((left + (At.x - x) / w * (right - left)) / window_w);
			At.down = static_cast<float>((top + (At.y - y) / h * (bottom - top)) / window_h);
		}
	};

	double At(const RValue& Instance, const char* Name)
	{
		return ToNumber(g_Yytk->CallBuiltin("variable_instance_get", { Instance, RValue(Name) }));
	}

	// Every instance of an object in the room.
	std::vector<RValue> Instances(const char* Object)
	{
		std::vector<RValue> found;
		RValue object = g_Yytk->CallBuiltin("asset_get_index", { RValue(Object) });
		if (object.m_Kind != VALUE_REF && ToNumber(object) < 0)
			return found;
		int count = static_cast<int>(ToNumber(g_Yytk->CallBuiltin("instance_number", { object })));
		for (int i = 0; i < count; i++)
			found.push_back(g_Yytk->CallBuiltin("instance_find", { object, RValue(static_cast<double>(i)) }));
		return found;
	}

	constexpr const char* TIER_LABELS[4] = { "Breach I", "Breach II", "Breach III", "Breach V" };

	struct Esp : Feature
	{
		bool breaches = true;
		// Each tier, one wave, two, three, chaos: 0 not marked, 1 marked, 2 marked as one to avoid.
		int tiers[4] = { 1, 1, 1, 2 };
		float avoid_color[3] = { 0.67f, 0.67f, 0.67f };
		// A line from the hero to each one.
		bool lines = true;
		// Lines are drawn to those no further than this, in the units the distance is shown in; 0 for any.
		float line_reach = 60;
		// Of the text, for wanted ones and for avoided ones: the distance, the name, and sitting around the hero
		// this many pixels out on the way to the place, instead of by the place.
		bool distances[2] = { true, true };
		bool labels[2] = { false, true };
		bool text_near[2] = { true, true };
		// Ordinary, cataclysm and exalted breaches: blue, red and yellow.
		float breach_color[3][3] = { { 0.45f, 0.80f, 1.0f }, { 1.0f, 0.30f, 0.30f }, { 1.0f, 0.85f, 0.25f } };
		float thickness = 2;
		float size = 2;
		// A circle around each one in view, this many pixels in radius.
		bool circle = true;
		float circle_radius = 100;
		float circle_thickness = 3;
		// Where the name and distance sit from the marked place, in pixels.
		float text_at[2] = { 0, 158 };
		// Names and distances are shown for those no further than this; 0 for any.
		float text_reach = 0;
		// Text only for those a line is drawn to.
		bool text_with_line = true;
		// An arrow by the hero toward the nearest wanted one: how far out it sits and how long it is, in pixels.
		bool arrow = true;
		float arrow_ring = 20;
		float arrow_size = 28;
		// Its own color, instead of the color of the one it points at.
		bool arrow_colored = false;
		float arrow_color[3] = { 1, 1, 1 };
		// Drawn around the letters, this many pixels wide; 0 for none.
		float outline = 2;
		float outline_color[3] = { 0, 0, 0 };
		float text_ring = 120;

		// Shows and hides every drawing.
		int hide_key = VK_F8;
		bool hidden = false;
		// Starts set, and is set again when the key is rebound, so that press does not count.
		bool key_was_down = true;

		std::vector<Mark> marks;
		Mark hero;
		bool in_view = false;
		ULONGLONG preview_until = 0;

		Esp() : Feature("Mark drawings on screen", "esp",
			"Draws markers over the game for the things turned on below: on them when in view, and pointing their way when not.")
		{
			tab = "ESP";
		}

#ifdef SLORM_DEV
		// What the room holds, logged whenever it changes, to learn how the game keeps it.
		void Trace(const View& Now, const std::vector<RValue>& Breaches)
		{
			static std::string last_view, last_coords;
			static double last_breach = -1;
			static size_t last_count = 0;
			char text[256];
			snprintf(text, sizeof(text), "view %.0fx%.0f drawn at %.0f,%.0f-%.0f,%.0f of window %.0fx%.0f", Now.w, Now.h, Now.left, Now.top, Now.right, Now.bottom, Now.window_w, Now.window_h);
			if (last_view != text)
			{
				last_view = text;
				Log("esp: %s, hero at %.0f,%.0f, room %.0fx%.0f", text, hero.x, hero.y, RoomSize("room_width"), RoomSize("room_height"));
			}
			if (CInstance* procedural = FindInstance("obj_procedural"))
			{
				std::string coords = Show(Local(procedural, "breach_array_coords"), 3, 200);
				if (coords != last_coords)
				{
					last_coords = coords;
					Log("esp: breach_array_coords %s", coords.substr(0, 3000).c_str());
				}
			}
			if (Breaches.size() != last_count)
			{
				last_count = Breaches.size();
				std::string places;
				for (const RValue& breach : Breaches)
				{
					snprintf(text, sizeof(text), " %.0f,%.0f %s type %.0f seal %.0f waves %.0f outpost %.0f;", At(breach, "x"), At(breach, "y"), Sprite(breach).c_str(), At(breach, "breach_type"), At(breach, "seal_index"), At(breach, "breach_waves"), At(breach, "breach_outpost"));
					places += text;
				}
				Log("esp: %zu obj_breach:%s", Breaches.size(), places.c_str());
			}
			if (!Breaches.empty() && At(Breaches[0], "id") != last_breach)
			{
				last_breach = At(Breaches[0], "id");
				RValue names = g_Yytk->CallBuiltin("variable_instance_get_names", { Breaches[0] });
				std::string all;
				if (names.IsArray())
					for (const RValue& name : names.ToVector())
						if (name.IsString())
							all += name.ToString() + "=" + Show(g_Yytk->CallBuiltin("variable_instance_get", { Breaches[0], name }), 1, 8) + "; ";
				for (size_t from = 0; from < all.size(); from += 700)
					Log("esp: breach %.0f: %s", last_breach, all.substr(from, 700).c_str());
			}
		}

		static double RoomSize(const char* Name)
		{
			RValue value;
			return AurieSuccess(g_Yytk->GetBuiltin(Name, nullptr, NULL_INDEX, value)) ? ToNumber(value) : 0;
		}
#endif

		void Tick(CInstance* Self) override
		{
			bool key_down = (GetAsyncKeyState(hide_key) & 0x8000) != 0;
			if (key_down && !key_was_down && GetForegroundWindow() == GetActiveWindow())
				hidden = !hidden;
			key_was_down = key_down;
			marks.clear();
			in_view = false;
			CInstance* player = FindInstance("obj_parent_hero");
			View view;
			if (!player || !view.Read())
				return;
			hero.x = ToNumber(Local(player, "x"));
			hero.y = ToNumber(Local(player, "y"));
			view.Place(hero);
			in_view = true;
			std::vector<RValue> found = breaches ? Instances("obj_breach") : std::vector<RValue>();
			for (const RValue& breach : found)
			{
				Mark mark;
				int waves = static_cast<int>(At(breach, "breach_waves"));
				mark.tier = At(breach, "breach_outpost") > 0 ? 3 : std::clamp(waves, 1, 3) - 1;
				if (!tiers[mark.tier])
					continue;
				mark.avoid = tiers[mark.tier] == 2;
				mark.x = At(breach, "x");
				mark.y = At(breach, "y");
				mark.distance = sqrt((mark.x - hero.x) * (mark.x - hero.x) + (mark.y - hero.y) * (mark.y - hero.y));
				view.Place(mark);
				std::string sprite = Sprite(breach);
				mark.kind = sprite.find("_red") != std::string::npos ? 1 : sprite.find("_yellow") != std::string::npos ? 2 : 0;
				marks.push_back(mark);
			}
#ifdef SLORM_DEV
			Trace(view, found);
#endif
		}

		// The name of the sprite a breach shows while closed; it ends in its color when not blue.
		static std::string Sprite(const RValue& Breach)
		{
			RValue sprite = g_Yytk->CallBuiltin("variable_instance_get", { Breach, RValue("animation_inactive") });
			if (sprite.m_Kind != VALUE_REF && ToNumber(sprite) <= 0)
				return "";
			return ToText(g_Yytk->CallBuiltin("sprite_get_name", { sprite }));
		}

		ImU32 Color(const Mark& Of) const
		{
			if (Of.avoid)
				return ImGui::GetColorU32(ImVec4(avoid_color[0], avoid_color[1], avoid_color[2], 1));
			int Kind = Of.kind;
			return ImGui::GetColorU32(ImVec4(breach_color[Kind][0], breach_color[Kind][1], breach_color[Kind][2], 1));
		}

		void DrawMark(ImDrawList* Draw, ImVec2 Screen, ImVec2 From, const Mark& Of, ImU32 Color, const char* Label) const
		{
			const ImU32 shade = IM_COL32(0, 0, 0, 200);
			float unit = ImGui::GetFontSize() * size;
			ImVec2 at(Of.across * Screen.x, Of.down * Screen.y);
			float edge = unit * 2;
			bool seen = at.x >= edge && at.y >= edge && at.x <= Screen.x - edge && at.y <= Screen.y - edge;
			bool line = lines && (line_reach <= 0 || Of.distance / 10 <= line_reach);
			bool written = text_with_line ? line : text_reach <= 0 || Of.distance / 10 <= text_reach;
			char text[64] = "";
			const char* warning = Of.avoid ? "Avoid " : "";
			bool labels = this->labels[Of.avoid], distances = this->distances[Of.avoid], text_near = this->text_near[Of.avoid];
			if (labels && distances) snprintf(text, sizeof(text), "%s%s %.0f", warning, Label, Of.distance / 10);
			else if (labels) snprintf(text, sizeof(text), "%s%s", warning, Label);
			else if (distances) snprintf(text, sizeof(text), "%.0f", Of.distance / 10);
			ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(unit, FLT_MAX, 0, text);
			auto write = [&](ImVec2 Middle)
			{
				ImVec2 place(std::clamp(Middle.x - extent.x / 2, 4.0f, (std::max)(4.0f, Screen.x - extent.x - 4)), std::clamp(Middle.y - extent.y / 2, 4.0f, (std::max)(4.0f, Screen.y - extent.y - 4)));
				if (outline > 0)
				{
					ImU32 rim = ImGui::GetColorU32(ImVec4(outline_color[0], outline_color[1], outline_color[2], 1));
					for (int turn = 0; turn < 8; turn++)
						Draw->AddText(ImGui::GetFont(), unit, { place.x + cosf(turn * 0.785398f) * outline, place.y + sinf(turn * 0.785398f) * outline }, rim, text);
				}
				Draw->AddText(ImGui::GetFont(), unit, place, Color, text);
			};
			// Around the hero, on the way to it.
			auto write_near = [&]
			{
				ImVec2 toward(at.x - From.x, at.y - From.y);
				float far_off = sqrtf(toward.x * toward.x + toward.y * toward.y);
				if (far_off < 1)
					return;
				write({ From.x + toward.x / far_off * text_ring, From.y + toward.y / far_off * text_ring });
			};
			if (seen)
			{
				if (line)
				{
					Draw->AddLine(From, at, shade, thickness + 2);
					Draw->AddLine(From, at, Color, thickness);
				}
				if (circle)
				{
					Draw->AddCircle(at, circle_radius, shade, 0, circle_thickness + 2);
					Draw->AddCircle(at, circle_radius, Color, 0, circle_thickness);
					if (Of.avoid)
					{
						// Crossed out.
						float arm = circle_radius * 0.7071f;
						for (float side : { 1.0f, -1.0f })
						{
							Draw->AddLine({ at.x - arm, at.y - arm * side }, { at.x + arm, at.y + arm * side }, shade, circle_thickness + 2);
							Draw->AddLine({ at.x - arm, at.y - arm * side }, { at.x + arm, at.y + arm * side }, Color, circle_thickness);
						}
					}
				}
				if (text[0] && written && text_near) write_near();
				else if (text[0] && written) write({ at.x + text_at[0], at.y + text_at[1] });
				return;
			}
			// Out of view: where the way to it from the hero leaves the screen.
			ImVec2 way(at.x - From.x, at.y - From.y);
			float length = sqrtf(way.x * way.x + way.y * way.y);
			if (length < 1)
				return;
			way.x /= length;
			way.y /= length;
			float reach = length;
			if (way.x > 0) reach = (std::min)(reach, (Screen.x - edge - From.x) / way.x);
			if (way.x < 0) reach = (std::min)(reach, (edge - From.x) / way.x);
			if (way.y > 0) reach = (std::min)(reach, (Screen.y - edge - From.y) / way.y);
			if (way.y < 0) reach = (std::min)(reach, (edge - From.y) / way.y);
			reach = (std::max)(reach, 0.0f);
			ImVec2 tip(From.x + way.x * reach, From.y + way.y * reach);
			if (line)
			{
				Draw->AddLine(From, tip, shade, thickness + 2);
				Draw->AddLine(From, tip, Color, thickness);
			}
			if (text[0] && written && text_near) write_near();
			else if (text[0] && written) write({ tip.x - way.x * unit * 3, tip.y - way.y * unit * 3 });
		}

		void DrawArrow(ImDrawList* Draw, ImVec2 Screen, ImVec2 From, const Mark& Of) const
		{
			ImVec2 way(Of.across * Screen.x - From.x, Of.down * Screen.y - From.y);
			float length = sqrtf(way.x * way.x + way.y * way.y);
			if (length < 1)
				return;
			way.x /= length;
			way.y /= length;
			ImVec2 side(-way.y, way.x);
			ImVec2 tip(From.x + way.x * (arrow_ring + arrow_size), From.y + way.y * (arrow_ring + arrow_size));
			ImVec2 back(From.x + way.x * arrow_ring, From.y + way.y * arrow_ring);
			ImVec2 a(back.x + side.x * arrow_size * 0.45f, back.y + side.y * arrow_size * 0.45f), b(back.x - side.x * arrow_size * 0.45f, back.y - side.y * arrow_size * 0.45f);
			Draw->AddTriangleFilled(tip, a, b, arrow_colored ? ImGui::GetColorU32(ImVec4(arrow_color[0], arrow_color[1], arrow_color[2], 1)) : Color(Of));
			Draw->AddTriangle(tip, a, b, IM_COL32(0, 0, 0, 220), 2);
		}

		void Hud() override
		{
			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			ImVec2 screen = ImGui::GetIO().DisplaySize;
			// While its looks are being set with none to show, made-up ones in view and out of it.
			if (GetTickCount64() < preview_until && marks.empty())
			{
				ImVec2 middle(screen.x / 2, screen.y / 2);
				Mark near_by, far_off;
				near_by.across = 0.62f;
				near_by.down = 0.38f;
				near_by.distance = 310;
				far_off.across = -0.6f;
				far_off.down = 0.8f;
				far_off.distance = 2450;
				Mark third = near_by;
				third.across = 0.35f;
				third.kind = 2;
				far_off.kind = 1;
				Mark fourth = near_by;
				fourth.down = 0.7f;
				fourth.avoid = true;
				DrawMark(draw, screen, middle, near_by, Color(near_by), TIER_LABELS[0]);
				DrawMark(draw, screen, middle, far_off, Color(far_off), TIER_LABELS[1]);
				DrawMark(draw, screen, middle, third, Color(third), TIER_LABELS[2]);
				DrawMark(draw, screen, middle, fourth, Color(fourth), TIER_LABELS[3]);
				if (arrow) DrawArrow(draw, screen, middle, near_by);
				return;
			}
			if (!in_view || hidden)
				return;
			ImVec2 from(hero.across * screen.x, hero.down * screen.y);
			for (const Mark& mark : marks)
				DrawMark(draw, screen, from, mark, Color(mark), TIER_LABELS[mark.tier]);
			const Mark* nearest = nullptr;
			for (const Mark& mark : marks)
				if (!mark.avoid && (!nearest || mark.distance < nearest->distance)) nearest = &mark;
			if (arrow && nearest) DrawArrow(draw, screen, from, *nearest);
		}

		bool Draw() override
		{
			bool changed = false, touched = false;
			float width = ImGui::GetFontSize() * 12;
			auto touch = [&] { touched |= ImGui::IsItemActive() || ImGui::IsItemHovered(); };
			auto tip = [](const char* Text) { if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Text); };
			auto slider = [&](const char* Label, float* Value, float Low, float High, const char* Format, const char* Tip)
			{
				ImGui::SetNextItemWidth(width);
				changed |= ImGui::SliderFloat(Label, Value, Low, High, Format, ImGuiSliderFlags_AlwaysClamp);
				touch();
				if (Tip) tip(Tip);
			};
			auto color = [&](const char* Label, float* Value, const char* Tip)
			{
				changed |= ImGui::ColorEdit3(Label, Value, ImGuiColorEditFlags_NoInputs);
				touch();
				if (Tip) tip(Tip);
			};
			if (!ImGui::BeginTabBar("esp"))
				return changed;

			if (ImGui::BeginTabItem("Breaches"))
			{
				changed |= ImGui::Checkbox("Mark breaches", &breaches);
				tip("The breaches enemies come out of, as on the Battlefield maps.");

				ImGui::SeparatorText("Tiers");
				const char* names[4] = { "Breach I", "Breach II", "Breach III", "Breach V" };
				const char* tips[4] = {
					"Ordinary breaches, of one wave.",
					"Multi-Waves Breaches whose seal shows two waves.",
					"Multi-Waves Breaches whose seal shows three waves.",
					"Chaos Breaches: the large seal, releasing enemies without pause once opened.",
				};
				for (int tier = 0; tier < 4; tier++)
				{
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
					changed |= ImGui::Combo(names[tier], &tiers[tier], "Hide\0Mark\0Avoid\0");
					tip(tips[tier]);
				}
				ImGui::TextDisabled("Hide: no marker. Mark: wanted. Avoid: crossed out.");

				ImGui::SeparatorText("Colors");
				color("breach", breach_color[0], "The color of the markers of ordinary breaches, the blue ones.");
				ImGui::SameLine();
				color("cataclysm", breach_color[1], "The color of the markers of cataclysm breaches, the red ones.");
				ImGui::SameLine();
				color("exalted", breach_color[2], "The color of the markers of exalted breaches, the yellow ones.");
				ImGui::SameLine();
				color("avoided", avoid_color, "The color of the markers of tiers set to Avoid.");

				ImGui::SeparatorText("Text");
				const char* whose[2] = { "Wanted", "Avoided" };
				for (int set = 0; set < 2; set++)
				{
					ImGui::PushID(set);
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(whose[set]);
					tip(set ? "Breaches of the tiers set to Avoid." : "Breaches of the tiers set to Mark.");
					ImGui::SameLine(ImGui::GetFontSize() * 5);
					changed |= ImGui::Checkbox("Name", &labels[set]);
					ImGui::SameLine();
					changed |= ImGui::Checkbox("Distance", &distances[set]);
					ImGui::SameLine();
					int place = text_near[set] ? 1 : 0;
					ImGui::SetNextItemWidth(width);
					if (ImGui::Combo("##place", &place, "By the breach\0Around the hero\0"))
					{
						text_near[set] = place == 1;
						changed = true;
					}
					touch();
					tip("By the breach: next to it, or at the screen's edge when it is out of view.\nAround the hero: a set way out from the hero, in its direction.");
					ImGui::PopID();
				}

				ImGui::SeparatorText("Nearest breach");
				changed |= ImGui::Checkbox("Arrow to the nearest breach", &arrow);
				tip("An arrow by the hero pointing at the nearest breach.\nBreaches of tiers set to Avoid or Hide are passed over.");
				if (arrow)
				{
					ImGui::Indent();
					slider("from the hero##arrow", &arrow_ring, 20, 800, "%.0f px", "How far out from the hero the arrow sits.");
					slider("size##arrow", &arrow_size, 8, 200, "%.0f px", nullptr);
					changed |= ImGui::Checkbox("Recolor", &arrow_colored);
					tip("Unticked, the arrow takes the color of the breach it points at.");
					if (arrow_colored)
					{
						ImGui::SameLine();
						color("##arrow color", arrow_color, nullptr);
					}
					ImGui::Unindent();
				}
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Settings"))
			{
				if (KeyButton("hide", &hide_key))
				{
					key_was_down = true;
					changed = true;
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(hidden ? "hides or shows every drawing (hidden now)" : "hides or shows every drawing");
				ImGui::SeparatorText("Line");
				changed |= ImGui::Checkbox("Line from the hero", &lines);
				if (lines)
				{
					ImGui::Indent();
					slider("thickness##line", &thickness, 1, 12, "%.0f", nullptr);
					slider("max distance##line", &line_reach, 0, 400, line_reach <= 0 ? "any" : "%.0f", "No line is drawn to anything further away than this.\nIn the numbers the markers show; all the way left for any distance.");
					ImGui::Unindent();
				}

				ImGui::SeparatorText("Circle");
				changed |= ImGui::Checkbox("Circle around it", &circle);
				if (circle)
				{
					ImGui::Indent();
					slider("radius", &circle_radius, 4, 800, "%.0f px", nullptr);
					slider("thickness##circle", &circle_thickness, 1, 12, "%.0f", nullptr);
					ImGui::Unindent();
				}

				ImGui::SeparatorText("Text");
				slider("size", &size, 0.5f, 4, "%.1f", nullptr);
				slider("outline", &outline, 0, 6, outline <= 0 ? "none" : "%.0f px", "How wide the rim around the letters is; all the way left for none.");
				if (outline > 0)
				{
					ImGui::SameLine();
					color("outline color", outline_color, nullptr);
				}
				changed |= ImGui::Checkbox("Only where a line is drawn", &text_with_line);
				tip("Text is shown only for what the line reaches: the line must be on, and the thing within the line's max distance.");
				if (!text_with_line)
					slider("max distance##text", &text_reach, 0, 400, text_reach <= 0 ? "any" : "%.0f", "No text is shown for anything further away than this.\nIn the numbers the markers show; all the way left for any distance.");
				ImGui::TextDisabled("Placed by the thing marked");
				slider("across", &text_at[0], -400, 400, "%.0f px", "How far right of it the text sits; negative for left.");
				slider("down", &text_at[1], -400, 400, "%.0f px", "How far below it the text sits; negative for above.");
				ImGui::TextDisabled("Placed around the hero");
				slider("from the hero", &text_ring, 20, 800, "%.0f px", "How far out from the hero the text sits.");
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
			touched |= ImGui::IsPopupOpen("picker", ImGuiPopupFlags_AnyPopupId);
			if (touched) preview_until = GetTickCount64() + 1500;
			return changed;
		}

		void Load(const std::string& Name, std::istringstream& Words) override
		{
			float value = 0;
			if (Name == "avoid_color" || Name == "outline_color" || Name == "arrow_color")
			{
				float color[3];
				float* into = Name == "avoid_color" ? avoid_color : Name == "arrow_color" ? arrow_color : outline_color;
				if (Words >> color[0] >> color[1] >> color[2])
					for (int i = 0; i < 3; i++) into[i] = std::clamp(color[i], 0.0f, 1.0f);
				return;
			}
			for (int kind = 0; kind < 3; kind++)
			{
				if (Name != "breach_color_" + std::to_string(kind))
					continue;
				float color[3];
				if (Words >> color[0] >> color[1] >> color[2])
					for (int i = 0; i < 3; i++) breach_color[kind][i] = std::clamp(color[i], 0.0f, 1.0f);
				return;
			}
			if (!(Words >> value))
				return;
			if (Name == "text_at")
			{
				float down = 0;
				if (Words >> down)
				{
					text_at[0] = std::clamp(value, -400.0f, 400.0f);
					text_at[1] = std::clamp(down, -400.0f, 400.0f);
				}
				return;
			}
			if (Name == "breaches") breaches = value != 0;
			else if (Name.rfind("tier_", 0) == 0 && Name.size() == 6 && Name[5] >= '0' && Name[5] <= '3') tiers[Name[5] - '0'] = std::clamp(static_cast<int>(value), 0, 2);
			else if (Name == "lines") lines = value != 0;
			else if (Name == "line_reach") line_reach = std::clamp(value, 0.0f, 400.0f);
			else if (Name == "distances") distances[0] = value != 0;
			else if (Name == "labels") labels[0] = value != 0;
			else if (Name == "avoid_distances") distances[1] = value != 0;
			else if (Name == "avoid_labels") labels[1] = value != 0;
			else if (Name == "avoid_near") text_near[1] = value != 0;
			else if (Name == "thickness") thickness = std::clamp(value, 1.0f, 12.0f);
			else if (Name == "circle_thickness") circle_thickness = std::clamp(value, 1.0f, 12.0f);
			else if (Name == "size") size = std::clamp(value, 0.5f, 4.0f);
			else if (Name == "circle") circle = value != 0;
			else if (Name == "text_near") text_near[0] = value != 0;
			else if (Name == "text_ring") text_ring = std::clamp(value, 20.0f, 800.0f);
			else if (Name == "outline") outline = std::clamp(value, 0.0f, 6.0f);
			else if (Name == "arrow") arrow = value != 0;
			else if (Name == "arrow_colored") arrow_colored = value != 0;
			else if (Name == "hide_key" && value >= 8 && value <= 254) hide_key = static_cast<int>(value);
			else if (Name == "arrow_ring") arrow_ring = std::clamp(value, 20.0f, 800.0f);
			else if (Name == "arrow_size") arrow_size = std::clamp(value, 8.0f, 200.0f);
			else if (Name == "text_with_line") text_with_line = value != 0;
			else if (Name == "text_reach") text_reach = std::clamp(value, 0.0f, 400.0f);
			else if (Name == "circle_radius") circle_radius = std::clamp(value, 4.0f, 800.0f);
		}

		void Save(std::ostream& File) override
		{
			File << "esp.breaches " << (breaches ? 1 : 0) << '\n'
				<< "esp.lines " << (lines ? 1 : 0) << '\n'
				<< "esp.line_reach " << line_reach << '\n'
				<< "esp.distances " << (distances[0] ? 1 : 0) << '\n'
				<< "esp.labels " << (labels[0] ? 1 : 0) << '\n'
				<< "esp.avoid_distances " << (distances[1] ? 1 : 0) << '\n'
				<< "esp.avoid_labels " << (labels[1] ? 1 : 0) << '\n'
				<< "esp.avoid_near " << (text_near[1] ? 1 : 0) << '\n'
				<< "esp.thickness " << thickness << '\n'
				<< "esp.size " << size << '\n'
				<< "esp.circle " << (circle ? 1 : 0) << '\n'
				<< "esp.circle_radius " << circle_radius << '\n'
				<< "esp.circle_thickness " << circle_thickness << '\n'
				<< "esp.text_at " << text_at[0] << ' ' << text_at[1] << '\n'
				<< "esp.arrow " << (arrow ? 1 : 0) << '\n'
				<< "esp.hide_key " << hide_key << '\n'
				<< "esp.arrow_ring " << arrow_ring << '\n'
				<< "esp.arrow_size " << arrow_size << '\n'
				<< "esp.arrow_colored " << (arrow_colored ? 1 : 0) << '\n'
				<< "esp.arrow_color " << arrow_color[0] << ' ' << arrow_color[1] << ' ' << arrow_color[2] << '\n'
				<< "esp.text_reach " << text_reach << '\n'
				<< "esp.text_with_line " << (text_with_line ? 1 : 0) << '\n'
				<< "esp.outline " << outline << '\n'
				<< "esp.outline_color " << outline_color[0] << ' ' << outline_color[1] << ' ' << outline_color[2] << '\n'
				<< "esp.text_near " << (text_near[0] ? 1 : 0) << '\n'
				<< "esp.text_ring " << text_ring << '\n';
			for (int tier = 0; tier < 4; tier++)
				File << "esp.tier_" << tier << ' ' << tiers[tier] << '\n';
			File << "esp.avoid_color " << avoid_color[0] << ' ' << avoid_color[1] << ' ' << avoid_color[2] << '\n';
			for (int kind = 0; kind < 3; kind++)
				File << "esp.breach_color_" << kind << ' ' << breach_color[kind][0] << ' ' << breach_color[kind][1] << ' ' << breach_color[kind][2] << '\n';
		}
	};
	Esp g_Esp;
}
