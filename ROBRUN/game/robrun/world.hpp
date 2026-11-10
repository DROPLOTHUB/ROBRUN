#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <array>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
#include <chrono>

#include "sdk.hpp"

// =============================================================================
// ROB RUN world access.
//
// The battle state is a managed ECS: BattleModel / BattleEntitiesModel /
// WorldEntitiesModel are plain objects created per match.  There is *no*
// static field pointing at any of them (verified by scanning every
// *_StaticFields struct in il2cpp.h), and no MonoBehaviour exposes them:
// BattleEntitiesUpdater keeps them as instance fields.
//
// So the entry point is a bounded, typed breadth-first search from engine
// statics (event bus singleton, delegate roots, Cinemachine, ...).  Every
// edge followed is either
//   * a field recorded in generated/holder_graph.hpp (type-checked at
//     generation time from dump.cs), or
//   * a container element (List/Dictionary/array) or delegate target.
// No word-guessing over arbitrary objects, which keeps the search small.
//
// Once found, the handles are revalidated every frame (a few reads) and only
// re-searched if the match ends.
// =============================================================================

// Defined by the host (Main.cpp) as a global: the on-screen render size.
extern Aspect::Vector2 screen_size;

namespace robrun
{
    enum class handle_kind { none, world, battle, battle_model, updater };

    struct camera_state
    {
        bool valid = false;
        bool from_brain = false;
        vec3 pos{};
        vec3 fwd{0.f, 0.f, 1.f};
        vec3 right{1.f, 0.f, 0.f};
        vec3 up{0.f, 1.f, 0.f};
        float fov = 60.f;
        float aspect = 16.f / 9.f;
        vec3 velocity{};           // m/s, for render-thread coast
        double sampled_at = 0.0;   // steady_clock seconds of the last accepted sample
        vec3 local_follow{};       // CinemachineAimingCameraFollow follow target
        bool local_follow_valid = false;
    };

    // Hard cap for the per-character skeleton. BonesCount is ~60-90 on the
    // humanoid rigs in this build; the cap only protects against corrupt data.
    constexpr int kMaxBones = 192;

    struct entity
    {
        std::uintptr_t model = 0;
        int id = 0;
        std::string name;
        std::string role;       // raw role id/name from the game
        std::string role_label; // display label (Policeman/Robber/Prisoner/Civilian/...)
        std::string stance;
        std::string weapon_name;
        vec3 pos{};
        vec3 velocity{};           // m/s from the position delta (render-thread coast)
        double sampled_at = 0.0;   // steady_clock seconds of this position sample
        float distance = 0.f;
        float height = 0.f;   // capsule height (metres), 0 = unknown
        float width = 0.f;    // capsule diameter (metres), 0 = unknown
        // World-space vertical bounds used for the box. Computed from the
        // capsule + ground contact (or the position pivot as fallback), so the
        // box lands on the character instead of a fixed 1.8 m column above
        // whatever Y the position model happens to store.
        float box_bottom = 0.f;
        float box_top = 0.f;
        bool  box_bounds_valid = false;
        bool  box_anchored = false;
        bool  ground_found = false;
        bool  pos_is_center = false; // position model is the capsule centre
        int health = 0;
        int max_health = 0;
        bool dead = false;
        bool local = false;
        bool group_member = false; // same party/group as local (best-effort)
        bool has_armor = false;
        bool knocked_out = false;
        bool handcuffed = false;
        bool handcuffing = false;
        bool surrendered = false;
        bool sprinting = false;
        bool crouching = false;
        bool armed = false;

        // Loot carried by player
        std::string loot_item_name;
        int loot_value = 0;
        bool has_loot = false;
        
        // Last locker position when player hides
        vec3 locker_last_pos{};
        bool in_locker = false;

        // Skeleton ESP. Bone positions are world-space; bone_parent[i] is the
        // index of the parent bone (-1 for the root, -2 when unreachable).
        bool skeleton_valid = false;
        int  bone_count = 0;
        int  bone_root = -1;
        vec3 skeleton_anchor{};
        std::vector<vec3> bones;
        std::vector<std::int16_t> bone_parent;

        // Raw capsule values used for the box (diagnostics only).
        float cap_radius = 0.f;
        float cap_length = 0.f;
        float cap_ground_gap = 0.f;

        bool player() const { return !name.empty(); }
    };

    // Loot entities (safes, cash registers, shelves, bags, etc.)
    enum class loot_type
    {
        none,
        safe,
        cash_register,
        shelf,
        weight,
        dropped_bag,
        police_award_box,
        cash_point,
        robbed_hideout
    };

    struct loot_entity
    {
        std::uintptr_t model = 0;
        loot_type type = loot_type::none;
        std::string name;
        vec3 pos{};
        float distance = 0.f;
        int value = 0;            // cash amount or item value
        bool locked = false;       // safe/locker lock status
        bool robbed = false;       // already robbed
        bool interactable = true;
    };

    // Vehicle entities (cars, helicopters)
    struct vehicle_entity
    {
        std::uintptr_t model = 0;
        std::string type_name;     // "Car", "Helicopter", etc.
        vec3 pos{};
        float distance = 0.f;
        bool has_driver = false;
        bool has_passengers = false;
        bool siren_active = false;
        bool parked = false;
        std::string driver_role;   // "Police", "Robber", etc.
        int passenger_count = 0;
    };

    // Shared projection. Used by world::project and by the render-thread
    // snapshot draw, which must not touch live world state.
    inline bool project_point(const camera_state& cam, const vec3& point,
                              float view_w, float view_h,
                              float& screen_x, float& screen_y)
    {
        if (!cam.valid) return false;
        const vec3 d = point - cam.pos;
        const float z = d.dot(cam.fwd);
        if (z < 0.05f) return false;

        const float tan_half = std::tan(cam.fov * 0.5f * 3.14159265358979f / 180.f);
        if (tan_half <= 0.f) return false;

        const float nx = d.dot(cam.right) / (z * tan_half * cam.aspect);
        const float ny = d.dot(cam.up) / (z * tan_half);
        if (!std::isfinite(nx) || !std::isfinite(ny)) return false;

        screen_x = (nx * 0.5f + 0.5f) * view_w;
        screen_y = (1.f - (ny * 0.5f + 0.5f)) * view_h;
        return true;
    }

    // Finite-difference velocity. Identical samples do not advance the clock:
    // a replicated position often repeats, and treating that repeat as zero
    // speed is what makes the box sit and then skip. Teleports drop the
    // velocity instead of flinging the box.
    inline void track_velocity(vec3& vel, vec3& last, double& last_t,
                               const vec3& pos, double now_s)
    {
        if (!(last_t > 0.0) || !last.finite() || last.zero() || !pos.finite())
        {
            vel = {};
            last = pos;
            last_t = now_s;
            return;
        }
        const double dt = now_s - last_t;
        if (dt <= 0.002 || dt >= 0.30)
        {
            vel = {};
            last = pos;
            last_t = now_s;
            return;
        }
        const vec3 delta = pos - last;
        const float disp2 = delta.dot(delta);
        if (disp2 < (0.004f * 0.004f) && dt < 0.12)
            return;
        const vec3 step = delta * static_cast<float>(1.0 / dt);
        if (step.dot(step) > (14.f * 14.f))
        {
            vel = {};
            last = pos;
            last_t = now_s;
            return;
        }
        constexpr float k = 0.55f;
        if (vel.dot(step) < -1.f)
            vel = step;
        else
            vel = vel * (1.f - k) + step * k;
        last = pos;
        last_t = now_s;
    }

    // ---------------------------------------------------------------------
    // Unity transform hierarchy solver (skeleton ESP).
    //
    // One record per node (stride 0x30): local position at +0x00, local
    // rotation at +0x10, local scale at +0x20. Parent indices are int32 and
    // -1 terminates the chain. World transforms are composed exactly the way
    // UnityPlayer does it:
    //     pos = parent.pos + parent.rot * (local.pos * parent.scale)
    //     rot = parent.rot * local.rot
    //     scale = parent.scale * local.scale
    // ---------------------------------------------------------------------
    struct uskel_node_world
    {
        vec3 pos{};
        quat rot{};
        vec3 scl{1.f, 1.f, 1.f};
        std::uint8_t ok = 0;
    };

    inline void uskel_solve_node(const std::uint8_t* rec,
                                 const std::int32_t* par,
                                 int lo, int span, int rel,
                                 uskel_node_world* w)
    {
        if (rel < 0 || rel >= span || w[rel].ok) return;

        // The skeleton is a shallow tree; anything deeper means the parent
        // array is corrupt, so give up instead of looping forever.
        constexpr int kMaxDepth = 96;
        int chain[kMaxDepth];
        int depth = 0;
        int cur = rel;
        while (cur >= 0 && cur < span && !w[cur].ok && depth < kMaxDepth)
        {
            chain[depth++] = cur;
            const int p = par[cur];
            if (p < lo || p >= lo + span) break;
            cur = p - lo;
        }
        if (depth == 0) return;

        for (int k = depth - 1; k >= 0; --k)
        {
            const int i = chain[k];
            const std::uint8_t* r =
                rec + static_cast<std::size_t>(i) * unity_native::Record_Stride;

            vec3 lp{};
            quat lr{};
            vec3 ls{1.f, 1.f, 1.f};
            std::memcpy(&lp, r + unity_native::Record_Position, sizeof(lp));
            std::memcpy(&lr, r + unity_native::Record_Rotation, sizeof(lr));
            std::memcpy(&ls, r + unity_native::Record_Scale, sizeof(ls));

            if (!lp.finite() || !lr.finite() || !ls.finite()
                || std::fabs(ls.x) > 100.f || std::fabs(ls.y) > 100.f
                || std::fabs(ls.z) > 100.f)
            {
                w[i].ok = 0;
                continue;
            }

            const int p = par[i];
            if (p < lo || p >= lo + span)
            {
                // Hierarchy root: local space is world space.
                w[i].pos = lp;
                w[i].rot = lr;
                w[i].scl = ls;
                w[i].ok = 1;
                continue;
            }

            const uskel_node_world& pw = w[p - lo];
            if (!pw.ok) { w[i].ok = 0; continue; }

            const vec3 scaled{ lp.x * pw.scl.x, lp.y * pw.scl.y, lp.z * pw.scl.z };
            w[i].pos = pw.pos + pw.rot.rotate(scaled);
            w[i].rot = pw.rot * lr;
            w[i].scl = vec3{ pw.scl.x * ls.x, pw.scl.y * ls.y, pw.scl.z * ls.z };
            w[i].ok = 1;
        }
    }

    // Classes worth naming in the resolver log. Anything matching here proves
    // the BFS got into game code, as opposed to only engine/package types.
    inline bool is_interesting_class(const std::string& name)
    {
        if (name.find("Battle") != std::string::npos) return true;
        if (name.find("WorldEntities") != std::string::npos) return true;
        if (name.find("UnityEntry") != std::string::npos) return true;
        if (name.find("ScreenManager") != std::string::npos) return true;
        if (name.find("ViewContainer") != std::string::npos) return true;
        if (name.find("StartGame") != std::string::npos) return true;
        if (name.find("Updater") != std::string::npos) return true;
        if (name.compare(0, 5, "Game.") == 0) return true;
        if (name.compare(0, 6, "Start.") == 0) return true;
        return false;
    }

    class world
    {
    public:
        struct settings
        {
            // first search
            std::uint32_t node_budget = 30000;
            int max_depth = 12;
            bool aggressive_scan = true;    // scan display classes not in the graph
            bool verbose = true;

            // Fallback when the typed walk from statics fails: search the
            // private (managed heap) VAD ranges for objects whose first word is
            // one of the known goal class pointers. This is what finds the
            // per-match models, which have no static reference at all.
            bool heap_scan = true;
            std::uint32_t heap_scan_interval_ms = 30000; // min time between scans
            std::uint32_t heap_scan_max_results = 4096;
            std::uint64_t heap_scan_margin = 0x200000000ULL; // +/- 8 GB of known objects

            // local player fallback when no PlayerEntityModel/player id is known
            float local_radius = 2.5f;

            // Chains cost a few scatter rounds when a model is first seen (new
            // spawn, new match). Spread them over frames so a full lobby does
            // not stall a single refresh.
            std::uint32_t max_new_chains_per_frame = 12;

            // manual override (0 = auto).  Any of these short-circuits the search.
            std::uintptr_t manual_world = 0;
            std::uintptr_t manual_battle = 0;
            std::uintptr_t manual_battle_model = 0;
            std::uintptr_t manual_updater = 0;
        };

        struct diagnostics
        {
            bool resolved = false;
            bool searching = false;
            std::string found;          // class name of the first handle found
            std::string root;           // static root that led to it
            std::vector<std::string> chain;
            std::uint32_t nodes = 0;
            std::uint32_t klasses = 0;
            std::uint32_t roots_total = 0;
            std::uint32_t roots_valid = 0;
            std::vector<std::string> interesting;  // reachable game classes
            bool goals_logged = false;
            bool roots_logged = false;
            bool truncated = false;     // node budget was hit before a handle
            double last_ms = 0.0;
            unsigned fails = 0;
            unsigned heap_scans = 0;    // full-heap fallback scans run so far
        };

        settings cfg;
        diagnostics diag;

        // ---- handles -----------------------------------------------------
        std::uintptr_t world_model = 0;        // Models.Models.WorldEntities.WorldEntitiesModel
        std::uintptr_t battle_entities = 0;    // Game.Battle.BattleEntitiesModel
        std::uintptr_t battle_model = 0;       // Game.Battle.BattleModel
        std::uintptr_t battle_updater = 0;     // Game.Battle.BattleEntitiesUpdater
        std::uintptr_t local_model = 0;        // BattleCharacterModel of the local player
        int local_id = -1;

        camera_state camera;
        std::vector<entity> entities;
        std::vector<loot_entity> loot;
        std::vector<vehicle_entity> vehicles;
        std::unordered_set<int> party_ids;   // entity ids in the local party
        int scan_stage = 0;
        int scan_skip = 0;
        int carry_cursor = 0;
        std::vector<loot_entity> loot_build;
        std::vector<vehicle_entity> vehicle_build;
        std::unordered_map<int, vec3> locker_build;

        // Last refresh source counts (for the log status line). The master
        // BattleEntitiesModel dictionary can be fully populated while the world
        // character dictionary is nearly empty, so both are merged now.
        std::uint32_t mapped_from_world = 0;
        std::uint32_t mapped_from_battle = 0;
        bool battle_hist_logged = false;

        // Worker timing / DMA traffic diagnostics (milliseconds).
        double update_ms = 0.0;
        mutable double update_ms_peak = 0.0;
        double resolve_ms = 0.0;
        double camera_ms = 0.0;
        double entities_ms = 0.0;
        double update_ms_sum = 0.0;   // sum since the last perf log
        std::uint64_t update_count = 0;
        double esp_hz = 0.0;          // measured worker update rate
        std::uint64_t last_resolve_tick = 0;
        // Per-refresh accounting: how many scatter requests the entity refresh
        // issued and how much wall time they cost on the DMA device.
        std::uint64_t ent_requests = 0;
        double ent_scatter_ms = 0.0;
        double ent_db_ms = 0.0;      // dictionary / model-list scan
        double ent_skel_ms = 0.0;    // Unity-rig skeleton refresh (CPU)
        double ent_build_ms = 0.0;   // entity build / bounds loop
        std::uint32_t ent_pos_fallbacks = 0;  // serial read_position calls
        std::uint32_t ent_chain_posbox = 0;   // chains with a cached value box
        std::uint32_t ent_chain_posmodel = 0;
        std::uint32_t ent_chain_tx = 0;
        std::uint32_t ent_scatter_pos_ok = 0;
        mutable std::uint32_t ent_chain_rejects = 0;  // build_chain rejections this update
        std::uint32_t ent_pos_misses = 0;     // chains that fell through to stale
        std::uint32_t ent_skel_far = 0;       // rig root farther than 2.5 m from body
        mutable std::uint64_t uskel_far_log_tick = 0;
        mutable std::uint32_t pos_fallback_calls = 0;
        // Diagnostics noise control: report each model's missing/empty
        // gameplay SkeletonModel only once per session. The Unity-rig path is
        // the real skeleton source here, so rebuilding a chain was re-logging
        // the same known-empty model every few seconds.
        mutable std::unordered_set<std::uintptr_t> skel_diag_logged;
        mutable std::uint64_t skel_diag_tick = 0;
        std::uint64_t pos_probe_tick = 0;
        std::uint64_t scatter_requests() const { return sc_.requests_total; }
        std::uint64_t scatter_runs() const { return sc_.runs_total; }

        // The master BattleEntitiesModel dictionary keeps a high-water entry
        // array (~hundreds of slots per game) that only changes when someone
        // spawns or despawns. Reading and resolving every slot each frame was
        // the main ESP stutter source, so the resolved wrapper -> ServerModel
        // results are cached and only refreshed when the dictionary changes or
        // the cache ages out.
        std::vector<std::uintptr_t> battle_models_cache;
        // BattleCharacterEntityModel.SimulatedModel pointers. The client
        // animates this copy; its position is the one the mesh is standing on.
        std::unordered_set<std::uintptr_t> simulated_models;
        // wrapper object -> resolved (ServerModel, SimulatedModel). Wrappers
        // are stable for a dictionary epoch, so this avoids re-reading the
        // class + model pointers of ~139 high-water entries every second.
        struct battle_entry
        {
            std::uintptr_t server = 0;
            std::uintptr_t sim = 0;
            std::uint64_t tick = 0;
        };
        std::unordered_map<std::uintptr_t, battle_entry> battle_entry_cache;
        std::uintptr_t battle_cache_dict = 0;
        std::int32_t battle_cache_count = -1;
        std::uint64_t battle_cache_tick = 0;

        // Live character-model list from the dictionaries. Rescanning the
        // dictionaries costs several DMA rounds; the contents only change when
        // a character spawns/despawns, so it is throttled instead of per-frame.
        std::vector<std::uintptr_t> models_cache;
        std::uint64_t models_tick = 0;
        std::uintptr_t models_world = 0;
        std::uintptr_t models_battle = 0;

        // Set by the ESP layer from esp_settings::draw_skeleton. When false
        // the skeleton pointers are never resolved and no extra DMA traffic
        // is generated.
        bool want_skeleton = false;

        // ---- API ---------------------------------------------------------
        bool ready() const
        {
            return world_model || battle_entities || battle_model || battle_updater;
        }

        bool game_cached() const { return game_instance != 0; }

        bool resolve(bool force = false);
        bool update();
        // Bone / view reads. Called after the position snapshot is published so
        // a skeleton scatter cannot hold the boxes on the previous sample.
        void refresh_skeleton();
        // Loot / vehicle reads. Budgeted so they never stall the skeleton scatter.
        void scan_world_objects();
        void reset();

        bool project(const vec3& point, float& screen_x, float& screen_y) const;

        // Concise capsule/ground state for one model (diagnostics only).
        std::string describe_chain(std::uintptr_t model) const;

    private:
        struct node
        {
            std::uintptr_t obj = 0;
            std::uintptr_t klass = 0;
            std::int32_t parent = -1;
            std::uint8_t depth = 0;
            std::uint16_t root = 0;   // index into root_labels
        };

        bool resolve_impl();
        bool validate_handles() const;
        void link_handles();
        void collect_roots(std::vector<node>& out);
        bool search(std::vector<node>& queue);
        bool scan_heap(std::vector<node>& queue);
        void enqueue(std::vector<node>& queue,
                     std::unordered_set<std::uintptr_t>& seen,
                     std::uintptr_t obj, std::uintptr_t klass,
                     std::int32_t parent, int depth, std::uint16_t root);
        void record_hit(handle_kind kind, std::uintptr_t obj,
                        const std::vector<node>& nodes, std::size_t index);

        void refresh_camera();
        void refresh_camera_from_brain(std::uintptr_t core);
        void refresh_camera_from_battle();
        void decode_camera_state(const std::uint8_t* buf);
        void refresh_local_follow(std::uintptr_t core);

        void refresh_entities();
        void read_party_ids();
        void apply_party();
        bool read_character(std::uintptr_t model, entity& out) const;
        bool read_position(std::uintptr_t model, vec3& out) const;
        void mark_local();

        // Per-model chain of the pointers the entity refresh needs. Building it
        // costs a dozen serial reads but only happens when a model is first
        // seen; every later frame reads the whole set with two scatter batches.
        struct ent_chain
        {
            std::uintptr_t model = 0;
            std::int32_t id = -1;
            bool rejected = false;   // build_chain failed; do not retry every tick

            // position sources (first one present wins)
            std::uintptr_t pos_prim = 0;      // IPrimitiveData<Vector3> -> box @0x18
            std::uintptr_t pos_model = 0;     // plain float x/y/z model
            std::uintptr_t tx_prim = 0;       // TransformModel X/Y/Z primitives
            std::uintptr_t ty_prim = 0;
            std::uintptr_t tz_prim = 0;
            bool position_is_center = false;

            // Resolved primitive value boxes. The primitive -> box pointer is
            // set once when the model spawns and does not move while the model
            // is alive, so resolving it per frame just doubled the DMA traffic.
            std::uintptr_t pos_box = 0;
            std::uintptr_t health_box = 0;
            std::uintptr_t sprint_box = 0;
            std::uintptr_t crouch_box = 0;
            std::uintptr_t armed_box = 0;
            std::uintptr_t dead_box = 0;
            std::uintptr_t cap_r_box = 0;
            std::uintptr_t cap_l_box = 0;
            std::uintptr_t cap_g_box = 0;
            std::uintptr_t tx_box = 0;
            std::uintptr_t ty_box = 0;
            std::uintptr_t tz_box = 0;

            // Character visual rotation (TransformModel, degrees). Read every
            // tick so the skeleton turns smoothly with the body.
            std::uintptr_t rx_prim = 0, ry_prim = 0, rz_prim = 0;
            std::uintptr_t rx_box = 0, ry_box = 0, rz_box = 0;

            // State (health / flags / capsule / ground) is refreshed at a much
            // lower rate than the position; position is the only value that
            // needs the full tick rate for smooth boxes.
            std::uint64_t state_tick = 0;
            bool     state_valid = false;
            std::uint64_t slow_tick = 0;      // sprint/armed/dead (~1 Hz)
            std::uint64_t capsule_tick = 0;   // capsule radius/length (~2 Hz)
            std::int32_t st_health = 0;
            std::uint8_t st_sprint = 0, st_crouch = 0, st_armed = 0, st_dead = 0;
            std::uint8_t st_ground_found = 0;
            float    st_cap_plain[3]{};
            float    st_cap_r = 0.f, st_cap_l = 0.f, st_cap_g = 0.f;
            float    st_rx = 0.f, st_ry = 0.f, st_rz = 0.f;
            vec3     st_ground_pt{};

            // state primitives (IPrimitiveData)
            std::uintptr_t sprint_prim = 0;
            std::uintptr_t crouch_prim = 0;
            std::uintptr_t armed_prim = 0;
            std::uintptr_t dead_prim = 0;

            // ---- skeleton -------------------------------------------------
            // SkeletonModel -> SkeletonConfig + float[] pose. The config and
            // hierarchy are static, so they are decoded once here. The pose is
            // re-read at a low rate because it is a contiguous ~4 KB array
            // (animation), then bone world positions are computed on the CPU
            // every tick from position + rotation.
            std::uintptr_t transform = 0;         // ITransformModel
            std::uintptr_t skeleton_model = 0;
            std::uintptr_t skeleton_config = 0;
            std::uintptr_t bones_array = 0;
            std::uintptr_t bones_data_addr = 0;   // bones_array + array data
            int  skel_bones = 0;
            int  skel_bone_size = 0;
            int  skel_local_off = 0;
            int  skel_model_off = 0;
            int  skel_length_off = 0;
            int  skel_root = -1;
            int  skel_order_count = 0;
            bool skel_valid = false;
            bool skel_pose_valid = false;
            std::uint64_t skel_tick = 0;
            std::uint64_t skel_fail_tick = 0;   // last build with no usable skeleton
            std::vector<float> skel_floats;                 // raw decoded pose
            std::array<quat, kMaxBones> skel_local{};
            std::array<quat, kMaxBones> skel_model{};
            std::array<float, kMaxBones> skel_len{};
            std::array<std::int16_t, kMaxBones> skel_parent{};
            std::array<std::int16_t, kMaxBones> skel_order{};

            // Box size smoothing. Feet-offset and height are eased, never the
            // absolute world Y — smoothing the anchor made the rectangle trail
            // the body. Large jumps (teleport/respawn) snap.
            bool box_smooth_valid = false;
            std::uint64_t box_smooth_tick = 0;
            float sm_feet = 0.f, sm_height = 0.f, sm_radius = 0.f;

            // health
            std::uintptr_t health_model = 0;
            std::uintptr_t health_prim = 0;
            std::int32_t max_health = 0;

            // capsule collider (bounding box)
            std::uintptr_t capsule_model = 0; // raw ICapsuleColliderParamsModel
            std::uintptr_t capsule = 0;       // NotReplicated -> plain floats
            bool capsule_plain = false;
            std::uintptr_t cap_r_prim = 0;    // Replicated -> primitives
            std::uintptr_t cap_l_prim = 0;
            std::uintptr_t cap_g_prim = 0;

            // ground contact (CharacterControllerGroundModel)
            std::uintptr_t ground_model = 0;

            float cap_dist_to_ground = 0.f;

            // PhysicsCharacterConfig fallback (DefaultRadius / StandLength /
            // CrouchLength).
            float def_radius = 0.f;
            float stand_length = 0.f;
            float crouch_length = 0.f;

