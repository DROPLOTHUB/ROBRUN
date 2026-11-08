#pragma once

// =============================================================================
// perf.hpp — lightweight diagnostic counters for the DMA worker pipeline.
//
// Each "bucket" (perf_bucket_id) tracks time spent in a section of code plus an
// optional integer payload (e.g. number of scatter requests issued, number of
// entities processed). The worker thread uses RAII PerfScope timers around hot
// sections; the menu thread reads a snapshot via `perf::snapshot()` to display
// per-second rates without locking. The worker also dumps a sorted summary to
// ROBRUN.log so lag can be pinpointed without opening the Diagnostics tab.
//
// Storage is lock-free: each bucket is a tuple of three std::atomic<uint64_t>
// (count, total_ns, payload). Snapshots are taken by atomic loads — readers
// may briefly see counters mid-update but the values are monotonic so a stale
// snapshot is always self-consistent on its own.
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <array>
#include <string_view>
#include <iostream>
#include <algorithm>
#include <vector>

namespace perf {

// Ordered by pipeline stage so Diagnostics / log dump read top-to-bottom.
enum perf_bucket_id : int
{
    BK_TICK_TOTAL = 0,      // whole entity_update::update() call
    BK_WORLD_DISCOVERY,     // periodic world discovery
    BK_LEVEL_META_SCATTER,  // scatter (Actors.Data, Actors.Num) per ULevel
    BK_LEVEL_WALK,          // actor-array scatter + CPU classify / queue
    BK_LEVEL_KLASS_SCATTER, // class scatter for cache misses
    BK_WORLD_SCATTER,       // world actors
    BK_DEBUG_SCATTER,       // debug-actor RootComponent + RelLoc
    BK_SCATTER_A_BULK,      // bulk actor window for ALL pawns
    BK_SCATTER_B_POS,       // RelLoc positions for ALL pawns
    BK_SCATTER_B2_C2W,      // C2W fallback for zero RelLoc pawns
    BK_SCATTER_C_CHAIN,     // Health / StatusFlags / Prone / Mesh
    BK_SCATTER_C2_PSTATE,   // TeamId / Role / PlayerName FString hdr
    BK_SCATTER_C3_NAMES,    // PlayerName wchar buffers
    BK_SCATTER_D1_BONE_META,// c2w_buf + bone_arr_ptr
    BK_SCATTER_D2_BONE_ARR, // bone-array prefix
    BK_BONE_CPU,            // CPU bone matrix multiplies
    BK_FIRST_TOUCH_BONES,   // slow per-mesh bone resolve (cache miss)
    BK_LOCAL_PLAYER,        // local pawn snapshot (still mostly serial)
    BK_AIMBOT_TICK,         // aim_loop hot path
    BK_AIMBOT_PREDICTION,   // apply_prediction subsection
    BK_AIMBOT_WRITE,        // ControlRotation DMA write
    // ---- pipeline stages ----
    BK_CAMERA,              // FPS/optic resolve + view matrix
    BK_PLAYERS_LIST,        // registered players + identity fills
    BK_BONES_STAGE,         // bone scatter + apply (whole stage)
    BK_DEATH_CHECK,         // Observed death DMA (throttled)
    BK_PUBLISH_SWAP,        // build back buffer + swap_buffers
    BK_POST_META,           // deferred health/look/hands/chams
    BK_POST_WORLD,          // world pass
    BK_POST_LOOT_POS,       // loot/corpse position RR + gear value
    BK_POST_LOOT_SCAN,      // loot list scan slice
    BK_ESP_PRESENT,         // whole cESP::run()
    BK_ESP_MATRIX,          // Present view-matrix DMA
    BK_ESP_DRAW_PLAYERS,    // player ESP draw pass
    BK_ESP_DRAW_LOOT,       // loot/exfil/nade draw pass
    BK_WORKER_AMMO,         // background ammo resolve
    BK_WORKER_QUEST,        // quest helper refresh
    BK_COUNT
};

inline constexpr std::array<std::string_view, BK_COUNT> bucket_name = {
    "tick total",
    "world discovery",
    "level meta scatter",
    "level walk+classify",
    "class scatter",
    "world actors",
    "debug actor scatter",
    "scatter A: pawn bulk",
    "scatter B: pawn pos",
    "scatter B2: C2W fb",
    "scatter C: hp/mesh",
    "scatter C2: pstate",
    "scatter C3: names",
    "scatter D1: bone meta",
    "scatter D2: bone arr",
    "CPU: bones",
    "first-touch bones",
    "local player snap",
    "aimbot tick",
    "aimbot: prediction",
    "aimbot: DMA write",
    "camera / viewmtx",
    "players identity",
    "bones stage",
    "death checks",
    "publish+swap",
    "post: meta/chams",
    "post: world",
    "post: loot pos",
    "post: loot scan",
    "ESP Present total",
    "ESP matrix DMA",
    "ESP draw players",
    "ESP draw loot+",
    "worker ammo resolve",
    "worker quest helper",
};

// True if the bucket's payload is scatter request count (menu shows "rq/s").
inline constexpr std::array<bool, BK_COUNT> bucket_is_scatter = {
    false, // tick
    false, // world discovery
    true,  // level meta
    true,  // level walk (actor arrays)
    true,  // klass
    true,  // world
    true,  // debug
    true,  // A
    true,  // B
    true,  // B2
    true,  // C
    true,  // C2
    true,  // C3
    true,  // D1
    true,  // D2
    false, // bone cpu
    false, // first touch
    false, // local
    false, // aim
    false, // predict
    false, // write
    false, // camera
    false, // players
    false, // bones stage
    false, // death
    false, // publish
    false, // post meta
    false, // post world
    false, // post loot pos
    false, // post loot scan
    false, // esp present
    false, // esp matrix
    false, // esp players
    false, // esp loot
    false, // worker ammo
    false, // worker quest
};

struct atomic_bucket
{
    std::atomic<uint64_t> count   {0};  // # times this bucket was entered
    std::atomic<uint64_t> total_ns{0};  // accumulated wall time
    std::atomic<uint64_t> payload {0};  // domain-specific (scatter req count etc)
};

inline std::array<atomic_bucket, BK_COUNT> g_buckets{};

struct sample
{
    uint64_t count    = 0;
    uint64_t total_ns = 0;
    uint64_t payload  = 0;
};

inline void reset()
{
    for (auto& b : g_buckets)
    {
        b.count   .store(0, std::memory_order_relaxed);
        b.total_ns.store(0, std::memory_order_relaxed);
        b.payload .store(0, std::memory_order_relaxed);
    }
}

inline std::array<sample, BK_COUNT> snapshot()
{
    std::array<sample, BK_COUNT> out{};
    for (int i = 0; i < BK_COUNT; ++i)
    {
        out[i].count    = g_buckets[i].count   .load(std::memory_order_relaxed);
        out[i].total_ns = g_buckets[i].total_ns.load(std::memory_order_relaxed);
        out[i].payload  = g_buckets[i].payload .load(std::memory_order_relaxed);
    }
    return out;
}

inline void add_payload(perf_bucket_id id, uint64_t value)
{
    g_buckets[id].payload.fetch_add(value, std::memory_order_relaxed);
}

// Dump a sorted per-second breakdown to stdout (ROBRUN.log). Call ~1 Hz from
// the worker so lag can be pinpointed without opening the Diagnostics tab.
inline void log_delta_summary(const std::array<sample, BK_COUNT>& prev,
                              const std::array<sample, BK_COUNT>& cur,
                              double dt_sec)
{
    if (dt_sec < 0.05) return;

    struct Row { int id; double ms_s; double avg_ms; uint64_t calls_s; uint64_t pay_s; };
    std::vector<Row> rows;
    rows.reserve(BK_COUNT);

    const uint64_t tick_ns = cur[BK_TICK_TOTAL].total_ns - prev[BK_TICK_TOTAL].total_ns;
    const uint64_t tick_n  = cur[BK_TICK_TOTAL].count    - prev[BK_TICK_TOTAL].count;
    const double tick_hz   = tick_n / dt_sec;
    const double tick_avg  = tick_n ? (double(tick_ns) / double(tick_n) / 1e6) : 0.0;
    const double tick_load = (double(tick_ns) / (dt_sec * 1e9)) * 100.0;

    for (int i = 0; i < BK_COUNT; ++i)
    {
        if (i == BK_TICK_TOTAL) continue;
        const uint64_t dns = cur[i].total_ns - prev[i].total_ns;
        const uint64_t dn  = cur[i].count    - prev[i].count;
        const uint64_t dp  = cur[i].payload  - prev[i].payload;
        if (dns == 0 && dn == 0) continue;
        Row r{};
        r.id      = i;
        r.ms_s    = double(dns) / 1e6 / dt_sec;
        r.avg_ms  = dn ? double(dns) / double(dn) / 1e6 : 0.0;
        r.calls_s = (uint64_t)(double(dn) / dt_sec + 0.5);
        r.pay_s   = (uint64_t)(double(dp) / dt_sec + 0.5);
        rows.push_back(r);
    }

    std::sort(rows.begin(), rows.end(),
        [](const Row& a, const Row& b) { return a.ms_s > b.ms_s; });

    std::cout << "[perf] tick " << tick_hz << " Hz  avg " << tick_avg
              << " ms  load " << tick_load << "%  (dt=" << dt_sec << "s)\n";

    const size_t show = rows.size() < 16 ? rows.size() : 16;
    for (size_t i = 0; i < show; ++i)
    {
        const Row& r = rows[i];
        std::cout << "[perf]   #" << (i + 1) << "  "
                  << bucket_name[r.id] << "  "
                  << r.ms_s << " ms/s  avg " << r.avg_ms << " ms  "
                  << r.calls_s << "/s";
        if (bucket_is_scatter[r.id] && r.pay_s)
            std::cout << "  " << r.pay_s << " rq/s";
        else if (r.pay_s)
            std::cout << "  pay " << r.pay_s << "/s";
        std::cout << '\n';
    }
    if (!rows.empty())
        std::cout << "[perf] HOTTEST = " << bucket_name[rows[0].id]
                  << " (" << rows[0].ms_s << " ms/s)\n";
    std::cout.flush();
}

class scope
{
public:
    explicit scope(perf_bucket_id id) noexcept
        : id_(id), start_(std::chrono::steady_clock::now()) {}

    ~scope()
    {
        const auto elapsed = std::chrono::steady_clock::now() - start_;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
        g_buckets[id_].total_ns.fetch_add(static_cast<uint64_t>(ns), std::memory_order_relaxed);
        g_buckets[id_].count   .fetch_add(1,                         std::memory_order_relaxed);
    }

    scope(const scope&)            = delete;
    scope& operator=(const scope&) = delete;

private:
    perf_bucket_id                                  id_;
    std::chrono::steady_clock::time_point           start_;
};

} // namespace perf

#define PERF_CONCAT_(a, b) a##b
#define PERF_CONCAT(a, b)  PERF_CONCAT_(a, b)
#define PERF_SCOPE(id) ::perf::scope PERF_CONCAT(_perf_scope_, __LINE__){::perf::id}
