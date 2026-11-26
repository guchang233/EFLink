#pragma once
// Room-wide combat damage statistics & DPS tracker. No Unity dependencies.
// Host aggregates authoritative stats for all room members (and works solo
// offline); clients receive bounded snapshots from the host.
#include "nlohmann/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace eflink {

inline const char* damageTypeKey(int type) {
    switch (type) {
    case 0: return "physical";
    case 1: return "real";
    case 2: return "fire";
    case 3: return "pulse";
    case 4: return "cryst";
    case 5: return "lifedrain";
    case 6: return "natural";
    case 7: return "ether";
    case 8: return "fixed";
    default: return "other";
    }
}

inline const char* damageTypeLabel(const std::string& key) {
    if (key == "physical") return "物理";
    if (key == "fire") return "灼热";
    if (key == "pulse") return "电磁";
    if (key == "cryst") return "寒冷";
    if (key == "natural") return "自然";
    if (key == "ether") return "源石";
    if (key == "real") return "真实";
    if (key == "fixed") return "固定";
    if (key == "lifedrain") return "汲取";
    return "其他";
}

inline std::string formatDamageNumber(double value) {
    if (!std::isfinite(value) || value < 0.0) value = 0.0;
    char buf[48]{};
    if (value >= 1e8) std::snprintf(buf, sizeof buf, "%.2f亿", value / 1e8);
    else if (value >= 1e4) std::snprintf(buf, sizeof buf, "%.1f万", value / 1e4);
    else std::snprintf(buf, sizeof buf, "%.0f", value);
    return buf;
}

class CombatStats {
public:
    static constexpr std::size_t kTimelineSeconds = 30;
    static constexpr std::uint64_t kCombatTimeoutMs = 6000;
    static constexpr int kTypeCount = 9;

    struct Entry {
        std::uint16_t id = 0;
        std::string name;
        std::string op;
        double damage = 0.0;
        double maxHit = 0.0;
        double taken = 0.0;
        std::uint64_t hits = 0;
        std::uint64_t crits = 0;
        std::uint64_t kills = 0;
        std::array<double, kTypeCount> byType{};
    };

private:
    std::uint64_t generation_ = 1;
    std::uint64_t revision_ = 0;
    std::uint64_t startedAt_ = 0;
    std::uint64_t lastHitAt_ = 0;
    std::map<std::uint16_t, Entry> entries_;
    // Rolling 1-second buckets for team DPS sparkline.
    std::uint64_t bucketSec_ = 0;
    std::array<double, kTimelineSeconds> buckets_{};
    // Guest mirror state when receiving authoritative host stats.
    bool mirrored_ = false;
    nlohmann::json mirroredSnapshot_;

    void advanceBuckets(std::uint64_t nowMs) {
        const std::uint64_t sec = nowMs / 1000;
        if (bucketSec_ == 0) {
            bucketSec_ = sec;
            buckets_.fill(0.0);
            return;
        }
        if (sec <= bucketSec_) return;
        const std::uint64_t diff = sec - bucketSec_;
        if (diff >= kTimelineSeconds) {
            buckets_.fill(0.0);
        } else {
            for (std::size_t i = 0; i + diff < kTimelineSeconds; ++i) {
                buckets_[i] = buckets_[i + diff];
            }
            for (std::size_t i = kTimelineSeconds - std::size_t(diff); i < kTimelineSeconds; ++i) {
                buckets_[i] = 0.0;
            }
        }
        bucketSec_ = sec;
    }

public:
    bool autoReset = false;
    std::uint64_t autoResetIdleMs = 15000;

    std::uint64_t generation() const { return generation_; }
    std::uint64_t revision() const { return revision_; }

    void reset(std::uint64_t /*nowMs*/ = 0) {
        ++generation_;
        ++revision_;
        startedAt_ = 0;
        lastHitAt_ = 0;
        entries_.clear();
        bucketSec_ = 0;
        buckets_.fill(0.0);
        mirrored_ = false;
        mirroredSnapshot_ = nlohmann::json();
    }

