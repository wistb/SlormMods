#pragma once
// The cheapest expected way to reinforce an item up to a level, within floors on the odds of each activation:
// which Slormandrites of Fate, Aptitude, Virtue and Harmony to load before the next one. No game headers: tests
// build it with g++.
#include <array>
#include <string>
#include <vector>

namespace opt
{

constexpr int KINDS = 4;
enum Kind { FATE = 0, APTITUDE = 1, VIRTUE = 2, HARMONY = 3 };
// Potentials are valued on a grid and interpolated between its points.
constexpr int STEPS = 20;

// What one unit of a kind adds to the odds (Harmony's failure is negative), and what one is worth in goldus.
struct Unit
{
	double chance = 0, critical = 0, critical_potential = 0, twice = 0, failure = 0, price = 0;
	bool operator==(const Unit&) const = default;
};

struct Load
{
	int count[KINDS] = {};
};

// What the Slormitizer shows for a load; cost is the activation's goldus.
struct Odds
{
	double chance = 0, critical = 0, critical_potential = 0, twice = 0, failure = 0, cost = 0;
};

struct Model
{
	int level = 0;
	int target = 15;
	// The item's potential, in percent: the chance a reinforce starts from. A success spends it, a Critical
	// Reinforce refills it to the critical potential, a failure zeroes it.
	double potential = 0;
	// The odds with none of the four kinds loaded, less the potential and the slormite level terms below:
	// slormelines, reputation, and the like. Failure unclamped.
	double base_chance = 0, base_critical = 0, base_critical_potential = 0, base_twice = 0, base_failure = 0;
	double base_cost = 0;
	// Per tier the slormite is over the next level: chance, critical, double. Per level it is short: failure.
	double over_chance = 25, over_critical = 0, over_twice = 0, under_failure = 50;
	int tier = 1, max_tier = 15;
	// Goldus charged per goldus of purchase price loaded.
	double apply = 1;
	// A slormite goes on every success.
	double slormite_price = 0;
	Unit unit[KINDS];
	double stock[KINDS] = {};
	// Modifier slots not taken by other kinds.
	int free_slots = 4;
	int max_each = 999;
	// Floors every activation on the way must respect: none by default.
	double min_chance = 0, min_critical = 0, min_twice = 0, max_failure = 100;
	bool operator==(const Model&) const = default;
};

struct Result
{
	bool possible = false;
	std::string why;
	Load load;
	Odds odds;
	// Goldus, materials and slormites at purchase price, expected until the item reaches the target.
	double expected_cost = 0;
	double expected_activations = 0;
};

Odds OddsOf(const Model& M, int Level, double Potential, const Load& L);

// A mix of Fate, Aptitude and Virtue at one level, with what does not depend on the Harmony or the potential.
struct Mix
{
	Load load;
	// The Harmony that zeroes failure, within the stock and the slots.
	int most = 0;
	double raw0 = 0, failure0 = 0, spend0 = 0, critical = 0, twice = 0;
	double success_cost = 0, success_count = 0;
};

// Keeps the costs to come solved for one shape of model: everything but the item's level and potential, and
// stock beyond what any load could use. A run then only looks its new level up, and a lower level only adds rows.
class Planner
{
public:
	// Plan(M) would be a lookup, not a solve.
	bool Ready(const Model& M) const;
	// The slow part: solves or extends the table for M. Fine on another thread, with the Planner to itself.
	void Prepare(const Model& M);
	// Prepares if needed, then picks the load at M's level and potential.
	Result Plan(const Model& M);

private:
	struct Row
	{
		std::array<double, STEPS + 1> cost, count;
	};
	bool have = false;
	Model shape;
	// From level target - 1 downwards: rows[target - 1 - level].
	std::vector<Row> rows;
	// The mixes of the level last solved or planned: a run plans the same level many times.
	int mixes_level = -1;
	std::vector<Mix> mixes;
	void Come(int Level, double Potential, double& Cost, double& Count) const;
	const std::vector<Mix>& MixesAt(int Level);
	Row Solve(int Level);
};

// One shot, with a Planner of its own.
Result Optimize(const Model& M);

}
