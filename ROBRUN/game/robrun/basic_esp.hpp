#pragma once
#include <algorithm>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "world.hpp"

// =============================================================================
// ROB RUN basic ESP.
//
//   robrun::esp esp;
//   esp.initialize();              // once, after the driver is attached
//
//   worker thread:                 render thread:
//     esp.update();                  snap = esp.make_snapshot();
//     snap = esp.make_snapshot();    esp.draw(snap);
//                                    render_menu(esp.cfg, *snap, ...);
//
// The snapshot is a plain copy of the entity list + camera + status string, so
// the render thread never blocks on a slow resolve / entity refresh. That is
// what keeps the menu from flickering while the worker is inside the DMA BFS.
// =============================================================================

namespace robrun
{
    // ---------------------------------------------------------------------
    // Settings
    // ---------------------------------------------------------------------
    struct esp_settings
    {
        bool enabled        = true;

        // Player ESP
        bool draw_box       = true;
        bool draw_health    = true;
        bool draw_name      = true;
        bool draw_role      = true;    // role label: Policeman/Robber/Prisoner/Civilian
        bool draw_stance    = true;
        bool draw_distance  = true;
        bool draw_skeleton  = true;    // bone skeleton from the game rig
        bool draw_weapon    = true;    // weapon/item in hands
        bool draw_armor     = true;    // armor indicator
        bool draw_status    = true;    // knockout / handcuffs / surrender
        bool approach_warning = true;  // distance + direction when close

        bool show_local     = false;   // draw your own character too
        bool show_dead      = false;
        bool only_players   = false;   // hide entities without a name
        bool exclude_group  = true;    // exclude group members from ESP/aim targets (best-effort)

        float max_distance  = 400.f;   // metres
        float approach_distance = 60.f; // approach warning radius (metres)
        float box_height    = 1.80f;   // standing height in metres
        float box_height_crouch = 1.15f;

        // Height source: true when the model Position is the character centre
        // (CharacterCenterPositionModel) instead of its feet. Only used when a
        // capsule/ground anchor is unavailable.
        bool position_is_center = false;

        // Bounds: use the game's CapsuleColliderParams for the box height and
        // width instead of the fixed box_height values above.
        bool use_capsule_bounds = true;

        // Place the box on the ground contact (CharacterControllerGroundModel)
        // rather than the raw position pivot.
        bool use_ground_anchor  = true;

        // Manual vertical nudge in metres, applied after the anchor.
        float box_offset    = 0.f;

        bool show_debug_status = true; // resolver diagnostics on screen

        // Colours (IM_COL32 packed 0xAABBGGRR via helpers below)
        unsigned int col_box        = 0xFFFFFFFF;
        unsigned int col_box_dead   = 0x80555555;
        unsigned int col_text       = 0xFFFFFFFF;
        unsigned int col_text_dim   = 0xFFB0B0B0;
        unsigned int col_bg         = 0x60000000;
        unsigned int col_local      = 0xFF40FF80;
        unsigned int col_skeleton   = 0xFF60D0FF;
        unsigned int col_group      = 0xFF50E0FF; // group member
        unsigned int col_police     = 0xFFFFA050; // policeman (blue-ish, ABGR)
        unsigned int col_robber     = 0xFF465AFF; // robber (red)
        unsigned int col_prisoner   = 0xFF46C8F0; // prisoner (yellow)
        unsigned int col_civilian   = 0xFF78DC5A; // civilian (green)
        unsigned int col_warn       = 0xFF40A0FF; // approach warning

        // --- Loot ESP ---
        bool loot_enabled = true;
        bool loot_robbery_points = true;  // safes, cash registers, shelves
        bool loot_cash_points = true;     // cash piles with exact amount
        bool loot_weights = true;         // gym weights
        bool loot_dropped_bags = true;
        bool loot_police_award = true;
        bool loot_safes = true;
        bool loot_robbed_hideouts = true; // already robbed points
        bool loot_show_lock_status = true;
        float loot_max_distance = 150.f;
        
        unsigned int col_loot_safe = 0xFFFFD700;       // gold
        unsigned int col_loot_register = 0xFF00FF00;   // green
        unsigned int col_loot_shelf = 0xFF9696FF;      // purple
        unsigned int col_loot_weight = 0xFFFF64FF;     // pink
        unsigned int col_loot_bag = 0xFFFFFF00;        // yellow
        unsigned int col_loot_award = 0xFFFF6464;      // blue
        unsigned int col_loot_cash = 0xFF00DC5A;       // lime
        
        // --- Vehicle ESP ---
        bool transport_enabled = true;
        bool transport_show_parked = false;
        bool transport_show_passengers = true;
        bool transport_show_siren = true;
        float transport_max_distance = 300.f;
        
        // --- Hardware Aimbot ---
        bool aim_enabled = false;           // master switch
        bool aim_hardware = true;           // use hardware device (MakCU/KMBox/Arduino)
        bool aim_triggerbot = false;        // auto-fire when on target
        bool aim_rcs = false;               // recoil control
        bool aim_ignore_group = true;       // exclude group members from targeting
        float aim_rcs_strength = 2.5f;      // extra downward pixels while firing
        float aim_fov = 120.f;              // aim FOV radius (pixels from screen center)
        float aim_smooth = 0.15f;           // smoothing factor (0.0 - 1.0)
        std::string aim_target_bone = "Head"; // "Head" or "Torso"
        std::string aim_com = "COM1";          // CH343 port (Medius / Makcu / KMBox)
        mutable char aim_link[96] = "hardware idle";
    };

    // A render-thread copy: nothing in here is touched by the worker again.
    struct esp_snapshot
    {
        std::vector<entity> entities;
        std::vector<loot_entity> loot;
        std::vector<vehicle_entity> vehicles;
        camera_state camera;
        std::string status;
        bool ready = false;
        bool resolved = false;
    };

    // ARGB helpers (ImGui uses IM_COL32 which is 0xAABBGGRR on little endian
    // only when written through the macro; these are already-packed values).
    namespace esp_col
    {
        inline unsigned int rgba(int r, int g, int b, int a = 255)
        {
            return (static_cast<unsigned int>(a) << 24)
                 | (static_cast<unsigned int>(b) << 16)
                 | (static_cast<unsigned int>(g) << 8)
                 |  static_cast<unsigned int>(r);
        }

        inline unsigned int health(float frac)
        {
            frac = (std::max)(0.f, (std::min)(1.f, frac));
            const int r = static_cast<int>(255.f * (1.f - frac));
            const int g = static_cast<int>(255.f * frac);
            return rgba(r, g, 40, 255);
        }

