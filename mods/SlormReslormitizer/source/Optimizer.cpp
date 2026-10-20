#include "Optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace opt
{

static constexpr double INF = std::numeric_limits<double>::infinity();
static constexpr double EPS = 1e-9;
static constexpr double STEP = 100.0 / STEPS;

static double Clamp100(double X) { return std::clamp(X, 0.0, 100.0); }
static int Over(const Model& M, int Level) { return (std::max)(0, M.tier - 1 - Level); }
// The highest tier is never short by more than one level.
static int Under(const Model& M, int Level)
{
	if (Level <= M.tier - 1) return 0;
	return M.tier >= M.max_tier ? 1 : Level - (M.tier - 1);
}

// The odds before the clamps; chance is the raw chance, before failure cuts it.
static Odds Raw(const Model& M, int Level, double Potential, const Load& L)
{
	Odds o;
	o.chance = Potential + M.base_chance + M.over_chance * Over(M, Level);
	o.critical = M.base_critical + M.over_critical * Over(M, Level);
	o.critical_potential = M.base_critical_potential;
	o.twice = M.base_twice + M.over_twice * Over(M, Level);
	o.failure = M.base_failure + M.under_failure * Under(M, Level);
	o.cost = M.base_cost;
	for (int k = 0; k < KINDS; k++)
	{
		const Unit& u = M.unit[k];
		double n = L.count[k];
		o.chance += n * u.chance;
		o.critical += n * u.critical;
		o.critical_potential += n * u.critical_potential;
		o.twice += n * u.twice;
		o.failure += n * u.failure;
		o.cost += n * u.price * M.apply;
	}
	return o;
}

Odds OddsOf(const Model& M, int Level, double Potential, const Load& L)
{
	Odds o = Raw(M, Level, Potential, L);
	o.failure = Clamp100(o.failure);
	o.chance = Clamp100(o.chance) * (1 - o.failure / 100);
	return o;
}

// Units of a kind until Need is covered, counting only the effects it has.
static int Units(double Need, double Each) { return Need <= 0 || Each <= 0 ? 0 : static_cast<int>(std::ceil(Need / Each - EPS)); }

static int Most(const Model& M, int Kind, int Wanted)
{
	return (std::min)({ Wanted, M.max_each, static_cast<int>(std::floor(M.stock[Kind] + EPS)) });
}

// Units of a kind until it has nothing left to add at a level, with nothing else loaded.
static int Wanted(const Model& M, int Level, int Kind)
{
	const Unit& u = M.unit[Kind];
	Odds o = Raw(M, Level, 0, Load());
	return (std::max)({ Units(100 - o.chance, u.chance), Units(100 - o.critical, u.critical),
		Units(100 - o.critical_potential, u.critical_potential), Units(100 - o.twice, u.twice) });
}

// The most of a kind any load up to the target could use: the slormite's level helps least at the top level.
static int Bound(const Model& M, int Kind)
{
	int top = M.target - 1;
	if (Kind != HARMONY) return Most(M, Kind, Wanted(M, top, Kind));
	Load l;
	l.count[FATE] = Bound(M, FATE), l.count[VIRTUE] = Bound(M, VIRTUE);
	return Most(M, HARMONY, Units(Raw(M, top, 0, l).failure, -M.unit[HARMONY].failure));
}

// The model less what a run moves without changing any row: the level, the potential, and stock past the bound.
static Model Shape(const Model& M)
{
	Model s = M;
	s.level = 0, s.potential = 0;
	for (int k = 0; k < KINDS; k++) s.stock[k] = (std::min)(M.stock[k], static_cast<double>(Bound(M, k)));
	return s;
}

// The cheapest Harmony for a mix at a potential, against the best so far. Harmony only moves failure and the cost
// to come is linear in it, so only the least the floors allow and the count that zeroes failure are tried. With
// no potential a failure leads back to the same state: V = spend + c (S + slormite) + (1 - c) V.
static void Try(const Model& M, const Mix& X, double Potential, bool Loop, double FailCost, double FailCount,
	double& Best, Load& Pick, double& Count)
{
	if (X.critical < M.min_critical - EPS || X.twice < M.min_twice - EPS) return;
	double raw = Clamp100(X.raw0 + Potential);
	double cap = M.max_failure;
	if (M.min_chance > 0)
	{
		if (raw <= 0) return;
		cap = (std::min)(cap, 100 * (1 - M.min_chance / raw));
	}
	double drop = -M.unit[HARMONY].failure;
	int least = Units(X.failure0 - cap, drop);
	if (least > X.most || (std::max)(0.0, X.failure0 - least * drop) > cap + EPS) return;
	double harmony_each = M.unit[HARMONY].price * (1 + M.apply);
	for (int h : { least, X.most })
	{
		double failure = Clamp100(X.failure0 - h * drop);
		double c = raw / 100 * (1 - failure / 100);
		double spend = X.spend0 + h * harmony_each;
		double cost, count;
		if (Loop)
		{
			if (c <= 0) continue;
			cost = (spend + c * (X.success_cost + M.slormite_price)) / c;
			count = (1 + c * X.success_count) / c;
		}
		else
		{
			// A failure that cannot be recovered from rules a load out unless it cannot fail.
			cost = spend + c * (X.success_cost + M.slormite_price) + (c < 1 ? (1 - c) * FailCost : 0);
			count = 1 + c * X.success_count + (c < 1 ? (1 - c) * FailCount : 0);
		}
		if (cost < Best)
		{
			Best = cost;
			Pick = X.load;
			Pick.count[HARMONY] = h;
			Count = count;
		}
		if (h == X.most) break;
	}
}

// Cost and activations still to come from a level with a potential; nothing past the target.
void Planner::Come(int Level, double Potential, double& Cost, double& Count) const
{
	if (Level >= shape.target) { Cost = Count = 0; return; }
	const Row& r = rows[shape.target - 1 - Level];
	double x = Clamp100(Potential) / STEP;
	int a = static_cast<int>(std::floor(x)), b = (std::min)(a + 1, STEPS);
	double t = x - a;
	// An unreachable point stays infinite without touching its neighbor.
	if (t <= 0) { Cost = r.cost[a], Count = r.count[a]; return; }
	Cost = r.cost[a] * (1 - t) + r.cost[b] * t;
	Count = r.count[a] * (1 - t) + r.count[b] * t;
}

// The mixes worth trying at a level: each kind up to where it has nothing left to add.
const std::vector<Mix>& Planner::MixesAt(int Level)
{
	if (Level == mixes_level) return mixes;
	const Model& M = shape;
	std::vector<Mix>& Out = mixes;
	mixes_level = Level;
	int fate = Most(M, FATE, Wanted(M, Level, FATE));
	int aptitude = Most(M, APTITUDE, Wanted(M, Level, APTITUDE));
	int virtue = Most(M, VIRTUE, Wanted(M, Level, VIRTUE));
	double drop = -M.unit[HARMONY].failure;
	Out.clear();
	for (int f = 0; f <= fate; f++)
		for (int a = 0; a <= aptitude; a++)
			for (int v = 0; v <= virtue; v++)
			{
				int kinds = (f > 0) + (a > 0) + (v > 0);
				if (kinds > M.free_slots) continue;
				Mix x;
				x.load.count[FATE] = f, x.load.count[APTITUDE] = a, x.load.count[VIRTUE] = v;
				Odds o = Raw(M, Level, 0, x.load);
				x.most = kinds >= M.free_slots ? 0 : Most(M, HARMONY, Units(o.failure, drop));
				x.raw0 = o.chance, x.failure0 = o.failure, x.spend0 = o.cost;
				for (int k = 0; k < KINDS; k++) x.spend0 += x.load.count[k] * M.unit[k].price;
				x.critical = Clamp100(o.critical), x.twice = Clamp100(o.twice);
				double crit = x.critical / 100, twice = x.twice / 100;
				double c1, n1, c1k, n1k, c2, n2, c2k, n2k;
				Come(Level + 1, 0, c1, n1);
				Come(Level + 1, o.critical_potential, c1k, n1k);
				Come(Level + 2, 0, c2, n2);
				Come(Level + 2, o.critical_potential, c2k, n2k);
				x.success_cost = crit * ((1 - twice) * c1k + twice * c2k) + (1 - crit) * ((1 - twice) * c1 + twice * c2);
				x.success_count = crit * ((1 - twice) * n1k + twice * n2k) + (1 - crit) * ((1 - twice) * n1 + twice * n2);
				Out.push_back(x);
			}
	return Out;
}

// The row of a level, from the rows above it: the zero-potential point first, since a failure elsewhere lands there.
Planner::Row Planner::Solve(int Level)
{
	const std::vector<Mix>& mixes = MixesAt(Level);
	Row row;
	row.cost.fill(INF), row.count.fill(INF);
	Load pick;
	for (const Mix& x : mixes)
		Try(shape, x, 0, true, INF, INF, row.cost[0], pick, row.count[0]);
	for (const Mix& x : mixes)
		for (int g = 1; g <= STEPS; g++)
			Try(shape, x, g * STEP, false, row.cost[0], row.count[0], row.cost[g], pick, row.count[g]);
	return row;
}

bool Planner::Ready(const Model& M) const
{
	if (M.level >= M.target) return true;
	return have && static_cast<int>(rows.size()) >= M.target - M.level && Shape(M) == shape;
}

void Planner::Prepare(const Model& M)
{
	if (M.level >= M.target) return;
	Model s = Shape(M);
	if (!have || !(s == shape))
	{
		shape = s;
		rows.clear();
		mixes_level = -1;
		have = true;
	}
	while (static_cast<int>(rows.size()) < M.target - M.level)
		rows.push_back(Solve(M.target - 1 - static_cast<int>(rows.size())));
}

Result Planner::Plan(const Model& M)
{
	Result r;
	if (M.level >= M.target) { r.why = "the item is at that level already"; return r; }
	Prepare(M);
	// The choice now, at the exact potential.
	const std::vector<Mix>& mixes = MixesAt(M.level);
	const Row& row = rows[M.target - 1 - M.level];
	Load pick;
	double cost = INF, count = INF;
	for (const Mix& x : mixes)
		Try(shape, x, M.potential, false, row.cost[0], row.count[0], cost, pick, count);
	if (cost >= INF)
	{
		bool floors = M.min_chance > 0 || M.min_critical > 0 || M.min_twice > 0 || M.max_failure < 100;
		r.why = floors ? "no load in stock meets the floors all the way to the level"
			: M.stock[FATE] < 1 && Raw(M, M.level, M.potential, Load()).chance <= 0
			? "no potential, no Fate in stock and a slormite no higher than the item: no way to reinforce"
			: "no way to reach the level";
		return r;
	}
	r.possible = true;
	r.load = pick;
	r.odds = OddsOf(M, M.level, M.potential, r.load);
	r.expected_cost = cost;
	r.expected_activations = count;
	return r;
}

Result Optimize(const Model& M)
{
	Planner p;
	return p.Plan(M);
}

}