            // extra state (read on throttled ticks)
            std::uintptr_t knockout_model = 0;
            std::uintptr_t has_armor_prim = 0;
            std::uintptr_t has_armor_box = 0;
            std::uintptr_t item_hands_prim = 0; // CommonData.ItemInHands primitive
            std::uintptr_t item_hands_desc = 0; // cached InventoryItemDescription*
            std::uint64_t info_tick = 0;         // weapon/knockout/armor refresh
            std::uint8_t st_has_armor = 0, st_knocked = 0, st_cuffed = 0, st_cuffing = 0, st_surrendered = 0;

            std::string name;
            std::string role;
            std::string role_label;
            std::string stance;
            std::string weapon_name;

            std::uint64_t last_seen = 0;
            std::uint64_t built_tick = 0;
            std::uint32_t state_phase = 0;   // spreads throttled reads over ticks

            // Client simulated model (the instance the game actually animates).
            // The server copy's position trails that mesh.
            bool simulated = false;

            // Visual TransformModel value boxes are resolved once. visual_try
            // stops a missing transform from being re-read every tick.
            std::uint64_t visual_try = 0;

            // Position history for the box coast. last_pos is also the root
            // the cached skeleton is slid toward between pose reads.
            vec3 last_pos{};
            vec3 velocity{};
            double pos_time_s = 0.0;
        };

        // ---- Unity-transform skeleton (visual rig) -------------------------
        // The gameplay SkeletonModel is never populated in this build, so the
        // skeleton comes from the character view's Unity transforms. The
        // static part (managed Transform -> native hierarchy index, parent
        // edges) is resolved once per view; the dynamic part (local TRS
        // records) is re-read at a low rate in a single scatter round for all
        // characters and composed on the CPU.
        static constexpr int kUslots = 96;

        struct uskel_view
        {
            std::uint64_t resolve_tick = 0;
            std::uint64_t resolve_fail_tick = 0;
            bool bound = false;
            int fail_stage = -1;              // last resolve_uskel gate that rejected
            int dbg_anchor = -3;              // slot of CharacterMovementView capsule
            std::uintptr_t dbg_mv = 0, dbg_av = 0, dbg_wv = 0;
            float dbg_dist = -1.f;            // closest solved bone to entity pos

            std::uintptr_t hierarchy = 0;
            std::uintptr_t records = 0;
            std::uintptr_t parents = 0;
            int lo = 0;
            int hi = -1;                 // inclusive
            int slots = 0;
            std::array<int, kUslots> node{};
            std::array<std::int16_t, kUslots> edge{};

            std::vector<std::uint8_t> rec_buf;
            std::vector<std::int32_t> par_buf;

            std::uint64_t tick = 0;
            bool valid = false;
            int count = 0;
            std::array<vec3, kUslots> bones{};
            std::array<std::int16_t, kUslots> bone_parent{};
            vec3 root_pos{};
        };

        bool refresh_view_map(std::uint64_t now_tick);
        std::uintptr_t view_for_id(int id) const;
        bool resolve_uskel(std::uintptr_t view, const vec3& epos, uskel_view& b);
        void compute_uskel(uskel_view& b, const vec3& epos) const;
        void refresh_phys_defaults(std::uint64_t now_tick) const;

        bool build_chain(std::uintptr_t model, ent_chain& out) const;
        bool build_skeleton(ent_chain& out) const;
        void decode_skeleton_pose(ent_chain& c) const;
        void compute_skeleton(entity& e, const ent_chain& c, const quat& world_ori) const;

        std::string role_name(std::uintptr_t role) const;
        std::string stance_name(std::uintptr_t stance) const;

        std::uint64_t last_fail_tick = 0;
        std::uint64_t last_scan_tick = 0;
        std::uint64_t last_empty_rescan = 0;
        mutable std::uint64_t validate_tick = 0;
        int empty_rescans = 0;
        bool force_scan = false;               // next resolve must scan the heap

        // Camera pointers / list caches. Walking the active-brain and
        // active-camera lists (plus every virtual-camera component) every tick
        // was dozens of serial DMA reads per frame; the pointers only change
        // on scene load, so they are refreshed at 1 Hz and the brain state is
        // read directly every tick.
        std::uintptr_t cam_core = 0;
        std::uintptr_t cam_brain = 0;
        std::uintptr_t follow_comp = 0;
        std::uint64_t cam_core_tick = 0;
        std::uint64_t cam_list_tick = 0;
        std::uint64_t follow_tick = 0;

        // Camera state is read inside the same scatter round as the entities
        // instead of as a separate serial transaction every tick.
        std::uint8_t cam_state_buf[cam::State::Size]{};
        bool cam_state_pending = false;
        vec3 cam_prev_pos{};
        vec3 cam_velocity{};
        double cam_prev_t = 0.0;
        std::unordered_map<std::uintptr_t, ent_chain> chain_cache;
        // PhysicsCharacterConfigs.Default (global body size), used when a
        // character has no capsule and no per-character config pointer.
        mutable std::uintptr_t phys_default = 0;
        mutable std::uint64_t phys_default_tick = 0;
        mutable float phys_def_radius = 0.f;
        mutable float phys_stand_len = 0.f;
        mutable float phys_crouch_len = 0.f;
        mutable scatter_batch sc_;

        // Entity id -> BattleCharacterEntityView, refreshed with the model
        // scan. The view holds the Unity transforms the skeleton is read from.
        std::unordered_map<int, std::uintptr_t> view_by_id;
        std::uint64_t view_tick = 0;
        std::uintptr_t view_dict = 0;
        std::int32_t view_count = -1;
        // Ids drawn last refresh (name + last-seen tick), to explain characters
        // that disappear (e.g. when a player gets into a vehicle).
        std::unordered_map<int, std::pair<std::string, std::uint64_t>> last_entity_ids;
        std::unordered_map<std::uintptr_t, uskel_view> uskel_cache;
        std::uint64_t uskel_log_tick = 0;
        std::uint64_t uskel_fail_log_tick = 0;
        int uskel_stage_fail[11]{};        // per-gate reject counts, reset each summary
        std::uint64_t chain_frame = 0;
        std::uintptr_t game_instance = 0;      // cached Game root (heap-scan result)
        std::uintptr_t klass_world = 0;
        std::uintptr_t klass_battle = 0;
        std::uintptr_t klass_bmodel = 0;
        std::uintptr_t klass_updater = 0;
        std::uintptr_t klass_character = 0;
        std::uintptr_t klass_player = 0;
        std::uintptr_t klass_multicast = 0;
        std::uintptr_t klass_game = 0;
        std::vector<const char*> root_labels;
    };

    // =========================================================================
    // Implementation
    // =========================================================================

    inline void world::reset()
    {
        world_model = battle_entities = battle_model = battle_updater = 0;
        local_model = 0;
        local_id = -1;
        camera = {};
        entities.clear();
        loot.clear();
        vehicles.clear();
        loot_build.clear();
        vehicle_build.clear();
        locker_build.clear();
        party_ids.clear();
        scan_stage = 0;
        scan_skip = 0;
        carry_cursor = 0;
        chain_cache.clear();
        skel_diag_logged.clear();
        chain_frame = 0;
        battle_models_cache.clear();
        simulated_models.clear();
        battle_entry_cache.clear();
        battle_cache_dict = 0;
        battle_cache_count = -1;
        battle_cache_tick = 0;
        models_cache.clear();
        models_tick = 0;
        models_world = 0;
        models_battle = 0;
        cam_prev_pos = {};
        cam_velocity = {};
        cam_prev_t = 0.0;
        cam_core = 0;
        cam_brain = 0;
        follow_comp = 0;
        cam_core_tick = 0;
        cam_list_tick = 0;
        follow_tick = 0;
        cam_state_pending = false;
        std::memset(cam_state_buf, 0, sizeof(cam_state_buf));
        diag = {};
    }

    inline bool world::validate_handles() const
    {
        auto alive = [](std::uintptr_t p, const char* expect) -> bool
        {
            if (!obj_ptr(p)) return false;
            const klass_info* i = info_of(p);
            return i && i->name == expect;
        };
        if (world_model && !alive(world_model, "WorldEntitiesModel")) return false;
        if (battle_entities && !alive(battle_entities, "BattleEntitiesModel")) return false;
        if (battle_model && !alive(battle_model, "BattleModel")) return false;
        if (battle_updater && !alive(battle_updater, "BattleEntitiesUpdater")) return false;
        return ready();
    }

    inline void world::link_handles()
    {
        // Derivation rules between the four handles:
        //   updater -> battle (0x18), battle model (0x70), player id (0x28)
        //   battle  -> world (0x60), battle model (0x90), player entity (0xD0)
        //   model   -> battle (0x90)
        for (int pass = 0; pass < 3; ++pass)
        {
            if (battle_updater)
            {
                if (!battle_entities) battle_entities = read_ptr(battle_updater + off::BattleEntitiesUpdater::BattleEntitiesModel);
                if (!battle_model) battle_model = read_ptr(battle_updater + off::BattleEntitiesUpdater::BattleModel);
                const int pid = read_t<std::int32_t>(battle_updater + off::BattleEntitiesUpdater::PlayerEntityId);
                if (pid > 0) local_id = pid;
            }
            if (battle_entities)
            {
                if (!world_model) world_model = read_ptr(battle_entities + off::BattleEntitiesModel::WorldEntitiesModel);
                if (!battle_model) battle_model = read_ptr(battle_entities + off::BattleEntitiesModel::BattleModel);
                if (0 == local_model)
                {
                    const std::uintptr_t player = read_ptr(battle_entities + off::BattleEntitiesModel::Player);
                    if (player)
                    {
                        const std::uintptr_t server = read_ptr(player + off::PlayerEntityModel::ServerModel);
                        if (server) local_model = server;
                        const int id = read_t<std::int32_t>(player + off::EntityModel::Id);
                        if (id > 0) local_id = id;
                    }
                }
            }
            if (battle_model && !battle_entities)
                battle_entities = read_ptr(battle_model + off::BattleModel::BattleEntitiesModel);
            if (battle_model && !world_model)
                world_model = read_ptr(battle_model + off::BattleModel::WorldEntitiesModel);
        }

        if (local_model && !obj_ptr(local_model)) local_model = 0;
    }

    inline void world::enqueue(std::vector<node>& queue, std::unordered_set<std::uintptr_t>& seen,
                               std::uintptr_t obj, std::uintptr_t klass,
                               std::int32_t parent, int depth, std::uint16_t root)
    {
        if (!obj_ptr(obj)) return;
        if (!obj_ptr(klass)) klass = read_ptr(obj);
        if (!obj_ptr(klass)) return;
        if (!seen.insert(obj).second) return;
        node n;
        n.obj = obj;
        n.klass = klass;
        n.parent = parent;
        n.depth = static_cast<std::uint8_t>((std::min)(depth, 255));
        n.root = root;
        queue.push_back(n);
    }

    inline void world::record_hit(handle_kind kind, std::uintptr_t obj,
                                  const std::vector<node>& nodes, std::size_t index)
    {
        switch (kind)
        {
        case handle_kind::world: world_model = obj; break;
        case handle_kind::battle: battle_entities = obj; break;
        case handle_kind::battle_model: battle_model = obj; break;
        case handle_kind::updater: battle_updater = obj; break;
        default: break;
        }

        const klass_info* ki = info_of_klass(nodes[index].klass);
        diag.found = ki ? ki->full : "?";
        const std::uint16_t root = nodes[index].root;
        diag.root = root < root_labels.size() ? root_labels[root] : "?";

        diag.chain.clear();
        std::int32_t cur = static_cast<std::int32_t>(index);
        while (cur >= 0 && diag.chain.size() < 24)
        {
            const klass_info* ci = info_of_klass(nodes[static_cast<std::size_t>(cur)].klass);
            diag.chain.push_back(ci ? ci->full : "?");
            cur = nodes[static_cast<std::size_t>(cur)].parent;
        }
        std::reverse(diag.chain.begin(), diag.chain.end());
    }

    inline void world::collect_roots(std::vector<node>& out)
    {
        root_labels.clear();
        diag.roots_total = 0;
        diag.roots_valid = 0;
        auto push = [&](std::uintptr_t obj, const char* label)
        {
            ++diag.roots_total;
            if (!obj_ptr(obj)) return;
            node n;
            n.obj = obj;
            n.klass = read_ptr(obj);
            if (!obj_ptr(n.klass)) return;
            const std::uint16_t idx = static_cast<std::uint16_t>(root_labels.size());
            root_labels.push_back(label);
            n.root = idx;
            out.push_back(n);
            ++diag.roots_valid;
            if (cfg.verbose && !diag.roots_logged)
            {
                const klass_info* ki = info_of_klass(n.klass);
                ROBRUN_LOG("[robrun] root " << label << " = 0x" << std::hex << obj
                        << std::dec << " class=" << (ki ? ki->full : "?"));
            }
        };

        // Managed singletons
        const std::uintptr_t sound_bus =
            static_value(rva::SoundEventBus_TypeInfo, "SoundEventBus", statics::SoundEventBus::Instance);
        push(sound_bus, "SoundEventBus");
        push(static_value(rva::ContentContext_TypeInfo, "ContentContext", statics::ContentContext::Instance), "ContentContext");
        const std::uintptr_t cinemachine =
            static_value(rva::CinemachineCore_TypeInfo, "CinemachineCore", statics::CinemachineCore::SInstance);
        push(cinemachine, "CinemachineCore");
        push(static_value(rva::PlayerInputManager_TypeInfo, "PlayerInputManager", statics::PlayerInputManager::Instance), "PlayerInputManager");
        const std::uintptr_t dbg_updater =
            static_value(rva::GlobalDebugUpdater_TypeInfo, "GlobalDebugUpdater", statics::GlobalDebugUpdater::Instance);
        push(dbg_updater, "GlobalDebugUpdater");
        if (dbg_updater)
            push(read_ptr(dbg_updater + off::GlobalDebugUpdater::MainUpdaters), "GlobalDebugUpdater.mainUpdaters");
        push(static_value(rva::Debug_TypeInfo, "Debug", statics::Debug_::Logger), "Debug.logger");
        push(static_value(rva::Debug_TypeInfo, "Debug", statics::Debug_::DefaultLogger), "Debug.defaultLogger");

        // Generic containers held by those singletons. The generated graph only
        // records fields whose declared type can reach a seed, so these
        // Dictionary<Type, List<Delegate>> / List<CinemachineBrain> fields would
        // otherwise never be followed. Expanding the delegate targets is the
        // normal way a walk gets from an engine singleton into game code.
        if (sound_bus)
            push(read_ptr(sound_bus + 0x10), "SoundEventBus.subscribers");
        if (cinemachine)
        {
            push(read_ptr(cinemachine + 0x10), "CinemachineCore.brains");
            push(read_ptr(cinemachine + 0x18), "CinemachineCore.cameras");
            push(read_ptr(cinemachine + 0x28), "CinemachineCore.allCameras");
        }

        // Engine delegates: their m_target is game code (scene loaders, etc.)
        const std::uintptr_t app_klass = klass_from_rva(rva::Application_TypeInfo, "Application");
        if (app_klass)
        {
            const std::uintptr_t sf = static_fields_of(app_klass);
            if (sf)
                for (std::uintptr_t slot : { statics::Application::LowMemory, statics::Application::MemoryUsageChanged,
                                             statics::Application::LogCallback, statics::Application::LogCallbackThreaded,
                                             statics::Application::FocusChanged, statics::Application::DeepLinkActivated,
                                             statics::Application::WantsToQuit, statics::Application::Quitting,
                                             statics::Application::Unloading })
                    push(read_ptr(sf + slot), "Application.delegate");
        }

        const std::uintptr_t scene_klass = klass_from_rva(rva::SceneManager_TypeInfo, "SceneManager");
        if (scene_klass)
        {
            const std::uintptr_t sf = static_fields_of(scene_klass);
            if (sf)
                for (std::uintptr_t slot : { statics::SceneManager::SceneLoaded,
                                             statics::SceneManager::SceneUnloaded,
                                             statics::SceneManager::ActiveSceneChanged })
                    push(read_ptr(sf + slot), "SceneManager.delegate");
        }

        // Debug gizmos: SortedSet<ValueTuple<long,long,Action>>
        const std::uintptr_t dbg_klass = klass_from_rva(rva::DebugUtils_TypeInfo, "DebugUtils");
        if (dbg_klass)
        {
            const std::uintptr_t sf = static_fields_of(dbg_klass);
            if (sf)
            {
                push(read_ptr(sf + statics::DebugUtils::Gizmos), "DebugUtils.gizmos");
                push(read_ptr(sf + statics::DebugUtils::Now), "DebugUtils.now");
            }
        }

        // Cached heap-scan result: the Game object lives for the whole process,
        // so once found we can start every walk from it. This keeps the
        // (expensive) heap scan to a single run instead of repeating it while
        // waiting for a match to start.
        if (game_instance && obj_ptr(read_ptr(game_instance)))
        {
            push(game_instance, "Game.cached");
            push(read_ptr(game_instance + off::Game::Updaters), "Game.updaters");
            push(read_ptr(game_instance + off::Game::StartGameProcessor), "Game.startGameProcessor");
        }

        if (cfg.verbose && !diag.roots_logged)
        {
            diag.roots_logged = true;
            ROBRUN_LOG("[robrun] roots live: " << diag.roots_valid << "/" << diag.roots_total);
        }
    }

