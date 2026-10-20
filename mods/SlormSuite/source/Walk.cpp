// Walks the hero to a place with the game's own click to move, which finds the way. The game takes
// the target from the mouse while the click is held, so it is told the mouse is at the place, then
// the click is pressed on the hero. Driven from the input hook, once a step of the hero.
#include "Suite.hpp"
#include <cmath>
#include <vector>
using namespace Aurie;
using namespace YYTK;

namespace
{
	Walk g_Walk;
	// The mouse is moved this long before the click, which is held until this long after the start of a try.
	constexpr ULONGLONG AHEAD_MS = 60, PRESS_MS = 210;
	// How long the hero gets to start moving.
	constexpr ULONGLONG START_MS = 800;
	// Idle this long while not there ends a try. Where the hero is, is looked at this often: hardly
	// further on than the last time while it means to walk, and it is stuck.
	constexpr ULONGLONG IDLE_MS = 120, LOOK_MS = 300;
	constexpr double STUCK = 6;
	// A stuck hero steps aside this far for this long, then is sent on: to each side, back and to the
	// sides, straight back, and so on, as seen from where it is going.
	constexpr double ASIDE = 48;
	constexpr ULONGLONG ASIDE_MS = 420;
	constexpr double TURNS[] = { 90, -90, 135, -135, 180, 60, -60, 150, -150 };
	// Tries that never got the hero moving, and steps aside, before the walk is given up.
	constexpr int TRIES = 3, STEPS = 9;

	bool g_Want = false, g_Relative = false;
	double g_WantX = 0, g_WantY = 0, g_WantReach = 24;
	ULONGLONG g_StartedAt = 0, g_TryAt = 0, g_MovedAt = 0, g_IdleAt = 0;
	double g_LastX = 0, g_LastY = 0;
	// Where the hero was when last looked at, and whether it was stuck then.
	ULONGLONG g_LookedAt = 0;
	double g_LookX = 0, g_LookY = 0;
	bool g_Stuck = false;
	// The step aside being taken, and how many have been.
	ULONGLONG g_AsideAt = 0;
	double g_AsideX = 0, g_AsideY = 0;
	int g_Steps = 0, g_Unstarted = 0;
	// The places to walk by on the way, the last being where to, and the one being walked to. One
	// is left for the next from this near. Without the floor's way, where to is all there is.
	std::vector<std::pair<double, double>> g_Route;
	size_t g_Leg = 0;
	constexpr double PASS = 40;
	// Where the way was last found anew for a hero that stopped; stopped there again, it steps aside.
	bool g_Stopped = false;
	double g_StopX = 0, g_StopY = 0;
	constexpr double SAME = 32;

	void Plan(double X, double Y)
	{
		g_Route = FloorRoute(X, Y, g_Walk.x, g_Walk.y);
		if (g_Route.empty())
			g_Route.push_back({ g_Walk.x, g_Walk.y });
		g_Leg = 0;
		if (g_Route.size() < 2)
			return;
		std::string by;
		char text[32];
		for (size_t i = 0; i + 1 < g_Route.size(); i++)
		{
			snprintf(text, sizeof(text), " %.0f,%.0f", g_Route[i].first, g_Route[i].second);
			by += text;
		}
		Log("walk: by%s", by.c_str());
	}

	void End(int State, const char* Why, double X, double Y)
	{
		g_Walk.state = State;
		g_Walk.why = Why;
		g_Walk.seconds = (GetTickCount64() - g_StartedAt) / 1000.0;
		Log("walk: %s at %.0f %.0f, %.0f from %.0f %.0f, after %.1f s and %d tries: %s", State == WALK_ARRIVED ? "arrived" : "failed",
			X, Y, hypot(g_Walk.x - X, g_Walk.y - Y), g_Walk.x, g_Walk.y, g_Walk.seconds, g_Walk.tries, Why);
	}
}

const Walk& WalkStatus() { return g_Walk; }

bool AimAt(double X, double Y, int Milliseconds)
{
	RValue camera = g_Yytk->CallBuiltin("view_get_camera", { RValue(0.0) });
	double view_x = ToNumber(g_Yytk->CallBuiltin("camera_get_view_x", { camera })), view_y = ToNumber(g_Yytk->CallBuiltin("camera_get_view_y", { camera }));
	double view_w = ToNumber(g_Yytk->CallBuiltin("camera_get_view_width", { camera })), view_h = ToNumber(g_Yytk->CallBuiltin("camera_get_view_height", { camera }));
	if (view_w <= 0 || view_h <= 0)
		return false;
	OverlayFakeCursor(static_cast<float>((X - view_x) / view_w), static_cast<float>((Y - view_y) / view_h), Milliseconds);
	return true;
}

namespace
{
	// Where the hero's step is to find the mouse, until when; and whether the game has been told so.
	ULONGLONG g_AimUntil = 0;
	float g_AimAcross = 0, g_AimDown = 0;
	bool g_Lent = false;
	int g_Depth = 0;
	// The game's cursor object keeps the mouse's place in the room, and the hero walks to that: it is
	// put on the place for the hero's step and put back after.
	double g_AimX = 0, g_AimY = 0;
	const char* const CURSOR[] = { "x", "y", "controller_offset_x", "controller_offset_y" };
	RValue g_Cursor[4];
	bool g_Moved = false;