    void clearMirror() {
        mirrored_ = false;
        mirroredSnapshot_ = nlohmann::json();
    }

    void recordHit(std::uint16_t peer, const std::string& name, const std::string& op,
                   double amount, bool isCrit, int damageType, bool killed, std::uint64_t nowMs) {
        if (!std::isfinite(amount) || amount <= 0.0 || amount > 1e12) return;
        mirrored_ = false;
        if (autoReset && lastHitAt_ > 0 && nowMs > lastHitAt_ && (nowMs - lastHitAt_) >= autoResetIdleMs) {
            reset(nowMs);
        }
        if (startedAt_ == 0) startedAt_ = nowMs;
        lastHitAt_ = nowMs;
        advanceBuckets(nowMs);
        buckets_.back() += amount;

        auto& e = entries_[peer];
        e.id = peer;
        if (!name.empty()) e.name = name.substr(0, 64);
        if (!op.empty()) e.op = op.substr(0, 48);
        e.damage += amount;
        if (amount > e.maxHit) e.maxHit = amount;
        ++e.hits;
        if (isCrit) ++e.crits;
        if (killed) ++e.kills;
        const int idx = (damageType >= 0 && damageType < kTypeCount) ? damageType : 0;
        e.byType[std::size_t(idx)] += amount;
        ++revision_;
    }

    void recordTaken(std::uint16_t peer, const std::string& name, const std::string& op,
                     double amount, std::uint64_t nowMs) {
        if (!std::isfinite(amount) || amount <= 0.0 || amount > 1e12) return;
        mirrored_ = false;
        auto& e = entries_[peer];
        e.id = peer;
        if (!name.empty()) e.name = name.substr(0, 64);
        if (!op.empty()) e.op = op.substr(0, 48);
        e.taken += amount;
        if (startedAt_ == 0) startedAt_ = nowMs;
        ++revision_;
    }

    void updatePeerMeta(std::uint16_t peer, const std::string& name, const std::string& op) {
        auto it = entries_.find(peer);
        if (it == entries_.end()) return;
        if (!name.empty()) it->second.name = name.substr(0, 64);
        if (!op.empty()) it->second.op = op.substr(0, 48);
    }

    nlohmann::json snapshot(std::uint64_t nowMs) const {
        if (mirrored_ && mirroredSnapshot_.is_object()) return mirroredSnapshot_;
        const bool hasHits = startedAt_ > 0 && lastHitAt_ >= startedAt_;
        const bool inCombat = hasHits && nowMs >= lastHitAt_ && (nowMs - lastHitAt_) <= kCombatTimeoutMs;
        double durationSec = 0.0;
        if (hasHits) {
            const std::uint64_t endTs = inCombat ? (std::max)(nowMs, lastHitAt_) : lastHitAt_;
            durationSec = (std::max)(1.0, double(endTs - startedAt_) / 1000.0);
        }

        double totalDamage = 0.0;
        std::uint64_t totalHits = 0, totalKills = 0;
        std::vector<const Entry*> ranked;
        ranked.reserve(entries_.size());
        for (const auto& [id, e] : entries_) {
            totalDamage += e.damage;
            totalHits += e.hits;
            totalKills += e.kills;
            ranked.push_back(&e);
        }
        std::sort(ranked.begin(), ranked.end(), [](const Entry* a, const Entry* b) {
            if (a->damage != b->damage) return a->damage > b->damage;
            return a->id < b->id;
        });

        nlohmann::json players = nlohmann::json::array();
        for (const Entry* e : ranked) {
            nlohmann::json types = nlohmann::json::object();
            for (int i = 0; i < kTypeCount; ++i) {
                if (e->byType[std::size_t(i)] > 0.5) {
                    types[damageTypeKey(i)] = std::round(e->byType[std::size_t(i)]);
                }
            }
            const double share = totalDamage > 0.0 ? (e->damage / totalDamage) : 0.0;
            const double dps = durationSec > 0.0 ? (e->damage / durationSec) : 0.0;
            const double critRate = e->hits > 0 ? (double(e->crits) / double(e->hits)) : 0.0;
            players.push_back({
                {"id", e->id},
                {"name", e->name.empty() ? ("#" + std::to_string(e->id)) : e->name},
                {"op", e->op},
                {"damage", std::round(e->damage)},
                {"dps", std::round(dps)},
                {"share", share},
                {"hits", e->hits},
                {"crits", e->crits},
                {"crit_rate", critRate},
                {"max_hit", std::round(e->maxHit)},
                {"kills", e->kills},
                {"taken", std::round(e->taken)},
                {"by_type", std::move(types)}
            });
        }

        // Shift timeline view to current second without mutating const state.
        nlohmann::json timeline = nlohmann::json::array();
        const std::uint64_t curSec = nowMs / 1000;
        const std::uint64_t shift = (bucketSec_ > 0 && curSec > bucketSec_) ? (curSec - bucketSec_) : 0;
        for (std::size_t i = 0; i < kTimelineSeconds; ++i) {
            double v = 0.0;
            if (shift < kTimelineSeconds && i + shift < kTimelineSeconds) {
                v = buckets_[i + std::size_t(shift)];
            }
            timeline.push_back(std::round(v));
        }

        return {
            {"generation", generation_},
            {"in_combat", inCombat},
            {"duration_s", std::round(durationSec * 10.0) / 10.0},
            {"total_damage", std::round(totalDamage)},
            {"team_dps", durationSec > 0.0 ? std::round(totalDamage / durationSec) : 0.0},
            {"total_hits", totalHits},
            {"total_kills", totalKills},
            {"timeline", std::move(timeline)},
            {"players", std::move(players)}
        };
    }