    // Last-resort instance discovery.
    //
    // Every reference to the per-match models lives inside other per-match
    // objects, so when the typed walk from statics cannot reach them (they are
    // created in a scene we have no static handle into) the only reliable way
    // is to find the object itself. A managed object always begins with its
    // Il2CppClass*, so searching the private (managed heap) VAD ranges for the
    // known class pointers returns the live instances directly. Only runs after
    // the graph walk failed and at most once every cfg.heap_scan_interval_ms.
    inline bool world::scan_heap(std::vector<node>& queue)
    {
        if (!cfg.heap_scan || !mem.vHandle || !ga_base() || queue.empty()) return false;

        // The objects already visited by the walk are managed-heap objects, so
        // their address span brackets the heap. Search that span plus a margin
        // instead of the entire 128 TB user address space.
        std::uintptr_t lo = ~static_cast<std::uintptr_t>(0);
        std::uintptr_t hi = 0;
        for (const node& n : queue)
        {
            if (!n.obj) continue;
            if (n.obj < lo) lo = n.obj;
            if (n.obj > hi) hi = n.obj;
        }
        if (!hi || lo >= hi) return false;

        const std::uintptr_t margin = static_cast<std::uintptr_t>(cfg.heap_scan_margin);
        lo = lo > margin ? lo - margin : 0x10000;
        hi = hi < ~static_cast<std::uintptr_t>(0) - margin
                 ? hi + margin
                 : ~static_cast<std::uintptr_t>(0) - 0x1000;

        std::vector<std::uint64_t> patterns;
        for (std::uintptr_t k : { klass_world, klass_battle, klass_bmodel, klass_updater, klass_game, klass_character, klass_player })
            if (k) patterns.push_back(static_cast<std::uint64_t>(k));
        if (patterns.empty()) return false;

        std::vector<std::uint64_t> hits;
        std::uint64_t bytes_read = 0;
        ++diag.heap_scans;
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = mem.MemSearch(patterns, lo, hi, hits,
                                      cfg.heap_scan_max_results ? cfg.heap_scan_max_results : 256,
                                      &bytes_read, /*force_pte=*/true);
        // Present-page (PTE) walking reads only committed memory, which is
        // much faster than walking the large reserved VAD regions. If it finds
        // nothing, retry with the VAD walk (some drivers map pages in a way
        // the PTE map does not cover).
        if (ok && hits.empty())
        {
            std::uint64_t vad_bytes = 0;
            const bool vad_ok = mem.MemSearch(patterns, lo, hi, hits,
                                              cfg.heap_scan_max_results ? cfg.heap_scan_max_results : 256,
                                              &vad_bytes, /*force_pte=*/false);
            bytes_read += vad_bytes;
            ROBRUN_LOG("[robrun] heap scan vad retry " << (vad_ok ? "done" : "failed")
                    << " hits=" << hits.size() << " read=" << (vad_bytes / (1024ull * 1024ull)) << "MB");
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        ROBRUN_LOG("[robrun] heap scan " << (ok ? "done" : "failed")
                << " range=0x" << std::hex << lo << "-0x" << hi << std::dec
                << " patterns=" << patterns.size() << " hits=" << hits.size()
                << " read=" << (bytes_read / (1024ull * 1024ull)) << "MB"
                << " in " << ms << " ms");

        // A hit is only "the klass pointer appears at this address" - static
        // fields, TypeInfo arrays and dictionaries also contain class pointers,
        // so every candidate is validated by the object's own fields before it
        // is trusted.
        std::vector<std::uintptr_t> c_games, c_worlds, c_battles, c_bmodels, c_updaters, c_characters, c_players;
        for (std::uint64_t h : hits)
        {
            const std::uintptr_t p = static_cast<std::uintptr_t>(h);
            if (!obj_ptr(p)) continue;
            const std::uintptr_t k = read_ptr(p);
            if (k == klass_game) c_games.push_back(p);
            else if (k == klass_world) c_worlds.push_back(p);
            else if (k == klass_battle) c_battles.push_back(p);
            else if (k == klass_bmodel) c_bmodels.push_back(p);
            else if (k == klass_updater) c_updaters.push_back(p);
            else if (k == klass_character) c_characters.push_back(p);
            else if (k == klass_player) c_players.push_back(p);
        }

        auto klass_is = [&](std::uintptr_t obj, std::uintptr_t klass) -> bool
        {
            return obj_ptr(obj) && klass && read_ptr(obj) == klass;
        };
        auto is_dict = [&](std::uintptr_t obj) -> bool
        {
            if (!obj_ptr(obj)) return false;
            const klass_info* ki = info_of(obj);
            return ki && ki->name.compare(0, 10, "Dictionary") == 0;
        };
        // Dictionary<TKey,TValue>._count is at 0x20; a value outside this range
        // means the pointer was not a dictionary at all.
        auto dict_count = [&](std::uintptr_t d) -> std::int32_t
        {
            if (!is_dict(d)) return -1;
            const std::int32_t c = read_t<std::int32_t>(d + 0x20);
            return (c >= 0 && c < 1000000) ? c : -1;
        };

        auto is_world = [&](std::uintptr_t w) -> bool
        {
            if (read_ptr(w) != klass_world) return false;
            return is_dict(read_ptr(w + off::WorldEntitiesModel::BattleCharacters))
                && is_dict(read_ptr(w + off::WorldEntitiesModel::NotSpawnedBattleCharacters));
        };
        auto is_battle = [&](std::uintptr_t b) -> bool
        {
            if (read_ptr(b) != klass_battle) return false;
            return is_dict(read_ptr(b + off::BattleEntitiesModel::Models))
                && klass_is(read_ptr(b + off::BattleEntitiesModel::WorldEntitiesModel), klass_world);
        };
        auto is_bmodel = [&](std::uintptr_t m) -> bool
        {
            if (read_ptr(m) != klass_bmodel) return false;
            return klass_is(read_ptr(m + off::BattleModel::BattleEntitiesModel), klass_battle)
                && klass_is(read_ptr(m + off::BattleModel::WorldEntitiesModel), klass_world);
        };
        auto is_updater = [&](std::uintptr_t u) -> bool
        {
            if (read_ptr(u) != klass_updater) return false;
            return klass_is(read_ptr(u + off::BattleEntitiesUpdater::BattleEntitiesModel), klass_battle)
                && klass_is(read_ptr(u + off::BattleEntitiesUpdater::BattleModel), klass_bmodel);
        };

        auto pick = [&](const std::vector<std::uintptr_t>& v, auto&& ok) -> std::uintptr_t
        {
            for (std::uintptr_t p : v) if (ok(p)) return p;
            return 0;
        };

        // A real Game has a live List<IUpdater> at 0x20.
        auto is_game = [&](std::uintptr_t p) -> bool
        {
            if (read_ptr(p) != klass_game) return false;
            const std::uintptr_t list = read_ptr(p + off::Game::Updaters);
            if (!obj_ptr(list)) return false;
            const std::uintptr_t items = read_ptr(list + 0x10);   // List<T>._items
            const std::int32_t size = read_t<std::int32_t>(list + 0x18); // List<T>._size
            if (!obj_ptr(items) || size <= 0 || size > 4096) return false;
            const std::uintptr_t first = read_ptr(items);
            return obj_ptr(first) && obj_ptr(read_ptr(first));
        };

        const std::uintptr_t game = pick(c_games, is_game);

        // Prefer the updater that is actually in a battle (valid player id),
        // then derive everything from its own live pointers.
        std::uintptr_t updater = 0;
        for (std::uintptr_t u : c_updaters)
        {
            if (!is_updater(u)) continue;
            if (!updater) updater = u;
            if (read_t<std::int32_t>(u + off::BattleEntitiesUpdater::PlayerEntityId) > 0)
            {
                updater = u;
                break;
            }
        }

        std::uintptr_t battle = updater
            ? read_ptr(updater + off::BattleEntitiesUpdater::BattleEntitiesModel) : 0;
        if (!is_battle(battle)) battle = pick(c_battles, is_battle);

        std::uintptr_t bmodel = updater
            ? read_ptr(updater + off::BattleEntitiesUpdater::BattleModel) : 0;
        if (!is_bmodel(bmodel)) bmodel = pick(c_bmodels, is_bmodel);

        std::uintptr_t w_up = 0;
        if (battle) w_up = read_ptr(battle + off::BattleEntitiesModel::WorldEntitiesModel);
        if (!is_world(w_up) && bmodel) w_up = read_ptr(bmodel + off::BattleModel::WorldEntitiesModel);

        auto chars_in = [&](std::uintptr_t w) -> std::int32_t
        {
            return w ? dict_count(read_ptr(w + off::WorldEntitiesModel::BattleCharacters)) : -1;
        };

        std::uintptr_t world = 0;
        // Prefer a world whose battle-character dictionary actually has live
        // entries; the updater's own world wins when it is populated.
        if (is_world(w_up) && chars_in(w_up) > 0) world = w_up;
        if (!world)
            for (std::uintptr_t w : c_worlds)
                if (is_world(w) && chars_in(w) > 0) { world = w; break; }
        if (!world && is_world(w_up)) world = w_up;
        if (!world) world = pick(c_worlds, is_world);

        // Fall back to the first (unvalidated) hit if the structure checks all
        // failed, so a layout change degrades instead of losing the handles.
        if (!world && !c_worlds.empty()) world = c_worlds.front();
        if (!battle && !c_battles.empty()) battle = c_battles.front();
        if (!bmodel && !c_bmodels.empty()) bmodel = c_bmodels.front();
        if (!updater && !c_updaters.empty()) updater = c_updaters.front();

        const std::int32_t player_id = updater
            ? read_t<std::int32_t>(updater + off::BattleEntitiesUpdater::PlayerEntityId) : -1;
        const std::int32_t world_chars = world
            ? dict_count(read_ptr(world + off::WorldEntitiesModel::BattleCharacters)) : -1;
        const std::int32_t battle_models = battle
            ? dict_count(read_ptr(battle + off::BattleEntitiesModel::Models)) : -1;

        ROBRUN_LOG("[robrun] scan candidates game=" << c_games.size()
                << " world=" << c_worlds.size()
                << " battle=" << c_battles.size()
                << " bmodel=" << c_bmodels.size()
                << " updater=" << c_updaters.size()
                << " characters=" << c_characters.size()
                << " players=" << c_players.size()
                << " valid=" << (game ? 1 : 0) << "/" << (world ? 1 : 0) << "/"
                << (battle ? 1 : 0) << "/" << (bmodel ? 1 : 0) << "/" << (updater ? 1 : 0)
                << " pid=" << player_id
                << " worldChars=" << world_chars
                << " battleModels=" << battle_models);

        // Per-candidate field dump: shows whether the hit is a real object and
        // whether its referenced models/dictionaries are live.
        auto kname = [&](std::uintptr_t obj) -> const char*
        {
            const klass_info* ki = info_of(obj);
            return ki ? ki->name.c_str() : "?";
        };
        for (std::size_t i = 0; i < c_worlds.size() && i < 5; ++i)
        {
            const std::uintptr_t w = c_worlds[i];
            const std::uintptr_t ns = read_ptr(w + off::WorldEntitiesModel::NotSpawnedBattleCharacters);
            const std::uintptr_t bc = read_ptr(w + off::WorldEntitiesModel::BattleCharacters);
            ROBRUN_LOG("[robrun] cand world=0x" << std::hex << w << std::dec
                    << " ns=0x" << std::hex << ns << std::dec << "(" << dict_count(ns) << "," << kname(ns) << ")"
                    << " bc=0x" << std::hex << bc << std::dec << "(" << dict_count(bc) << "," << kname(bc) << ")");
        }
        for (std::size_t i = 0; i < c_battles.size() && i < 5; ++i)
        {
            const std::uintptr_t b = c_battles[i];
            const std::uintptr_t w = read_ptr(b + off::BattleEntitiesModel::WorldEntitiesModel);
            const std::uintptr_t m = read_ptr(b + off::BattleEntitiesModel::Models);
            const std::uintptr_t p = read_ptr(b + off::BattleEntitiesModel::Player);
            ROBRUN_LOG("[robrun] cand battle=0x" << std::hex << b << std::dec
                    << " models=0x" << std::hex << m << std::dec << "(" << dict_count(m) << "," << kname(m) << ")"
                    << " world=0x" << std::hex << w << std::dec << "(" << kname(w) << ")"
                    << " player=0x" << std::hex << p << std::dec << "(" << kname(p) << ")");
        }
        for (std::size_t i = 0; i < c_bmodels.size() && i < 5; ++i)
        {
            const std::uintptr_t m = c_bmodels[i];
            ROBRUN_LOG("[robrun] cand bmodel=0x" << std::hex << m << std::dec
                    << " battle=0x" << std::hex << read_ptr(m + off::BattleModel::BattleEntitiesModel) << std::dec
                    << " world=0x" << std::hex << read_ptr(m + off::BattleModel::WorldEntitiesModel) << std::dec);
        }
        for (std::size_t i = 0; i < c_updaters.size() && i < 5; ++i)
        {
            const std::uintptr_t u = c_updaters[i];
            ROBRUN_LOG("[robrun] cand updater=0x" << std::hex << u << std::dec
                    << " battle=0x" << std::hex << read_ptr(u + off::BattleEntitiesUpdater::BattleEntitiesModel) << std::dec
                    << " bmodel=0x" << std::hex << read_ptr(u + off::BattleEntitiesUpdater::BattleModel) << std::dec
                    << " pid=" << read_t<std::int32_t>(u + off::BattleEntitiesUpdater::PlayerEntityId));
        }

        if (game || world || battle || bmodel || updater)
        {
            ROBRUN_LOG("[robrun] heap scan anchors game=0x" << std::hex << game
                    << " world=0x" << world
                    << " battle=0x" << battle
                    << " bmodel=0x" << bmodel
                    << " updater=0x" << updater << std::dec);
        }
        if (game && !game_instance)
        {
            game_instance = game;
            ROBRUN_LOG("[robrun] cached Game root = 0x" << std::hex << game << std::dec);
        }

        // Seed the handles directly from the validated anchors. The updater is
        // the most complete source because it points at the live battle
        // entities model (0x18) and battle model (0x70); link_handles() derives
        // the rest and fills in the local player id.
        if (updater) battle_updater = updater;
        if (battle) battle_entities = battle;
        if (bmodel) battle_model = bmodel;
        if (world) world_model = world;
        link_handles();

        auto add = [&](std::uintptr_t obj, const char* label) -> bool
        {
            if (!obj_ptr(obj)) return false;
            const std::uintptr_t k = read_ptr(obj);
            if (!obj_ptr(k)) return false;
            node n;
            n.obj = obj;
            n.klass = k;
            n.parent = -1;
            n.depth = 0;
            n.root = static_cast<std::uint16_t>(root_labels.size());
            root_labels.push_back(label);
            queue.push_back(n);
            return true;
        };

        bool appended = false;
        appended |= add(game, "heap.game");
        if (game)
        {
            // Push the two Game fields that lead to every battle object so the
            // typed walk can still replace the seeded handles with the ones
            // reached live from Game.
            appended |= add(read_ptr(game + off::Game::Updaters), "heap.gameUpdaters");
            appended |= add(read_ptr(game + off::Game::StartGameProcessor), "heap.gameStartProcessor");
        }
        return appended || ready();
    }

    inline bool world::search(std::vector<node>& queue)
    {
        std::unordered_set<std::uintptr_t> seen;
        seen.reserve(queue.size() * 4);
        // Roots were pushed without going through enqueue(); register them so
        // the graph walk does not re-visit them later.
        for (const node& n : queue) seen.insert(n.obj);

        std::vector<std::uintptr_t> array_buf;
        std::uint32_t budget = cfg.node_budget ? cfg.node_budget : 20000;
        const auto search_start = std::chrono::steady_clock::now();
        const auto time_budget_ms = 8000.0;

        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            if (queue.size() > budget) { diag.truncated = true; break; }

            // Never let one search monopolise the worker: the ESP needs fresh
            // snapshots, so a slow walk is cut off and reported instead.
            if ((head & 0x3FF) == 0 && head != 0)
            {
                const double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - search_start).count();
                if (ms > time_budget_ms)
                {
                    diag.truncated = true;
                    ROBRUN_LOG("[robrun] search stopped at time budget after "
                            << static_cast<unsigned long long>(head) << " nodes, "
                            << static_cast<unsigned long long>(diag.interesting.size())
                            << " game classes seen");
                    break;
                }
            }

            const node cur = queue[head];
            const klass_info* ki = info_of_klass(cur.klass);
            if (!ki) continue;

            // Record which gameplay classes the walk actually reaches. When a
            // resolve fails this list is logged, so "BFS never even got close"
            // is distinguishable from "found things but no handle".
            if (diag.interesting.size() < 32 && is_interesting_class(ki->full))
            {
                if (std::find(diag.interesting.begin(), diag.interesting.end(), ki->full)
                    == diag.interesting.end())
                    diag.interesting.push_back(ki->full);
            }

            // ---- direct hit? -------------------------------------------
            handle_kind hit = handle_kind::none;
            if (ki->klass == klass_world || ki->name == "WorldEntitiesModel") hit = handle_kind::world;
            else if (ki->klass == klass_battle || ki->name == "BattleEntitiesModel") hit = handle_kind::battle;
            else if (ki->klass == klass_updater || ki->name == "BattleEntitiesUpdater") hit = handle_kind::updater;
            else if (ki->klass == klass_bmodel || ki->name == "BattleModel") hit = handle_kind::battle_model;

            if (hit != handle_kind::none)
            {
                record_hit(hit, cur.obj, queue, head);
                link_handles();
                // A world handle alone is enough; anything else also gives us
                // the world after one link pass.
                if (world_model) return true;
                continue;
            }

            if (cur.depth >= cfg.max_depth) continue;

            const std::int32_t parent = static_cast<std::int32_t>(head);

            // ---- containers --------------------------------------------
            if (ki->is_array)
            {
                const std::int32_t len = read_t<std::int32_t>(cur.obj + il2cpp::Array_Length);
                if (len > 0)
                {
                    const int n = (std::min)(len, 1024);
                    array_buf.resize(static_cast<std::size_t>(n));
                    if (read_mem(cur.obj + il2cpp::Array_Data, array_buf.data(),
                                 array_buf.size() * sizeof(std::uintptr_t)))
                        for (std::uintptr_t v : array_buf)
                            enqueue(queue, seen, v, 0, parent, cur.depth + 1, cur.root);
                }
                continue;
            }

            if (ki->name.compare(0, 4, "List") == 0)
            {
                for_each_list(cur.obj, [&](std::uintptr_t v)
                {
                    enqueue(queue, seen, v, 0, parent, cur.depth + 1, cur.root);
                });
                continue;
            }

            if (ki->name.compare(0, 10, "Dictionary") == 0)
            {
                for_each_dict(cur.obj, [&](int, std::uintptr_t v)
                {
                    enqueue(queue, seen, v, 0, parent, cur.depth + 1, cur.root);
                });
                continue;
            }

            if (ki->is_delegate)
            {
                enqueue(queue, seen, read_ptr(cur.obj + il2cpp::Delegate_Target), 0, parent, cur.depth + 1, cur.root);
                const std::uintptr_t list = read_ptr(cur.obj + il2cpp::MulticastDelegate_List);
                if (list)
                    for_each_list(list, [&](std::uintptr_t d)
                    {
                        enqueue(queue, seen, read_ptr(d + il2cpp::Delegate_Target), 0, parent, cur.depth + 1, cur.root);
                    });
                continue;
            }

            // ---- typed edges from the generated holder graph ------------
            for (std::uint16_t i = 0; i < ki->holder_count; ++i)
            {
                const std::uintptr_t v = read_ptr(cur.obj + ki->holder_offsets[i]);
                enqueue(queue, seen, v, 0, parent, cur.depth + 1, cur.root);
            }

            // ---- fallback: display classes and unknown updaters ---------
            if (cfg.aggressive_scan && ki->holder_count == 0
                && (ki->name.find("DisplayClass") != std::string::npos
                    || ki->name.compare(ki->name.size() > 7 ? ki->name.size() - 7 : 0, 7, "Updater") == 0))
            {
                // Skip the 0x10 byte object header (klass, monitor) - those are
                // not instance references.
                const std::uint32_t size = (std::min)(ki->instance_size, 0x200u);
                if (size > 0x10)
                {
                    const std::uint32_t n = size - 0x10;
                    std::vector<std::uintptr_t> words(n / sizeof(std::uintptr_t));
                    if (words.size() && read_mem(cur.obj + 0x10, words.data(),
                                                 words.size() * sizeof(std::uintptr_t)))
                        for (std::uintptr_t v : words)
                            enqueue(queue, seen, v, 0, parent, cur.depth + 1, cur.root);
                }
            }
        }
        return false;
    }

    inline bool world::resolve_impl()
    {
        const auto t0 = std::chrono::steady_clock::now();

        // A forced resolve must be allowed to scan the heap again even when a
        // Game root is already cached from an earlier (possibly pre-match)
        // scan.
        const bool want_scan = force_scan;
        force_scan = false;

        // Fast path: previously found handles are still live.
        if (!cfg.manual_world && !cfg.manual_battle && !cfg.manual_battle_model && !cfg.manual_updater)
        {
            if (validate_handles())
            {
                link_handles();
                diag.resolved = ready();
                return diag.resolved;
            }
        }

        world_model = battle_entities = battle_model = battle_updater = 0;
        local_model = 0;
        local_id = -1;
        battle_models_cache.clear();
        simulated_models.clear();
        battle_entry_cache.clear();
        battle_cache_dict = 0;
        battle_cache_count = -1;
        battle_cache_tick = 0;
        models_cache.clear();
        models_tick = 0;
        models_world = 0;
        models_battle = 0;

        klass_world = klass_from_rva(rva::WorldEntitiesModel_TypeInfo, "WorldEntitiesModel");
        klass_battle = klass_from_rva(rva::BattleEntitiesModel_TypeInfo, "BattleEntitiesModel");
        klass_bmodel = klass_from_rva(rva::BattleModel_TypeInfo, "BattleModel");
        klass_updater = klass_from_rva(rva::BattleEntitiesUpdater_TypeInfo, "BattleEntitiesUpdater");
        klass_character = klass_from_rva(rva::BattleCharacterModel_TypeInfo, "BattleCharacterModel");
        klass_player = klass_from_rva(rva::PlayerEntityModel_TypeInfo, "PlayerEntityModel");
        klass_multicast = multicast_delegate_klass();
        klass_game = klass_from_rva(rva::Game_Game_TypeInfo, "Game");

        if (cfg.verbose && !diag.goals_logged)
        {
            diag.goals_logged = true;
            ROBRUN_LOG("[robrun] typeinfo world=0x" << std::hex << klass_world
                    << " battle=0x" << klass_battle
                    << " bmodel=0x" << klass_bmodel
                    << " updater=0x" << klass_updater
                    << " character=0x" << klass_character
                    << " player=0x" << klass_player
                    << " multicast=0x" << klass_multicast
                    << " game=0x" << klass_game << std::dec);
        }

        if (cfg.manual_world) world_model = cfg.manual_world;
        if (cfg.manual_battle) battle_entities = cfg.manual_battle;
        if (cfg.manual_battle_model) battle_model = cfg.manual_battle_model;
        if (cfg.manual_updater) battle_updater = cfg.manual_updater;
        if (ready())
        {
            link_handles();
            diag.resolved = ready();
            diag.found = "manual";
            return diag.resolved;
        }

        std::vector<node> queue;
        collect_roots(queue);
        diag.nodes = static_cast<std::uint32_t>(queue.size());
        diag.searching = true;

        const bool ok = [&]() -> bool
        {
            bool found = search(queue);
            if (found || ready()) return found;

            // The typed walk from statics is cheap. If it failed, fall back to
            // locating the instances themselves on the managed heap, but never
            // more often than the configured interval (a scan reads a lot of
            // memory, so we do not want it in the 30 ms update loop).
            const ULONGLONG now = GetTickCount64();
            if (!cfg.heap_scan) return false;
            // Once the Game object is known it is a permanent root, and the
            // typed walk from it covers every updater/model for the rest of the
            // process lifetime. No need to keep scanning the heap - unless a
            // forced re-scan was requested because no entities were found.
            if (game_instance && !want_scan) return false;
            if (!want_scan && last_scan_tick && (now - last_scan_tick) < cfg.heap_scan_interval_ms)
                return false;

            const bool scanned = scan_heap(queue);
            // Start the cooldown after the scan completes so a slow failing
            // scan cannot immediately trigger another one.
            last_scan_tick = GetTickCount64();
            if (!scanned) return false;

            // scan_heap seeds the handles directly from the anchors, so the
            // (potentially expensive) typed walk is not needed when that
            // already produced a usable set.
            if (ready()) return true;

            // Re-run the walk with the discovered instances as extra roots so
            // the normal typed edges (Game -> List<IUpdater> -> BattleUpdater)
            // still do the linking and produce the path the log shows.
            return search(queue);
        }();
        diag.searching = false;
        link_handles();

        if (!ok && !ready())
        {
            world_model = battle_entities = battle_model = battle_updater = 0;
            battle_models_cache.clear();
            simulated_models.clear();
            battle_cache_dict = 0;
            battle_cache_count = -1;
            battle_cache_tick = 0;
            models_cache.clear();
            models_tick = 0;
            models_world = 0;
            models_battle = 0;
            diag.resolved = false;
            diag.found.clear();
            diag.fails++;
            diag.nodes = static_cast<std::uint32_t>(queue.size());
            diag.klasses = static_cast<std::uint32_t>(klass_cache().size());
            last_fail_tick = GetTickCount64();

            if (cfg.verbose)
            {
                const auto t1 = std::chrono::steady_clock::now();
                const double failed_ms =
                    std::chrono::duration<double, std::milli>(t1 - t0).count();
                ROBRUN_LOG("[robrun] resolve FAILED after " << failed_ms << " ms"
                        << " nodes=" << queue.size()
                        << " classes=" << klass_cache().size()
                        << " roots=" << diag.roots_valid << "/" << diag.roots_total
                        << " truncated=" << (diag.truncated ? 1 : 0));
                if (!diag.interesting.empty())
                {
                    std::string list;
                    for (const auto& c : diag.interesting)
                    {
                        if (!list.empty()) list += ", ";
                        list += c;
                    }
                    ROBRUN_LOG("[robrun] reached: " << list);
                }
                else
                {
                    ROBRUN_LOG("[robrun] reached no game class (no live roots, "
                            "or not in a match yet)");
                }
            }
            return false;
        }

        diag.resolved = ready();
        diag.nodes = static_cast<std::uint32_t>(queue.size());
        diag.klasses = static_cast<std::uint32_t>(klass_cache().size());

        const auto t1 = std::chrono::steady_clock::now();
        diag.last_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (cfg.verbose)
        {
            ROBRUN_LOG("[robrun] world resolved in " << diag.last_ms << " ms"
                    << " nodes=" << diag.nodes
                    << " world=0x" << std::hex << world_model
                    << " battle=0x" << battle_entities
                    << " bmodel=0x" << battle_model
                    << " updater=0x" << battle_updater
                    << " local=0x" << local_model
                    << " id=" << std::dec << local_id);
            if (!diag.chain.empty())
            {
                std::string chain;
                for (const auto& c : diag.chain) { if (!chain.empty()) chain += " -> "; chain += c; }
                ROBRUN_LOG("[robrun] path: " << chain);
            }
        }
        return diag.resolved;
    }

    inline bool world::resolve(bool force)
    {
        if (force) { reset(); force_scan = true; validate_tick = 0; }
        if (ready() && !force)
        {
            const std::uint64_t now = GetTickCount64();
            if (validate_tick && now - validate_tick < 1000ull) return true;
            validate_tick = now;
            if (validate_handles()) return true;
        }

        // Do not burn a full BFS every frame while sitting in a menu.
        if (!ready() && last_fail_tick && (GetTickCount64() - last_fail_tick) < 3000)
            return false;

        return resolve_impl();
    }

    inline bool world::update()
    {
        const auto t0 = std::chrono::steady_clock::now();

        // Handle validation + link_handles() costs ~10 serial DMA reads. The
        // handles only change when a match starts/ends, so there is no reason
        // to re-derive them on every worker tick. Revalidate every 250 ms (or
        // whenever nothing is resolved yet).
        const std::uint64_t now_tick = GetTickCount64();
        if (!ready() || now_tick - last_resolve_tick >= 250ull)
        {
            last_resolve_tick = now_tick;
            if (!resolve(false))
            {
                resolve_ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                return false;
            }
        }
        const auto t_resolve = std::chrono::steady_clock::now();
        refresh_camera();
        const auto t_camera = std::chrono::steady_clock::now();
        refresh_entities();
        const auto t_entities = std::chrono::steady_clock::now();

        resolve_ms = std::chrono::duration<double, std::milli>(t_resolve - t0).count();
        camera_ms = std::chrono::duration<double, std::milli>(t_camera - t_resolve).count();
        entities_ms = std::chrono::duration<double, std::milli>(t_entities - t_camera).count();

        // If the handles were captured before the match finished loading (the
        // ESP process was started during a loading screen), the dictionaries
        // stay empty for the whole session because the cached handles are never
        // re-scanned. While every player dictionary is empty, retry the heap
        // scan with a backoff so entering a match recovers on its own.
        if (!entities.empty())
        {
            empty_rescans = 0;
            last_empty_rescan = 0;
        }
        else if (empty_rescans < 6)
        {
            auto raw_count = [](std::uintptr_t d) -> int
            {
                if (!obj_ptr(d)) return 0;
                const std::int32_t c = read_t<std::int32_t>(d + il2cpp::Dictionary_Count);
                return (c > 0 && c < 1000000) ? c : 0;
            };
            int populated = 0;
            if (world_model)
            {
                populated += raw_count(read_ptr(world_model + off::WorldEntitiesModel::BattleCharacters));
                populated += raw_count(read_ptr(world_model + off::WorldEntitiesModel::NotSpawnedBattleCharacters));
            }
            if (battle_entities)
                populated += raw_count(read_ptr(battle_entities + off::BattleEntitiesModel::Models));

            const std::uint64_t now = GetTickCount64();
            if (!last_empty_rescan) last_empty_rescan = now;
            else if (populated == 0 && now - last_empty_rescan >= 20000ull)
            {
                ++empty_rescans;
                ROBRUN_LOG("[robrun] no entities; re-scanning heap (attempt "
                        << empty_rescans << "/6)");
                resolve(true);
                refresh_camera();
                refresh_entities();
                last_empty_rescan = GetTickCount64();
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        update_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (update_ms > update_ms_peak) update_ms_peak = update_ms;
        update_ms_sum += update_ms;
        ++update_count;
        return true;
    }

    // ---------------------------------------------------------------------
    // Camera
    // ---------------------------------------------------------------------
    inline void world::refresh_camera_from_brain(std::uintptr_t core)
    {
        // The active-brain list only changes on scene load. Resolve it at 1 Hz;
        // the brain's camera state itself is read in the entity scatter batch
        // (decode_camera_state) so it costs no extra DMA transaction.
        const std::uint64_t now = GetTickCount64();
        if (!obj_ptr(cam_brain) || now - cam_list_tick >= 1000ull)
        {
            cam_brain = 0;
            const std::uintptr_t brains = read_ptr(core + cam::CoreObject::ActiveBrains);
            for_each_list(brains, [&](std::uintptr_t brain)
            {
                if (!cam_brain) cam_brain = brain;
            });
            cam_list_tick = now;
        }
    }

    inline void world::decode_camera_state(const std::uint8_t* buf)
    {
        float fov = 0.f;
        vec3 raw{}, corr{};
        quat raw_q{}, corr_q{};
        std::memcpy(&fov, buf + cam::State::Lens + cam::Lens::FieldOfView, 4);
        std::memcpy(&raw, buf + cam::State::RawPosition, 12);
        std::memcpy(&raw_q, buf + cam::State::RawOrientation, 16);
        std::memcpy(&corr, buf + cam::State::PositionCorrection, 12);
        std::memcpy(&corr_q, buf + cam::State::OrientationCorrection, 16);

        if (fov < 5.f || fov > 170.f) return;

        const vec3 pos = raw + corr;
        const quat rot = raw_q * corr_q;
        if (!pos.finite() || !rot.finite()) return;

        camera.pos = pos;
        camera.fov = fov;
        camera.fwd = rot.rotate({ 0.f, 0.f, 1.f });
        camera.up = rot.rotate({ 0.f, 1.f, 0.f });
        camera.right = rot.rotate({ 1.f, 0.f, 0.f });
        camera.valid = camera.fwd.length() > 0.5f;
        camera.from_brain = camera.valid;
    }

    inline void world::refresh_camera_from_battle()
    {
        if (!battle_model) return;
        const std::uintptr_t bcm = read_ptr(battle_model + off::BattleModel::BattleCameraModel);
        if (!obj_ptr(bcm)) return;

        vec3 pos{};
        quat rot{};
        if (!read_mem(bcm + off::BattleCameraModel::Position, &pos, 12)) return;
        if (!read_mem(bcm + off::BattleCameraModel::Orientation, &rot, 16)) return;
        if (!pos.finite() || !rot.finite()) return;

        camera.pos = pos;
        camera.fov = 60.f;
        camera.fwd = rot.rotate({ 0.f, 0.f, 1.f });
        camera.up = rot.rotate({ 0.f, 1.f, 0.f });
        camera.right = rot.rotate({ 1.f, 0.f, 0.f });
        camera.valid = camera.fwd.length() > 0.5f;
        camera.from_brain = false;
    }

    inline void world::refresh_local_follow(std::uintptr_t core)
    {
        // The local follow target is only used to identify the local entity.
        // Cache the AimingCameraFollow component and re-read just its target
        // at 1 Hz; the full camera/component walk is what used to cost dozens
        // of serial reads every tick.
        const std::uint64_t now = GetTickCount64();
        if (now - follow_tick < 1000ull) return;
        follow_tick = now;

        if (obj_ptr(follow_comp))
        {
            vec3 p{};
            if (read_mem(follow_comp + cam::AimingCameraFollow::PreviousFollowTarget, &p, 12)
                && p.finite() && !p.zero())
            {
                camera.local_follow = p;
                camera.local_follow_valid = true;
                return;
            }
            follow_comp = 0;
            camera.local_follow_valid = false;
        }

        const std::uintptr_t cams = read_ptr(core + cam::CoreObject::ActiveCameras);
        if (!cams) return;

        for_each_list(cams, [&](std::uintptr_t vcam)
        {
            if (follow_comp) return;
            const std::uintptr_t pipe = read_ptr(vcam + cam::VirtualCamera::ComponentPipeline);
            if (!obj_ptr(pipe)) return;

            // m_ComponentPipeline is a CinemachineComponentBase[] (IL2CPP array).
            const std::int32_t len = read_t<std::int32_t>(pipe + il2cpp::Array_Length);
            if (len <= 0 || len > 64) return;
            std::vector<std::uintptr_t> comps(static_cast<std::size_t>(len));
            if (!read_mem(pipe + il2cpp::Array_Data, comps.data(), comps.size() * sizeof(std::uintptr_t)))
                return;

            for (std::uintptr_t comp : comps)
            {
                if (follow_comp) break;
                if (!obj_ptr(comp)) continue;
                const klass_info* ci = info_of(comp);
                if (!ci || ci->name.find("AimingCameraFollow") == std::string::npos) continue;
                vec3 p{};
                if (read_mem(comp + cam::AimingCameraFollow::PreviousFollowTarget, &p, 12) && p.finite() && !p.zero())
                {
                    follow_comp = comp;
                    camera.local_follow = p;
                    camera.local_follow_valid = true;
                }
            }
        });
    }

    inline void world::refresh_camera()
    {
        screen_size.x = (std::max)(screen_size.x, 1.f);
        screen_size.y = (std::max)(screen_size.y, 1.f);
        camera.aspect = screen_size.x / screen_size.y;

        const std::uint64_t now = GetTickCount64();
        if (!obj_ptr(cam_core) || now - cam_core_tick >= 1000ull)
        {
            cam_core = static_value(rva::CinemachineCore_TypeInfo, "CinemachineCore",
                                    statics::CinemachineCore::SInstance);
            cam_core_tick = now;
        }

        camera.valid = false;
        camera.from_brain = false;
        cam_state_pending = false;
        if (cam_core)
        {
            refresh_camera_from_brain(cam_core);
            refresh_local_follow(cam_core);
            // Defer the state read to refresh_entities, which folds it into
            // the entity scatter round.
            cam_state_pending = obj_ptr(cam_brain) != 0;
        }

        if (!cam_state_pending)
        {
            // Keep the cached follow target; only the projection basis resets.
            const vec3 lf = camera.local_follow;
            const bool lfv = camera.local_follow_valid;
            camera = {};
            camera.aspect = screen_size.x / screen_size.y;
            camera.local_follow = lf;
            camera.local_follow_valid = lfv;
            refresh_camera_from_battle();
        }
    }

    // ---------------------------------------------------------------------
    // Entities
    // ---------------------------------------------------------------------
    inline bool world::read_position(std::uintptr_t model, vec3& out) const
    {
        ++pos_fallback_calls;
        // 1) replicated common data (authoritative server position)
        const std::uintptr_t data = read_ptr(model + off::BattleCharacterModel::Data);
        const std::uintptr_t common = data ? read_ptr(data + off::BattleCharacterData::CommonData) : 0;
        if (common)
        {
            vec3 v{};
            if (prim_vec3(read_ptr(common + off::BattleCharacterCommonData::Position), v)
                && v.finite() && !v.zero())
            {
                out = v;
                return true;
            }
        }

        // 2) center position model (position model holds plain floats)
        const std::uintptr_t center = read_ptr(model + off::BattleCharacterModel::CenterPosition);
        if (center)
        {
            std::uintptr_t pos_model = center;
            const klass_info* ci = info_of(center);
            if (ci && ci->name == "CharacterCenterPositionModel")
                pos_model = read_ptr(center + off::CharacterCenterPositionModel::PositionModel);
            if (obj_ptr(pos_model))
            {
                vec3 v{};
                if (read_mem(pos_model + off::PositionModel::X, &v.x, 4)
                    && read_mem(pos_model + off::PositionModel::Y, &v.y, 4)
                    && read_mem(pos_model + off::PositionModel::Z, &v.z, 4)
                    && v.finite() && !v.zero())
                {
                    out = v;
                    return true;
                }
            }
        }

        // 3) replicated transform
        const std::uintptr_t rep = read_ptr(model + off::BattleCharacterModel::ReplicatedTransform);
        if (rep)
        {
            vec3 v{};
            if (prim_vec3(read_ptr(rep + off::CharacterReplicatedTransformModel::Position), v)
                && v.finite() && !v.zero())
            {
                out = v;
                return true;
            }
        }

        // 4) transform model (three float primitives)
        const std::uintptr_t tm = read_ptr(model + off::BattleCharacterModel::Transform);
        if (tm)
        {
            float x = 0.f, y = 0.f, z = 0.f;
            if (prim_float(read_ptr(tm + off::TransformModel::X), x)
                && prim_float(read_ptr(tm + off::TransformModel::Y), y)
                && prim_float(read_ptr(tm + off::TransformModel::Z), z))
            {
                vec3 v{ x, y, z };
                if (v.finite() && !v.zero()) { out = v; return true; }
            }
        }
        return false;
    }

    inline std::string world::role_name(std::uintptr_t role) const
    {
        if (!obj_ptr(role)) return {};

        static std::unordered_map<std::uintptr_t, std::string> cache;

        struct role_slot { std::uintptr_t slot; const char* name; };
        static const role_slot slots[] = {
            { statics::PlayerRoleDescriptions::Settler, "Settler" },
            { statics::PlayerRoleDescriptions::Citizen, "Citizen" },
            { statics::PlayerRoleDescriptions::Robber, "Robber" },
            { statics::PlayerRoleDescriptions::Cop, "Cop" },
            { statics::PlayerRoleDescriptions::ClanBattleMember, "ClanBattle" },
            { statics::PlayerRoleDescriptions::Prisoner, "Prisoner" },
            { statics::PlayerRoleDescriptions::Hubber, "Hubber" },
        };

        // Build the pointer -> name table once. The role objects are the seven
        // static description instances; resolving them by identity avoids the
        // per-entity string read.
        static bool table_built = false;
        if (!table_built)
        {
            const std::uintptr_t klass =
                klass_from_rva(rva::PlayerRoleDescriptions_TypeInfo, "PlayerRoleDescriptions");
            const std::uintptr_t sf = klass ? static_fields_of(klass) : 0;
            if (sf)
            {
                scatter_batch sb;
                std::uintptr_t vals[7]{};
                for (int i = 0; i < 7; ++i) sb.add(sf + slots[i].slot, vals[i]);
                sb.run();
                for (int i = 0; i < 7; ++i)
                    if (obj_ptr(vals[i])) cache.emplace(vals[i], slots[i].name);
                table_built = true;
            }
        }

        const auto it = cache.find(role);
        if (it != cache.end()) return it->second;

        const std::string name = read_il2cpp_string(read_ptr(role + off::Identified::Id), 32);
        cache.emplace(role, name);
        return name;
    }

    inline std::string world::stance_name(std::uintptr_t stance) const
    {
        if (!obj_ptr(stance)) return {};

        static std::unordered_map<std::uintptr_t, std::string> cache;

        struct stance_slot { std::uintptr_t slot; const char* name; };
        static const stance_slot slots[] = {
            { statics::BattleStanceDescription::Disabled, "Disabled" },
            { statics::BattleStanceDescription::Weapon, "Weapon" },
            { statics::BattleStanceDescription::Boxing, "Boxing" },
        };

        static bool table_built = false;
        if (!table_built)
        {
            const std::uintptr_t klass =
                klass_from_rva(rva::BattleStanceDescription_TypeInfo, "BattleStanceDescription");
            const std::uintptr_t sf = klass ? static_fields_of(klass) : 0;
            if (sf)
            {
                scatter_batch sb;
                std::uintptr_t vals[3]{};
                for (int i = 0; i < 3; ++i) sb.add(sf + slots[i].slot, vals[i]);
                sb.run();
                for (int i = 0; i < 3; ++i)
                    if (obj_ptr(vals[i])) cache.emplace(vals[i], slots[i].name);
                table_built = true;
            }
        }

        const auto it = cache.find(stance);
        if (it != cache.end()) return it->second;

        const std::string name = read_il2cpp_string(read_ptr(stance + off::Identified::Id), 24);
        cache.emplace(stance, name);
        return name;
    }

    inline bool world::build_chain(std::uintptr_t model, ent_chain& out) const
    {
        out = ent_chain{};
        out.model = model;
        auto log_drop = [&](const char* why)
        {
            ++ent_chain_rejects;
            static ULONGLONG window = 0;
            static int shown = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - window >= 2000ull) { window = now; shown = 0; }
            if (shown++ >= 4) return;
            ROBRUN_LOG("[robrun] chain drop model=0x" << std::hex << model << std::dec
                    << " reason=" << why);
        };
        if (!obj_ptr(model))
        {
            log_drop("no-obj");
            return false;
        }

        const klass_info* ki = info_of(model);
        if (!ki)
        {
            log_drop("no-klass");
            return false;
        }
        if (ki->name.find("BattleCharacter") == std::string::npos
            && ki->name.find("PlayerEntity") == std::string::npos)
        {
            log_drop(ki->name.c_str());
            return false;
        }

        // ---- stage A: model -> direct fields -------------------------------
        std::uintptr_t data = 0, config = 0, replicated = 0, center = 0;
        std::uintptr_t priv = 0, pbm = 0, capsule = 0, stance = 0, ground = 0;
        std::uintptr_t skel = 0, tm = 0;
        std::int32_t id = 0, id2 = 0;
        {
            sc_.add(model + off::BattleCharacterModel::Id, id);
            sc_.add(model + off::EntityModel::Id, id2);
            sc_.add(model + off::BattleCharacterModel::Data, data);
            sc_.add(model + off::BattleCharacterModel::Config, config);
            sc_.add(model + off::BattleCharacterModel::ReplicatedTransform, replicated);
            sc_.add(model + off::BattleCharacterModel::CenterPosition, center);
            sc_.add(model + off::BattleCharacterModel::PrivateModel, priv);
            sc_.add(model + off::BattleCharacterModel::PlayerBattleModel, pbm);
            sc_.add(model + off::BattleCharacterModel::CapsuleColliderParams, capsule);
            sc_.add(model + off::BattleCharacterModel::BattleStanceModel, stance);
            sc_.add(model + off::BattleCharacterModel::GroundModel, ground);
            // The visual transform drives both the rotation of the skeleton and
            // is a fallback position source.
            sc_.add(model + off::BattleCharacterModel::Transform, tm);
            if (want_skeleton)
                sc_.add(model + off::BattleCharacterModel::SkeletonModel, skel);
            sc_.run();
        }
        out.transform = tm;

        out.id = id > 0 ? id : id2;

        // ---- stage B: one scatter round for every nested pointer/value -----
        std::uintptr_t common = 0, health = 0, pdata = 0, physcfg = 0;
        std::uintptr_t stance_data = 0;
        std::uintptr_t cap_r = 0, cap_l = 0, cap_g = 0;
        std::uintptr_t knockout = 0, armor_prim = 0;
        float cap_plain[3]{};
        float def_radius = 0.f, stand_len = 0.f, crouch_len = 0.f;
        std::int32_t max_hp = 0;
        std::uint8_t ground_found = 0;
        vec3 ground_pt{};
        const bool capsule_plain =
            capsule && info_of(capsule) && info_of(capsule)->name.find("NotReplicated") != std::string::npos;

        {
            if (data)     sc_.add(data + off::BattleCharacterData::CommonData, common);
            if (priv)     sc_.add(priv + off::BattleCharacterPrivateModel::Health, health);
            if (pbm)      sc_.add(pbm + off::PlayerBattleModel::Data, pdata);
            if (config)   sc_.add(config + off::BattleCharacterConfig::PhysicsCharacter, physcfg);
            if (stance)   sc_.add(stance + 0x18, stance_data);
            sc_.add(model + off::BattleCharacterModel::KnockoutModel, knockout);
            sc_.add(model + off::BattleCharacterModel::HasArmor, armor_prim);
            if (ground)
            {
                sc_.add(ground + off::CharacterControllerGroundModel::GroundFound, ground_found);
                sc_.add(ground + off::CharacterControllerGroundModel::GroundPoint, ground_pt);
            }
            if (capsule)
            {
                if (capsule_plain)
                {
                    sc_.add(capsule + off::CapsuleColliderParamsModel::Radius, cap_plain, sizeof(cap_plain));
                }
                else
                {
                    sc_.add(capsule + off::ReplicatedCapsuleColliderParamsModel::Radius, cap_r);
                    sc_.add(capsule + off::ReplicatedCapsuleColliderParamsModel::Length, cap_l);
                    sc_.add(capsule + off::ReplicatedCapsuleColliderParamsModel::DistanceToGround, cap_g);
                }
            }
            if (health)
            {
                sc_.add(health + off::HealthModel::MaxAmount, max_hp);
                sc_.add(health + off::HealthModel::Health, out.health_prim);
            }
            if (physcfg)
            {
                sc_.add(physcfg + off::PhysicsCharacterConfig::DefaultRadius, def_radius);
                sc_.add(physcfg + off::PhysicsCharacterConfig::StandLength, stand_len);
                sc_.add(physcfg + off::PhysicsCharacterConfig::CrouchLength, crouch_len);
            }
            sc_.run();
        }

        out.knockout_model = obj_ptr(knockout) ? knockout : 0;
        out.has_armor_prim = armor_prim;

        out.capsule_model = capsule;
        out.capsule = capsule_plain ? capsule : 0;
        out.capsule_plain = capsule_plain;
        if (capsule_plain)
        {
            out.cap_dist_to_ground = cap_plain[2];
        }
        else
        {
            out.cap_r_prim = cap_r;
            out.cap_l_prim = cap_l;
            out.cap_g_prim = cap_g;
        }
        out.ground_model = ground;
        out.def_radius = def_radius;
        out.stand_length = stand_len;
        out.crouch_length = crouch_len;
        out.health_model = health;
        out.max_health = max_hp;
        (void)ground_found;
        (void)ground_pt;

        // ---- position sources (pointer chase, two round trips max) ---------
        std::uintptr_t common_ptr = common;
        if (common_ptr)
        {
            sc_.add(common_ptr + off::BattleCharacterCommonData::Position, out.pos_prim);
            sc_.add(common_ptr + off::BattleCharacterCommonData::IsSprinting, out.sprint_prim);
            sc_.add(common_ptr + off::BattleCharacterCommonData::IsCrouching, out.crouch_prim);
            sc_.add(common_ptr + off::BattleCharacterCommonData::IsArmed, out.armed_prim);
            sc_.add(common_ptr + off::BattleCharacterCommonData::IsDead, out.dead_prim);
            std::uintptr_t role_prim = 0;
            sc_.add(common_ptr + off::BattleCharacterCommonData::Role, role_prim);
            sc_.add(common_ptr + off::BattleCharacterCommonData::ItemInHands, out.item_hands_prim);
            sc_.run();
            if (role_prim) out.role = role_name(prim_ref(role_prim));
            if (obj_ptr(out.item_hands_prim))
            {
                out.item_hands_desc = prim_ref(out.item_hands_prim);
                if (obj_ptr(out.item_hands_desc))
                    out.weapon_name = read_il2cpp_string(
                        read_ptr(out.item_hands_desc + off::Identified::Id), 48);
            }
        }
        auto role_label_of = [](const std::string& r) -> std::string
        {
            if (r == "Cop") return "Policeman";
            if (r == "Robber") return "Robber";
            if (r == "Prisoner") return "Prisoner";
            if (r == "Citizen" || r == "Settler" || r == "Hubber") return "Civilian";
            if (r == "ClanBattle") return "Clan Battle";
            if (r.empty()) return {};
            return r;
        };
        out.role_label = role_label_of(out.role);

        if (!obj_ptr(out.pos_prim) && replicated)
        {
            sc_.add(replicated + off::CharacterReplicatedTransformModel::Position, out.pos_prim);
            // Execute before the value-box stage below, which needs pos_prim
            // to be a real pointer. Without this run the box request was added
            // with prim == 0 and every refresh fell back to read_position()
            // (several serial DMA reads per entity, ~80 ms per tick).
            sc_.run();
        }
        if (!obj_ptr(out.pos_prim) && center)
        {
            std::uintptr_t pm = 0;
            sc_.add(center + off::CharacterCenterPositionModel::PositionModel, pm);
            sc_.run();
            if (obj_ptr(pm))
            {
                out.pos_model = pm;
                out.position_is_center = true;
            }
        }
        // Always resolve the visual transform, even when CommonData.Position
        // is present. CommonData is the replicated gameplay position and it
        // trails the mesh; the transform is the root the renderer uses.
        if (tm)
        {
            sc_.add(tm + off::TransformModel::X, out.tx_prim);
            sc_.add(tm + off::TransformModel::Y, out.ty_prim);
            sc_.add(tm + off::TransformModel::Z, out.tz_prim);
            sc_.run();
        }

        // ---- character rotation (skeleton orientation) ---------------------
        // Prefer the visual TransformModel (Pitch/Yaw/Roll in degrees); the
        // replicated transform is the network source used as a fallback.
        if (want_skeleton && tm)
        {
            sc_.add(tm + off::TransformModel::Rx, out.rx_prim);
            sc_.add(tm + off::TransformModel::Ry, out.ry_prim);
            sc_.add(tm + off::TransformModel::Rz, out.rz_prim);
            sc_.run();
        }
        if (want_skeleton && !obj_ptr(out.rx_prim) && replicated)
        {
            sc_.add(replicated + off::CharacterReplicatedTransformModel::Rx, out.rx_prim);
            sc_.add(replicated + off::CharacterReplicatedTransformModel::Ry, out.ry_prim);
            sc_.add(replicated + off::CharacterReplicatedTransformModel::Rz, out.rz_prim);
            sc_.run();
        }

        // ---- skeleton config + static hierarchy ----------------------------
        if (want_skeleton && obj_ptr(skel))
        {
            // SkeletonModel -> description + float[] pose.
            out.skeleton_model = skel;
            sc_.add(skel + off::SkeletonModel::SkeletonDescription, out.skeleton_config);
            sc_.add(skel + off::SkeletonModel::BonesData, out.bones_array);
            sc_.run();
        }

        // ---- resolve the primitive value addresses once ---------------------
        // IPrimitiveData<T> stores the value inline at +0x18. The refresh loop
        // reads at (box + 0x10), so represent the value address as box =
        // prim + 0x08. This is pure address arithmetic: the old implementation
        // did a dependent scatter round here AND returned a garbage pointer
        // (the value bytes interpreted as a pointer), which silently forced
        // every refresh through the slow serial read_position() fallback.
        {
            auto add_box = [&](std::uintptr_t prim, std::uintptr_t& box)
            {
                box = prim ? prim + (il2cpp::PrimitiveData_Value - 0x10) : 0;
            };
            add_box(out.pos_prim, out.pos_box);
            add_box(out.health_prim, out.health_box);
            add_box(out.sprint_prim, out.sprint_box);
            add_box(out.crouch_prim, out.crouch_box);
            add_box(out.armed_prim, out.armed_box);
            add_box(out.dead_prim, out.dead_box);
            add_box(out.has_armor_prim, out.has_armor_box);
            add_box(out.cap_r_prim, out.cap_r_box);
            add_box(out.cap_l_prim, out.cap_l_box);
            add_box(out.cap_g_prim, out.cap_g_box);
            add_box(out.tx_prim, out.tx_box);
            add_box(out.ty_prim, out.ty_box);
            add_box(out.tz_prim, out.tz_box);
            add_box(out.rx_prim, out.rx_box);
            add_box(out.ry_prim, out.ry_box);
            add_box(out.rz_prim, out.rz_box);
        }

        // ---- decode the skeleton (static config + current pose) ------------
        if (want_skeleton)
        {
            if (!obj_ptr(out.skeleton_model))
            {
                const std::uint64_t dt = GetTickCount64();
                if (cfg.verbose && dt - skel_diag_tick >= 10000ull &&
                    skel_diag_logged.insert(model).second)
                {
                    skel_diag_tick = dt;
                    const klass_info* mi = info_of(model);
                    ROBRUN_LOG("[robrun] skeleton: no SkeletonModel on 0x" << std::hex
                            << model << std::dec << " class="
                            << (mi ? mi->name : "?"));
                }
            }
            else if (!build_skeleton(out))
            {
                const std::uint64_t dt = GetTickCount64();
                if (cfg.verbose && dt - skel_diag_tick >= 10000ull &&
                    skel_diag_logged.insert(model).second)
                {
                    skel_diag_tick = dt;
                    // Raw probe: this is what tells us whether we are looking
                    // at the server copy (no hitbox/animation, empty skeleton)
                    // or at an object the client actually animates.
                    std::uintptr_t desc = 0, bones = 0, hitbox = 0;
                    std::uintptr_t anim = 0, animator = 0;
                    read_mem(out.skeleton_model + off::SkeletonModel::SkeletonDescription, &desc, sizeof(desc));
                    read_mem(out.skeleton_model + off::SkeletonModel::BonesData, &bones, sizeof(bones));
                    read_mem(model + off::BattleCharacterModel::HitBoxAccess, &hitbox, sizeof(hitbox));
                    read_mem(model + off::BattleCharacterModel::SkeletonAnimationModel, &anim, sizeof(anim));
                    read_mem(model + off::BattleCharacterModel::Animator, &animator, sizeof(animator));
                    const klass_info* mi = info_of(model);
                    const klass_info* si = info_of(out.skeleton_model);
                    ROBRUN_LOG("[robrun] skeleton: decode failed on 0x" << std::hex
                            << model << " class=" << (mi ? mi->name : "?")
                            << " sm=0x" << out.skeleton_model
                            << " smclass=" << (si ? si->name : "?")
                            << " cfg=0x" << out.skeleton_config
                            << " arr=0x" << out.bones_array
                            << " desc=0x" << desc << " bones=0x" << bones
                            << " hitbox=0x" << hitbox << " anim=0x" << anim
                            << " animator=0x" << animator << std::dec);
                }
            }
        }

        // Stance + display name (build-time only; cached by pointer for role).
        if (stance_data)
            out.stance = stance_name(prim_ref(stance_data));
        if (pdata)
            out.name = read_il2cpp_string(prim_ref(read_ptr(pdata + off::PlayerBattleData::CopName)), 40);

        return obj_ptr(out.pos_prim) || out.pos_model
            || (out.tx_prim && out.ty_prim && out.tz_prim);
    }

    inline bool world::build_skeleton(ent_chain& out) const
    {
        if (!obj_ptr(out.skeleton_model) || !obj_ptr(out.skeleton_config)
            || !obj_ptr(out.bones_array))
            return false;

        const klass_info* ki = info_of(out.skeleton_model);
        if (ki && ki->name.find("Skeleton") == std::string::npos)
            return false;

        // SkeletonConfig is a flat block of ints/pointers from 0x10 to 0x4C.
        std::uint8_t cfg_buf[0x40]{};
        if (!read_mem(out.skeleton_config + off::SkeletonConfig::BonesCount,
                      cfg_buf, sizeof(cfg_buf)))
            return false;

        auto rd_i32 = [&](std::size_t at) -> std::int32_t
        {
            std::int32_t v = 0;
            std::memcpy(&v, cfg_buf + at, sizeof(v));
            return v;
        };
        auto rd_ptr = [&](std::size_t at) -> std::uintptr_t
        {
            std::uintptr_t v = 0;
            std::memcpy(&v, cfg_buf + at, sizeof(v));
            return plausible(v) ? v : 0;
        };

        constexpr std::size_t kBase = 0x10; // SkeletonConfig::BonesCount
        const std::int32_t bones  = rd_i32(off::SkeletonConfig::BonesCount        - kBase);
        const std::int32_t bsize  = rd_i32(off::SkeletonConfig::BoneDataSize      - kBase);
        const std::int32_t loff   = rd_i32(off::SkeletonConfig::LocalPosOriOffset - kBase);
        const std::int32_t moff   = rd_i32(off::SkeletonConfig::ModelOriOffset    - kBase);
        const std::int32_t lenoff = rd_i32(off::SkeletonConfig::LengthOffset      - kBase);
        const std::uintptr_t hier = rd_ptr(off::SkeletonConfig::HierarchyData     - kBase);
        const std::int32_t maxch  = rd_i32(off::SkeletonConfig::MaxChildrenCount  - kBase);
        std::int32_t root         = rd_i32(off::SkeletonConfig::RootIndex         - kBase);

        if (bones <= 0 || bones > kMaxBones) return false;
        if (bsize <= 0 || bsize > 64) return false;
        if (maxch <= 0 || maxch > 32) return false;
        if (loff < 0 || moff < 0 || lenoff < 0) return false;
        if (!obj_ptr(hier) || !obj_ptr(out.bones_array)) return false;
        if (root < 0 || root >= bones) root = 0;

        // ---- hierarchy: row-major, MaxChildrenCount ints per bone ----------
        const std::int32_t hlen = read_t<std::int32_t>(hier + il2cpp::Array_Length);
        if (hlen <= 0) return false;
        const int want_h = bones * maxch;
        const int have_h = (std::min)(hlen, want_h);
        std::vector<std::int32_t> h(static_cast<std::size_t>(have_h), -1);
        if (have_h > 0 &&
            !read_mem(hier + il2cpp::Array_Data, h.data(),
                      static_cast<std::size_t>(have_h) * sizeof(std::int32_t)))
            return false;

        std::vector<std::int16_t> parent(static_cast<std::size_t>(bones), -1);
        std::vector<std::vector<int>> children(static_cast<std::size_t>(bones));
        for (int i = 0; i < bones; ++i)
        {
            for (int k = 0; k < maxch; ++k)
            {
                const int at = i * maxch + k;
                if (at >= have_h) break;
                const int child = h[static_cast<std::size_t>(at)];
                if (child < 0 || child >= bones || child == i) break;
                if (child == root) continue;
                if (parent[static_cast<std::size_t>(child)] != -1) continue;
                parent[static_cast<std::size_t>(child)] = static_cast<std::int16_t>(i);
                children[static_cast<std::size_t>(i)].push_back(child);
            }
        }

        // BFS from the root so parents are always computed before children.
        std::array<std::int16_t, kMaxBones> order{};
        int order_count = 0;
        std::array<bool, kMaxBones> seen{};
        std::vector<int> queue;
        queue.reserve(static_cast<std::size_t>(bones));
        queue.push_back(root);
        seen[root] = true;
        for (std::size_t q = 0; q < queue.size(); ++q)
        {
            const int i = queue[q];
            if (order_count < kMaxBones) order[order_count++] = static_cast<std::int16_t>(i);
            for (int c : children[static_cast<std::size_t>(i)])
            {
                if (c < 0 || c >= bones || seen[static_cast<std::size_t>(c)]) continue;
                seen[static_cast<std::size_t>(c)] = true;
                queue.push_back(c);
            }
        }
        if (order_count <= 1) return false;

        // ---- pose array ----------------------------------------------------
        const std::int32_t blen =
            read_t<std::int32_t>(out.bones_array + il2cpp::Array_Length);
        if (blen <= 0) return false;
        const int want_f = bones * bsize;
        const int have_f = (std::min)(blen, want_f);
        if (have_f < bsize) return false;
        if (have_f < loff + 4) return false;

        out.skel_floats.resize(static_cast<std::size_t>(have_f));
        if (!read_mem(out.bones_array + il2cpp::Array_Data,
                      out.skel_floats.data(),
                      static_cast<std::size_t>(have_f) * sizeof(float)))
        {
            out.skel_floats.clear();
            return false;
        }

        out.skel_bones = bones;
        out.skel_bone_size = bsize;
        out.skel_local_off = loff;
        out.skel_model_off = moff;
        out.skel_length_off = lenoff;
        out.skel_root = root;
        out.skel_order_count = order_count;
        out.skel_order = order;
        for (int i = 0; i < bones; ++i)
        {
            const bool reachable = seen[static_cast<std::size_t>(i)];
            out.skel_parent[static_cast<std::size_t>(i)] =
                reachable ? parent[static_cast<std::size_t>(i)]
                          : static_cast<std::int16_t>(-2);
        }
        out.bones_data_addr = out.bones_array + il2cpp::Array_Data;
        out.skel_valid = true;
        out.skel_pose_valid = false;
        out.skel_tick = 0;
        decode_skeleton_pose(out);

        if (cfg.verbose)
        {
            float len_sum = 0.f;
            int len_n = 0;
            for (int i = 0; i < bones && i < kMaxBones; ++i)
            {
                const float l = out.skel_len[static_cast<std::size_t>(i)];
                if (l > 0.f) { len_sum += l; ++len_n; }
            }
            const float len_avg = len_n > 0 ? len_sum / static_cast<float>(len_n) : 0.f;
            const quat q0 = out.skel_local[static_cast<std::size_t>(root)];
            ROBRUN_LOG("[robrun] skeleton bones=" << bones << " size=" << bsize
                    << " offs=" << loff << "/" << moff << "/" << lenoff
                    << " rows=" << maxch << " root=" << root
                    << " order=" << order_count
                    << " bytes=" << (have_f * 4)
                    << " lenAvg=" << len_avg
                    << " rootQ=(" << q0.x << "," << q0.y << "," << q0.z << "," << q0.w << ")");
        }
        return true;
    }

    inline void world::decode_skeleton_pose(ent_chain& c) const
    {
        if (!c.skel_valid || c.skel_bone_size <= 0) return;
        const int n = static_cast<int>(c.skel_floats.size());
        const int bs = c.skel_bone_size;
        if (n <= 0) return;

        const float* f = c.skel_floats.data();
        auto read_q = [&](int base, int off, quat& q) -> bool
        {
            if (off < 0 || base + off + 3 >= n) return false;
            q = quat{ f[base + off], f[base + off + 1],
                      f[base + off + 2], f[base + off + 3] };
            return q.finite();
        };

        for (int i = 0; i < c.skel_bones && i < kMaxBones; ++i)
        {
            const int base = i * bs;
            quat l{}, m{};
            if (!read_q(base, c.skel_local_off, l)) l = quat{};
            if (!read_q(base, c.skel_model_off, m)) m = quat{};
            c.skel_local[static_cast<std::size_t>(i)] = l;
            c.skel_model[static_cast<std::size_t>(i)] = m;

            float len = 0.f;
            const int at = base + c.skel_length_off;
            if (at >= 0 && at < n) len = f[at];
            if (!std::isfinite(len) || len < 0.f || len > 3.f) len = 0.f;
            c.skel_len[static_cast<std::size_t>(i)] = len;
        }
        c.skel_pose_valid = true;
    }

    inline void world::compute_skeleton(entity& e, const ent_chain& c,
                                        const quat& world_ori) const
    {
        e.skeleton_valid = false;
        if (!c.skel_valid || !c.skel_pose_valid || c.skel_order_count <= 0)
            return;

        const int n = c.skel_bones;
        if (n <= 0 || n > kMaxBones) return;

        e.bones.resize(static_cast<std::size_t>(n));
        e.bone_parent.resize(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i)
        {
            e.bones[static_cast<std::size_t>(i)] = e.skeleton_anchor;
            e.bone_parent[static_cast<std::size_t>(i)] = -2;
        }

        const quat ori = world_ori.finite() ? world_ori : quat{};
        const vec3 up{ 0.f, 1.f, 0.f };
        std::array<quat, kMaxBones> wori{};

        for (int k = 0; k < c.skel_order_count; ++k)
        {
            const int i = c.skel_order[static_cast<std::size_t>(k)];
            if (i < 0 || i >= n) continue;

            const int p = c.skel_parent[static_cast<std::size_t>(i)];
            if (p < -1) continue; // unreachable bone

            vec3 parent_pos = e.skeleton_anchor;
            quat parent_wori = ori;
            if (p >= 0 && p < n)
            {
                parent_pos = e.bones[static_cast<std::size_t>(p)];
                parent_wori = wori[static_cast<std::size_t>(p)];
            }

            const quat local = c.skel_local[static_cast<std::size_t>(i)];
            const quat q = parent_wori * local;
            const vec3 dir = q.rotate(up) * c.skel_len[static_cast<std::size_t>(i)];

            e.bones[static_cast<std::size_t>(i)] = parent_pos + dir;
            wori[static_cast<std::size_t>(i)] =
                ori * c.skel_model[static_cast<std::size_t>(i)];
            e.bone_parent[static_cast<std::size_t>(i)] =
                static_cast<std::int16_t>(p);
        }

        e.bone_count = n;
        e.bone_root = c.skel_root;
        e.skeleton_valid = true;
    }

    inline std::string world::describe_chain(std::uintptr_t model) const
    {
        const auto it = chain_cache.find(model);
        if (it == chain_cache.end()) return "chain=none";
        const ent_chain& c = it->second;
        char buf[320];
        std::snprintf(buf, sizeof(buf),
            "cap=0x%llX capmodel=0x%llX plain=%d capr=0x%llX capl=0x%llX capg=0x%llX defr=%.3f stand=%.3f crouch=%.3f ground=0x%llX age=%llums",
            static_cast<unsigned long long>(c.capsule),
            static_cast<unsigned long long>(c.capsule_model),
            c.capsule_plain ? 1 : 0,
            static_cast<unsigned long long>(c.cap_r_prim),
            static_cast<unsigned long long>(c.cap_l_prim),
            static_cast<unsigned long long>(c.cap_g_prim),
            c.def_radius, c.stand_length, c.crouch_length,
            static_cast<unsigned long long>(c.ground_model),
            static_cast<unsigned long long>(
                c.built_tick ? GetTickCount64() - c.built_tick : 0));
        return buf;
    }

    // ---------------------------------------------------------------------
    // Skeleton source: the character view's Unity transforms.
    //
    // BattleEntitiesUpdater._characterEntityViews is the bridge from the
    // gameplay entity id to the MonoBehaviour that owns the rig. It only
    // changes when characters spawn/despawn, so it is cached for ~1 s.
    // ---------------------------------------------------------------------
    inline std::uintptr_t world::view_for_id(int id) const
    {
        const auto it = view_by_id.find(id);
        return it == view_by_id.end() ? 0 : it->second;
    }

    inline bool world::refresh_view_map(std::uint64_t now_tick)
    {
        if (!obj_ptr(battle_updater))
        {
            view_by_id.clear();
            view_dict = 0;
            view_count = -1;
            view_tick = now_tick;
            return false;
        }

        const std::uintptr_t dict =
            read_ptr(battle_updater + off::BattleEntitiesUpdater::CharacterEntityViews);
        if (!obj_ptr(dict))
        {
            view_by_id.clear();
            view_dict = 0;
            view_count = -1;
            view_tick = now_tick;
            return false;
        }

        if (dict == view_dict && now_tick - view_tick < 1000ull)
            return !view_by_id.empty();

        std::uintptr_t entries = 0;
        std::int32_t count = 0;
        {
            scatter_batch& sb = sc_;
            sb.add(dict + il2cpp::Dictionary_Entries, entries);
            sb.add(dict + il2cpp::Dictionary_Count, count);
            sb.run();
        }

        view_dict = dict;
        view_count = count;
        view_tick = now_tick;

        if (!obj_ptr(entries) || count <= 0)
        {
            view_by_id.clear();
            return false;
        }

        const int n = (std::min)(count, 128);
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(n)
                                      * il2cpp::Dictionary_EntryStride);
        if (!read_mem(entries + il2cpp::Array_Data, buf.data(), buf.size()))
            return !view_by_id.empty();

        view_by_id.clear();
        for (int i = 0; i < n; ++i)
        {
            const std::uint8_t* p =
                buf.data() + static_cast<std::size_t>(i) * il2cpp::Dictionary_EntryStride;
            std::int32_t hash = 0;
            std::int32_t key = 0;
            std::uintptr_t value = 0;
            std::memcpy(&hash, p + il2cpp::Dictionary_EntryHashCode, 4);
            if (hash < 0) continue;
            std::memcpy(&key, p + il2cpp::Dictionary_EntryKey, 4);
            std::memcpy(&value, p + il2cpp::Dictionary_EntryValue, sizeof(value));
            if (!obj_ptr(value) || key <= 0) continue;
            view_by_id.emplace(key, value);
        }

        // Drop bindings for characters that are gone.
        if (view_by_id.size() < 64)
        {
            for (auto it = uskel_cache.begin(); it != uskel_cache.end(); )
            {
                bool alive = false;
                for (const auto& kv : view_by_id)
                    if (kv.second == it->first) { alive = true; break; }
                if (!alive) it = uskel_cache.erase(it);
                else ++it;
            }
        }

        return !view_by_id.empty();
    }

    inline bool world::resolve_uskel(std::uintptr_t view, const vec3& epos,
                                     uskel_view& b)
    {
        b = uskel_view{};
        if (!obj_ptr(view)) return false;

        // Records which gate rejected this view so the log can explain why a
        // character has no skeleton.
        auto fail = [&](int stage) { b.fail_stage = stage; return false; };

        constexpr int kMaxArrayBones = 6;

        // ---- A: view -> sub views ------------------------------------------
        std::uintptr_t mv = 0, av = 0, wv = 0;
        std::uintptr_t head_pos = 0, spine_ad = 0, neck_ad = 0, head_ad = 0;
        {
            scatter_batch& sb = sc_;
            sb.add(view + off::BattleCharacterEntityView::MovementView, mv);
            sb.add(view + off::BattleCharacterEntityView::AnimationView, av);
            sb.add(view + off::BattleCharacterEntityView::WithWeaponAnimationView, wv);
            sb.add(view + off::BattleCharacterEntityView::HeadPosition, head_pos);
            sb.add(view + off::BattleCharacterEntityView::SpineAdaptation, spine_ad);
            sb.add(view + off::BattleCharacterEntityView::NeckAdaptation, neck_ad);
            sb.add(view + off::BattleCharacterEntityView::HeadAdaptation, head_ad);
            sb.run();
        }
        b.dbg_mv = mv; b.dbg_av = av; b.dbg_wv = wv;
        if (!obj_ptr(mv) && !obj_ptr(av) && !obj_ptr(wv)) return fail(1);

        // ---- B: sub views -> bone holders ----------------------------------
        std::uintptr_t root_tf = 0, head_view = 0, torso_view = 0;
        std::uintptr_t ikv[9]{};
        {
            scatter_batch& sb = sc_;
            if (obj_ptr(mv))
                sb.add(mv + off::CharacterMovementView::Transform, root_tf);
            if (obj_ptr(av))
            {
                sb.add(av + off::CharacterAnimationView::HeadManualControl, head_view);
                sb.add(av + off::CharacterAnimationView::TorsoDeformation, torso_view);
            }
            static const std::uintptr_t wv_slots[9] = {
                off::CharacterWithWeaponAnimationView::ArmedPose,
                off::CharacterWithWeaponAnimationView::LeftElbowPose,
                off::CharacterWithWeaponAnimationView::LeftWristPose,
                off::CharacterWithWeaponAnimationView::RightWristPose,
                off::CharacterWithWeaponAnimationView::LeftHandIK,
                off::CharacterWithWeaponAnimationView::RightHandIK,
                off::CharacterWithWeaponAnimationView::LeftFootIK,
                off::CharacterWithWeaponAnimationView::RightFootIK,
                off::CharacterWithWeaponAnimationView::NeckIK,
            };
            if (obj_ptr(wv))
                for (int i = 0; i < 9; ++i) sb.add(wv + wv_slots[i], ikv[i]);
            sb.run();
        }

        // ---- C: holders -> transforms / Transform[] arrays -----------------
        std::uintptr_t tf[7]{};
        std::uintptr_t named[8]{};
        std::uintptr_t arrays[9]{};
        {
            scatter_batch& sb = sc_;
            if (obj_ptr(root_tf)) tf[0] = root_tf;
            if (obj_ptr(torso_view))
            {
                sb.add(torso_view + off::CharacterTorsoDeformationView::Root, tf[1]);
                sb.add(torso_view + off::CharacterTorsoDeformationView::Spine2, tf[2]);
                sb.add(torso_view + off::CharacterTorsoDeformationView::Spine3, tf[3]);
            }
            if (obj_ptr(head_view))
            {
                sb.add(head_view + off::CharacterHeadManualControlView::SpineTop, tf[4]);
                sb.add(head_view + off::CharacterHeadManualControlView::Neck, tf[5]);
                sb.add(head_view + off::CharacterHeadManualControlView::Head, tf[6]);
            }
            // Spine / neck / head bones the game itself bends. Same transforms
            // as the rig when they overlap, so push_slot keeps a single copy.
            if (obj_ptr(spine_ad))
            {
                sb.add(spine_ad + off::SpineAdaptationManualBehavior::Root, named[0]);
                sb.add(spine_ad + off::SpineAdaptationManualBehavior::BoneEdge, named[1]);
                sb.add(spine_ad + off::SpineAdaptationManualBehavior::BoneTarget, named[2]);
            }
            if (obj_ptr(neck_ad))
            {
                sb.add(neck_ad + off::HeadAdaptationManualBehavior::Root, named[3]);
                sb.add(neck_ad + off::HeadAdaptationManualBehavior::BoneTarget, named[4]);
            }
            if (obj_ptr(head_ad))
            {
                sb.add(head_ad + off::HeadAdaptationManualBehavior::Root, named[5]);
                sb.add(head_ad + off::HeadAdaptationManualBehavior::BoneTarget, named[6]);
            }
            // HeadPosition (0xE8) is an aim/camera point, not a rig bone: it
            // sits above the head and drew the long center spike above the box.
            // The real head bone comes from CharacterHeadManualControlView.
            for (int i = 4; i < 9; ++i)
            {
                if (!obj_ptr(ikv[i])) continue;
                // 0..3 are PoseIKView (weapon targets, not body bones).
                // 4..8 are the arm, leg and neck chains.
                const std::uintptr_t bones_off = off::LimbIKView::Bones;
                sb.add(ikv[i] + bones_off, arrays[i]);
            }
            sb.run();
        }

        // ---- D: array headers + first transform refs -----------------------
        struct array_hdr
        {
            std::int32_t len = 0;
            std::int32_t pad = 0;
            std::uintptr_t refs[kMaxArrayBones]{};
        };
        std::array<array_hdr, 9> hdrs{};
        {
            scatter_batch& sb = sc_;
            bool any = false;
            for (int i = 0; i < 9; ++i)
                if (obj_ptr(arrays[i]))
                {
                    sb.add(arrays[i] + il2cpp::Array_Length, hdrs[i]);
                    any = true;
                }
            if (any) sb.run();
        }

        std::array<std::uintptr_t, kUslots> slots{};
        int nslots = 0;
        auto push_slot = [&](std::uintptr_t t)
        {
            if (!obj_ptr(t) || nslots >= kUslots) return;
            for (int i = 0; i < nslots; ++i)
                if (slots[i] == t) return; // duplicate transform -> stray/third limb
            slots[nslots++] = t;
        };
        for (int i = 0; i < 7; ++i) push_slot(tf[i]);
        // Adaptation behaviours (spine/neck/head) duplicate rig bones, and when
        // their transforms differ they add a floating center line through the
        // body. The real bones above already cover those segments.
        // for (int i = 0; i < 7; ++i) push_slot(named[i]);
        for (int i = 0; i < 9; ++i)
        {
            if (!obj_ptr(arrays[i])) continue;
            int n = hdrs[i].len;
            if (n <= 0) continue;
            if (n > kMaxArrayBones) n = kMaxArrayBones;
            for (int k = 0; k < n; ++k) push_slot(hdrs[i].refs[k]);
        }
        if (nslots < 4) return fail(2);

        // ---- E: managed Transform -> {hierarchy, index} --------------------
        struct native_ref
        {
            std::uintptr_t obj = 0;
            std::uintptr_t hier = 0;
            std::int32_t idx = -1;
            std::int32_t pad = 0;
        };
        std::array<native_ref, kUslots> nr{};
        std::array<std::uintptr_t, kUslots> nat{};
        for (int i = 0; i < nslots; ++i) nr[i].obj = slots[i];
        {
            scatter_batch& sb = sc_;
            for (int i = 0; i < nslots; ++i)
                sb.add(slots[i] + unity_native::Object_CachedPtr, nat[i]);
            sb.run();
        }
        {
            scatter_batch& sb = sc_;
            for (int i = 0; i < nslots; ++i)
            {
                if (!obj_ptr(nat[i])) { nr[i].obj = 0; continue; }
                sb.add(nat[i] + unity_native::Transform_Hierarchy, nr[i].hier);
                sb.add(nat[i] + unity_native::Transform_Index, nr[i].idx);
            }
            sb.run();
        }

        // The whole rig lives in one hierarchy; if the reads produced several
        // (weapon sub-prefabs), keep the majority one.
        std::uintptr_t hier = 0;
        int best = 0;
        for (int i = 0; i < nslots; ++i)
        {
            if (!nr[i].obj || !nr[i].hier || nr[i].idx < 0) continue;
            int c = 0;
            for (int k = 0; k < nslots; ++k)
                if (nr[k].hier == nr[i].hier) ++c;
            if (c > best) { best = c; hier = nr[i].hier; }
        }
        if (!hier || best < 4) return fail(3);

        std::uintptr_t records = 0, parents = 0;
        {
            scatter_batch& sb = sc_;
            sb.add(hier + unity_native::Hierarchy_Records, records);
            sb.add(hier + unity_native::Hierarchy_Parents, parents);
            sb.run();
        }
        if (!obj_ptr(records) || !obj_ptr(parents)) return fail(4);

        b.node.fill(-1);
        b.edge.fill(-2);
        int lo = 0x7FFFFFFF, hi = -1;
        int used = 0;
        for (int i = 0; i < nslots; ++i)
        {
            if (!nr[i].obj || nr[i].hier != hier || nr[i].idx < 0) continue;
            if (nr[i].idx > 0x800000) continue;
            b.node[used] = nr[i].idx;
            ++used;
            lo = (std::min)(lo, static_cast<int>(nr[i].idx));
            hi = (std::max)(hi, static_cast<int>(nr[i].idx));
        }
        if (used < 4 || hi < 0) return fail(5);

        // The same transform can appear in more than one IK chain (e.g. the
        // neck); keep exactly one slot per hierarchy node.
        for (int i = 0; i < used; ++i)
        {
            if (b.node[i] < 0) continue;
            for (int k = 0; k < i; ++k)
                if (b.node[k] == b.node[i]) { b.node[i] = -1; break; }
        }
        b.slots = used;

        // ---- F: read the block, expand until every ancestor is covered -----
        std::vector<std::uint8_t> rec;
        std::vector<std::int32_t> par;
        bool converged = false;
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            const int span = hi - lo + 1;
            if (span <= 0 || span > 4096) return fail(6);
            rec.resize(static_cast<std::size_t>(span) * unity_native::Record_Stride);
            par.resize(static_cast<std::size_t>(span));

            {
                scatter_batch& sb = sc_;
                sb.add(records + static_cast<std::uintptr_t>(lo)
                                 * unity_native::Record_Stride,
                       rec.data(), rec.size());
                sb.add(parents + static_cast<std::uintptr_t>(lo) * sizeof(std::int32_t),
                       par.data(), par.size() * sizeof(std::int32_t));
                sb.run();
            }

            bool changed = false;
            for (int rel = 0; rel < span; ++rel)
            {
                const int p = par[static_cast<std::size_t>(rel)];
                if (p >= 0 && p < lo) { lo = p; changed = true; }
                else if (p > hi)      { hi = p; changed = true; }
            }
            if (!changed) { converged = true; break; }
        }
        if (!converged) return fail(7);

        b.hierarchy = hier;
        b.records = records;
        b.parents = parents;
        b.lo = lo;
        b.hi = hi;
        b.rec_buf = std::move(rec);
        b.par_buf = std::move(par);

        // CharacterMovementView.Transform is the capsule the game calls
        // SetPosition on. It sits in the middle of the body, not on the rig.
        // Bones still solve through it, but no line is drawn to it.
        int anchor_node = -1;
        for (int i = 0; i < nslots; ++i)
        {
            if (slots[i] == root_tf && nr[i].hier == hier && nr[i].idx >= 0)
            {
                anchor_node = nr[i].idx;
                break;
            }
        }

        // ---- G: draw edges (closest bound ancestor) ------------------------
        for (int i = 0; i < b.slots; ++i)
        {
            const int ni = b.node[i];
            if (ni < 0) continue;
            int cur = ni;
            int guard = 0;
            while (guard++ < 256)
            {
                const int p = b.par_buf[static_cast<std::size_t>(cur - b.lo)];
                if (p < 0) break;
                int found = -1;
                for (int k = 0; k < b.slots; ++k)
                    if (b.node[k] == p) { found = k; break; }
                if (found >= 0) { b.edge[i] = static_cast<std::int16_t>(found); break; }
                if (p < b.lo || p > b.hi) break;
                cur = p;
            }
        }
        if (anchor_node >= 0)
        {
            int anchor_slot = -1;
            for (int i = 0; i < b.slots; ++i)
                if (b.node[i] == anchor_node) { anchor_slot = i; break; }
            if (anchor_slot >= 0)
            {
                b.edge[anchor_slot] = -2;
                for (int i = 0; i < b.slots; ++i)
                    if (b.edge[i] == anchor_slot) b.edge[i] = -2;
            }
            b.dbg_anchor = anchor_slot;
        }

        compute_uskel(b, epos);
        if (!b.valid)
        {
            // Characters the game is not rendering are parked at a sentinel
            // position far above the map. They have no skeleton to draw.
            if (b.root_pos.finite() && b.root_pos.y > 5000.f) return fail(10);
            return fail(8);
        }

        b.bound = true;
        b.resolve_tick = GetTickCount64();

        if (cfg.verbose)
        {
            ROBRUN_LOG("[robrun] uskel bind view=0x" << std::hex << view
                    << std::dec << " slots=" << b.slots
                    << " span=" << (b.hi - b.lo + 1)
                    << " lo=" << b.lo
                    << " root=(" << b.root_pos.x << "," << b.root_pos.y
                    << "," << b.root_pos.z << ")"
                    << " pos=(" << epos.x << "," << epos.y << "," << epos.z << ")");
        }
        return true;
    }

    inline void world::compute_uskel(uskel_view& b, const vec3& epos) const
    {
        b.valid = false;
        b.count = 0;
        const int span = b.hi - b.lo + 1;
        if (span <= 0 || b.rec_buf.empty() || b.par_buf.empty()) return;

        std::vector<uskel_node_world> w(static_cast<std::size_t>(span));
        int near_count = 0, total = 0;
        const bool have_ref = epos.finite() && !epos.zero();
        float best_dist = -1.f;

        for (int i = 0; i < b.slots; ++i)
        {
            const int ni = b.node[i];
            b.bone_parent[i] = -2;
            b.bones[i] = vec3{};
            if (ni < b.lo || ni > b.hi) continue;

            uskel_solve_node(b.rec_buf.data(), b.par_buf.data(), b.lo, span,
                             ni - b.lo, w.data());
            const uskel_node_world& nw = w[static_cast<std::size_t>(ni - b.lo)];
            if (!nw.ok || !nw.pos.finite()) continue;

            b.bones[i] = nw.pos;
            b.bone_parent[i] = b.edge[i];
            ++total;
            if (!have_ref || nw.pos.distance(epos) < 6.f) ++near_count;
            if (have_ref)
            {
                const float d = nw.pos.distance(epos);
                if (best_dist < 0.f || d < best_dist) best_dist = d;
            }
        }

        b.root_pos = b.bones[0];
        if (!b.root_pos.finite() || b.root_pos.zero())
        {
            for (int i = 0; i < b.slots; ++i)
                if (b.bones[i].finite() && !b.bones[i].zero())
                { b.root_pos = b.bones[i]; break; }
        }

        // Drop bones that are not on the body. IK / weapon targets can sit
        // anywhere in the scene and produced the long stray line.
        if (have_ref)
        {
            for (int i = 0; i < b.slots; ++i)
            {
                if (b.bone_parent[i] == -2) continue;
                const vec3& p = b.bones[i];
                const float dx = p.x - epos.x, dz = p.z - epos.z;
                const float dh = std::sqrt(dx * dx + dz * dz);
                if (dh > 1.9f || p.y < epos.y - 0.7f || p.y > epos.y + 2.15f)
                {
                    b.bone_parent[i] = -2;
                    b.bones[i] = vec3{};
                }
            }
        }

        // Re-link children of dropped bones to their nearest kept ancestor so
        // no line is drawn to an empty slot.
        const std::array<std::int16_t, kUslots> link = b.bone_parent;
        for (int i = 0; i < b.slots; ++i)
        {
            int par = link[i];
            int guard = 0;
            while (par >= 0 && guard++ < 24
                   && b.bones[static_cast<std::size_t>(par)].zero())
                par = link[static_cast<std::size_t>(par)];
            b.bone_parent[i] = static_cast<std::int16_t>(par);
        }

        // A humanoid bone segment is never longer than ~1.1 m; anything more
        // is a broken link (e.g. an arm joined to a leg), not a bone.
        for (int i = 0; i < b.slots; ++i)
        {
            const int par = b.bone_parent[i];
            if (par < 0) continue;
            if (b.bones[i].distance(b.bones[static_cast<std::size_t>(par)]) > 1.15f)
                b.bone_parent[i] = -2;
        }

        // Reject garbage: at least a third of the bones must be within a body's
        // reach of the entity position. A wrong native layout produces
        // scattered/NaN transforms and fails here.
        b.valid = total >= 4 && near_count * 3 >= total;
        b.count = b.slots;
        b.dbg_dist = best_dist;
    }

    inline bool world::read_character(std::uintptr_t model, entity& out) const
    {
        if (!obj_ptr(model)) return false;
        // Accept the base class and any subclass/specialisation of it.  The
        // position read below is the real validation.
        const klass_info* ki = info_of(model);
        if (ki && ki->name.find("BattleCharacter") == std::string::npos
               && ki->name.find("PlayerEntity") == std::string::npos)
            return false;

        entity e;
        e.model = model;
        e.id = read_t<std::int32_t>(model + off::BattleCharacterModel::Id);
        if (e.id <= 0) e.id = read_t<std::int32_t>(model + off::EntityModel::Id);

        if (!read_position(model, e.pos)) return false;

        const std::uintptr_t data = read_ptr(model + off::BattleCharacterModel::Data);
        const std::uintptr_t common = data ? read_ptr(data + off::BattleCharacterData::CommonData) : 0;
        if (common)
        {
            bool b = false;
            if (prim_bool(read_ptr(common + off::BattleCharacterCommonData::IsSprinting), b)) e.sprinting = b;
            if (prim_bool(read_ptr(common + off::BattleCharacterCommonData::IsCrouching), b)) e.crouching = b;
            if (prim_bool(read_ptr(common + off::BattleCharacterCommonData::IsArmed), b)) e.armed = b;
            if (prim_bool(read_ptr(common + off::BattleCharacterCommonData::IsDead), b)) e.dead = b;

            e.role = role_name(prim_ref(read_ptr(common + off::BattleCharacterCommonData::Role)));

            const std::uintptr_t stance_model = read_ptr(model + off::BattleCharacterModel::BattleStanceModel);
            if (stance_model)
            {
                const std::uintptr_t stance_data = read_ptr(stance_model + 0x18);
                e.stance = stance_name(prim_ref(stance_data));
            }
        }

        // health
        const std::uintptr_t priv = read_ptr(model + off::BattleCharacterModel::PrivateModel);
        const std::uintptr_t health = priv ? read_ptr(priv + off::BattleCharacterPrivateModel::Health) : 0;
        if (health)
        {
            std::int32_t hp = 0;
            if (prim_int(read_ptr(health + off::HealthModel::Health), hp)) e.health = hp;
            e.max_health = read_t<std::int32_t>(health + off::HealthModel::MaxAmount);
        }

        if (!e.dead)
        {
            bool b = false;
            const std::uintptr_t dead_model = read_ptr(model + off::BattleCharacterModel::IsDead);
            if (dead_model && pm_bool(dead_model, b)) e.dead = b;
        }

        // display name (players only)
        const std::uintptr_t pbm = read_ptr(model + off::BattleCharacterModel::PlayerBattleModel);
        if (pbm)
        {
            const std::uintptr_t pdata = read_ptr(pbm + off::PlayerBattleModel::Data);
            if (pdata)
                e.name = read_il2cpp_string(prim_ref(read_ptr(pdata + off::PlayerBattleData::CopName)), 40);
        }

        out = std::move(e);
        return true;
    }

    inline void world::mark_local()
    {
        if (!entities.empty())
        {
            if (local_model)
            {
                for (auto& e : entities)
                    if (e.model == local_model) { e.local = true; return; }
            }
            if (local_id > 0)
            {
                for (auto& e : entities)
                    if (e.id == local_id) { e.local = true; local_model = e.model; return; }
            }
            // proximity to the camera / cinemachine follow target
            vec3 reference{};
            bool have_ref = false;
            if (camera.local_follow_valid) { reference = camera.local_follow; have_ref = true; }
            else if (camera.valid) { reference = camera.pos; have_ref = true; }
            if (have_ref)
            {
                entity* best = nullptr;
                float best_d = cfg.local_radius;
                for (auto& e : entities)
                {
                    const float d = e.pos.distance(reference);
                    if (d < best_d) { best_d = d; best = &e; }
                }
                if (best) { best->local = true; local_model = best->model; local_id = best->id; }
            }
        }
    }

    // PhysicsCharacterConfigs.Default is a static readonly instance created by
    // the game's .cctor. Per-character BattleCharacterConfig trees did not
    // expose a PhysicsCharacter pointer on the models we read (defr/stand/
    // crouch all 0), so the shared default is used as the last-resort body
    // size when a character has no capsule model.
    inline void world::refresh_phys_defaults(std::uint64_t now_tick) const
    {
        if (phys_default && now_tick - phys_default_tick < 30000ull)
            return;
        if (now_tick - phys_default_tick < 3000ull)
            return; // back off after a failed lookup
        phys_default_tick = now_tick;

        const std::uintptr_t klass =
            klass_from_rva(rva::PhysicsCharacterConfigs_TypeInfo,
                           "PhysicsCharacterConfigs");
        const std::uintptr_t sf = klass ? static_fields_of(klass) : 0;
        if (!sf) return;
        const std::uintptr_t def = read_ptr(sf);
        if (!obj_ptr(def)) return;

        float r = 0.f, s = 0.f, c = 0.f;
        if (sc_.valid())
        {
            sc_.add(def + off::PhysicsCharacterConfig::DefaultRadius, r);
            sc_.add(def + off::PhysicsCharacterConfig::StandLength, s);
            sc_.add(def + off::PhysicsCharacterConfig::CrouchLength, c);
            sc_.run();
        }
        else
        {
            read_mem(def + off::PhysicsCharacterConfig::DefaultRadius, &r, sizeof(r));
            read_mem(def + off::PhysicsCharacterConfig::StandLength, &s, sizeof(s));
            read_mem(def + off::PhysicsCharacterConfig::CrouchLength, &c, sizeof(c));
        }
        if (r > 0.02f && r < 1.5f) phys_def_radius = r;
        if (s > 0.6f && s < 3.f) phys_stand_len = s;
        if (c > 0.6f && c < 3.f) phys_crouch_len = c;
        if (phys_stand_len > 0.f || phys_crouch_len > 0.f)
            phys_default = def;
    }

    inline void world::refresh_skeleton()
    {
        if (!want_skeleton) return;
        const auto skel_t0 = std::chrono::steady_clock::now();
        const std::uint64_t now_tick = GetTickCount64();

        // Gameplay pose. Usually empty in this build; kept off the position
        // scatter so a multi-kilobyte bone array cannot stall the boxes.
        {
            std::vector<ent_chain*> pose;
            pose.reserve(chain_cache.size());
            for (auto& kv : chain_cache)
            {
                ent_chain& c = kv.second;
                if (!c.skel_valid || !c.bones_data_addr || c.skel_floats.empty()) continue;
                if (c.skel_tick != 0 &&
                    now_tick - c.skel_tick < 50ull + c.state_phase * 5ull)
                    continue;
                pose.push_back(&c);
            }
            if (!pose.empty() && sc_.valid())
            {
                for (ent_chain* c : pose)
                    sc_.add(c->bones_data_addr, c->skel_floats.data(),
                            c->skel_floats.size() * sizeof(float));
                sc_.run();
                for (ent_chain* c : pose)
                {
                    decode_skeleton_pose(*c);
                    c->skel_tick = now_tick;
                }
            }
        }

        // Unity visual rig. Bindings are resolved once per view; the local TRS
        // records are refreshed in one scatter and composed on the CPU.
        if (now_tick - view_tick >= 1000ull)
            refresh_view_map(now_tick);

        if (cfg.verbose && now_tick - uskel_log_tick >= 5000ull)
        {
            uskel_log_tick = now_tick;
            int bound = 0, valid = 0, bones = 0;
            for (const auto& kv : uskel_cache)
            {
                if (kv.second.bound) ++bound;
                if (kv.second.valid) { ++valid; bones += kv.second.count; }
            }
            ROBRUN_LOG("[robrun] uskel: views=" << view_by_id.size()
                    << " cache=" << uskel_cache.size()
                    << " bound=" << bound << " valid=" << valid
                    << " bones=" << bones
                    << " failA=" << uskel_stage_fail[1]
                    << " failSlots=" << uskel_stage_fail[2]
                    << " failHier=" << uskel_stage_fail[3]
                    << " failHdr=" << uskel_stage_fail[4]
                    << " failUsed=" << uskel_stage_fail[5]
                    << " failSpan=" << uskel_stage_fail[6]
                    << " failConv=" << uskel_stage_fail[7]
                    << " failPos=" << uskel_stage_fail[8]
                    << " failRun=" << uskel_stage_fail[9]
                    << " failParked=" << uskel_stage_fail[10]
                    << " updater=0x" << std::hex << battle_updater
                    << std::dec);
            for (int& v : uskel_stage_fail) v = 0;
        }

        struct due_item
        {
            uskel_view* b = nullptr;
            vec3 epos{};
        };
        std::vector<due_item> due;
        due.reserve(chain_cache.size());
        std::unordered_set<std::uintptr_t> seen_views;

        for (auto& kv : chain_cache)
        {
            ent_chain& c = kv.second;
            const std::uintptr_t view = view_for_id(c.id);
            if (!obj_ptr(view) || !seen_views.insert(view).second) continue;

            uskel_view& b = uskel_cache[view];
            vec3 epos = c.last_pos;
            if (!epos.finite() || epos.zero()) epos = {};

            if (!b.bound)
            {
                if (b.resolve_fail_tick &&
                    now_tick - b.resolve_fail_tick
                        < (b.fail_stage == 10 ? 10000ull : 3000ull))
                    continue;
                if (!resolve_uskel(view, epos, b))
                {
                    // Duplicate chains can carry the same entity id at
                    // different positions (client copy vs stale/server copy).
                    // If the solved rig matches one of those copies, resolve
                    // against that position instead of rejecting the rig.
                    bool recovered = false;
                    if (b.fail_stage == 8 && b.root_pos.finite()
                        && !b.root_pos.zero())
                    {
                        for (const auto& okv : chain_cache)
                        {
                            if (okv.second.id != c.id) continue;
                            vec3 alt = okv.second.last_pos;
                            if (!alt.finite() || alt.zero()) continue;
                            if (alt.distance(b.root_pos) > 6.f) continue;
                            if (alt.distance(epos) < 1.f) continue;
                            if (resolve_uskel(view, alt, b))
                            {
                                epos = alt;
                                recovered = true;
                                break;
                            }
                        }
                    }
                    if (recovered)
                    {
                        b.resolve_fail_tick = 0;
                    }
                    else
                    {
                    if (b.fail_stage >= 0 && b.fail_stage < 11)
                        ++uskel_stage_fail[b.fail_stage];
                    b.resolve_fail_tick = now_tick;
                    if (cfg.verbose && b.fail_stage != 10
                        && now_tick - uskel_fail_log_tick >= 500ull)
                    {
                        uskel_fail_log_tick = now_tick;
                        int near_id = -1;
                        float near_d = -1.f;
                        if (b.fail_stage == 8 && b.root_pos.finite()
                            && !b.root_pos.zero())
                        {
                            for (const auto& okv : chain_cache)
                            {
                                const vec3 op = okv.second.last_pos;
                                if (!op.finite() || op.zero()) continue;
                                const float od = op.distance(b.root_pos);
                                if (near_d < 0.f || od < near_d)
                                {
                                    near_d = od;
                                    near_id = okv.second.id;
                                }
                            }
                        }
                        ROBRUN_LOG("[robrun] uskel fail stage=" << b.fail_stage
                                << " view=0x" << std::hex << view << std::dec
                                << " id=" << c.id
                                << " mv=0x" << std::hex << b.dbg_mv
                                << " av=0x" << b.dbg_av
                                << " wv=0x" << b.dbg_wv << std::dec
                                << " dist=" << b.dbg_dist
                                << " nearId=" << near_id
                                << " nearD=" << near_d
                                << " root=(" << b.root_pos.x << ","
                                << b.root_pos.y << "," << b.root_pos.z << ")"
                                << " pos=(" << epos.x << "," << epos.y
                                << "," << epos.z << ")");
                    }
                        continue;
                    }
                }
                b.resolve_fail_tick = 0;
            }

            // A rig bound before the character had a position was accepted as
            // a wildcard. Once a real position is known, check the rig actually
            // sits on that body; otherwise the skeleton would float elsewhere.
            if (b.bound && epos.finite() && !epos.zero() && b.root_pos.finite()
                && !b.root_pos.zero() && b.root_pos.y < 5000.f)
            {
                const float d = b.root_pos.distance(epos);
                if (d > 6.f && cfg.verbose
                    && now_tick - uskel_far_log_tick >= 2000ull)
                {
                    uskel_far_log_tick = now_tick;
                    ROBRUN_LOG("[robrun] uskel far d=" << d
                            << " view=0x" << std::hex << view << std::dec
                            << " id=" << c.id
                            << " root=(" << b.root_pos.x << "," << b.root_pos.y
                            << "," << b.root_pos.z << ")"
                            << " pos=(" << epos.x << "," << epos.y
                            << "," << epos.z << ")");
                }
            }

            if (b.tick == 0 || now_tick - b.tick >= 33ull)
            {
                due_item d;
                d.b = &b;
                d.epos = epos;
                due.push_back(d);
            }
        }

        if (!due.empty())
        {
            scatter_batch& sb = sc_;
            for (const due_item& d : due)
            {
                sb.add(d.b->records + static_cast<std::uintptr_t>(d.b->lo)
                                      * unity_native::Record_Stride,
                       d.b->rec_buf.data(), d.b->rec_buf.size());
                sb.add(d.b->parents + static_cast<std::uintptr_t>(d.b->lo)
                                      * sizeof(std::int32_t),
                       d.b->par_buf.data(),
                       d.b->par_buf.size() * sizeof(std::int32_t));
            }
            sb.run();

            for (const due_item& d : due)
            {
                compute_uskel(*d.b, d.epos);
                d.b->tick = now_tick;
                if (!d.b->valid)
                {
                    d.b->bound = false;
                    d.b->fail_stage = 9;
                    d.b->resolve_fail_tick = now_tick;
                }
            }
        }

        ent_skel_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - skel_t0).count();
    }

    inline void world::refresh_entities()
    {
        entities.clear();
        ++chain_frame;

        const auto rf_t0 = std::chrono::steady_clock::now();
        const std::uint64_t rq_start = sc_.requests_total;
        const double sc_start = sc_.run_ms_total;
        pos_fallback_calls = 0;
        ent_skel_far = 0;

        // Shared fallback body size (PhysicsCharacterConfigs.Default). Cached
        // for 30 s; used for characters that never expose a capsule.
        refresh_phys_defaults(GetTickCount64());

        // ---- collect the live character models ----------------------------
        // Spawn/despawn only changes the dictionaries a few times per second,
        // so the model list (and the multi-round dictionary walk that builds
        // it) is throttled. Every other tick is a single scatter round that
        // only reads the live values.
        const std::uint64_t now_tick = GetTickCount64();
        // Never let an empty/unresolved model list turn into a per-tick scan:
        // the dictionary walk costs several scatter transactions. Retry an
        // empty list at 2 Hz, a populated one at 1 Hz.
        const bool handles_changed =
            models_world != world_model || models_battle != battle_entities;
        const bool need_scan =
            handles_changed || models_tick == 0 ||
            now_tick - models_tick >= (models_cache.empty() ? 500ull : 1000ull);

        if (need_scan)
        {
        std::vector<std::uintptr_t> models;
        models.reserve(64);
        auto add_model = [&](std::uintptr_t m)
        {
            if (!obj_ptr(m)) return;
            for (std::uintptr_t v : models)
                if (v == m) return;
            models.push_back(m);
        };

        // Dictionary headers (entries array + count) for every source are read
        // in one scatter round, then each entry array in one bulk read. That
        // replaces the serial pointer/header reads for_each_dict() used.
        //
        // Two world instances can exist (BattleEntitiesModel.WorldEntitiesModel
        // and BattleModel.WorldEntitiesModel). In some builds only one of them
        // is actually populated, so gather characters from both.
        constexpr int kMaxDicts = 6;
        std::uintptr_t dicts[kMaxDicts]{};
        std::uintptr_t dict_entries[kMaxDicts]{};
        std::int32_t dict_counts[kMaxDicts]{};
        int dict_used = 0;
        int battle_dict_idx = -1;
        auto queue_dict = [&](std::uintptr_t d) -> int
        {
            if (!obj_ptr(d)) return -1;
            for (int i = 0; i < dict_used; ++i)
                if (dicts[i] == d) return i;
            if (dict_used >= kMaxDicts) return -1;
            dicts[dict_used] = d;
            return dict_used++;
        };

        std::uintptr_t world_models[2]{ world_model, 0 };
        if (battle_model)
        {
            const std::uintptr_t bmw =
                read_ptr(battle_model + off::BattleModel::WorldEntitiesModel);
            if (bmw && bmw != world_model) world_models[1] = bmw;
        }

        std::uintptr_t wd[2]{}, nd[2]{}, bd = 0;
        {
            for (int k = 0; k < 2; ++k)
            {
                if (!world_models[k]) continue;
                sc_.add(world_models[k] + off::WorldEntitiesModel::BattleCharacters, wd[k]);
                sc_.add(world_models[k] + off::WorldEntitiesModel::NotSpawnedBattleCharacters, nd[k]);
            }
            if (battle_entities)
                sc_.add(battle_entities + off::BattleEntitiesModel::Models, bd);
            sc_.run();
        }

        int wd_idx[2]{ -1, -1 }, nd_idx[2]{ -1, -1 };
        for (int k = 0; k < 2; ++k)
        {
            if (!world_models[k]) continue;
            wd_idx[k] = queue_dict(wd[k]);
            nd_idx[k] = queue_dict(nd[k]);
        }
        battle_dict_idx = queue_dict(bd);

        for (int i = 0; i < dict_used; ++i)
        {
            sc_.add(dicts[i] + il2cpp::Dictionary_Entries, dict_entries[i]);
            sc_.add(dicts[i] + il2cpp::Dictionary_Count, dict_counts[i]);
        }
        if (dict_used) sc_.run();

        auto gather_dict_entries = [&](int i, std::vector<std::uintptr_t>& out)
        {
            if (i < 0 || i >= dict_used) return;
            if (!obj_ptr(dict_entries[i]) || dict_counts[i] <= 0) return;
            const int n = (std::min)(dict_counts[i], 8192);
            const std::size_t stride = il2cpp::Dictionary_EntryStride;
            std::vector<std::uint8_t> buf(static_cast<std::size_t>(n) * stride);
            if (!read_mem(dict_entries[i] + il2cpp::Array_Data, buf.data(), buf.size())) return;
            out.reserve(out.size() + static_cast<std::size_t>(n));
            for (int e = 0; e < n; ++e)
            {
                const std::uint8_t* p = buf.data() + static_cast<std::size_t>(e) * stride;
                std::int32_t hash = 0;
                std::uintptr_t value = 0;
                std::memcpy(&hash, p + il2cpp::Dictionary_EntryHashCode, 4);
                if (hash < 0) continue;
                std::memcpy(&value, p + il2cpp::Dictionary_EntryValue, sizeof(value));
                if (!obj_ptr(value)) continue;
                out.push_back(value);
            }
        };

        if (world_models[0] || world_models[1])
        {
            std::vector<std::uintptr_t> wms;
            for (int k = 0; k < 2; ++k)
            {
                gather_dict_entries(wd_idx[k], wms);
                gather_dict_entries(nd_idx[k], wms);
            }
            for (std::uintptr_t m : wms) add_model(m);
        }
        mapped_from_world = static_cast<std::uint32_t>(models.size());

        // Merge the master entity dictionary as well. It stores IEntityModel
        // wrappers, and the actual BattleCharacterModel lives in ServerModel at
        // a class-specific offset. In this game build the world character
        // dictionary can hold only the local character (or a stale entry) while
        // every remote player is present only here, so do not gate this on an
        // empty world dictionary.
        //
        // The dictionary's high-water entry array can hold hundreds of slots,
        // most of them freed. Scanning and resolving all of them every frame
        // was the main ESP stutter source, so the resolved models are cached
        // and only rebuilt when the dictionary handle/count changes or the
        // cache ages out (players spawn/despawn without a stable count).
        mapped_from_battle = 0;
        std::unordered_map<std::string, int> battle_hist;
        const bool want_hist = !battle_hist_logged && mapped_from_world < 8;

        const bool battle_dict_ok =
            battle_entities && battle_dict_idx >= 0 &&
            obj_ptr(dict_entries[battle_dict_idx]) && dict_counts[battle_dict_idx] > 0;

        if (!battle_dict_ok)
        {
            battle_models_cache.clear();
            simulated_models.clear();
            battle_entry_cache.clear();
            battle_cache_dict = 0;
            battle_cache_count = -1;
            battle_cache_tick = 0;
        }
        else
        {
            const std::int32_t battle_count = dict_counts[battle_dict_idx];
            const bool dict_changed =
                battle_cache_dict != bd || battle_cache_count != battle_count;
            const bool stale =
                dict_changed || battle_cache_tick == 0 ||
                now_tick - battle_cache_tick >= 2000ull;

            if (stale)
            {
                std::vector<std::uintptr_t> ems;
                gather_dict_entries(battle_dict_idx, ems);

                // Only resolve wrappers that are new, whose model pointer has
                // not appeared yet, or that are old enough to be revalidated.
                // In steady state this costs ZERO scatter requests instead of
                // two 139-request rounds every second.
                if (battle_cache_dict != bd)
                    battle_entry_cache.clear();
                else if (dict_changed)
                {
                    // Spawn/despawn: prune wrappers that left the dictionary
                    // instead of throwing away every cached model (which would
                    // re-read all ~139 high-water entries).
                    std::unordered_set<std::uintptr_t> live(ems.begin(), ems.end());
                    for (auto it = battle_entry_cache.begin();
                         it != battle_entry_cache.end(); )
                    {
                        if (!live.count(it->first))
                            it = battle_entry_cache.erase(it);
                        else
                            ++it;
                    }
                }

                std::vector<std::uintptr_t> unknown;
                unknown.reserve(ems.size());
                for (std::uintptr_t em : ems)
                {
                    const auto it = battle_entry_cache.find(em);
                    if (it == battle_entry_cache.end())
                    {
                        unknown.push_back(em);
                        continue;
                    }
                    const std::uint64_t age = now_tick - it->second.tick;
                    const bool empty = !it->second.server && !it->second.sim;
                    if (age >= 5000ull || (empty && age >= 1000ull))
                        unknown.push_back(em);
                }

                if (!unknown.empty())
                {
                    std::vector<std::uintptr_t> klasses(unknown.size(), 0);
                    for (std::size_t i = 0; i < unknown.size(); ++i)
                        sc_.add(unknown[i], klasses[i]);
                    sc_.run();

                    std::vector<std::uintptr_t> servers(unknown.size(), 0);
                    std::vector<std::uintptr_t> sims(unknown.size(), 0);
                    std::vector<std::uint8_t> resolved(unknown.size(), 0);
                    bool need_server = false;
                    for (std::size_t i = 0; i < unknown.size(); ++i)
                    {
                        const klass_info* ki = info_of_klass(klasses[i]);
                        if (!ki) continue;
                        std::uintptr_t field = 0;
                        std::uintptr_t sim_field = 0;
                        if (ki->name == "BattleCharacterEntityModel")
                        {
                            field = off::BattleCharacterEntityModel::ServerModel;
                            // The simulated model is the client-side predicted
                            // instance. It is the one the client animates, so
                            // its SkeletonModel is populated while the server
                            // copy can stay empty.
                            sim_field = off::BattleCharacterEntityModel::SimulatedModel;
                        }
                        else if (ki->name == "PlayerEntityModel")
                            field = off::PlayerEntityModel::ServerModel;
                        else if (ki->name == "BattleCharacterModel")
                        {
                            battle_entry_cache[unknown[i]] =
                                battle_entry{ unknown[i], 0, now_tick };
                            resolved[i] = 1;
                            continue;
                        }
                        else
                            continue;
                        resolved[i] = 1;
                        if (field) sc_.add(unknown[i] + field, servers[i]);
                        if (sim_field) sc_.add(unknown[i] + sim_field, sims[i]);
                        need_server = true;
                        if (want_hist && battle_hist.size() < 24) ++battle_hist[ki->name];
                    }
                    if (need_server) sc_.run();

                    for (std::size_t i = 0; i < unknown.size(); ++i)
                    {
                        if (!resolved[i]) continue;
                        if (resolved[i] && !servers[i] && !sims[i] &&
                            battle_entry_cache.find(unknown[i]) != battle_entry_cache.end())
                            continue; // already stored (plain BattleCharacterModel)
                        battle_entry_cache[unknown[i]] =
                            battle_entry{ servers[i], sims[i], now_tick };
                    }
                }

                battle_models_cache.clear();
                simulated_models.clear();
                for (std::uintptr_t em : ems)
                {
                    const auto it = battle_entry_cache.find(em);
                    if (it == battle_entry_cache.end()) continue;
                    // Simulated copy first: it is the instance the client
                    // animates. The server copy's position trails the mesh.
                    if (it->second.sim)
                    {
                        battle_models_cache.push_back(it->second.sim);
                        simulated_models.insert(it->second.sim);
                    }
                    if (it->second.server) battle_models_cache.push_back(it->second.server);
                }

                battle_cache_dict = bd;
                battle_cache_count = battle_count;
                battle_cache_tick = now_tick;
            }

            for (std::uintptr_t m : battle_models_cache)
            {
                add_model(m);
                ++mapped_from_battle;
            }
        }

        if (want_hist)
        {
            battle_hist_logged = true;
            std::string hist;
            for (const auto& kv : battle_hist)
            {
                if (!hist.empty()) hist += ", ";
                hist += kv.first + "*" + std::to_string(kv.second);
            }
            ROBRUN_LOG("[robrun] battle dict characters: " << (hist.empty() ? "none" : hist));
        }

        models_cache = std::move(models);
        models_tick = now_tick;
        models_world = world_model;
        models_battle = battle_entities;
        } // need_scan

        ent_db_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - rf_t0).count();

        const std::vector<std::uintptr_t>& models = models_cache;

        // ---- resolve / refresh the cached pointer chains ------------------
        //
        // A chain built before the model finished spawning can permanently
        // miss the capsule/config pointers (they are created after the model
        // itself), which left some boxes without bounds. Such incomplete
        // chains are rebuilt periodically instead of being cached forever.
        std::vector<ent_chain*> chains;
        chains.reserve(models.size());
        std::uint32_t new_chains = 0;
        const std::uint32_t new_budget = cfg.max_new_chains_per_frame
            ? cfg.max_new_chains_per_frame : 12;
        const std::uint64_t chain_now = GetTickCount64();
        chain_cache.reserve(models.size() + chain_cache.size() + 8);
        for (std::uintptr_t m : models)
        {
            auto it = chain_cache.find(m);
            if (it != chain_cache.end() && it->second.rejected)
            {
                it->second.last_seen = chain_frame;
                // A failed model used to be rebuilt every tick and spent the
                // whole new-chain budget, so real characters never got a chain.
                if (chain_now - it->second.built_tick < 3000ull || new_chains >= new_budget)
                    continue;
            }
            else if (it != chain_cache.end())
            {
                ent_chain& existing = it->second;
                existing.last_seen = chain_frame;
                existing.simulated = simulated_models.count(m) != 0;
                const bool no_capsule =
                    !existing.capsule && !existing.cap_r_prim &&
                    !(existing.def_radius > 0.02f);
                const bool no_pos =
                    !existing.pos_box && !existing.pos_model &&
                    !(existing.tx_box && existing.ty_box && existing.tz_box);
                // Rebuilding a chain costs 3-6 scatter transactions (each a
                // separate device operation). A permanently missing skeleton
                // must therefore back off instead of retrying every few
                // seconds forever, and an incomplete capsule chain is only
                // retried every 5 s.
                const bool no_skel =
                    want_skeleton && !existing.skel_valid &&
                    chain_now - (existing.skel_fail_tick
                                     ? existing.skel_fail_tick
                                     : existing.built_tick) >= 15000ull;
                const std::uint64_t since_build =
                    chain_now - existing.built_tick;
                const bool rebuild_due =
                    ((no_capsule || no_pos) && since_build >= 5000ull) ||
                    (no_skel && since_build >= 15000ull);
                if (!rebuild_due || new_chains >= new_budget)
                {
                    chains.push_back(&existing);
                    continue;
                }
            }
            else if (new_chains >= new_budget)
            {
                continue;
            }

            ent_chain c;
            if (!build_chain(m, c))
            {
                // A chain that already worked keeps its last good pointers.
                // A model that has never resolved is remembered as rejected so
                // the next ticks do not scatter-read it again.
                if (it != chain_cache.end() && !it->second.rejected)
                {
                    it->second.built_tick = chain_now;
                    chains.push_back(&it->second);
                }
                else if (it != chain_cache.end())
                {
                    it->second.rejected = true;
                    it->second.built_tick = chain_now;
                    it->second.last_seen = chain_frame;
                }
                else
                {
                    ent_chain bad;
                    bad.model = m;
                    bad.rejected = true;
                    bad.built_tick = chain_now;
                    bad.last_seen = chain_frame;
                    chain_cache.emplace(m, std::move(bad));
                }
                ++new_chains;
                continue;
            }
            ++new_chains;
            c.last_seen = chain_frame;
            c.built_tick = chain_now;
            c.skel_fail_tick = (want_skeleton && !c.skel_valid) ? chain_now : 0;
            c.state_phase = static_cast<std::uint32_t>(
                (static_cast<unsigned int>(c.id >= 0 ? c.id : 0) * 7u + 3u) % 10u);
            c.state_valid = false;
            c.simulated = simulated_models.count(m) != 0;
            if (it != chain_cache.end())
            {
                it->second = std::move(c);
                chains.push_back(&it->second);
            }
            else
            {
                it = chain_cache.emplace(m, std::move(c)).first;
                chains.push_back(&it->second);
            }
        }

        // A model can be discovered twice: through its server copy and through
        // its client simulated copy. Both chains read the same character, so
        // deduplicate by id here instead of paying two position reads per tick.
        {
            std::unordered_map<int, std::size_t> id_slot;
            std::vector<ent_chain*> unique;
            unique.reserve(chains.size());
            for (ent_chain* pc : chains)
            {
                if (pc->id > 0)
                {
                    const auto it = id_slot.find(pc->id);
                    if (it != id_slot.end())
                    {
                        ent_chain*& keep = unique[it->second];
                        // Prefer the copy that carries the most live data: a
                        // populated skeleton first, then any capsule bounds
                        // (server and simulated copies differ), then a
                        // position primitive. Without the capsule tier the
                        // server copy could win and fall back to the fixed
                        // 1.8 m box even though the simulated copy had the
                        // authoritative capsule.
                        const auto has_capsule = [](const ent_chain* c)
                        {
                            return c->capsule_model || c->cap_r_prim ||
                                   c->def_radius > 0.02f;
                        };
                        // The simulated copy is the one the client draws. A
                        // server copy with a capsule must not replace it, or
                        // the box follows the lagged replicated position.
                        if (pc->simulated != keep->simulated)
                        {
                            if (pc->simulated) keep = pc;
                            continue;
                        }
                        const bool better =
                            (pc->skel_valid && !keep->skel_valid) ||
                            (pc->skel_valid == keep->skel_valid &&
                             !has_capsule(keep) && has_capsule(pc)) ||
                            (pc->skel_valid == keep->skel_valid &&
                             has_capsule(pc) == has_capsule(keep) &&
                             !keep->pos_box && pc->pos_box);
                        if (better) keep = pc;
                        continue;
                    }
                    id_slot.emplace(pc->id, unique.size());
                }
                unique.push_back(pc);
            }
            chains.swap(unique);
        }

        // Chains built before the visual transform was wired still have a
        // TransformModel pointer and no value boxes. One scatter fills them;
        // a miss backs off instead of retrying every tick.
        {
            std::vector<ent_chain*> need;
            need.reserve(chains.size());
            for (ent_chain* pc : chains)
            {
                if (!pc->transform || pc->tx_box) continue;
                if (pc->visual_try && chain_now - pc->visual_try < 2000ull) continue;
                need.push_back(pc);
            }
            if (!need.empty())
            {
                for (ent_chain* pc : need)
                {
                    pc->visual_try = chain_now;
                    sc_.add(pc->transform + off::TransformModel::X, pc->tx_prim);
                    sc_.add(pc->transform + off::TransformModel::Y, pc->ty_prim);
                    sc_.add(pc->transform + off::TransformModel::Z, pc->tz_prim);
                }
                sc_.run();
                for (ent_chain* pc : need)
                {
                    auto add_box = [](std::uintptr_t prim, std::uintptr_t& box)
                    {
                        box = prim ? prim + (il2cpp::PrimitiveData_Value - 0x10) : 0;
                    };
                    add_box(pc->tx_prim, pc->tx_box);
                    add_box(pc->ty_prim, pc->ty_box);
                    add_box(pc->tz_prim, pc->tz_box);
                }
            }
        }

        if (chains.empty())
        {
            if (!models.empty()) chain_cache.clear();
            // Still refresh the camera: it is normally decoded from the entity
            // scatter, which does not run when there is nothing to draw.
            if (cam_state_pending && obj_ptr(cam_brain))
            {
                if (sc_.valid())
                {
                    sc_.add(cam_brain + cam::Brain::CurrentCameraState,
                            cam_state_buf, sizeof(cam_state_buf));
                    sc_.run();
                    decode_camera_state(cam_state_buf);
                }
                else if (read_mem(cam_brain + cam::Brain::CurrentCameraState,
                                  cam_state_buf, sizeof(cam_state_buf)))
                {
                    decode_camera_state(cam_state_buf);
                }
            }
            ent_requests = sc_.requests_total - rq_start;
            ent_scatter_ms = sc_.run_ms_total - sc_start;
            return;
        }

        struct slot
        {
            vec3 pos{};
            vec3 ground_pt{};
            float cap_plain[3] = { 0.f, 0.f, 0.f };
            float cap_r = 0.f, cap_l = 0.f, cap_g = 0.f;
            float tx = 0.f, ty = 0.f, tz = 0.f;
            float rx = 0.f, ry = 0.f, rz = 0.f;
            std::int32_t health = 0;
            std::uint8_t sprint = 0, crouch = 0, armed = 0, dead = 0, ground_found = 0;
            bool state_due = false;    // health / crouch / orientation
            bool slow_due = false;     // sprint / armed / dead / ground
            bool capsule_due = false;  // radius / length / distance-to-ground
        };
        std::vector<slot> slots(chains.size());

        // Per-character DMA request budget. A refresh used to issue ~15
        // requests per character every 200 ms; each scatter request costs
        // roughly a millisecond on the FPGA device, so that alone pinned the
        // whole ESP to ~20 Hz. Tiers:
        //   position                every tick (smooth boxes)
        //   health / crouch         ~5 Hz staggered
        //   sprint/armed/dead/ground ~1 Hz staggered (cosmetic)
        //   capsule                 ~2 Hz staggered (box size changes slowly)
        //   orientation             only while a gameplay skeleton is live
        // Steady state is ~1.5 requests per character instead of ~6.
        constexpr std::uint64_t kStateIntervalMs = 200;
        constexpr std::uint64_t kSlowIntervalMs = 1000;
        constexpr std::uint64_t kCapsuleIntervalMs = 500;
        {
            scatter_batch& sb = sc_;
            if (sb.valid())
            {
                // Fold the camera state into the same transaction. It used to
                // be a separate serial read every tick.
                if (cam_state_pending && obj_ptr(cam_brain))
                    sb.add(cam_brain + cam::Brain::CurrentCameraState,
                           cam_state_buf, sizeof(cam_state_buf));

                for (std::size_t i = 0; i < chains.size(); ++i)
                {
                    ent_chain& c = *chains[i];
                    slot& s = slots[i];
                    const bool state_due = !c.state_valid ||
                        now_tick - c.state_tick >= kStateIntervalMs + c.state_phase * 20ull;
                    const bool slow_due = !c.state_valid ||
                        now_tick - c.slow_tick >= kSlowIntervalMs + c.state_phase * 50ull;
                    const bool capsule_due = !c.state_valid ||
                        now_tick - c.capsule_tick >= kCapsuleIntervalMs + c.state_phase * 25ull;
                    s.state_due = state_due;
                    s.slow_due = slow_due;
                    s.capsule_due = capsule_due;

                    if (c.pos_box)        sb.add(c.pos_box + 0x10, &s.pos, sizeof(vec3));
                    else if (c.pos_model) sb.add(c.pos_model + 0x10, &s.pos, sizeof(vec3));
                    if (c.tx_box) sb.add(c.tx_box + 0x10, s.tx);
                    if (c.ty_box) sb.add(c.ty_box + 0x10, s.ty);
                    if (c.tz_box) sb.add(c.tz_box + 0x10, s.tz);

                    if (state_due)
                    {
                        // Orientation only feeds the gameplay skeleton; the
                        // Unity-rig path works in world space.
                        if (want_skeleton && c.skel_valid)
                        {
                            if (c.rx_box) sb.add(c.rx_box + 0x10, s.rx);
                            if (c.ry_box) sb.add(c.ry_box + 0x10, s.ry);
                            if (c.rz_box) sb.add(c.rz_box + 0x10, s.rz);
                        }
                        if (c.health_box) sb.add(c.health_box + 0x10, s.health);
                        if (c.crouch_box) sb.add(c.crouch_box + 0x10, s.crouch);
                    }
                    if (slow_due)
                    {
                        if (c.sprint_box) sb.add(c.sprint_box + 0x10, s.sprint);
                        if (c.armed_box)  sb.add(c.armed_box + 0x10, s.armed);
                        if (c.dead_box)   sb.add(c.dead_box + 0x10, s.dead);
                        if (c.ground_model)
                        {
                            sb.add(c.ground_model + off::CharacterControllerGroundModel::GroundFound,
                                   s.ground_found);
                            sb.add(c.ground_model + off::CharacterControllerGroundModel::GroundPoint,
                                   &s.ground_pt, sizeof(vec3));
                        }
                    }
                    if (capsule_due)
                    {
                        if (c.capsule_plain)
                            sb.add(c.capsule + off::CapsuleColliderParamsModel::Radius,
                                   s.cap_plain, sizeof(s.cap_plain));
                        if (c.cap_r_box) sb.add(c.cap_r_box + 0x10, s.cap_r);
                        if (c.cap_l_box) sb.add(c.cap_l_box + 0x10, s.cap_l);
                        if (c.cap_g_box) sb.add(c.cap_g_box + 0x10, s.cap_g);
                    }
                }
                sb.run();
                if (cam_state_pending && obj_ptr(cam_brain))
                    decode_camera_state(cam_state_buf);
            }
            else
            {
                for (std::size_t i = 0; i < chains.size(); ++i)
                {
                    ent_chain& c = *chains[i];
                    slot& s = slots[i];
                    const bool state_due = !c.state_valid ||
                        now_tick - c.state_tick >= kStateIntervalMs + c.state_phase * 20ull;
                    const bool slow_due = !c.state_valid ||
                        now_tick - c.slow_tick >= kSlowIntervalMs + c.state_phase * 50ull;
                    const bool capsule_due = !c.state_valid ||
                        now_tick - c.capsule_tick >= kCapsuleIntervalMs + c.state_phase * 25ull;
                    s.state_due = state_due;
                    s.slow_due = slow_due;
                    s.capsule_due = capsule_due;

                    if (c.pos_box)        read_mem(c.pos_box + 0x10, &s.pos, sizeof(vec3));
                    else if (c.pos_model) read_mem(c.pos_model + 0x10, &s.pos, sizeof(vec3));
                    if (c.tx_box) read_mem(c.tx_box + 0x10, &s.tx, sizeof(s.tx));
                    if (c.ty_box) read_mem(c.ty_box + 0x10, &s.ty, sizeof(s.ty));
                    if (c.tz_box) read_mem(c.tz_box + 0x10, &s.tz, sizeof(s.tz));

                    if (state_due)
                    {
                        if (want_skeleton && c.skel_valid)
                        {
                            if (c.rx_box) read_mem(c.rx_box + 0x10, &s.rx, sizeof(s.rx));
                            if (c.ry_box) read_mem(c.ry_box + 0x10, &s.ry, sizeof(s.ry));
                            if (c.rz_box) read_mem(c.rz_box + 0x10, &s.rz, sizeof(s.rz));
                        }
                        if (c.health_box) read_mem(c.health_box + 0x10, &s.health, sizeof(s.health));
                        if (c.crouch_box) read_mem(c.crouch_box + 0x10, &s.crouch, 1);
                    }
                    if (slow_due)
                    {
                        if (c.sprint_box) read_mem(c.sprint_box + 0x10, &s.sprint, 1);
                        if (c.armed_box)  read_mem(c.armed_box + 0x10, &s.armed, 1);
                        if (c.dead_box)   read_mem(c.dead_box + 0x10, &s.dead, 1);
                        if (c.ground_model)
                        {
                            read_mem(c.ground_model + off::CharacterControllerGroundModel::GroundFound,
                                     &s.ground_found, 1);
                            read_mem(c.ground_model + off::CharacterControllerGroundModel::GroundPoint,
                                     &s.ground_pt, sizeof(vec3));
                        }
                    }
                    if (capsule_due)
                    {
                        if (c.capsule_plain) read_mem(c.capsule + off::CapsuleColliderParamsModel::Radius,
                                                      s.cap_plain, sizeof(s.cap_plain));
                        if (c.cap_r_box) read_mem(c.cap_r_box + 0x10, &s.cap_r, sizeof(s.cap_r));
                        if (c.cap_l_box) read_mem(c.cap_l_box + 0x10, &s.cap_l, sizeof(s.cap_l));
                        if (c.cap_g_box) read_mem(c.cap_g_box + 0x10, &s.cap_g, sizeof(s.cap_g));
                    }
                }

                if (cam_state_pending && obj_ptr(cam_brain) &&
                    read_mem(cam_brain + cam::Brain::CurrentCameraState,
                             cam_state_buf, sizeof(cam_state_buf)))
                    decode_camera_state(cam_state_buf);
            }
        }

        // Save the state read this tick, then replay the last cached state for
        // entities that were skipped so the draw path sees a complete slot.
        for (std::size_t i = 0; i < chains.size(); ++i)
        {
            ent_chain& c = *chains[i];
            slot& s = slots[i];

            if (s.state_due)
            {
                c.st_health = s.health;
                c.st_crouch = s.crouch;
                c.st_rx = s.rx;
                c.st_ry = s.ry;
                c.st_rz = s.rz;
                c.state_valid = true;
                c.state_tick = now_tick;
            }
            else
            {
                s.health = c.st_health;
                s.crouch = c.st_crouch;
                s.rx = c.st_rx;
                s.ry = c.st_ry;
                s.rz = c.st_rz;
            }

            if (s.slow_due)
            {
                c.st_sprint = s.sprint;
                c.st_armed = s.armed;
                c.st_dead = s.dead;
                c.st_ground_found = s.ground_found;
                c.st_ground_pt = s.ground_pt;
                c.slow_tick = now_tick;
            }
            else
            {
                s.sprint = c.st_sprint;
                s.armed = c.st_armed;
                s.dead = c.st_dead;
                s.ground_found = c.st_ground_found;
                s.ground_pt = c.st_ground_pt;
            }

            if (s.capsule_due)
            {
                for (int k = 0; k < 3; ++k) c.st_cap_plain[k] = s.cap_plain[k];
                c.st_cap_r = s.cap_r;
                c.st_cap_l = s.cap_l;
                c.st_cap_g = s.cap_g;
                c.capsule_tick = now_tick;
            }
            else
            {
                for (int k = 0; k < 3; ++k) s.cap_plain[k] = c.st_cap_plain[k];
                s.cap_r = c.st_cap_r;
                s.cap_l = c.st_cap_l;
                s.cap_g = c.st_cap_g;
            }
        }

        // Skeleton DMA lives in refresh_skeleton(), which the worker calls
        // after the snapshot is published. Keeping it here made every box
        // wait on the bone scatter.
        ent_skel_ms = 0.0;

        // ---- build the entity list -----------------------------------------
        const auto build_t0 = std::chrono::steady_clock::now();
        ent_chain_posbox = ent_chain_posmodel = ent_chain_tx = 0;
        for (const ent_chain* pc : chains)
        {
            if (pc->pos_box) ++ent_chain_posbox;
            if (pc->pos_model) ++ent_chain_posmodel;
            if (pc->tx_box) ++ent_chain_tx;
        }
        ent_scatter_pos_ok = 0;
        for (const slot& s : slots)
            if (s.pos.finite() && !s.pos.zero()) ++ent_scatter_pos_ok;

        // One-shot position layout probe: tells whether IPrimitiveData<T>._value
        // is an inline value or a pointer to a boxed value for Vector3.
        if (cfg.verbose && now_tick - pos_probe_tick >= 5000)
        {
            for (const ent_chain* pc : chains)
            {
                if (!pc->pos_prim) continue;
                pos_probe_tick = now_tick;
                vec3 inline_v{}, boxed_v{}, plus20{};
                read_mem(pc->pos_prim + 0x18, &inline_v, sizeof(inline_v));
                read_mem(pc->pos_prim + 0x20, &plus20, sizeof(plus20));
                if (pc->pos_box) read_mem(pc->pos_box + 0x10, &boxed_v, sizeof(boxed_v));
                ROBRUN_LOG("[robrun] posprobe prim=0x" << std::hex << pc->pos_prim
                        << " box=0x" << pc->pos_box << std::dec
                        << " inline=(" << inline_v.x << "," << inline_v.y << "," << inline_v.z << ")"
                        << " plus20=(" << plus20.x << "," << plus20.y << "," << plus20.z << ")"
                        << " boxed=(" << boxed_v.x << "," << boxed_v.y << "," << boxed_v.z << ")");
                break;
            }
        }

        const double sample_s = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (camera.valid && camera.pos.finite() && !camera.pos.zero())
        {
            track_velocity(cam_velocity, cam_prev_pos, cam_prev_t, camera.pos, sample_s);
            camera.velocity = cam_velocity;
            camera.sampled_at = sample_s;
        }

        std::vector<std::uintptr_t> stale;
        for (std::size_t i = 0; i < chains.size(); ++i)
        {
            ent_chain& c = *chains[i];
            slot& s = slots[i];

            // CommonData / replicated position trails the mesh. The visual
            // transform is the root the renderer is standing on; use it when
            // it is the same body, and keep the gameplay position otherwise.
            vec3 gameplay{};
            const bool have_gameplay = s.pos.finite() && !s.pos.zero();
            if (have_gameplay) gameplay = s.pos;
            const vec3 visual(s.tx, s.ty, s.tz);
            const bool have_visual = c.tx_box && c.ty_box && c.tz_box
                && visual.finite() && !visual.zero();

            vec3 pos{};
            bool have_pos = false;
            if (have_visual && have_gameplay)
            {
                if (visual.distance(gameplay) < 8.f) { pos = visual; have_pos = true; }
                else { pos = gameplay; have_pos = true; }
            }
            else if (have_visual) { pos = visual; have_pos = true; }
            else if (have_gameplay) { pos = gameplay; have_pos = true; }
            if (!have_pos) have_pos = read_position(c.model, pos);
            if (!have_pos) { stale.push_back(c.model); ++ent_pos_misses; ROBRUN_LOG("[robrun] pos miss model=0x"
                    << std::hex << c.model << std::dec << " id=" << c.id
                    << " via=posbox/posmodel/txbox/posread"); continue; }

            track_velocity(c.velocity, c.last_pos, c.pos_time_s, pos, sample_s);

            // Throttled extra info (armor / knockout / cuffs). Serial reads are
            // cheap at ~1 Hz and avoid complicating the main scatter path.
            if (now_tick - c.info_tick >= 1000ull + c.state_phase * 60ull)
            {
                c.info_tick = now_tick;
                if (c.has_armor_box)
                {
                    std::uint8_t av = 0;
                    if (read_mem(c.has_armor_box + 0x10, &av, 1)) c.st_has_armor = av;
                }
                c.st_knocked = c.st_cuffed = c.st_cuffing = c.st_surrendered = 0;
                if (obj_ptr(c.knockout_model))
                {
                    std::uint8_t flags[3]{}; // surrendered, handcuffing, handcuffed @ 0x70
                    if (read_mem(c.knockout_model + off::KnockoutModel::IsSurrendered, flags, sizeof(flags)))
                    {
                        c.st_surrendered = flags[0];
                        c.st_cuffing = flags[1];
                        c.st_cuffed = flags[2];
                        c.st_knocked = (flags[0] || flags[1] || flags[2]) ? 1 : 0;
                    }
                }
                // Refresh weapon name when item-in-hands changes (best-effort).
                if (obj_ptr(c.item_hands_prim))
                {
                    const std::uintptr_t desc = prim_ref(c.item_hands_prim);
                    if (desc != c.item_hands_desc)
                    {
                        c.item_hands_desc = desc;
                        c.weapon_name = obj_ptr(desc)
                            ? read_il2cpp_string(read_ptr(desc + off::Identified::Id), 48)
                            : std::string{};
                    }
                }
            }

            entity e;
            e.model = c.model;
            e.id = c.id;
            e.name = c.name;
            e.role = c.role;
            e.role_label = c.role_label;
            e.stance = c.stance;
            e.weapon_name = c.weapon_name;
            e.pos = pos;
            e.velocity = c.velocity;
            e.sampled_at = sample_s;
            e.health = s.health;
            e.max_health = c.max_health;
            e.has_armor = c.st_has_armor != 0;
            e.knocked_out = c.st_knocked != 0;
            e.handcuffed = c.st_cuffed != 0;
            e.handcuffing = c.st_cuffing != 0;
            e.surrendered = c.st_surrendered != 0;
            e.sprinting = s.sprint != 0;
            e.crouching = s.crouch != 0;
            e.armed = s.armed != 0;
            e.dead = s.dead != 0;
            e.pos_is_center = c.position_is_center;

            // Capsule bounds: authoritative when replicated; otherwise fall
            // back to the character's configured stand/crouch height.
            float radius = 0.f, length = 0.f, dist_to_ground = 0.f;
            bool have_capsule = false;
            if (c.capsule_plain)
            {
                radius = s.cap_plain[0];
                length = s.cap_plain[1];
                dist_to_ground = s.cap_plain[2];
                have_capsule = radius > 0.02f || length > 0.05f;
            }
            else
            {
                radius = s.cap_r;
                length = s.cap_l;
                dist_to_ground = s.cap_g;
                have_capsule = radius > 0.02f || length > 0.05f;
                if (!have_capsule)
                    dist_to_ground = c.cap_dist_to_ground;
            }
            // Reject NaN / obviously corrupt values instead of letting them
            // poison the box maths (that is what produced a few boxes far
            // above or below the body).
            if (!std::isfinite(radius) || radius < 0.02f || radius > 1.5f)
                radius = 0.f;
            if (!std::isfinite(length) || length < 0.f || length > 3.f)
                length = 0.f;
            if (!std::isfinite(dist_to_ground)
                || dist_to_ground < -0.25f || dist_to_ground > 2.f)
                dist_to_ground = 0.f;

            // Box height. Authoritative capsule: Unity capsule height is the
            // cylinder length plus the two hemispherical caps (2 * radius).
            // Without a capsule the configured Stand/CrouchLength is already
            // the full body height, so it is used as-is. A hard default keeps
            // every entity boxed even when the config pointer is missing.
            float box_h = 0.f;
            if (have_capsule)
                box_h = length + 2.f * radius;
            if (!(box_h > 0.6f) || box_h > 3.5f)
            {
                // Per-character config first, then the shared default, then
                // the hard-coded standing/crouch height.
                const float cfg_h = e.crouching
                    ? (c.crouch_length > 0.6f ? c.crouch_length : phys_crouch_len)
                    : (c.stand_length > 0.6f ? c.stand_length : phys_stand_len);
                box_h = (cfg_h > 0.6f && cfg_h < 3.f)
                    ? cfg_h
                    : (e.crouching ? 1.15f : 1.80f);
            }
            if (!(radius > 0.02f) || radius > 1.5f)
            {
                if (c.def_radius > 0.05f && c.def_radius < 1.5f)
                    radius = c.def_radius;
                else if (phys_def_radius > 0.05f && phys_def_radius < 1.5f)
                    radius = phys_def_radius;
                else
                    radius = 0.4f;
            }

            e.cap_radius = radius;
            e.cap_length = length;
            e.cap_ground_gap = dist_to_ground;
            e.height = box_h;
            e.width = 2.f * radius;

            // Box anchor. CommonData.Position is normally the character's
            // feet/ground point, but a few replicated models store the capsule
            // centre instead. Instead of trusting one convention blindly,
            // compare the raw Y with the live ground contact:
            //   dy ~= 0                -> position is the feet
            //   dy ~= DistanceToGround -> position is the capsule centre
            // Airborne entities ignore the ground contact and use the raw Y.
            float bottom = pos.y;
            bool anchored = false;
            bool pos_center = c.position_is_center;
            e.ground_found = s.ground_found != 0;
            if (e.ground_found && s.ground_pt.finite())
            {
                const float dy = pos.y - s.ground_pt.y;
                const float half_h = box_h * 0.5f;
                if (dy > 0.4f &&
                    ((dist_to_ground > 0.25f && dist_to_ground < 2.f &&
                      std::fabs(dy - dist_to_ground) < 0.4f) ||
                     std::fabs(dy - half_h) < 0.6f))
                {
                    pos_center = true;
                    bottom = pos.y - (dist_to_ground > 0.25f ? dist_to_ground : half_h);
                    anchored = true;
                }
                else if (std::fabs(dy) < 0.5f)
                {
                    pos_center = false;
                    bottom = pos.y;
                    anchored = true;
                }
            }
            if (pos_center && !anchored)
                bottom = pos.y - box_h * 0.5f;

            e.pos_is_center = pos_center;

            // Smooth the feet offset and the height, then pin both to the
            // current position. Smoothing the absolute Y made the rectangle
            // trail the body by a fraction of a second.
            float feet = bottom - pos.y;
            float height = box_h;
            float sm_radius = radius;
            if (c.box_smooth_valid)
            {
                const float dt = now_tick > c.box_smooth_tick
                    ? static_cast<float>(now_tick - c.box_smooth_tick) * 0.001f
                    : 0.f;
                float a = 1.f - std::exp(-dt / 0.05f);
                if (!(a > 0.03f)) a = 0.03f;
                if (a > 0.85f) a = 0.85f;
                const bool snap =
                    std::fabs(feet - c.sm_feet) > 1.2f ||
                    std::fabs(height - c.sm_height) > 1.2f;
                if (!snap)
                {
                    feet      = c.sm_feet   + (feet - c.sm_feet) * a;
                    height    = c.sm_height + (height - c.sm_height) * a;
                    sm_radius = c.sm_radius + (sm_radius - c.sm_radius) * a;
                }
            }
            c.sm_feet = feet;
            c.sm_height = height;
            c.sm_radius = sm_radius;
            c.box_smooth_valid = true;
            c.box_smooth_tick = now_tick;

            e.box_bottom = pos.y + feet;
            e.box_top = e.box_bottom + height;
            e.height = height;
            e.width = 2.f * sm_radius;
            e.box_anchored = true;
            e.box_bounds_valid = (e.height > 0.4f);

            // Skeleton. Primary source: the Unity visual rig of the character
            // view (absolute world-space bone positions). The gameplay
            // SkeletonModel path is kept as a fallback; in this build it is
            // never populated because HitBoxesUpdater is gated off.
            if (want_skeleton)
            {
                const std::uintptr_t view = view_for_id(c.id);
                const auto vit = view ? uskel_cache.find(view) : uskel_cache.end();
                bool used_unity = false;
                if (vit != uskel_cache.end() && vit->second.valid
                    && vit->second.count > 1)
                {
                    const uskel_view& b = vit->second;
                    const int n = (std::min)(b.count, kMaxBones);
                    e.bones.resize(static_cast<std::size_t>(n));
                    e.bone_parent.resize(static_cast<std::size_t>(n));
                    // Pose is refreshed off the position tick. Slide the last
                    // solved bones by the root delta so the rig stays on the
                    // body instead of trailing it.
                    const vec3 slide = pos - b.root_pos;
                    const bool slide_ok = b.root_pos.finite() && !b.root_pos.zero()
                        && slide.dot(slide) < (2.5f * 2.5f);
                    if (!slide_ok)
                    {
                        // The same id also exists as a stale/server copy at a
                        // different position. The rig does not belong to this
                        // body; let the copy that matches it draw the skeleton.
                        ++ent_skel_far;
                    }
                    else
                    {
                        for (int k = 0; k < n; ++k)
                        {
                            vec3 p = b.bones[static_cast<std::size_t>(k)];
                            if (p.finite() && !p.zero()) p += slide;
                            e.bones[static_cast<std::size_t>(k)] = p;
                            e.bone_parent[static_cast<std::size_t>(k)] = b.bone_parent[static_cast<std::size_t>(k)];
                        }
                        e.bone_count = n;
                        e.bone_root = -1;
                        e.skeleton_anchor = vec3{ pos.x, e.box_bottom, pos.z };
                        e.skeleton_valid = true;
                        used_unity = true;
                    }
                }

                if (!used_unity && c.skel_valid && c.skel_pose_valid)
                {
                    const quat world_ori =
                        (c.rx_box || c.ry_box || c.rz_box)
                            ? quat_from_euler_deg(s.rx, s.ry, s.rz)
                            : quat{};
                    e.skeleton_anchor = vec3{ pos.x, e.box_bottom, pos.z };
                    compute_skeleton(e, c, world_ori);
                }
            }

            entities.push_back(std::move(e));
        }

        for (std::uintptr_t m : stale) chain_cache.erase(m);

        // Drop chains for models that disappeared from the dictionaries. The
        // live set (not last_seen) is used because the chain-build throttle
        // leaves fresh models unseen for a frame or two.
        if (!models.empty())
        {
            const std::unordered_set<std::uintptr_t> live(models.begin(), models.end());
            for (auto it = chain_cache.begin(); it != chain_cache.end(); )
            {
                if (live.find(it->first) == live.end()) it = chain_cache.erase(it);
                else ++it;
            }
        }

        // Characters that were drawn last refresh but are gone now. A player
        // getting into a vehicle is the usual reason; the log names the id so
        // we can see whether the model/view/position or the draw filter lost
        // them.
        {
            int announced = 0;
            std::unordered_set<int> now_ids;
            now_ids.reserve(entities.size());
            for (const auto& e : entities) now_ids.insert(e.id);
            for (auto it = last_entity_ids.begin(); it != last_entity_ids.end(); )
            {
                if (now_ids.count(it->first))
                {
                    it->second.second = now_tick;
                    ++it;
                    continue;
                }
                if (now_tick - it->second.second < 2000ull) { ++it; continue; }
                if (announced++ < 4)
                    ROBRUN_LOG("[robrun] ent gone id=" << it->first
                            << " name=" << it->second.first
                            << " rej=" << ent_chain_rejects
                            << " miss=" << ent_pos_misses);
                it = last_entity_ids.erase(it);
            }
            for (const auto& e : entities)
            {
                auto& slot = last_entity_ids[e.id];
                slot.first = e.name;
                if (slot.second == 0) slot.second = now_tick;
            }
        }

        std::sort(entities.begin(), entities.end(),
                  [](const entity& a, const entity& b) { return a.model < b.model; });
        entities.erase(std::unique(entities.begin(), entities.end(),
                                   [](const entity& a, const entity& b) { return a.model == b.model; }),
                       entities.end());

        ent_build_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - build_t0).count();
        ent_pos_fallbacks = pos_fallback_calls;

        // A character can be discovered twice: once through its server model
        // and once through the client simulated model. Keep exactly one entity
        // per id and prefer the copy whose skeleton is actually populated.
        {
            std::vector<entity> merged;
            merged.reserve(entities.size());
            std::unordered_map<int, std::size_t> by_id;
            for (entity& e : entities)
            {
                if (e.id <= 0)
                {
                    merged.push_back(std::move(e));
                    continue;
                }
                auto it = by_id.find(e.id);
                if (it == by_id.end())
                {
                    by_id.emplace(e.id, merged.size());
                    merged.push_back(std::move(e));
                    continue;
                }
                entity& keep = merged[it->second];
                const bool better_skel = e.skeleton_valid && !keep.skeleton_valid;
                const bool better_live = !better_skel &&
                    e.skeleton_valid == keep.skeleton_valid &&
                    e.health > keep.health;
                if (better_skel || better_live) keep = std::move(e);
            }
            entities = std::move(merged);
        }

        if (camera.valid)
            for (auto& e : entities) e.distance = e.pos.distance(camera.pos);

        read_party_ids();
        apply_party();

        ent_requests = sc_.requests_total - rq_start;
        ent_scatter_ms = sc_.run_ms_total - sc_start;
        mark_local();
    }

    inline void world::read_party_ids()
    {
        party_ids.clear();
        if (!battle_updater) return;

        const std::uintptr_t global = read_ptr(battle_updater + off::BattleEntitiesUpdater::GlobalModel);
        if (!obj_ptr(global)) return;
        const std::uintptr_t party = read_ptr(global + off::GlobalModel::Party);
        if (!obj_ptr(party)) return;
        const std::uintptr_t set = read_ptr(party + off::PartyModel::Entities);
        if (!obj_ptr(set)) return;

        const std::int32_t count = read_t<std::int32_t>(set + 0x20);
        const std::int32_t last = read_t<std::int32_t>(set + 0x24);
        const std::uintptr_t slots = read_ptr(set + 0x18);
        if (!obj_ptr(slots) || count <= 0 || count > 48 || last <= 0 || last > 128) return;

        auto consume = [&](int stride) -> int
        {
            std::vector<std::uint8_t> buf(static_cast<std::size_t>(last) * static_cast<std::size_t>(stride));
            if (!read_mem(slots + il2cpp::Array_Data, buf.data(), buf.size())) return 0;
            int got = 0;
            for (int i = 0; i < last; ++i)
            {
                const std::uint8_t* s = buf.data() + static_cast<std::size_t>(i) * static_cast<std::size_t>(stride);
                std::int32_t hash = 0, value = 0;
                std::memcpy(&hash, s, 4);
                std::memcpy(&value, s + 8, 4);
                if (hash < 0 || value <= 0 || value > 10000000) continue;
                party_ids.insert(value);
                ++got;
            }
            return got;
        };

        if (consume(12) == 0)
        {
            party_ids.clear();
            consume(16);
        }
    }

    inline void world::apply_party()
    {
        for (auto& e : entities)
            e.group_member = !e.local && e.id > 0 && party_ids.find(e.id) != party_ids.end();
    }

    inline void world::scan_world_objects()
    {
        // A full map walk is thousands of serial DMA reads. That was running
        // inside the position update and starving the skeleton scatter, so the
        // rig never stayed on the body. One stage slice per call keeps bones
        // on the fast path; the published loot/vehicle lists swap in when the
        // cycle finishes.
        if (!world_model)
        {
            loot.clear();
            vehicles.clear();
            loot_build.clear();
            vehicle_build.clear();
            locker_build.clear();
            scan_stage = 0;
            scan_skip = 0;
            return;
        }

        constexpr int kBudget = 8;
        int taken = 0;

        auto lower_has = [](const std::string& s, const char* key) -> bool
        {
            std::string a = s;
            for (char& c : a)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return a.find(key) != std::string::npos;
        };
        auto ident = [&](std::uintptr_t obj) -> std::string
        {
            if (!obj_ptr(obj)) return {};
            return read_il2cpp_string(read_ptr(obj + off::Identified::Id), 48);
        };
        auto kind_of = [&](const std::string& name) -> loot_type
        {
            if (lower_has(name, "weight") || lower_has(name, "dumbbell")) return loot_type::weight;
            if (lower_has(name, "bag")) return loot_type::dropped_bag;
            if (lower_has(name, "award") || lower_has(name, "reward") || lower_has(name, "badge"))
                return loot_type::police_award_box;
            if (lower_has(name, "register") || lower_has(name, "cashbox")) return loot_type::cash_register;
            if (lower_has(name, "safe")) return loot_type::safe;
            if (lower_has(name, "shelf") || lower_has(name, "shelv")) return loot_type::shelf;
            return loot_type::shelf;
        };
        auto push_loot = [&](loot_type type, std::string name, const vec3& pos, int value, bool locked, bool robbed)
        {
            if (loot_build.size() >= 500) return;
            if (!pos.finite() || pos.zero()) return;
            loot_entity le;
            le.type = robbed ? loot_type::robbed_hideout : type;
            le.name = name.empty() ? "Loot" : std::move(name);
            le.pos = pos;
            le.value = value;
            le.locked = locked;
            le.robbed = robbed;
            le.distance = camera.valid ? pos.distance(camera.pos) : 0.f;
            loot_build.push_back(std::move(le));
        };

        auto slice = [&](std::uintptr_t dict, auto&& fn, int cap)
        {
            if (taken >= kBudget) return;
            int index = 0;
            const int window = scan_skip + (kBudget - taken);
            if (window <= 0) return;
            for_each_dict(dict, [&](int key, std::uintptr_t obj)
            {
                if (index++ < scan_skip) return;
                if (taken >= kBudget) return;
                ++taken;
                fn(key, obj);
            }, (std::min)(window, cap));
        };
        auto read_vec = [&](std::uintptr_t addr, vec3& out) -> bool
        {
            if (!addr) return false;
            vec3 p{};
            if (!read_mem(addr, &p, sizeof(p)) || !p.finite() || p.zero()) return false;
            out = p;
            return true;
        };

        auto push_vehicle = [&](const char* type_name, std::uintptr_t model,
                                std::uintptr_t xf_off, std::uintptr_t vel_off,
                                std::uintptr_t role_off, std::uintptr_t desc_off,
                                std::uintptr_t siren_off)
        {
            if (vehicle_build.size() >= 128) return;
            const std::uintptr_t xf = read_ptr(model + xf_off);
            if (!obj_ptr(xf)) return;
            vec3 pos{};
            if (!read_vec(xf + off::VehicleTransformModel::PivotPosition, pos))
            {
                vec3 p{};
                if (!prim_vec3(read_ptr(xf + off::VehicleTransformModel::Position), p) || !p.finite() || p.zero())
                    return;
                pos = p;
            }
            for (const auto& existing : vehicle_build)
                if (existing.pos.distance(pos) < 2.f) return;

            vec3 vel{};
            const std::uintptr_t velm = read_ptr(model + vel_off);
            if (obj_ptr(velm))
                prim_vec3(read_ptr(velm + off::VelocityModel::Data), vel);

            vehicle_entity v;
            v.model = model;
            v.type_name = type_name;
            v.pos = pos;
            v.distance = camera.valid ? pos.distance(camera.pos) : 0.f;
            v.parked = !vel.finite() || vel.length() < 0.45f;
            if (desc_off)
            {
                std::string name = ident(read_ptr(model + desc_off));
                if (!name.empty()) v.type_name = std::string(type_name) + " " + name;
            }
            if (role_off)
            {
                const std::uintptr_t role_model = read_ptr(model + role_off);
                const std::uintptr_t data = obj_ptr(role_model)
                    ? read_ptr(role_model + il2cpp::PrimitiveModel_Value) : 0;
                if (obj_ptr(data))
                {
                    v.driver_role = role_name(prim_ref(data));
                    if (v.driver_role.empty()) v.driver_role = role_name(data);
                }
            }
            if (siren_off)
            {
                const std::uintptr_t siren = read_ptr(model + siren_off);
                bool on = false;
                if (obj_ptr(siren) && prim_bool(read_ptr(siren + off::CarSirenModel::IsEnabled), on))
                    v.siren_active = on;
            }
            vehicle_build.push_back(std::move(v));
        };

        const int taken_before = taken;
        switch (scan_stage)
        {
        case 0:
            slice(read_ptr(world_model + off::WorldEntitiesModel::LootNodes),
                [&](int, std::uintptr_t node)
                {
                    const std::uintptr_t desc = read_ptr(node + off::LootNodeModel::Description);
                    vec3 pos{};
                    if (!read_vec(desc + off::LocationLootNodeDescription::Position, pos)) return;
                    std::string name = ident(desc);
                    bool empty = false;
                    const std::uintptr_t empty_model = read_ptr(node + off::LootNodeModel::IsEmpty);
                    const std::uintptr_t empty_data = obj_ptr(empty_model)
                        ? read_ptr(empty_model + il2cpp::PrimitiveModel_Value) : 0;
                    if (obj_ptr(empty_data)) prim_bool(empty_data, empty);

                    bool locked = false;
                    const std::uintptr_t unlock = read_ptr(node + off::LootNodeModel::UnlockState);
                    const std::uintptr_t udata = obj_ptr(unlock)
                        ? read_ptr(unlock + il2cpp::PrimitiveModel_Value) : 0;
                    std::string ustate = ident(obj_ptr(udata) ? prim_ref(udata) : 0);
                    if (ustate.empty() && obj_ptr(udata)) ustate = ident(udata);
                    if (lower_has(ustate, "lock") && !lower_has(ustate, "unlock")) locked = true;

                    push_loot(kind_of(name), name, pos, 0, locked, empty);
                }, 400);
            break;
        case 1:
            slice(read_ptr(world_model + off::WorldEntitiesModel::CashLootNodes),
                [&](int, std::uintptr_t node)
                {
                    const std::uintptr_t desc = read_ptr(node + off::CashLootNodeModel::Description);
                    vec3 pos{};
                    if (!read_vec(desc + off::LocationCashLootNodeDescription::Position, pos)) return;
                    std::int32_t amount = 0;
                    prim_int(read_ptr(node + off::CashLootNodeModel::CashAmount), amount);
                    if (amount < 0) amount = 0;
                    std::string name = ident(desc);
                    if (name.empty()) name = "Cash";
                    push_loot(loot_type::cash_point, name, pos, amount, false, amount == 0);
                }, 200);
            break;
        case 2:
            slice(read_ptr(world_model + off::WorldEntitiesModel::SafeDoors),
                [&](int, std::uintptr_t door)
                {
                    const std::uintptr_t desc = read_ptr(door + off::SafeDoorModel::Description);
                    vec3 pos{};
                    if (!read_vec(desc + off::SafeDoorDescription::Position, pos)) return;
                    bool unlocked = false;
                    prim_bool(read_ptr(door + off::SafeDoorModel::IsUnlocked), unlocked);
                    std::string name = ident(desc);
                    if (name.empty()) name = "Safe";
                    push_loot(loot_type::safe, name, pos, 0, !unlocked, false);
                }, 200);
            break;
        case 3:
            slice(read_ptr(world_model + off::WorldEntitiesModel::Shelvings),
                [&](int, std::uintptr_t shelf)
                {
                    const std::uintptr_t dmodel = read_ptr(shelf + off::ShelvingModel::DescriptionModel);
                    const std::uintptr_t desc = obj_ptr(dmodel) ? read_ptr(dmodel + 0x10) : 0;
                    vec3 pos{};
                    if (!read_vec(desc + off::LocationShelvingDescription::Position, pos)) return;
                    std::string name = ident(read_ptr(desc + off::LocationShelvingDescription::ShelvingDescription));
                    if (name.empty()) name = ident(desc);
                    if (name.empty()) name = "Shelf";
                    push_loot(loot_type::shelf, name, pos, 0, false, false);
                }, 200);
            break;
        case 4:
            slice(read_ptr(world_model + off::WorldEntitiesModel::HideObjects),
                [&](int, std::uintptr_t hide)
                {
                    const std::int32_t eid = read_t<std::int32_t>(hide + off::HideObjectModel::EntityId);
                    const std::uintptr_t desc = read_ptr(hide + off::HideObjectModel::Description);
                    vec3 pos{};
                    if (!read_vec(desc + off::LocationHideObjectDescription::Position, pos)) return;
                    if (eid > 0) locker_build[eid] = pos;
                }, 200);
            break;
        case 5:
            slice(read_ptr(world_model + off::WorldEntitiesModel::Cars),
                [&](int, std::uintptr_t car)
                {
                    push_vehicle("Car", car,
                        off::CarModel::Transform, off::CarModel::LinearVelocity,
                        off::CarModel::OwnerRole, off::CarModel::Description,
                        off::CarModel::Siren);
                }, 128);
            break;
        case 6:
            slice(read_ptr(world_model + off::WorldEntitiesModel::Helicopters),
                [&](int, std::uintptr_t heli)
                {
                    push_vehicle("Helicopter", heli,
                        off::HelicopterModel::Transform, off::HelicopterModel::LinearVelocity,
                        0, off::HelicopterModel::Description, 0);
                }, 64);
            break;
        case 7:
            if (vehicle_build.empty())
            {
                slice(read_ptr(world_model + off::WorldEntitiesModel::Vehicles),
                    [&](int, std::uintptr_t veh)
                    {
                        push_vehicle("Vehicle", veh,
                            off::VehicleModel::Transform, off::VehicleModel::LinearVelocity,
                            off::VehicleModel::OwnerRole, 0, 0);
                    }, 128);
            }
            break;
        default:
            for (auto& v : vehicle_build)
            {
                int n = 0;
                float best = 8.f;
                std::string role;
                for (const auto& e : entities)
                {
                    if (e.dead || !e.pos.finite()) continue;
                    const float d = e.pos.distance(v.pos);
                    if (d > 6.f) continue;
                    ++n;
                    if (d < best && !e.role.empty())
                    {
                        best = d;
                        role = e.role;
                    }
                }
                v.passenger_count = n;
                v.has_driver = n > 0;
                v.has_passengers = n > 1;
                if (!role.empty()) v.driver_role = role;
                if (n > 0) v.parked = false;
            }
            loot.swap(loot_build);
            vehicles.swap(vehicle_build);
            loot_build.clear();
            vehicle_build.clear();
            for (auto& e : entities)
            {
                const auto it = locker_build.find(e.id);
                if (it != locker_build.end())
                {
                    e.in_locker = true;
                    e.locker_last_pos = it->second;
                }
                else
                    e.in_locker = false;
            }
            locker_build.clear();
            if (!entities.empty())
            {
                const int n = (std::min)(8, static_cast<int>(entities.size()));
                if (carry_cursor < 0 || carry_cursor >= static_cast<int>(entities.size()))
                    carry_cursor = 0;
                for (int i = 0; i < n; ++i)
                {
                    entity& e = entities[static_cast<std::size_t>((carry_cursor + i) % static_cast<int>(entities.size()))];
                    e.has_loot = false;
                    e.loot_item_name.clear();
                    e.loot_value = 0;
                    const std::uintptr_t costm = read_ptr(e.model + off::BattleCharacterModel::RobberInventoryCost);
                    if (!obj_ptr(costm)) continue;
                    const int cost = read_t<int>(costm + off::RobberInventoryCostModel::CharacterInventory);
                    if (cost > 0 && cost < 100000000)
                    {
                        e.has_loot = true;
                        e.loot_value = cost;
                        e.loot_item_name = "Carried loot";
                    }
                }
                carry_cursor = (carry_cursor + n) % static_cast<int>(entities.size());
            }
            scan_stage = 0;
            scan_skip = 0;
            return;
        }

        if (taken - taken_before >= kBudget)
            scan_skip += taken - taken_before;
        else
        {
            scan_stage++;
            scan_skip = 0;
        }
    }

    // ---------------------------------------------------------------------
    // Projection
    // ---------------------------------------------------------------------
    inline bool world::project(const vec3& point, float& screen_x, float& screen_y) const
    {
        return project_point(camera, point, screen_size.x, screen_size.y, screen_x, screen_y);
    }
}
