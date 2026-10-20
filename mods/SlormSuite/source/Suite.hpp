#pragma once
#include <YYToolkit/YYTK_Shared.hpp>
#include <mutex>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

// Overlay appearance, saved with the rest of the config.
struct Appearance
{
	int theme = 2;
	bool custom_accent = false;
	float accent[3] = { 0.78f, 0.62f, 0.25f };
	// Title bar, tabs, input fields and table headers.
	bool custom_primary = false;
	float primary[3] = { 0.16f, 0.29f, 0.48f };
	float opacity = 1.0f;
	float scale = 1.0f;
	// Virtual-key code that shows or hides the overlay.
	int hotkey = 0x76;
};

// Guards everything below; the game step writes it, the overlay reads and edits it.
extern std::recursive_mutex g_Lock;

extern Appearance g_Appearance;
// Set while the hotkey is being rebound, so the key press does not also toggle the overlay.
extern bool g_SuppressToggle;
extern bool g_Visible;

void SaveConfig();
// Where the mod's files sit: the folder of its DLL.
std::string ModFolder();
// A button showing a key; clicking it takes the next key pressed. True when the key changed.
bool KeyButton(const char* Id, int* Key);

// One mod of the suite. Each lives in its own file under features/ and adds itself to the list.
struct Feature
{
	// Shown in the overlay.
	const char* name;
	// Starts its lines in the config; no spaces.
	const char* key;
	const char* summary;
	// Names its own tab in the overlay; without one it is a line of the Mods tab.
	const char* tab = nullptr;
	// Names the mod whose tab holds this one's tab, when it is not one of the overlay's own.
	const char* under = nullptr;
	bool enabled = false;
	// Cleared by a mod whose switch another mod's tab draws; it is then left off the Mods tab.
	bool listed = true;

	Feature(const char* Name, const char* Key, const char* Summary);
	virtual ~Feature() = default;
	// Once at load, after the game's scripts can be looked up.
	virtual void Start() {}
	// Each step of the game's UI object while enabled.
	virtual void Tick(YYTK::CInstance* Self) {}
	// Once on the step after it is turned off, to put back what it changed in the game.
	virtual void Off(YYTK::CInstance* Self) {}
	bool was_enabled = false;
	// Its settings in the overlay, shown while enabled or in its own tab; true when something to save changed.
	virtual bool Draw() { return false; }
	// Set by a feature with looks to choose: it gets a tab of its own under Options, drawn by this.
	bool appearance = false;
	virtual bool DrawAppearance() { return false; }
	// Drawn over the game each frame while enabled, with the overlay shown or not.
	virtual void Hud() {}
	// One "key.Name ..." line of the config.
	virtual void Load(const std::string& Name, std::istringstream& Words) {}
	// Writes its settings as "key.name value" lines.
	virtual void Save(std::ostream& File) {}
};
std::vector<Feature*>& Features();
// A mod's switch and settings, as its tab shows them.
bool DrawFeature(Feature* feature);

// The game, as the features reach it.
extern YYTK::YYTKInterface* g_Yytk;
YYTK::RValue Global(const char* Name);
YYTK::RValue Local(YYTK::CInstance* Self, const char* Name);
// Numbers the game keeps as text count too; 0 for anything else.
double ToNumber(const YYTK::RValue& Value);
std::string ToText(const YYTK::RValue& Value);
// The room the game is in, as it names it: "rm_the_hub" in town.
std::string RoomName();
// The first instance of an object by name; null when the room has none.
YYTK::CInstance* FindInstance(const char* Object);
// A game script by its name without the gml_Script_ prefix; null when the game has none.
YYTK::PFUNC_YYGMLScript FindScript(const char* Name);
// Runs it as the UI object would.
YYTK::RValue CallScript(YYTK::PFUNC_YYGMLScript Script, YYTK::CInstance* Self, std::vector<YYTK::RValue> Arguments = {});

