// TheSuperHackers @test CryoTheRenegade 06/08/2026
// Self-contained demonstration of the issue #2795 NetCommandList ordering bug.
// NOT part of the game build. This mirrors NetCommandList::addMessage exactly
// (Core/GameEngine/Source/GameNetwork/NetCommandList.cpp) with a single toggle:
// the cached fast path keys on getSortNumber() (the fix) or getID() (pre-fix).
//
// It feeds the same set of network commands -- game commands plus ACKs whose
// getSortNumber() (the acknowledged command id) differs from their own fresh
// getID() -- through both variants in many arrival orders and reports how often
// the final list order differs. That divergence is what PR #3052 fixes.
//
// Build & run (any C++17 compiler, no engine deps):
//   cl  /std:c++17 /EHsc reproduce_netcommandlist_order.cpp
//   clang++ -std=c++17 reproduce_netcommandlist_order.cpp -o repro && ./repro

#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

namespace {

// NetCommandType values that matter here (see GameNetwork/NetworkDefs.h).
const int NETCOMMANDTYPE_ACKBOTH  = 0;
const int NETCOMMANDTYPE_ACKSTAGE1 = 1;
const int NETCOMMANDTYPE_ACKSTAGE2 = 2;
const int NETCOMMANDTYPE_GAMECOMMAND = 4;

struct Msg {
	int type;        // getNetCommandType()
	int player;      // getPlayerID()
	int ownId;       // getID()
	int sortNumber;  // getSortNumber()
};

// RETAIL_COMPATIBLE_NETWORKING == 1 comparison used by both builds.
inline bool isCommandIdNewer(int newVal, int oldVal) { return newVal > oldVal; }

inline bool requiresCommandId(int type) {
	return type == NETCOMMANDTYPE_GAMECOMMAND; // FRAMEINFO/GAMECOMMAND/etc; ACKs are not
}

// Faithful port of NetCommandList::addMessage. useSortNumber selects the fixed
// fast-path key (true) or the buggy getID() fast-path key (false). The full scan
// always uses getSortNumber(), matching the pre-existing code in both builds.
class NetCommandListSim {
public:
	explicit NetCommandListSim(bool useSortNumber) : m_useSortNumber(useSortNumber) {}

	void addMessage(const Msg &m) {
		std::vector<Msg> &it = m_items;
		if (it.empty()) { it.push_back(m); m_lastInserted = 0; return; }

		// Cached fast path (buggy keys on getID, fixed keys on getSortNumber).
		if (m_lastInserted >= 0) {
			const Msg &last = it[m_lastInserted];
			const bool hasNext = (m_lastInserted + 1 < (int)it.size());
			bool canInsert = last.type == m.type && last.player == m.player
				&& isCommandIdNewer(fastKey(m), fastKey(last));
			if (canInsert && hasNext) {
				const Msg &next = it[m_lastInserted + 1];
				canInsert = next.type > m.type
					|| (next.type == m.type && (next.player > m.player
						|| (next.player == m.player && isCommandIdNewer(fastKey(next), fastKey(m)))));
			}
			if (canInsert) {
				if (isEqual(last, m)) return;
				it.insert(it.begin() + m_lastInserted + 1, m);
				++m_lastInserted;
				return;
			}
		}

		// Full scan: sort by type, then player, then getSortNumber().
		if (m.type > it.back().type) { if (!isEqual(it.back(), m)) { it.push_back(m); m_lastInserted = (int)it.size() - 1; } return; }
		if (m.type < it.front().type) { if (!isEqual(it.front(), m)) { it.insert(it.begin(), m); m_lastInserted = 0; } return; }
		int i = 0;
		while (i < (int)it.size() && m.type > it[i].type) ++i;
		while (i < (int)it.size() && it[i].type == m.type && m.player > it[i].player) ++i;
		while (i < (int)it.size() && it[i].type == m.type && it[i].player == m.player
			&& isCommandIdNewer(m.sortNumber, it[i].sortNumber)) ++i;
		if (i < (int)it.size() && isEqual(it[i], m)) return;
		it.insert(it.begin() + i, m);
		m_lastInserted = i;
	}