	void MoveCursor(bool There)
	{
		CInstance* cursor = FindInstance("obj_cursor");
		if (!cursor || There == g_Moved)
			return;
		for (int i = 0; i < 4; i++)
		{
			if (There)
				g_Cursor[i] = Local(cursor, CURSOR[i]);
			g_Yytk->CallBuiltin("variable_instance_set", { RValue(cursor), RValue(CURSOR[i]), There ? RValue(i % 2 ? g_AimY : g_AimX) : g_Cursor[i] });
		}
		g_Moved = There;
	}
}

bool AimStep(double X, double Y)
{
#ifdef SLORM_DEV
	if (g_Dev.hold_mouse)
		return AimAt(X, Y, 100);
#endif
	RValue camera = g_Yytk->CallBuiltin("view_get_camera", { RValue(0.0) });
	double view_x = ToNumber(g_Yytk->CallBuiltin("camera_get_view_x", { camera })), view_y = ToNumber(g_Yytk->CallBuiltin("camera_get_view_y", { camera }));
	double view_w = ToNumber(g_Yytk->CallBuiltin("camera_get_view_width", { camera })), view_h = ToNumber(g_Yytk->CallBuiltin("camera_get_view_height", { camera }));
	if (view_w <= 0 || view_h <= 0)
		return false;
	g_AimAcross = static_cast<float>((X - view_x) / view_w);
	g_AimDown = static_cast<float>((Y - view_y) / view_h);
	g_AimUntil = GetTickCount64() + 100;
	g_AimX = X;
	g_AimY = Y;
	OverlayLendCursor(g_AimAcross, g_AimDown);
	MoveCursor(false);
	MoveCursor(true);
	g_Lent = true;
	return true;
}

void WalkScript(int Enter)
{
	g_Depth += Enter;
	if (Enter > 0 && GetTickCount64() < g_AimUntil)
	{
		OverlayLendCursor(g_AimAcross, g_AimDown);
		MoveCursor(true);
		g_Lent = true;
	}
	else if (Enter < 0 && g_Depth <= 0 && g_Lent)
	{
		g_Depth = 0;
		g_Lent = false;
		OverlayReturnCursor();
		MoveCursor(false);
		static bool said = false;
		if (!said)
			Log("walk: the mouse is lent to the hero's step and given back");
		said = true;
	}
}

void WalkTo(double X, double Y, bool Relative, double Reach)
{
	g_Want = true;
	g_WantReach = Reach;
	g_Relative = Relative;
	g_WantX = X;
	g_WantY = Y;
}

void WalkStop()
{
	g_Want = false;
	if (g_Walk.state == WALK_STARTING || g_Walk.state == WALK_WALKING)
	{
		g_Walk.state = WALK_FAILED;
		g_Walk.why = "stopped";
		Log("walk: stopped");
	}
}