        inline unsigned int role(const std::string& r)
        {
            if (r == "Robber")       return rgba(255, 90, 70);
            if (r == "Cop")          return rgba(80, 160, 255);
            if (r == "Citizen")      return rgba(90, 220, 120);
            if (r == "Settler")      return rgba(220, 220, 220);
            if (r == "Prisoner")     return rgba(240, 200, 70);
            if (r == "ClanBattle")   return rgba(230, 120, 255);
            if (r == "Hubber")       return rgba(160, 130, 255);
            return rgba(255, 255, 255);
        }
    }

    inline unsigned int entity_color(const entity& e, const esp_settings& cfg)
    {
        if (e.local) return cfg.col_local;
        if (e.dead)  return cfg.col_box_dead;
        if (e.group_member) return cfg.col_group;
        if (e.role == "Cop") return cfg.col_police;
        if (e.role == "Robber") return cfg.col_robber;
        if (e.role == "Prisoner") return cfg.col_prisoner;
        if (e.role == "Citizen" || e.role == "Settler" || e.role == "Hubber")
            return cfg.col_civilian;
        if (e.role == "ClanBattle") return esp_col::rgba(230, 120, 255);
        return cfg.col_box;
    }

    inline const char* approach_dir(float dx, float dz, const camera_state& cam)
    {
        // Direction from local to target in camera-relative terms (XZ plane).
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 0.001f) return "HERE";
        const float nx = dx / len, nz = dz / len;
        float fx = 0.f, fz = 1.f, rx = 1.f, rz = 0.f;
        if (cam.valid)
        {
            fx = cam.fwd.x; fz = cam.fwd.z;
            rx = cam.right.x; rz = cam.right.z;
            const float fl = std::sqrt(fx * fx + fz * fz);
            const float rl = std::sqrt(rx * rx + rz * rz);
            if (fl > 0.001f) { fx /= fl; fz /= fl; }
            if (rl > 0.001f) { rx /= rl; rz /= rl; }
        }
        const float fwd = nx * fx + nz * fz;
        const float side = nx * rx + nz * rz;
        if (fwd > 0.5f)  return side > 0.35f ? "FR" : (side < -0.35f ? "FL" : "F");
        if (fwd < -0.5f) return side > 0.35f ? "BR" : (side < -0.35f ? "BL" : "B");
        return side >= 0.f ? "R" : "L";
    }

    // Lead the last DMA sample by velocity * age. The overlay paints several
    // times between position reads; without this the box sits, then jumps.
    // Age and distance are capped so a stale or bad velocity cannot fling it.
    inline vec3 coast_delta(const vec3& vel, double sampled_at, double now_s)
    {
        if (!(sampled_at > 0.0)) return {};
        const double age = now_s - sampled_at;
        if (!(age > 0.0) || age > 0.050) return {};
        const vec3 d = vel * static_cast<float>(age);
        if (d.dot(d) > (0.45f * 0.45f)) return {};
        return d;
    }

    // ---------------------------------------------------------------------
    // Hardware aimbot. Defined before esp: initialize()/draw() new it and
    // call update(), which needs a complete type.
    // ---------------------------------------------------------------------
    class hardware_aimbot
    {
    public:
        hardware_aimbot()
        {
            alive.store(true);
            worker = std::thread([this] { io_loop(); });
        }
        ~hardware_aimbot()
        {
            alive.store(false);
            if (worker.joinable()) worker.join();
            close_port();
        }
        hardware_aimbot(const hardware_aimbot&) = delete;
        hardware_aimbot& operator=(const hardware_aimbot&) = delete;

        // Find best target within FOV
        const entity* get_best_target(const esp_snapshot& snap, const esp_settings& cfg,
                                      float screen_width, float screen_height) const
        {
            if (!cfg.aim_enabled || snap.entities.empty())
                return nullptr;

            const float cx = screen_width * 0.5f;
            const float cy = screen_height * 0.5f;
            const float fov_sq = cfg.aim_fov * cfg.aim_fov;

            const entity* best = nullptr;
            float closest_dist_sq = fov_sq;

            for (const auto& e : snap.entities)
            {
                if (e.local) continue;
                if (e.dead) continue;
                if (cfg.aim_ignore_group && e.group_member) continue;
                if (!e.player()) continue;
                if (e.health <= 0) continue;
                if (!e.pos.finite()) continue;

                vec3 target_pos = e.pos;
                if (cfg.aim_target_bone == "Head" && e.skeleton_valid && !e.bones.empty())
                {
                    float max_y = -9999.f;
                    for (const auto& bone : e.bones)
                    {
                        if (bone.y > max_y && bone.finite() && !bone.zero())
                        {
                            max_y = bone.y;
                            target_pos = bone;
                        }
                    }
                }
                else if (cfg.aim_target_bone == "Torso")
                {
                    target_pos.y = (e.box_bottom + e.box_top) * 0.5f;
                }

                float sx = 0.f, sy = 0.f;
                if (!project_point(snap.camera, target_pos, screen_width, screen_height, sx, sy))
                    continue;

                const float dx = sx - cx;
                const float dy = sy - cy;
                const float dist_sq = dx * dx + dy * dy;

                if (dist_sq < closest_dist_sq)
                {
                    closest_dist_sq = dist_sq;
                    best = &e;
                }
            }

            return best;
        }

        // Execute aimbot logic (called from render thread)
        void update(const esp_snapshot& snap, const esp_settings& cfg,
                   float screen_width, float screen_height)
        {
            {
                std::lock_guard<std::mutex> lock(link_mu);
                std::snprintf(cfg.aim_link, sizeof(cfg.aim_link), "%s", link);
            }
            if (!cfg.aim_hardware)
            {
                hold_port.store(false);
                return;
            }
            {
                std::lock_guard<std::mutex> lock(cfg_mu);
                com_want = cfg.aim_com.empty() ? std::string("COM1") : cfg.aim_com;
            }
            hold_port.store(true);
            if (!cfg.aim_enabled || !snap.ready)
                return;

            const entity* target = get_best_target(snap, cfg, screen_width, screen_height);
            float move_x = 0.f;
            float move_y = 0.f;

            if (target)
            {
                vec3 target_pos = target->pos;
                if (cfg.aim_target_bone == "Head" && target->skeleton_valid && !target->bones.empty())
                {
                    float max_y = -9999.f;
                    for (const auto& bone : target->bones)
                    {
                        if (bone.y > max_y && bone.finite() && !bone.zero())
                        {
                            max_y = bone.y;
                            target_pos = bone;
                        }
                    }
                }
                else if (cfg.aim_target_bone == "Torso")
                {
                    target_pos.y = (target->box_bottom + target->box_top) * 0.5f;
                }

                float sx = 0.f, sy = 0.f;
                if (project_point(snap.camera, target_pos, screen_width, screen_height, sx, sy))
                {
                    const float cx = screen_width * 0.5f;
                    const float cy = screen_height * 0.5f;
                    const float dx = sx - cx;
                    const float dy = sy - cy;
                    move_x = dx * cfg.aim_smooth;
                    move_y = dy * cfg.aim_smooth;

                    if (cfg.aim_triggerbot)
                    {
                        const float dist_sq = dx * dx + dy * dy;
                        const ULONGLONG now = GetTickCount64();
                        if (dist_sq < (8.f * 8.f) && now - last_click_ms >= 90ull)
                        {
                            last_click_ms = now;
                            pend_click.store(true);
                        }
                    }
                }
            }

            if (cfg.aim_rcs && (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
                move_y += cfg.aim_rcs_strength;

            if (std::abs(move_x) > 0.5f || std::abs(move_y) > 0.5f)
            {
                const int ix = (std::max)(-32767, (std::min)(32767, static_cast<int>(move_x)));
                const int iy = (std::max)(-32767, (std::min)(32767, static_cast<int>(move_y)));
                const int nx = pend_dx.fetch_add(ix) + ix;
                const int ny = pend_dy.fetch_add(iy) + iy;
                if (nx > 32767) pend_dx.store(32767);
                if (nx < -32767) pend_dx.store(-32767);
                if (ny > 32767) pend_dy.store(32767);
                if (ny < -32767) pend_dy.store(-32767);
            }
        }

    private:
        enum class link_kind { none, medius, km };

        std::atomic<bool> alive{ false };
        std::atomic<bool> hold_port{ false };
        std::atomic<int> pend_dx{ 0 };
        std::atomic<int> pend_dy{ 0 };
        std::atomic<bool> pend_click{ false };
        std::mutex cfg_mu;
        std::string com_want = "COM1";
        std::mutex link_mu;
        char link[96] = "hardware idle";
        std::thread worker;
        ULONGLONG last_click_ms = 0;

        HANDLE port = INVALID_HANDLE_VALUE;
        link_kind kind = link_kind::none;
        std::string open_com;
        std::uint8_t seq = 1;
        ULONGLONG next_try = 0;

        static std::uint16_t crc16(const std::uint8_t* data, size_t len)
        {
            unsigned crc = 0xFFFF;
            for (size_t i = 0; i < len; ++i)
            {
                crc ^= static_cast<unsigned>(data[i]) << 8;
                for (int b = 0; b < 8; ++b)
                    crc = (crc & 0x8000u) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
            }
            return static_cast<std::uint16_t>(crc);
        }

        void set_link(const char* text)
        {
            std::lock_guard<std::mutex> lock(link_mu);
            std::snprintf(link, sizeof(link), "%s", text ? text : "");
        }

        void close_port()
        {
            if (port != INVALID_HANDLE_VALUE)
            {
                CloseHandle(port);
                port = INVALID_HANDLE_VALUE;
            }
            kind = link_kind::none;
            open_com.clear();
        }

        void set_timeouts(DWORD total_ms)
        {
            if (port == INVALID_HANDLE_VALUE) return;
            COMMTIMEOUTS t{};
            if (total_ms == 0)
            {
                t.ReadIntervalTimeout = MAXDWORD;
                t.ReadTotalTimeoutMultiplier = 0;
                t.ReadTotalTimeoutConstant = 0;
            }
            else
            {
                t.ReadIntervalTimeout = 10;
                t.ReadTotalTimeoutConstant = total_ms;
            }
            t.WriteTotalTimeoutConstant = 20;
            SetCommTimeouts(port, &t);
        }

        bool open_baud(const std::string& com, DWORD baud)
        {
            close_port();
            std::string path = com;
            if (path.rfind("\\\\.\\", 0) != 0)
                path = "\\\\.\\" + path;
            port = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (port == INVALID_HANDLE_VALUE)
                return false;
            DCB dcb{};
            dcb.DCBlength = sizeof(dcb);
            if (!GetCommState(port, &dcb))
            {
                close_port();
                return false;
            }
            dcb.BaudRate = baud;
            dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;
            dcb.StopBits = ONESTOPBIT;
            dcb.fBinary = TRUE;
            dcb.fAbortOnError = FALSE;
            dcb.fDtrControl = DTR_CONTROL_ENABLE;
            dcb.fRtsControl = RTS_CONTROL_ENABLE;
            if (!SetCommState(port, &dcb))
            {
                close_port();
                return false;
            }
            EscapeCommFunction(port, SETDTR);
            EscapeCommFunction(port, SETRTS);
            PurgeComm(port, PURGE_RXCLEAR | PURGE_TXCLEAR);
            open_com = com;
            return true;
        }

        bool write_raw(const void* data, DWORD n)
        {
            if (port == INVALID_HANDLE_VALUE || !data || !n) return false;
            DWORD wrote = 0;
            if (!WriteFile(port, data, n, &wrote, nullptr) || wrote != n)
            {
                close_port();
                next_try = GetTickCount64() + 1500ull;
                return false;
            }
            return true;
        }

        bool write_medius(std::uint8_t type, const std::uint8_t* payload, size_t plen)
        {
            std::uint8_t frame[24]{};
            if (plen + 7 > sizeof(frame)) return false;
            frame[0] = 0xA5;
            frame[1] = type;
            frame[2] = seq++;
            if (seq == 0) seq = 1;
            frame[3] = static_cast<std::uint8_t>(plen & 0xFF);
            frame[4] = static_cast<std::uint8_t>((plen >> 8) & 0xFF);
            if (plen) std::memcpy(frame + 5, payload, plen);
            const std::uint16_t c = crc16(frame + 1, 4 + plen);
            frame[5 + plen] = static_cast<std::uint8_t>(c & 0xFF);
            frame[6 + plen] = static_cast<std::uint8_t>(c >> 8);
            return write_raw(frame, static_cast<DWORD>(7 + plen));
        }

        std::string read_for(int ms)
        {
            set_timeouts(20);
            std::string buf;
            const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(ms);
            while (GetTickCount64() < deadline && buf.size() < 256)
            {
                char chunk[64];
                DWORD got = 0;
                if (ReadFile(port, chunk, sizeof(chunk), &got, nullptr) && got)
                    buf.append(chunk, chunk + got);
            }
            return buf;
        }

        bool try_medius(const std::string& com)
        {
            if (!open_baud(com, 6000000u))
                return false;
            const std::uint8_t qv[1] = { 0x00 };
            if (!write_medius(0x05, qv, 1))
                return false;
            const std::string rx = read_for(400);
            if (rx.find("Medius") == std::string::npos)
            {
                close_port();
                return false;
            }
            kind = link_kind::medius;
            set_timeouts(0);
            char msg[96];
            std::snprintf(msg, sizeof(msg), "Medius %s @ 6000000", com.c_str());
            set_link(msg);
            ROBRUN_LOG("[robrun] aim " << msg);
            return true;
        }

        bool try_km(const std::string& com)
        {
            if (!open_baud(com, 115200u))
                return false;
            const char probe[] = ".version()\r\nkm.version()\r\n";
            if (!write_raw(probe, sizeof(probe) - 1))
                return false;
            const std::string rx = read_for(400);
            if (rx.find(">>>") == std::string::npos)
            {
                close_port();
                return false;
            }
            kind = link_kind::km;
            set_timeouts(0);
            char msg[96];
            std::snprintf(msg, sizeof(msg), "km.move %s @ 115200", com.c_str());
            set_link(msg);
            ROBRUN_LOG("[robrun] aim " << msg);
            return true;
        }

        void drain()
        {
            if (port == INVALID_HANDLE_VALUE) return;
            char scratch[256];
            for (int i = 0; i < 4; ++i)
            {
                DWORD got = 0;
                if (!ReadFile(port, scratch, sizeof(scratch), &got, nullptr) || got == 0)
                    break;
            }
        }

        void send_move(int dx, int dy)
        {
            dx = (std::max)(-32767, (std::min)(32767, dx));
            dy = (std::max)(-32767, (std::min)(32767, dy));
            if (dx == 0 && dy == 0) return;
            if (kind == link_kind::medius)
            {
                std::uint8_t payload[6] = {
                    0x00,
                    static_cast<std::uint8_t>(dx & 0xFF),
                    static_cast<std::uint8_t>((dx >> 8) & 0xFF),
                    static_cast<std::uint8_t>(dy & 0xFF),
                    static_cast<std::uint8_t>((dy >> 8) & 0xFF),
                    0x00
                };
                (void)write_medius(0x01, payload, 6);
            }
            else if (kind == link_kind::km)
            {
                char line[48];
                const int n = std::snprintf(line, sizeof(line), "km.move(%d, %d)\r\n", dx, dy);
                if (n > 0) (void)write_raw(line, static_cast<DWORD>(n));
            }
        }

        void send_click()
        {
            if (kind == link_kind::medius)
            {
                const std::uint8_t down[4] = { 0x00, 0x00, 0x00, 0x01 };
                const std::uint8_t up[4] = { 0x00, 0x00, 0x00, 0x00 };
                (void)write_medius(0x03, down, 4);
                Sleep(12);
                (void)write_medius(0x03, up, 4);
            }
            else if (kind == link_kind::km)
            {
                const char down[] = "km.left(1)\r\n";
                const char up[] = "km.left(0)\r\n";
                (void)write_raw(down, sizeof(down) - 1);
                Sleep(12);
                (void)write_raw(up, sizeof(up) - 1);
            }
        }

        void io_loop()
        {
            while (alive.load())
            {
                if (!hold_port.load())
                {
                    if (port != INVALID_HANDLE_VALUE)
                    {
                        close_port();
                        set_link("hardware off");
                    }
                    Sleep(40);
                    continue;
                }

                std::string want;
                {
                    std::lock_guard<std::mutex> lock(cfg_mu);
                    want = com_want;
                }
                if (port == INVALID_HANDLE_VALUE || want != open_com)
                {
                    const ULONGLONG now = GetTickCount64();
                    if (now < next_try)
                    {
                        Sleep(40);
                        continue;
                    }
                    if (!try_medius(want) && !try_km(want))
                    {
                        char msg[96];
                        std::snprintf(msg, sizeof(msg), "no reply on %s", want.c_str());
                        set_link(msg);
                        close_port();
                        next_try = GetTickCount64() + 2000ull;
                    }
                }

                if (port != INVALID_HANDLE_VALUE)
                {
                    const int dx = pend_dx.exchange(0);
                    const int dy = pend_dy.exchange(0);
                    const bool click = pend_click.exchange(false);
                    if (dx || dy) send_move(dx, dy);
                    if (click) send_click();
                    drain();
                }
                Sleep(2);
            }
        }
    };

    // ---------------------------------------------------------------------
    // ESP
    // ---------------------------------------------------------------------
    class esp
    {
    public:
        esp_settings cfg;
        world w;
        hardware_aimbot* aimbot = nullptr;

        bool initialized = false;
        ULONGLONG last_status_log = 0;
        ULONGLONG hz_tick = 0;
        unsigned hz_count = 0;

        void initialize()
        {
            initialized = true;
            w.cfg.verbose = true;
            if (!aimbot) aimbot = new hardware_aimbot();
            w.resolve(true);
        }

        ~esp()
        {
            if (aimbot)
            {
                delete aimbot;
                aimbot = nullptr;
            }
        }

        // Resolve handles + refresh models. Cheap after the first resolve.
        bool update()
        {
            if (!initialized) initialized = true;
            // Set before the refresh so the chain builder knows whether the
            // extra skeleton pointers/reads should be resolved this tick.
            w.want_skeleton = cfg.draw_skeleton;
            const bool ok = w.update();

            // Measured worker update rate (this is the real ESP refresh rate,
            // not the overlay's Present() FPS).
            ++hz_count;
            const ULONGLONG now = GetTickCount64();
            if (!hz_tick) hz_tick = now;
            else if (now - hz_tick >= 1000)
            {
                w.esp_hz = hz_count * 1000.0 / static_cast<double>(now - hz_tick);
                hz_count = 0;
                hz_tick = now;
            }

            // Throttled diagnostics: the renderer shows this in the menu, but
            // the log is the only way to see it from the worker thread.
            if (now - last_status_log >= 3000)
            {
                last_status_log = now;
                log_status();
            }
            return ok;
        }

        // Bone reads. The worker publishes the position snapshot first, then
        // calls this, so a skeleton scatter cannot hold the boxes still.
        void refresh_visuals()
        {
            if (!initialized) return;
            w.want_skeleton = cfg.draw_skeleton;
            if (cfg.draw_skeleton)
                w.refresh_skeleton();
            // After the bone scatter. A few objects per tick, never the whole map.
            w.scan_world_objects();
        }

        void log_status()
        {
            if (!w.ready()) return;

            auto dict_count = [](std::uintptr_t d) -> std::int32_t
            {
                return d ? read_t<std::int32_t>(d + 0x20) : 0; // Dictionary._count
            };

            const std::uintptr_t wd = w.world_model
                ? read_ptr(w.world_model + off::WorldEntitiesModel::BattleCharacters) : 0;
            const std::uintptr_t nd = w.world_model
                ? read_ptr(w.world_model + off::WorldEntitiesModel::NotSpawnedBattleCharacters) : 0;
            const std::uintptr_t bd = w.battle_entities
                ? read_ptr(w.battle_entities + off::BattleEntitiesModel::Models) : 0;
            const std::uintptr_t bmw = w.battle_model
                ? read_ptr(w.battle_model + off::BattleModel::WorldEntitiesModel) : 0;
            const std::uintptr_t bwd = bmw
                ? read_ptr(bmw + off::WorldEntitiesModel::BattleCharacters) : 0;

            int pos_bad = 0, bounds_ok = 0, ground_ok = 0, local_seen = 0;
            int skel_ok = 0, bones_total = 0;
            for (const auto& e : w.entities)
            {
                if (!e.pos.finite() || e.pos.zero()) ++pos_bad;
                if (e.box_bounds_valid) ++bounds_ok;
                if (e.ground_found) ++ground_ok;
                if (e.local) ++local_seen;
                if (e.skeleton_valid)
                {
                    ++skel_ok;
                    bones_total += e.bone_count;
                }
            }

            static std::uint64_t s_prev_req = 0, s_prev_runs = 0;
            static ULONGLONG s_prev_tick = 0;
            const ULONGLONG now_tick = GetTickCount64();
            const double span = s_prev_tick ? (now_tick - s_prev_tick) / 1000.0 : 3.0;
            const std::uint64_t req = w.scatter_requests();
            const std::uint64_t runs = w.scatter_runs();
            const double req_s = span > 0.0 ? (req - s_prev_req) / span : 0.0;
            const double runs_s = span > 0.0 ? (runs - s_prev_runs) / span : 0.0;
            s_prev_req = req; s_prev_runs = runs; s_prev_tick = now_tick;

            const double avg_ms = w.update_count
                ? w.update_ms_sum / static_cast<double>(w.update_count) : 0.0;

            ROBRUN_LOG("[robrun] perf esp_hz=" << static_cast<int>(w.esp_hz)
                    << " update_ms=" << w.update_ms
                    << " avg_ms=" << avg_ms
                    << " peak_ms=" << w.update_ms_peak
                    << " resolve_ms=" << w.resolve_ms
                    << " cam_ms=" << w.camera_ms
                    << " ent_ms=" << w.entities_ms
                    << " ent_sc_ms=" << w.ent_scatter_ms
                    << " ent_db_ms=" << w.ent_db_ms
                    << " ent_skel_ms=" << w.ent_skel_ms
                    << " ent_build_ms=" << w.ent_build_ms
                    << " posFb=" << w.ent_pos_fallbacks
                    << " posBox=" << w.ent_chain_posbox
                    << " posModel=" << w.ent_chain_posmodel
                    << " txBox=" << w.ent_chain_tx
                    << " posOk=" << w.ent_scatter_pos_ok
                    << " ent_req=" << w.ent_requests
                    << " scatter_req/s=" << static_cast<int>(req_s)
                    << " scatter_runs/s=" << static_cast<int>(runs_s)
                    << " posBad=" << pos_bad << " boundsOk=" << bounds_ok
                    << " groundOk=" << ground_ok << " local=" << local_seen
                    << " skelOk=" << skel_ok << " bones=" << bones_total
                    << " skelFar=" << w.ent_skel_far);
            w.update_ms_peak = 0.0;
            w.update_ms_sum = 0.0;
            w.update_count = 0;

            ROBRUN_LOG("[robrun] status entities=" << w.entities.size()
                    << " worldMapped=" << w.mapped_from_world
                    << " battleMapped=" << w.mapped_from_battle
                    << " modelWorldChars=" << dict_count(bwd)
                    << " dict=" << dict_count(wd)
                    << " notSpawned=" << dict_count(nd)
                    << " battleDict=" << dict_count(bd)
                    << " camera=" << (w.camera.valid ? (w.camera.from_brain ? "brain" : "battle") : "none")
                    << " fov=" << w.camera.fov
                    << " id=" << w.local_id
                    << " local=0x" << std::hex << w.local_model
                    << " world=0x" << w.world_model
                    << " battle=0x" << w.battle_entities
                    << " model=0x" << w.battle_model
                    << " updater=0x" << w.battle_updater << std::dec
                    << " rej=" << w.ent_chain_rejects << " miss=" << w.ent_pos_misses);

            for (const auto& e : w.entities)
            {
                if (!e.local) continue;
                ROBRUN_LOG("[robrun] local bounds pos=(" << e.pos.x << "," << e.pos.y
                        << "," << e.pos.z << ") ground=" << (e.ground_found ? 1 : 0)
                        << " anchored=" << (e.box_anchored ? 1 : 0)
                        << " bottom=" << e.box_bottom << " top=" << e.box_top
                        << " h=" << e.height << " w=" << e.width
                        << " center=" << (e.pos_is_center ? 1 : 0)
                        << " " << w.describe_chain(e.model));
                break;
            }

            // A few remote entities with the raw capsule values. This is how
            // the box anchor / capsule height semantics get verified against
            // the live game.
            int dumped = 0;
            for (const auto& e : w.entities)
            {
                if (e.local || dumped >= 4) continue;
                ROBRUN_LOG("[robrun] boxent pos=(" << e.pos.x << "," << e.pos.y
                        << "," << e.pos.z << ") gfound=" << (e.ground_found ? 1 : 0)
                        << " anchored=" << (e.box_anchored ? 1 : 0)
                        << " capR=" << e.cap_radius << " capL=" << e.cap_length
                        << " gap=" << e.cap_ground_gap
                        << " h=" << e.height << " w=" << e.width
                        << " bottom=" << e.box_bottom << " top=" << e.box_top
                        << " crouch=" << (e.crouching ? 1 : 0)
                        << " center=" << (e.pos_is_center ? 1 : 0)
                        << " " << w.describe_chain(e.model));
                ++dumped;
            }
        }

        // Worker-side: called when the menu asks for a fresh search.
        void request_resolve()
        {
            w.resolve(true);
        }

        // Copy everything the renderer needs. Safe to call from the worker.
        esp_snapshot make_snapshot() const
        {
            esp_snapshot s;
            s.entities = w.entities;
            s.loot = w.loot;
            s.vehicles = w.vehicles;
            s.camera = w.camera;
            s.status = status_text();
            s.ready = w.ready();
            s.resolved = w.diag.resolved;
            return s;
        }

        // Resolver/build status, for the menu and the debug overlay.
        std::string status_text() const
        {
            char buf[768];
            if (w.ready())
            {
                std::snprintf(buf, sizeof(buf),
                    "world=0x%llX battle=0x%llX model=0x%llX updater=0x%llX\n"
                    "entities=%zu local=0x%llX id=%d  camera=%s fov=%.1f",
                    static_cast<unsigned long long>(w.world_model),
                    static_cast<unsigned long long>(w.battle_entities),
                    static_cast<unsigned long long>(w.battle_model),
                    static_cast<unsigned long long>(w.battle_updater),
                    w.entities.size(),
                    static_cast<unsigned long long>(w.local_model),
                    w.local_id,
                    w.camera.valid ? (w.camera.from_brain ? "brain" : "battle") : "none",
                    w.camera.fov);
            }
            else
            {
                std::snprintf(buf, sizeof(buf),
                    "not resolved  fails=%u  nodes=%u  classes=%u\n"
                    "roots=%u/%u  found=%s  root=%s  heap=%u%s",
                    w.diag.fails, w.diag.nodes, w.diag.klasses,
                    w.diag.roots_valid, w.diag.roots_total,
                    w.diag.found.empty() ? "-" : w.diag.found.c_str(),
                    w.diag.root.empty() ? "-" : w.diag.root.c_str(),
                    w.diag.heap_scans,
                    w.game_cached() ? " (game cached)" : "");
            }
            return buf;
        }

        // ------------------------------------------------------------------
        // Drawing (render thread, from a snapshot)
        // ------------------------------------------------------------------
        void draw(const esp_snapshot& s) const
        {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            if (!dl) return;

            const double now_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            camera_state cam = s.camera;
            cam.pos += coast_delta(cam.velocity, cam.sampled_at, now_s);

            const ImFont* font = ImGui::GetFont();
            const float base_font = ImGui::GetFontSize() > 0.f ? ImGui::GetFontSize() : 13.f;

            // Local position for approach warnings / direction.
            const entity* local_e = nullptr;
            if (s.ready)
            {
                for (const auto& it : s.entities)
                    if (it.local) { local_e = &it; break; }
            }

            if (cfg.enabled && s.ready)
            {
                for (const auto& e : s.entities)
                {
                    if (e.local && !cfg.show_local) continue;
                    if (e.dead && !cfg.show_dead) continue;
                    if (e.group_member && cfg.exclude_group && !cfg.show_local) continue;
                    if (cfg.only_players && !e.player()) continue;
                    if (cfg.max_distance > 0.f && e.distance > cfg.max_distance) continue;
                    if (!e.pos.finite()) continue;

                    const vec3 lead = coast_delta(e.velocity, e.sampled_at, now_s);

                    // Resolve the world-space vertical extent. The game's
                    // capsule + ground contact gives the real character
                    // bounds; the config heights are only a fallback.
                    const float config_h = e.crouching ? cfg.box_height_crouch : cfg.box_height;
                    const float fallback_h = (cfg.use_capsule_bounds && e.height > 0.f)
                        ? e.height : config_h;
                    const bool capsule_bounds = cfg.use_capsule_bounds && e.box_bounds_valid;
                    const bool center_pivot = cfg.position_is_center || e.pos_is_center;

                    float w_bottom = e.pos.y;
                    float w_top = w_bottom + fallback_h;
                    if (capsule_bounds)
                    {
                        const float bh = (std::max)(0.4f, e.box_top - e.box_bottom);
                        if (cfg.use_ground_anchor && e.box_anchored)
                        {
                            w_bottom = e.box_bottom;
                        }
                        else if (center_pivot)
                        {
                            // Raw pivot is the capsule centre: recentre.
                            w_bottom = e.pos.y - bh * 0.5f;
                        }
                        w_top = w_bottom + bh;
                    }
                    else if (center_pivot)
                    {
                        w_bottom = e.pos.y - fallback_h * 0.5f;
                        w_top = w_bottom + fallback_h;
                    }
                    w_bottom += cfg.box_offset + lead.y;
                    w_top += cfg.box_offset + lead.y;

                    const vec3 feet(e.pos.x + lead.x, w_bottom, e.pos.z + lead.z);
                    const vec3 head(e.pos.x + lead.x, w_top, e.pos.z + lead.z);

                    float fx = 0.f, fy = 0.f, hx = 0.f, hy = 0.f;
                    if (!project_point(cam, feet, screen_size.x, screen_size.y, fx, fy)) continue;
                    if (!project_point(cam, head, screen_size.x, screen_size.y, hx, hy)) continue;

                    const float box_h = fy - hy;
                    if (box_h < 3.f || box_h > 4000.f) continue;
                    float box_w = box_h * 0.45f;
                    if (cfg.use_capsule_bounds && e.height > 0.f && e.width > 0.f)
                        box_w = box_h * (e.width / e.height);
                    box_w = (std::max)(2.f, (std::min)(box_h * 1.5f, box_w));
                    const float left = fx - box_w * 0.5f;
                    const float right = fx + box_w * 0.5f;
                    const float top = hy;
                    const float bottom = fy;

                    const unsigned int role_col = esp_col::role(e.role);
                    const unsigned int col = entity_color(e, cfg);

                    // ---- box -------------------------------------------
                    if (cfg.draw_box)
                    {
                        dl->AddRect(ImVec2(left, top), ImVec2(right, bottom),
                                    col, 0.f, 0, 1.4f);
                    }

                    // ---- skeleton --------------------------------------
                    // Bone world positions are computed on the worker from the
                    // game rig (see world::compute_skeleton), so the renderer
                    // only projects and joins them.
                    if (cfg.draw_skeleton && e.skeleton_valid && e.bone_count > 1)
                    {
                        const unsigned int scol = e.group_member ? cfg.col_group
                                                          : (e.local ? cfg.col_local : cfg.col_skeleton);
                        const int bc = (std::min)(e.bone_count,
                                                  static_cast<int>(e.bones.size()));
                        for (int bi = 0; bi < bc; ++bi)
                        {
                            const int par = (bi < static_cast<int>(e.bone_parent.size()))
                                ? e.bone_parent[static_cast<std::size_t>(bi)] : -2;
                            // Draw bone-to-bone only. Connecting roots/unlinked bones
                            // to an artificial anchor creates a center spike line.
                            if (par < 0 || par >= bc || par == bi) continue;

                            const vec3 child = e.bones[static_cast<std::size_t>(bi)] + lead;
                            const vec3 parent_world = e.bones[static_cast<std::size_t>(par)] + lead;
                            if (!child.finite() || child.zero() ||
                                !parent_world.finite() || parent_world.zero())
                                continue;

                            float cx = 0.f, cy = 0.f, px = 0.f, py = 0.f;
                            if (!project_point(cam, child, screen_size.x, screen_size.y, cx, cy))
                                continue;
                            if (!project_point(cam, parent_world, screen_size.x, screen_size.y, px, py))
                                continue;
                            dl->AddLine(ImVec2(px, py), ImVec2(cx, cy), scol, 1.5f);
                        }
                    }

                    // ---- health bar ------------------------------------
                    if (cfg.draw_health && e.max_health > 0)
                    {
                        const float frac = (std::max)(0.f, (std::min)(1.f,
                            static_cast<float>(e.health) / static_cast<float>(e.max_health)));
                        const float bw = (std::max)(2.f, box_w * 0.14f);
                        const float bx = left - bw - 3.f;
                        dl->AddRectFilled(ImVec2(bx - 1.f, top - 1.f),
                                          ImVec2(bx + bw + 1.f, bottom + 1.f),
                                          esp_col::rgba(0, 0, 0, 170));
                        const float h_top = bottom - box_h * frac;
                        dl->AddRectFilled(ImVec2(bx, h_top), ImVec2(bx + bw, bottom),
                                          esp_col::health(frac));
                    }

                    // ---- labels ----------------------------------------
                    const float label_size = (std::max)(11.f, (std::min)(base_font, box_h * 0.30f));
                    float y = top - 2.f;

                    if (cfg.draw_name && !e.name.empty())
                    {
                        y -= label_size + 2.f;
                        draw_centered(dl, font, label_size, fx, y, cfg.col_text, e.name.c_str());
                    }

                    if (cfg.draw_role)
                    {
                        const std::string& label = !e.role_label.empty() ? e.role_label : e.role;
                        if (!label.empty())
                        {
                            std::string line = label;
                            if (cfg.draw_stance && !e.stance.empty())
                                line += " / " + e.stance;
                            y -= label_size * 0.95f;
                            draw_centered(dl, font, label_size * 0.9f, fx, y, role_col, line.c_str());
                        }
                    }

                    if (cfg.draw_distance)
                    {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "%.0fm", e.distance);
                        dl->AddText(font, label_size * 0.85f,
                                    ImVec2(right + 3.f, bottom - label_size),
                                    cfg.col_text_dim, buf);
                    }

                    // ---- bottom tags (weapon / armor / knockout / cuffs / loot) ----
                    float by = bottom + 2.f;
                    if (cfg.draw_weapon && !e.weapon_name.empty())
                    {
                        draw_centered(dl, font, label_size * 0.85f, fx, by, cfg.col_text_dim, e.weapon_name.c_str());
                        by += label_size * 0.9f;
                    }
                    
                    // Portable loot carried by player
                    if (cfg.loot_enabled && e.has_loot && !e.loot_item_name.empty())
                    {
                        char loot_buf[64];
                        if (e.loot_value > 0)
                            std::snprintf(loot_buf, sizeof(loot_buf), "💰 %s ($%d)", e.loot_item_name.c_str(), e.loot_value);
                        else
                            std::snprintf(loot_buf, sizeof(loot_buf), "💰 %s", e.loot_item_name.c_str());
                        draw_centered(dl, font, label_size * 0.85f, fx, by, cfg.col_loot_bag, loot_buf);
                        by += label_size * 0.9f;
                    }
                    
                    if (cfg.draw_status || cfg.draw_armor)
                    {
                        std::string st;
                        if (cfg.draw_armor && e.has_armor) st += "[ARMOR] ";
                        if (cfg.draw_status)
                        {
                            if (e.handcuffed) st += "[CUFFED] ";
                            else if (e.handcuffing) st += "[CUFFING] ";
                            if (e.knocked_out) st += "[KNOCKED] ";
                            if (e.surrendered) st += "[SURRENDER] ";
                        }
                        if (!st.empty())
                        {
                            if (!st.empty() && st.back() == ' ') st.pop_back();
                            draw_centered(dl, font, label_size * 0.85f, fx, by,
                                          esp_col::rgba(255, 210, 80), st.c_str());
                            by += label_size * 0.9f;
                        }
                    }
                    
                    // Locker position marker
                    if (cfg.approach_warning && e.in_locker && e.locker_last_pos.finite() && !e.locker_last_pos.zero())
                    {
                        float lx = 0.f, ly = 0.f;
                        if (project_point(cam, e.locker_last_pos, screen_size.x, screen_size.y, lx, ly))
                        {
                            dl->AddCircleFilled(ImVec2(lx, ly), 8.f, esp_col::rgba(255, 165, 0, 180));
                            char locker_buf[64];
                            std::snprintf(locker_buf, sizeof(locker_buf), "%s [LOCKER]", e.name.c_str());
                            draw_centered(dl, font, label_size * 0.9f, lx, ly - 12.f, esp_col::rgba(255, 165, 0), locker_buf);
                        }
                    }

                    // ---- approach warning -----------------------------------
                    if (cfg.approach_warning && !e.local && !e.dead
                        && local_e && e.distance > 0.f && e.distance <= cfg.approach_distance)
                    {
                        char abuf[64];
                        const vec3 local_lead = coast_delta(local_e->velocity, local_e->sampled_at, now_s);
                        const char* dir = approach_dir(
                            (e.pos.x + lead.x) - (local_e->pos.x + local_lead.x),
                            (e.pos.z + lead.z) - (local_e->pos.z + local_lead.z), cam);
                        std::snprintf(abuf, sizeof(abuf), "APPROACH %.0fm %s", e.distance, dir);
                        draw_centered(dl, font, label_size * 0.95f, fx, by, cfg.col_warn, abuf);
                    }

                    // ---- status dots -----------------------------------
                    if (e.sprinting || e.armed)
                    {
                        const float r = (std::max)(1.5f, box_w * 0.09f);
                        const ImVec2 c(left + r + 1.f, bottom - r - 1.f);
                        dl->AddCircleFilled(c, r, e.armed ? esp_col::rgba(255, 200, 60)
                                                          : esp_col::rgba(120, 220, 255));
                    }
                }
            }

            // ---- Loot ESP -----------------------------------------------
            if (cfg.loot_enabled && s.ready)
            {
                for (const auto& loot : s.loot)
                {
                    if (cfg.loot_max_distance > 0.f && loot.distance > cfg.loot_max_distance)
                        continue;
                    if (!loot.pos.finite())
                        continue;
                    
                    // Filter by type
                    bool should_draw = false;
                    unsigned int loot_color = cfg.col_loot_safe;
                    
                    switch (loot.type)
                    {
                    case loot_type::safe:
                        should_draw = cfg.loot_safes;
                        loot_color = cfg.col_loot_safe;
                        break;
                    case loot_type::cash_register:
                        should_draw = cfg.loot_robbery_points;
                        loot_color = cfg.col_loot_register;
                        break;
                    case loot_type::shelf:
                        should_draw = cfg.loot_robbery_points;
                        loot_color = cfg.col_loot_shelf;
                        break;
                    case loot_type::weight:
                        should_draw = cfg.loot_weights;
                        loot_color = cfg.col_loot_weight;
                        break;
                    case loot_type::dropped_bag:
                        should_draw = cfg.loot_dropped_bags;
                        loot_color = cfg.col_loot_bag;
                        break;
                    case loot_type::police_award_box:
                        should_draw = cfg.loot_police_award;
                        loot_color = cfg.col_loot_award;
                        break;
                    case loot_type::cash_point:
                        should_draw = cfg.loot_cash_points;
                        loot_color = cfg.col_loot_cash;
                        break;
                    case loot_type::robbed_hideout:
                        should_draw = cfg.loot_robbed_hideouts;
                        loot_color = esp_col::rgba(100, 100, 100, 200);
                        break;
                    default:
                        break;
                    }
                    
                    if (!should_draw)
                        continue;
                    
                    float lx = 0.f, ly = 0.f;
                    if (!project_point(cam, loot.pos, screen_size.x, screen_size.y, lx, ly))
                        continue;
                    
                    // Draw loot marker
                    char loot_buf[128];
                    std::string lock_str = "";
                    if (cfg.loot_show_lock_status && loot.locked)
                        lock_str = " [LOCKED]";
                    else if (loot.robbed)
                        lock_str = " [ROBBED]";
                    
                    if (loot.value > 0)
                        std::snprintf(loot_buf, sizeof(loot_buf), "%s $%d [%.0fm]%s",
                                     loot.name.c_str(), loot.value, loot.distance, lock_str.c_str());
                    else
                        std::snprintf(loot_buf, sizeof(loot_buf), "%s [%.0fm]%s",
                                     loot.name.c_str(), loot.distance, lock_str.c_str());
                    
                    const float loot_size = (std::max)(10.f, (std::min)(13.f, base_font * 0.85f));
                    draw_centered(dl, font, loot_size, lx, ly - 8.f, loot_color, loot_buf);
                    
                    // Small indicator circle
                    dl->AddCircleFilled(ImVec2(lx, ly), 3.f, loot_color);
                }
            }

            // ---- Vehicle ESP --------------------------------------------
            if (cfg.transport_enabled && s.ready)
            {
                for (const auto& veh : s.vehicles)
                {
                    if (!cfg.transport_show_parked && veh.parked)
                        continue;
                    if (cfg.transport_max_distance > 0.f && veh.distance > cfg.transport_max_distance)
                        continue;
                    if (!veh.pos.finite())
                        continue;
                    
                    float vx = 0.f, vy = 0.f;
                    if (!project_point(cam, veh.pos, screen_size.x, screen_size.y, vx, vy))
                        continue;
                    
                    // Color based on driver role
                    unsigned int veh_color = cfg.col_box;
                    if (veh.driver_role == "Cop" || veh.driver_role == "Police")
                        veh_color = cfg.col_police;
                    else if (veh.driver_role == "Robber")
                        veh_color = cfg.col_robber;
                    else if (veh.driver_role == "Prisoner")
                        veh_color = cfg.col_prisoner;
                    else if (veh.driver_role == "Citizen" || veh.driver_role == "Settler"
                             || veh.driver_role == "Hubber")
                        veh_color = cfg.col_civilian;
                    
                    char veh_buf[128];
                    std::string siren_str = "";
                    if (cfg.transport_show_siren && veh.siren_active)
                        siren_str = " [SIREN]";
                    
                    if (cfg.transport_show_passengers && veh.passenger_count > 0)
                        std::snprintf(veh_buf, sizeof(veh_buf), "%s (%d) [%.0fm]%s",
                                     veh.type_name.c_str(), veh.passenger_count, veh.distance, siren_str.c_str());
                    else
                        std::snprintf(veh_buf, sizeof(veh_buf), "%s [%.0fm]%s",
                                     veh.type_name.c_str(), veh.distance, siren_str.c_str());
                    
                    const float veh_size = (std::max)(10.f, (std::min)(12.f, base_font * 0.80f));
                    draw_centered(dl, font, veh_size, vx, vy, veh_color, veh_buf);
                }
            }

            if (cfg.show_debug_status && (!s.resolved || s.entities.empty()))
            {
                dl->AddText(ImVec2(20.f, 20.f), esp_col::rgba(255, 120, 120), s.status.c_str());
            }

            if (aimbot)
                aimbot->update(s, cfg, screen_size.x, screen_size.y);
        }

        // ------------------------------------------------------------------
        // The visible menu lives in overlay/menu.cpp (styled shell). This class
        // only owns settings + the snapshot draw path.
        // ------------------------------------------------------------------

    private:
        static void draw_centered(ImDrawList* dl, const ImFont* font, float size,
                                  float cx, float y, unsigned int col, const char* text)
        {
            const ImVec2 sz = font->CalcTextSizeA(size, FLT_MAX, 0.f, text);
            dl->AddText(font, size, ImVec2(cx - sz.x * 0.5f, y), col, text);
        }
    };
}