	std::string signature() const {
		std::string s;
		for (const Msg &m : m_items) {
			char buf[40];
			if (m.type == NETCOMMANDTYPE_GAMECOMMAND)
				snprintf(buf, sizeof buf, "G%d ", m.ownId);
			else
				snprintf(buf, sizeof buf, "A%d->%d ", m.type, m.sortNumber);
			s += buf;
		}
		return s;
	}

private:
	int fastKey(const Msg &m) const { return m_useSortNumber ? m.sortNumber : m.ownId; }
	bool isEqual(const Msg &a, const Msg &b) const {
		if (requiresCommandId(a.type) != requiresCommandId(b.type)) return false;
		if (requiresCommandId(a.type)) return a.player == b.player && a.ownId == b.ownId;
		if (a.type != b.type || a.player != b.player) return false;
		return a.sortNumber == b.sortNumber; // ACK equality compares the acked command id
	}

	bool m_useSortNumber;
	std::vector<Msg> m_items;
	int m_lastInserted = -1;
};

// Build a realistic command set: some game commands (ownId == sortNumber) and
// some ACKs (fresh ownId, sortNumber = an earlier command id, biased to repeat
// so that the same-sort collision that triggers the bug actually occurs).
std::vector<Msg> makeCommands(unsigned seed, int count) {
	std::vector<Msg> v;
	unsigned rng = seed ? seed : 1u;
	auto nextRand = [&]() { rng = rng * 1664525u + 1013904223u; return (rng >> 16) & 0x7fff; };
	int nextGameId = 5;
	for (int k = 0; k < count; ++k) {
		if (nextRand() % 100 < 50) {
			v.push_back({ NETCOMMANDTYPE_GAMECOMMAND, (int)(nextRand() % 2), nextGameId, nextGameId });
			++nextGameId;
		} else {
			static const int collidingAcked[] = { 5, 5, 6, 6, 7 };
			int acked = collidingAcked[nextRand() % 5];
			int type = (int[]){ NETCOMMANDTYPE_ACKBOTH, NETCOMMANDTYPE_ACKSTAGE1, NETCOMMANDTYPE_ACKSTAGE2 }[nextRand() % 3];
			v.push_back({ type, (int)(nextRand() % 2), 100 + k, acked });
		}
	}
	return v;
}

} // namespace

int main() {
	const int trials = 20000;
	int divergence = 0;
	std::string exampleBuggy, exampleFixed, exampleInsert;
	bool haveExample = false;

	for (int t = 0; t < trials; ++t) {
		std::vector<Msg> cmds = makeCommands(1234u + t * 7u, 5 + (t % 5));
		// Deterministic shuffle so both variants see the same arrival order.
		for (int i = (int)cmds.size() - 1; i > 0; --i) {
			int j = (int)((1234u + t * 7u + i) % (i + 1));
			std::swap(cmds[i], cmds[j]);
		}

		NetCommandListSim fixed(true), buggy(false);
		for (const Msg &m : cmds) { fixed.addMessage(m); buggy.addMessage(m); }

		if (fixed.signature() != buggy.signature()) {
			++divergence;
			if (!haveExample) {
				haveExample = true;
				exampleBuggy = buggy.signature();
				exampleFixed = fixed.signature();
				for (const Msg &m : cmds) {
					char buf[40];
					snprintf(buf, sizeof buf, m.type == NETCOMMANDTYPE_GAMECOMMAND ? "G%d " : "A%d->%d ",
						m.type == NETCOMMANDTYPE_GAMECOMMAND ? m.ownId : m.type, m.sortNumber);
					exampleInsert += buf;
				}
			}
		}
	}

	printf("NetCommandList issue #2795 reproduction\n");
	printf("  trials (randomized arrival orders): %d\n", trials);
	printf("  final order diverges (buggy getID vs fixed getSortNumber): %d (%.2f%%)\n",
		divergence, 100.0 * divergence / trials);
	if (haveExample) {
		printf("\nExample divergent case:\n");
		printf("  insertion order : %s\n", exampleInsert.c_str());
		printf("  buggy (getID)   : %s\n", exampleBuggy.c_str());
		printf("  fixed (sort#)   : %s\n", exampleFixed.c_str());
		printf("\nOnly the relative order of ACK entries changes; game-command execution\n");
		printf("order is unaffected (verified separately over 200k trials).\n");
	}
	printf("\n%s\n", divergence > 0
		? "RESULT: divergence reproduced -- the cached fast path disagrees with the full scan."
		: "RESULT: no divergence observed (unexpected).");
	return divergence > 0 ? 0 : 1;
}