    static bool validRemoteSnapshot(const nlohmann::json& j) {
        if (!j.is_object() || j.dump().size() > 32768) return false;
        if (!j.contains("players") || !j["players"].is_array() || j["players"].size() > 16) return false;
        if (j.contains("timeline") && (!j["timeline"].is_array() || j["timeline"].size() > kTimelineSeconds)) return false;
        const double total = j.value("total_damage", -1.0);
        const double dps = j.value("team_dps", -1.0);
        const double dur = j.value("duration_s", -1.0);
        if (!std::isfinite(total) || total < 0.0 || total > 1e14) return false;
        if (!std::isfinite(dps) || dps < 0.0 || dps > 1e13) return false;
        if (!std::isfinite(dur) || dur < 0.0 || dur > 1e7) return false;
        for (const auto& p : j["players"]) {
            if (!p.is_object()) return false;
            if (!p.contains("id") || !p["id"].is_number_integer()) return false;
            const double d = p.value("damage", -1.0);
            if (!std::isfinite(d) || d < 0.0 || d > 1e14) return false;
        }
        return true;
    }

    bool applyRemoteSnapshot(const nlohmann::json& j) {
        if (!validRemoteSnapshot(j)) return false;
        mirrored_ = true;
        mirroredSnapshot_ = j;
        generation_ = j.value("generation", generation_);
        return true;
    }

    std::string formatChatSummary(std::uint64_t nowMs) const {
        const auto s = snapshot(nowMs);
        const auto& players = s.at("players");
        if (players.empty() || s.value("total_damage", 0.0) <= 0.0) {
            return "［伤害统计］当前暂无伤害记录";
        }
        char head[160]{};
        std::snprintf(head, sizeof head, "［伤害统计］总伤 %s · %.1fs · 团队DPS %s",
            formatDamageNumber(s.value("total_damage", 0.0)).c_str(),
            s.value("duration_s", 0.0),
            formatDamageNumber(s.value("team_dps", 0.0)).c_str());
        std::string out = head;
        int rank = 0;
        for (const auto& p : players) {
            if (++rank > 4) break;
            char row[160]{};
            std::snprintf(row, sizeof row, " | #%d %s %s(%.0f%%)",
                rank,
                p.value("name", std::string("#")).c_str(),
                formatDamageNumber(p.value("damage", 0.0)).c_str(),
                p.value("share", 0.0) * 100.0);
            out += row;
        }
        return out;
    }
};

} // namespace eflink
