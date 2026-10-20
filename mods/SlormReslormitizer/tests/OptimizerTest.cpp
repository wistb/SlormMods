// Checks of the optimizer against cases small enough to work out by hand: make test
#include "../source/Optimizer.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>

using namespace opt;
static int g_Failed = 0, g_Checks = 0;
#define CHECK(Cond) do { g_Checks++; if (!(Cond)) { g_Failed++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #Cond); } } while (0)
static bool Near(double A, double B, double Eps = 1e-6) { return std::fabs(A - B) <= Eps; }

// A machine with the game's usual per-unit effects and simple prices.
static Model Plain()
{
	Model m;
	m.level = 5, m.target = 6, m.tier = 6, m.potential = 100;
	m.base_cost = 100;
	m.unit[FATE] = { 1, 1, 1, 1, 1, 50 };
	m.unit[APTITUDE] = { 0, 5, 5, 0, 0, 20 };
	m.unit[VIRTUE] = { 0, 0, 0, 5, 3, 20 };
	m.unit[HARMONY] = { 0, 0, 0, 0, -1, 10 };
	for (int k = 0; k < KINDS; k++) m.stock[k] = 999;
	return m;
}

int main()
{
	// Odds follow the game's formula: clamp(raw) * (1 - failure / 100), failure clamped.
	{
		Model m = Plain();
		Load l;
		l.count[FATE] = 10, l.count[VIRTUE] = 4, l.count[HARMONY] = 5;
		Odds o = OddsOf(m, 5, 50, l);
		CHECK(Near(o.critical, 10) && Near(o.critical_potential, 10) && Near(o.twice, 30));
		CHECK(Near(o.failure, 10 + 12 - 5));
		CHECK(Near(o.chance, 60 * (1 - 0.17)));
		CHECK(Near(o.cost, 100 + 10 * 50 + 4 * 20 + 5 * 10));
		// A slormite two tiers over the next level: 25 chance per tier; one short: 50 failure.
		m.tier = 8;
		CHECK(Near(OddsOf(m, 5, 0, Load()).chance, 50));
		m.tier = 5;
		CHECK(Near(OddsOf(m, 5, 0, Load()).failure, 50));
		m.tier = 15, m.level = 14;
		CHECK(Near(OddsOf(m, 14, 0, Load()).failure, 0));
		CHECK(Near(OddsOf(m, 20, 0, Load()).failure, 50));
	}
	// Full potential, nothing to fix: one activation at the base cost, no Slormandrites.
	{
		Model m = Plain();
		m.slormite_price = 7;
		Result r = Optimize(m);
		CHECK(r.possible);
		for (int k = 0; k < KINDS; k++) CHECK(r.load.count[k] == 0);
		CHECK(Near(r.expected_cost, 107) && Near(r.expected_activations, 1));
		CHECK(Near(r.odds.chance, 100));
	}
	// Half potential: a failure zeroes it, after which only Fate works, and Fate brings failure that Harmony must
	// take off again. From zero the best is 100 Fate and 100 Harmony: (100 + 100 * 100 + 100 * 20) / 1 = 12100.
	// Now, 50 Fate and 50 Harmony make a sure success for 6100, less than a bare try at 100 + 0.5 * 12100.
	{
		Model m = Plain();
		m.potential = 50;
		Result r = Optimize(m);
		CHECK(r.possible && r.load.count[FATE] == 50 && r.load.count[HARMONY] == 50);
		CHECK(Near(r.expected_cost, 6100) && Near(r.expected_activations, 1));
	}
	// Failure worth removing: 50 failure, Harmony at 1 goldus; the plan loads 50 Harmony and succeeds for sure.
	{
		Model m = Plain();
		m.tier = 5;   // one level short: 50 failure
		m.unit[HARMONY].price = 1;
		Result r = Optimize(m);
		CHECK(r.possible && r.load.count[HARMONY] == 50);
		CHECK(Near(r.odds.failure, 0) && Near(r.odds.chance, 100));
		CHECK(Near(r.expected_cost, 100 + 50 * 2) && Near(r.expected_activations, 1));
	}
	// Two levels wanted, Virtue cheap: 20 Virtue give a sure Double Reinforce, unless Harmony for their failure
	// costs more than a second go. With Harmony at 1 goldus both are loaded.
	{
		Model m = Plain();
		m.target = 7;
		m.unit[VIRTUE].price = 1, m.unit[HARMONY].price = 1;
		Result r = Optimize(m);
		CHECK(r.possible && r.load.count[VIRTUE] == 20 && r.load.count[HARMONY] == 60);
		CHECK(Near(r.odds.twice, 100) && Near(r.odds.chance, 100));
		CHECK(Near(r.expected_activations, 1));
	}
	// Two levels wanted, nothing cheap but Aptitude: a Critical Reinforce keeps the potential for the second go,
	// so 20 Aptitude (critical 100, critical potential 100) beat a bare second try that would need Fate.
	{
		Model m = Plain();
		m.target = 7, m.tier = 7;   // the slormite is not short at level 6
		m.unit[FATE].price = 1000;
		m.unit[APTITUDE].price = 1;
		Result r = Optimize(m);
		CHECK(r.possible && r.load.count[APTITUDE] == 20);
		CHECK(Near(r.odds.critical, 100) && Near(r.odds.critical_potential, 100));
		CHECK(Near(r.expected_activations, 2));
		// The second go starts with full potential and needs nothing.
		CHECK(Near(r.expected_cost, (100 + 20 * 2) + 100));
	}
	// Nothing can reinforce: no potential, no Fate, slormite not over the item.
	{
		Model m = Plain();
		m.potential = 0;
		m.stock[FATE] = 0;
		Result r = Optimize(m);
		CHECK(!r.possible && !r.why.empty());
	}
	// Slots: with one free slot the plan cannot mix kinds.
	{
		Model m = Plain();
		m.target = 7;
		m.unit[VIRTUE].price = 1, m.unit[HARMONY].price = 1;
		m.free_slots = 1;
		Result r = Optimize(m);
		int kinds = 0;
		for (int k = 0; k < KINDS; k++) kinds += r.load.count[k] > 0;
		CHECK(r.possible && kinds <= 1);
	}
	// Floors. Fate without failure makes a failed go cheap to recover from, so by itself the plan risks a 50%
	// failure rather than pay 2000 a Harmony; a failure ceiling of 0, or a chance floor of 100, makes it pay.
	{
		Model m = Plain();
		m.tier = 5;
		m.unit[FATE].failure = 0;
		m.unit[HARMONY].price = 2000;
		Result r = Optimize(m);
		CHECK(r.possible && r.load.count[HARMONY] == 0);
		m.max_failure = 0;
		r = Optimize(m);
		CHECK(r.possible && r.load.count[HARMONY] == 50 && Near(r.odds.failure, 0));
		m.max_failure = 100, m.min_chance = 100;
		r = Optimize(m);
		CHECK(r.possible && r.load.count[HARMONY] == 50 && Near(r.odds.chance, 100));
		// A failure ceiling of 20 needs 30 Harmony, the least that meets it.
		m.min_chance = 0, m.max_failure = 20;
		r = Optimize(m);
		CHECK(r.possible && r.load.count[HARMONY] == 30 && Near(r.odds.failure, 20));
	}
	// A critical floor loads Aptitude the cost alone would not; floors out of reach say so.
	{
		Model m = Plain();
		m.unit[FATE].price = 1000, m.unit[APTITUDE].price = 500;
		m.min_critical = 100;
		Result r = Optimize(m);
		CHECK(r.possible && r.load.count[APTITUDE] == 20 && Near(r.odds.critical, 100));
		m.stock[APTITUDE] = 10, m.stock[FATE] = 0;
		r = Optimize(m);
		CHECK(!r.possible && r.why.find("floors") != std::string::npos);
	}
	// The planner keeps its rows across the level and the potential, and across stock no load could use; a lower
	// level only adds rows. Each answer is the one a fresh solve gives.
	{
		Model m = Plain();
		m.potential = 0, m.target = 15;
		Planner p;
		p.Plan(m);
		m.level = 9, m.potential = 35;
		CHECK(p.Ready(m));
		auto start = std::chrono::steady_clock::now();
		Result r = p.Plan(m);
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		Result fresh = Optimize(m);
		CHECK(r.possible && Near(r.expected_cost, fresh.expected_cost) && Near(r.expected_activations, fresh.expected_activations));
		for (int k = 0; k < KINDS; k++) CHECK(r.load.count[k] == fresh.load.count[k]);
		printf("a new level looked up: %.1f ms\n", ms);
		CHECK(ms < 20);
		m.potential = 60;
		start = std::chrono::steady_clock::now();
		r = p.Plan(m);
		ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		printf("the same level again: %.2f ms\n", ms);
		CHECK(r.possible && ms < 5);
		// Fate is useful up to 100 at the top level; more in stock changes nothing, less does.
		m.stock[FATE] = 100000;
		CHECK(p.Ready(m));
		m.stock[FATE] = 50;
		CHECK(!p.Ready(m));
		m.stock[FATE] = 999, m.level = 3, m.potential = 0;
		CHECK(!p.Ready(m));
		r = p.Plan(m);
		fresh = Optimize(m);
		CHECK(p.Ready(m) && Near(r.expected_cost, fresh.expected_cost));
		m.target = 16;
		CHECK(!p.Ready(m));
		m.level = 16;
		CHECK(p.Ready(m) && !p.Plan(m).possible);
	}
	// A worst case for speed: zero potential, every kind in stock, ten levels to go.
	{
		Model m = Plain();
		m.potential = 0, m.target = 15;
		auto start = std::chrono::steady_clock::now();
		Result r = Optimize(m);
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		printf("ten levels from zero: %.0f ms, %.0f expected goldus over %.1f activations\n", ms, r.expected_cost, r.expected_activations);
		CHECK(r.possible && ms < 500);
	}
	printf("%d checks, %d failed\n", g_Checks, g_Failed);
	return g_Failed ? 1 : 0;
}