void WalkInput(CInstance* Hero)
{
	if (!g_Want && g_Walk.state != WALK_STARTING && g_Walk.state != WALK_WALKING)
		return;
	auto set = [&](const char* Name, double Value) { g_Yytk->CallBuiltin("variable_instance_set", { RValue(Hero), RValue(Name), RValue(Value) }); };
	double x = ToNumber(Local(Hero, "x")), y = ToNumber(Local(Hero, "y"));
	std::string state = ToText(Local(Hero, "actual_state"));
	ULONGLONG now = GetTickCount64();
	if (g_Want)
	{
		g_Want = false;
		g_Walk = Walk();
		g_Walk.x = g_Relative ? x + g_WantX : g_WantX;
		g_Walk.y = g_Relative ? y + g_WantY : g_WantY;
		g_Walk.reach = g_WantReach;
		g_Walk.state = WALK_STARTING;
		g_Walk.tries = 1;
		g_StartedAt = g_TryAt = g_MovedAt = g_LookedAt = now;
		g_IdleAt = g_AsideAt = 0;
		g_Steps = g_Unstarted = 0;
		g_Stuck = g_Stopped = false;
		g_LastX = g_LookX = x;
		g_LastY = g_LookY = y;
		Log("walk: from %.0f %.0f to %.0f %.0f in %s", x, y, g_Walk.x, g_Walk.y, RoomName().c_str());
		Plan(x, y);
	}
	double away = hypot(g_Walk.x - x, g_Walk.y - y);
	if (state == "dead" || ToNumber(Local(Hero, "enable_input")) < 1)
		return End(WALK_FAILED, "the hero cannot be moved", x, y);
	if (now - g_StartedAt > static_cast<ULONGLONG>(g_Walk.limit * 1000))
		return End(WALK_FAILED, "took too long", x, y);
	if (fabs(x - g_LastX) > 0.05 || fabs(y - g_LastY) > 0.05)
		g_MovedAt = now;
	g_LastX = x;
	g_LastY = y;

	if (now - g_LookedAt >= LOOK_MS)
	{
		g_Stuck = state == "move" && hypot(x - g_LookX, y - g_LookY) < STUCK;
		g_LookedAt = now;
		g_LookX = x;
		g_LookY = y;
	}
	// A step aside: clicked like the walk itself, then the walk is clicked anew from where it ends.
	if (g_AsideAt)
	{
		if (now - g_AsideAt < ASIDE_MS)
		{
			AimStep(g_AsideX, g_AsideY);
			if (now - g_AsideAt >= AHEAD_MS && now - g_AsideAt < PRESS_MS)
				set("ctm_key", 1);
			return;
		}
		g_AsideAt = 0;
		g_Walk.state = WALK_STARTING;
		g_TryAt = g_MovedAt = g_LookedAt = now;
		g_IdleAt = 0;
		g_Stuck = false;
		g_LookX = x;
		g_LookY = y;
		Plan(x, y);
	}
	// Near a place on the way, the next one is clicked while the hero still walks.
	bool passed = false;
	while (g_Leg + 1 < g_Route.size() && hypot(g_Route[g_Leg].first - x, g_Route[g_Leg].second - y) <= PASS)
		g_Leg++, passed = true;
	if (passed)
		g_TryAt = now;
	// The click: held a moment, then the game walks on by itself.
	if (now - g_TryAt < PRESS_MS)
	{
		if (!AimStep(g_Route[g_Leg].first, g_Route[g_Leg].second))
			return End(WALK_FAILED, "no view of the room", x, y);
		if (now - g_TryAt >= AHEAD_MS)
			set("ctm_key", 1);
	}
	if (g_Walk.state == WALK_STARTING)
	{
		if (state == "move" && now - g_MovedAt < 50)
			g_Walk.state = WALK_WALKING;
		else if (away <= g_Walk.reach)
			return End(WALK_ARRIVED, "already there", x, y);
		else if (now - g_TryAt < START_MS)
			return;
	}
	// Idle for a moment, or in one place too long, ends the try.
	if (state == "move") g_IdleAt = 0;
	else if (!g_IdleAt) g_IdleAt = now;
	bool stopped = g_Walk.state == WALK_STARTING || (g_IdleAt && now - g_IdleAt > IDLE_MS) || g_Stuck;
	if (!stopped)
		return;
	// A reach of nothing is a click on a thing: done once the hero has stopped.
	if (away <= g_Walk.reach || g_Walk.reach <= 0)
		return End(WALK_ARRIVED, g_Walk.reach <= 0 ? "clicked" : "", x, y);
	// Never set off: the place may be one the game will not walk to, so that is tried a few times only.
	if (g_Walk.state == WALK_STARTING && ++g_Unstarted >= TRIES)
		return End(WALK_FAILED, "did not start moving", x, y);
	if (g_Walk.state != WALK_STARTING)
		g_Unstarted = 0;
	const char* why = g_Walk.state == WALK_STARTING ? "did not start moving" : g_Stuck ? "got stuck" : "stopped short";
	if (g_Steps >= STEPS)
		return End(WALK_FAILED, why, x, y);
	g_Walk.tries++;
	// A skill cast on the way stops the hero where nothing is in his way: stopped short further on
	// than the last time, he is only sent on. Stuck where the floor knows the way, it is found anew.
	if (g_Walk.state != WALK_STARTING && !(g_Stopped && hypot(x - g_StopX, y - g_StopY) < SAME))
	{
		size_t legs = g_Route.size();
		bool stuck = g_Stuck;
		g_Stopped = true;
		g_StopX = x;
		g_StopY = y;
		Plan(x, y);
		if (!stuck || legs > 1 || g_Route.size() > 1)
		{
			g_Steps += stuck ? 1 : 0;
			Log("walk: %s at %.0f %.0f, %.0f away; sent on, try %d", why, x, y, away, g_Walk.tries);
			g_Walk.state = WALK_STARTING;
			g_TryAt = g_MovedAt = g_LookedAt = now;
			g_IdleAt = 0;
			g_Stuck = false;
			g_LookX = x;
			g_LookY = y;
			return;
		}
	}
	// Otherwise the hero steps aside before it is sent on.
	if (g_Walk.state != WALK_STARTING && away > 1)
	{
		double turn = (atan2(g_Walk.y - y, g_Walk.x - x) * 180 / 3.14159265358979 + TURNS[g_Steps % 9]) * 3.14159265358979 / 180;
		g_AsideX = x + cos(turn) * ASIDE;
		g_AsideY = y + sin(turn) * ASIDE;
		g_AsideAt = now;
		g_Steps++;
		g_Stuck = false;
		Log("walk: %s at %.0f %.0f, %.0f away; step %d aside to %.0f %.0f", why, x, y, away, g_Steps, g_AsideX, g_AsideY);
		return;
	}
	Log("walk: %s at %.0f %.0f, %.0f away; try %d", why, x, y, away, g_Walk.tries);
	g_Walk.state = WALK_STARTING;
	g_TryAt = g_MovedAt = now;
	g_IdleAt = 0;
}