// Walking the hero to a place by the game's click to move.
enum WalkState { WALK_NONE, WALK_STARTING, WALK_WALKING, WALK_ARRIVED, WALK_FAILED };
struct Walk
{
	int state = WALK_NONE;
	// Where to, in the room's units.
	double x = 0, y = 0;
	// Close enough to count as there, and the longest it may take, in seconds.
	double reach = 24, limit = 20;
	int tries = 0;
	double seconds = 0;
	// Why it failed.
	std::string why;
};
// A place in the room, or one that far from the hero.
void WalkTo(double X, double Y, bool Relative = false, double Reach = 24);
void WalkStop();
const Walk& WalkStatus();
// Called for the hero from the input hook, after the game has read the real keys.
void WalkInput(YYTK::CInstance* Hero);
// Tells the game the mouse is on a place in the room, for this long; false without a view of the room.
bool AimAt(double X, double Y, int Milliseconds);
// The same for the hero's own step only, so the view and the real mouse are left alone.
bool AimStep(double X, double Y);
// Called as a script of the hero's step is entered (1) and left (-1).
void WalkScript(int Enter);
// The way on foot between two places of an expedition's floor, as the places to walk by one after
// the other, the last being where to; empty when the bot has not read the floor or it has no way.
std::vector<std::pair<double, double>> FloorRoute(double FromX, double FromY, double ToX, double ToY);
// The floor bot's presses, made for the hero from the input hook.
void BotInput(YYTK::CInstance* Hero);

template <typename... Args>
void Log(const char* Format, Args... Arguments)
{
	Aurie::DbgPrintEx(Aurie::LOG_SEVERITY_INFO, (std::string("[SlormSuite] ") + Format).c_str(), Arguments...);
}

#ifdef SLORM_DEV
// Dev builds only: dumps of the game's variables and a trace of its script calls.
struct Dev
{
	bool want_dump = false;
	std::string dump_note;
	// Names holding any of these words are printed in full; comma separated.
	std::string dump_words = "menu,launch,room,run,floor,breach,portal,temple,forge,hub,citizen,mission,level,chest,challenge,choice,wave,war_,reward,procedural,controller,camera,draw_game,surface,hero,aim_cursor,cursor,kb_,kbc_,input,ctm,skill_equipped,cooldown,warlord,nether,cataclysm,boss,objective,quest,primordial,mba";
	std::string result;
	bool trace = true;
	// Logs the hero's keys and movement variables when they change.
	bool watch = true;
	// Test presses, made in the input hook. The direction held, -1 for none, and until when; 0 before it starts.
	int walk = -1;
	unsigned long long walk_until = 0;
	// How far from the hero the walk test goes, in the room's units.
	float walk_offset[2] = { 300, 0 };
	// The game is told the mouse is where the hero walks to all the time, not only in the hero's step.
	bool hold_mouse = false;
	// A place the hero stood, to walk back to from far off.
	bool mark_want = false, marked = false;
	double mark_x = 0, mark_y = 0;
};
extern Dev g_Dev;
// Dumps without moving the mouse.
constexpr int DUMP_KEY = 0x78;
// Walk tests: by the offset of the Dev tab, and to the mouse.
constexpr int WALK_KEY = 0x75, WALK_MOUSE_KEY = 0x73;
// Remembers where the hero stands, and walks back there.
constexpr int MARK_KEY = 0x72, WALK_MARK_KEY = 0x71;
void DevStart(const std::string& Folder);
void DevTick(YYTK::CInstance* Self);
void DevEvent(YYTK::CCode* Code);
void DevInput(YYTK::CInstance* Self, const YYTK::RValue& Result, int ArgumentCount, YYTK::RValue** Arguments);
std::string Show(const YYTK::RValue& Value, int Depth, size_t Limit);
#endif

inline std::string Lower(std::string Text)
{
	for (char& c : Text) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return Text;
}

bool OverlayInstall(YYTK::YYTKInterface* Yytk);
bool OverlayWantsMouse();
bool OverlayWantsKeys();
// Tells the game the mouse is at this place, in shares of the window, and keeps the real mouse from
// moving it for this long.
void OverlayFakeCursor(float Across, float Down, int Milliseconds);
// The mouse told to the game for a moment, with no hold on the real one; and the real one told again.
void OverlayLendCursor(float Across, float Down);
void OverlayReturnCursor();
// A left button press or release told to the game at a place on the screen, as shares of it.
void OverlayClick(float Across, float Down, bool Press);
void OverlaySetIniPath(const std::string& Path);
