#pragma once
// Standard libraries
#include <map>
#include <string>
#include <fstream>
#include <iostream>
#include <windows.h>

// External libraries
#include <nlohmann/json.hpp>
#include "../libs/imgui/imgui.h"

// Aim bone index map. Unknown indices are passed through.
inline const std::map<int, const char*> player_aim_bones = {
    {133, "Head"},
    {132, "Neck"},
    {37,  "Chest"},
    {14,  "Pelvis"},
    {89,  "L shoulder"},
    {92,  "L elbow"},
    {94,  "L hand"},
    {110, "R shoulder"},
    {113, "R elbow"},
    {115, "R hand"},
    {15,  "L hip"},
    {16,  "L knee"},
    {18,  "L foot"},
    {20,  "R hip"},
    {21,  "R knee"},
    {23,  "R foot"},
};

// ============================================================================
// Weapon ballistics presets
// Muzzle velocities in meters per second. Index 0 is "Custom" which falls
// back to the aim_projectile_speed slider; the rest are real cartridge /
// platform ballpark values. When the user picks a preset the aimbot uses
// that speed directly without touching the slider.
// ============================================================================
struct weapon_preset_t { const char* name; float speed_mps; };
static const weapon_preset_t kWeaponPresets[] = {
    { "Custom",               0.f   },
    { "Pistol (9mm)",         380.f },
    { "SMG (MP5 / MP7)",      400.f },
    { "Shotgun (slug)",       450.f },
    { "AKM (7.62x39)",        715.f },
    { "AK-74 (5.45x39)",      880.f },
    { "M4 / HK416 (5.56)",    905.f },
    { "SCAR-H (7.62x51)",     820.f },
    { "DMR (7.62x51)",        850.f },
    { "Sniper (Mosin)",       830.f },
    { "Sniper (.338 LM)",     900.f },
    { "LMG (PKM / M249)",     825.f },
};
static constexpr int kWeaponPresetCount = (int)(sizeof(kWeaponPresets) / sizeof(kWeaponPresets[0]));

class c_settings {
private:
    // ------------------------------------------------------------------ bools
    std::map<std::string, bool> bool_settings = {
        // Core
        {"menu_open",           true},
        {"vsync",               false},
        {"debug_actors",        false},

        // Write features (modifies game memory — confirmed via menu warning)
        {"write_features_confirmed", false},
        // Experimental features (server-authoritative / untested writes — separate gate)
        {"experimental_features_confirmed", false},
        {"no_pitch_lock",       false},

        // Player ESP
        {"player_box",          true},
        {"player_skeleton",     true},
        {"player_health_bar",   true},
        {"player_name",         true},
        {"player_distance",     true},
        {"player_head_dot",     false},
        {"player_tracers",      false},
        {"player_arrows",       false},
        {"player_box_filled",   false},
        // Radar-style write chams (Basic/Visible + VisCheck factory)
        {"write_chams",           true},
        {"write_chams_clothing",  true},
        {"write_chams_gear",      true},
        {"write_chams_pmc",       true},
        {"write_chams_ai",        true},
        {"write_chams_teammate",  false},
        {"write_chams_advanced",  false}, // AssetBundles abandoned — steal NVG/Frost only
        // Legacy keys kept so old configs still load
        {"write_chams_vischeck",  true},
        {"write_chams_colored",   true},
        {"player_enemy_highlight", true},
        {"player_ai_highlight", true},
        {"player_teammate_highlight", true},
        {"player_corpse_highlight", true},
        // Per-player status tag toggles
        {"player_show_threat",       true},
        {"player_show_coma_tag",     true},
        {"player_show_armor_tag",    true},
        {"player_show_fire_mode",    true},
        {"player_show_faction_camp", true},
        {"player_weapon",            true},
        {"player_high_alert",        true},
        {"ai_weapon",                true},
        {"teammate_weapon",          true},
        {"grenade_esp_enabled",      true},

        // Per-category feature visibility — AI/Bots
        {"ai_box",         true},  {"ai_skeleton",   true},
        {"ai_health_bar",  true},  {"ai_name",       true},
        {"ai_distance",    true},  {"ai_head_dot",   true},
        {"ai_tracers",     false}, {"ai_weapon",     true},
        {"ai_arrows",      true},  {"ai_box_filled", false},
        // Per-AI status tag toggles
        {"ai_show_threat",       true},
        {"ai_show_coma_tag",     true},
        {"ai_show_armor_tag",    true},
        {"ai_show_fire_mode",    true},
        {"ai_show_faction_camp", true},

        // Per-category feature visibility — Teammates
        {"teammate_box",         false}, {"teammate_skeleton",    false},
        {"teammate_health_bar",  true},  {"teammate_name",        true},
        {"teammate_distance",    true},  {"teammate_head_dot",    false},
        {"teammate_tracers",     false}, {"teammate_weapon",      true},
        {"teammate_arrows",      false}, {"teammate_box_filled",  false},
        // Per-teammate status tag toggles
        {"teammate_show_threat",       true},
        {"teammate_show_coma_tag",     true},
        {"teammate_show_armor_tag",    true},
        {"teammate_show_fire_mode",    true},
        {"teammate_show_faction_camp", true},

        {"player_weapon",       true},
        {"player_show_dead_tag",  true},
        {"player_show_gear_value", true}, // gear value on living ESP
        {"ai_show_gear_value",     false},
        {"teammate_show_gear_value", false},
        {"player_enemy_highlight",    true},
        {"player_ai_highlight",       true},
        {"player_teammate_highlight", false},
        {"player_corpse_highlight",   true},

        // Aimbot
        {"aim_enabled",             false},
        {"aim_fov_enabled",         false},
        {"aim_at_closest",          false},
        {"aim_line",                false},
        {"aim_target_players",      true},
        {"aim_target_ai",           true},
        {"aim_prediction_enabled",  true},
        {"aim_weapon_auto",         true},
        {"aim_zeroing_enabled",     true},
        {"aim_zeroing_auto_match",  true},
        // Silent aim
        {"aim_silent_enabled",      true},  // legacy gate (method==5 is the real switch)
        {"aim_silent_autobone",     true},  // pick bone closest to crosshair
        {"aim_silent_safelock",     true},  // drop lock when no bone in FOV
        {"combat_mode",             false},

        // World ESP
        {"vehicle_esp_enabled",     false},
        {"vehicle_show_distance",   true},
        {"vehicle_show_team",       true},
        {"vehicle_show_icons",      true},
        {"vehicle_show_classname",  false},
        {"flag_esp_enabled",        false},
        {"flag_show_distance",      true},
        {"exfil_esp_enabled",       true},
        {"exfil_show_distance",     true},
        {"exfil_show_status",       true},
        {"exfil_hide_closed",       true},
        {"fob_esp_enabled",         false},
        {"fob_show_distance",       true},
        {"rally_esp_enabled",       false},
        {"rally_show_distance",     true},
        {"show_esp_fps",            false},
        {"heli_esp_enabled",           false},
        {"lz_world_esp_enabled",       false},
        {"minimap_enabled",            false},
        {"raid_clock_enabled",         true},
        {"day_night_clock_enabled",    true},
        {"item_hide_empty_containers", false},
        {"corpse_attacker_line",       true},

        // Item ESP
        {"item_esp_enabled",    true},
        {"item_show_category",  true},

        // Per-category visibility toggles
        {"item_cat_weapon",     true},
        {"item_cat_mag",        true},
        {"item_cat_ammo",       true},
        {"item_cat_medical",    true},
        {"item_cat_attachment", true},
        {"item_cat_armor",      true},
        {"item_cat_backpack",   true},
        {"item_cat_food",       true},
        {"item_cat_misc",       true},
        {"item_distance",       true},
        {"item_show_loot",      true},
        {"item_show_container", true},
        {"item_show_dead_body", true},
        {"item_show_quest",     true},
        {"item_show_grenade",   false},
        {"item_show_generic",   false},
        {"item_show_money",     true},
        {"item_show_price",     true},
        {"item_show_corpse_value", true}, // corpse ESP uses summed gear value
        {"item_show_level",     false},
        {"item_scale_with_dist",false},
        {"item_show_icons",     true},
        {"item_icons_focus_only", true}, // large icon only when crosshair-near
        {"item_icons_prefetch", true},  // background download full catalog

        // Quest Helper — active quests (Profile.QuestsData) + zones / mission loot.
        {"quest_helper_enabled",   true},
        {"quest_show_intel",       true},  // visit / discover zones
        {"quest_show_pickup",      true},  // plant / place-item zones
        {"quest_show_box",         true},  // kill / other zones
        {"quest_show_loot",        true},  // required mission items on ground
        {"quest_loot_mission_only", true}, // only RequiredItems (not every QuestItem flag)
        {"quest_tracers",          true},
        {"quest_offscreen_arrows", true},
        {"quest_show_distance",    true},
        {"quest_active_missions_only", true}, // zones/items for current map when possible
        {"quest_required_filter",  false},
        {"quest_show_optional",    false},    // include optional objectives
        {"quest_hud",              true},     // on-screen quest list

        {"crosshair_enabled",          false},
        {"show_ammo_hud",              true},  // bottom-right mag count
        {"always_day",                 false},
        // Write features
        {"robrun_no_recoil",               false}, // force amount to 0% (slider ignored while on)
        {"robrun_no_recoil_punch",         false},
        {"robrun_no_spread",               false},
        {"robrun_no_sway",                 false}, // force amount to 0% (slider ignored while on)
        // Safe-ish QoL heap writes (gate: write_features_confirmed)
        {"robrun_no_visor",                false},
        {"robrun_disable_inv_blur",        false},
        {"robrun_disable_weapon_collision",false},
        {"robrun_med_panel",               false},
        {"robrun_always_nvg",              false},
        {"robrun_thermal",                 false},
        {"robrun_clear_weather",           false},
        {"robrun_no_malfunctions",         false},
        {"robrun_anti_afk",                false},
        {"robrun_disable_grass",           false},
        {"robrun_no_camera_shake",         false},
        {"robrun_infinite_ammo",           false}, // WeaponConfig bInfiniteAmmo/Mags
        {"robrun_rapid_fire",              false}, // TimeBetweenShots -> 0.01
        {"robrun_force_full_auto",         false}, // FireModes -> -1
        {"robrun_quick_swap",              false}, // Equip/Unequip durations -> 0.01
        {"robrun_quick_zoom",              false}, // DefaultFOV -> 30
        {"robrun_instant_grenade",         false}, // grenade throw timings only (no montage write)
        {"robrun_speed_hack",              false},
        {"robrun_air_stuck",               false}, // MOVE_Flying + fly speeds
        {"robrun_disable_collision",       false}, // NoCollision on root (requires air stuck)
        {"robrun_disable_suppression",     false},
        {"robrun_shoot_main_base",         false},
        {"robrun_interaction_distances",   false}, // extended interact distance/radius


        // ---- Weapon mods
        // Each property has its own override toggle. When OFF the original
        // value is restored. When ON the slider value is written every tick.
        {"wmod_fire_rate_enable",      false},
        {"wmod_muzzle_speed_enable",   false},
        {"wmod_fire_mode_enable",      false},
        {"wmod_moa_enable",            false},
        {"wmod_speed_modifier_enable", false},
        {"wmod_recoil_manip_enable",  false},
        {"wmod_fire_noise_enable",     false},
        {"wmod_mag_capacity_enable",   false},
        {"wmod_proj_count_enable",     false},
        {"wmod_proj_velocity_enable",  false},
        {"wmod_proj_energy_enable",    false},
        {"wmod_proj_weight_enable",    false},
        {"wmod_proj_moa_enable",       false},

        // ---- Tier 1: Suppressor / muzzle-device overrides ------------------
        {"wmod_silent_supp",           false}, // suppressor LoudnessModifier -> 0
        {"wmod_supp_no_moa",           false}, // suppressor MOAModifier -> 0
        {"wmod_muzzle_no_moa",         false}, // muzzle device MOAModifier -> 0

        // ---- Tier 2: Attachment walker (no-recoil / instant-reload / stamina)
        {"wmod_no_recoil",             false}, // RecoilControl = 100 on grip/stock/handguard/slide
        {"wmod_no_arm_stamina",        false}, // ArmStaminaModifier = 0 on grips/stock
        {"wmod_instant_reload",        false}, // ReloadTimeModifier = 0.01 on handle/slide/magazine
        {"wmod_fast_handling",         false}, // ManipulationModifier = 0.05 on all parts
        {"wmod_fast_firerate",         false}, // FireRateModifier multiplier on gas block

        // ---- Tier 3: Player movement / camera ------------------------------
        {"pmod_super_speed",           false},
        {"pmod_super_jump",            false},
        {"pmod_low_gravity",           false},
        {"pmod_air_control",           false},
        {"pmod_no_pitch_lock",         false},
        {"pmod_instant_crouch",        false},
        {"pmod_fast_turn",             false},

        // ---- Tier 5: Sandbox / posture / perception spoof ------------------
        {"pmod_omni_sprint",           false}, // SprintAngleTolerance huge
        {"pmod_prone_sprint",          false}, // IsProne cleared each tick — sprint while prone
        {"pmod_lean_lock",             false}, // MovementLeanFactor pinned
        {"pmod_tall_prone",            false}, // ProneEyeHeight bumped
        {"pmod_low_profile",           false}, // CrouchFactor + MaxCapsule... low
        {"pmod_force_relaxed",         false}, // AnimationState = 0 Relaxed
        {"pmod_force_camp",            false}, // InFactionCamp = true
        {"pmod_spoof_ai",              false}, // IsAI = true

        // ---- Tier 6: Movement physics fun ----------------------------------
        {"pmod_fly_mode",              false}, // MovementMode = 5 (Flying)
        {"pmod_slippery",              false}, // GroundFriction = 0
        {"pmod_snappy_accel",          false}, // MaxAcceleration big
        {"pmod_tiny_crouch",           false}, // CrouchedHalfHeight small

        // ---- Tier 6: Camera + visual lock-ins ------------------------------
        {"wmod_no_death_camera",       false}, // CameraMod_Death.Alpha = 0
        {"pmod_force_aiming",          false}, // WantsAiming = 1 each tick

        // ---- Tier 7: Enemy debuffs (writes to OTHER actors) ----------------
        {"emod_freeze_enemies",        false}, // MaxWalkSpeed = 0 on AI
        {"emod_snail_enemies",         false}, // MaxWalkSpeed = snail_speed on AI
        {"emod_friendly_enemies",      false}, // TeamId = local team
        {"emod_mark_enemies_dead",     false}, // IsDead = 1 on AI

        // ---- Tier 4: Grenade + armor ---------------------------------------
        {"wmod_mega_grenade",          false}, // applies to all grenades in inventory
        {"wmod_instant_grenade",       false}, // FuseTime + ArmTime -> ~0
        {"wmod_god_armor",             false}, // BodyArmor + HeadArmor OverallJoules huge

        // ---- World mods ----------------------------------------------------
        {"wmod_time_of_day_enable",    false}, // pick any hour (overrides always_day)
        {"wmod_freeze_time",           false}, // freeze the day cycle component
        {"wmod_day_speed_enable",      false}, // override TimeSpeedMultiplier
        // wmod_no_radiation removed — radiation feature isn't live.
        {"wmod_heli_boost",            false}, // every AMFGHelicopter CurrentSpeed override

        // ---- Camera --------------------------------------------------------
        {"wmod_fov_enable",            false}, // override APlayerCameraManager DefaultFOV
        {"wmod_no_recoil_camera",      false}, // null the recoil camera modifier
    };

    // ------------------------------------------------------------------- ints
    std::map<std::string, int> int_settings = {
        // Aimbot
        {"aim_key",             VK_XBUTTON1},
        {"combat_key",          VK_CAPITAL},
        {"aim_bone",            133},     // head
        {"aim_max_distance",    300},     // meters
        {"aim_weapon_preset",   4},       // index into kWeaponPresets[] (default = AKM)
        // Hardware mouse input
        {"aim_input_method",    2},       // 0=look write, 1=KMBox, 2=Makcu, 3=B-Pro, 4=Ferrum, 5=silent
        {"aim_kmbox_port",      8960},    // KMBox Net UDP port
        {"aim_makcu_baud",      115200},  // Makcu serial baud rate (driver auto-upgrades to 4M on connect)
        {"aim_ferum_baud",      115200},  // Ferrum starting baud (driver also sweeps 38400/57600/9600)
        // Trigger bot (fires a key/click when crosshair is on target)
        {"aim_trigger_enabled", 0},       // 0 = off
        {"aim_trigger_hid_key", 0x28},    // HID key to press when triggered (0x28 = Enter; set to desired fire key)
        {"aim_trigger_vk_key",  VK_RETURN},// Windows VK code for SendInput fallback (memory-write mode)
        {"aim_trigger_hold_ms", 60},      // how long to hold the key per trigger pulse (ms)

        // Player ESP distances (meters).
        // Always applied (no "0 = unlimited"); also drives early DMA cull.
        {"player_max_distance",         1000},
        {"player_enemy_max_distance",   1000},
        {"player_ai_max_distance",      1000},
        {"player_teammate_max_distance",1000},
        {"player_corpse_max_distance",  400},
        // Skeleton bone DMA / draw caps (meters).
        {"player_skeleton_max_distance",   150},
        {"ai_skeleton_max_distance",       150},
        {"teammate_skeleton_max_distance", 150},
        {"player_box_style",    2},       // 0 = Full, 1 = Filled, 2 = Corners
        // ChamsMode: 1=Basic 2=VisCheckGlow 3=Visible 4=VisCheckFlat 5=WireFrame
        // Default Visible until NativeHookFuncPtr is set (VisCheck needs it).
        {"write_chams_clothing_mode", 3},
        {"write_chams_gear_mode",     3},
        {"minimap_size",        180},
        {"minimap_range",       300},

        // World ESP distances (meters).
        {"vehicle_max_distance", 2000},
        {"flag_max_distance",    2000},
        {"exfil_max_distance",   2000},
        {"fob_max_distance",     2000},
        {"rally_max_distance",   500},

        // Item ESP - per-kind distance caps (meters). 0 = disabled.
        {"item_max_distance",           200},  // legacy global, still honored as upper clamp
        {"item_loot_max_distance",      150},
        {"item_container_max_distance", 300},
        {"item_dead_body_max_distance", 300},
        {"item_quest_max_distance",     500},
        {"item_grenade_max_distance",   100},
        {"grenade_max_distance",        120},
        {"item_generic_max_distance",   100},

        // Quest Helper per-kind distance caps (meters). Tend to be much
        // larger than regular loot since quest objectives reward spotting
        // from a distance. 0 = disabled.
        {"quest_max_distance",          2000}, // global cap
        {"quest_intel_max_distance",    1500},
        {"quest_pickup_max_distance",   1500},
        {"quest_box_max_distance",      1500},
        {"quest_loot_max_distance",     1500},
        // World LZ (MFGLandingZone): max draw distance in meters; 0 = unlimited.
        {"lz_world_max_distance",       0},
        {"heli_esp_max_distance",       0},

        // ---- Weapon Mods values --------------------------------------------
        {"wmod_fire_rate",        900},   // rounds/min  (uint16 in game)
        {"wmod_muzzle_speed",     1200},  // m/s         (uint16)
        {"wmod_fire_mode",        4},     // 0=Safe 1=Single 2=Burst 4=Auto
        {"wmod_mag_capacity",     60},    // uint8
        {"wmod_proj_count",       1},     // uint32 (pellets per shot)

        // ---- Tier 4: Grenade values ----------------------------------------
        {"wmod_grenade_kill_radius",   1500}, // cm (so 15m kill radius default)
        {"wmod_grenade_fragments",     200},  // shrapnel pellets

        // ---- World values --------------------------------------------------
        {"wmod_time_of_day_hour",      12},   // 0..23
    };

    // ----------------------------------------------------------------- floats
    std::map<std::string, float> float_settings = {
        // Aimbot
        {"aim_smoothness",         0.15f}, // fraction of pixel-delta per 5ms tick
        {"aim_smoothness_y",       0.15f},
        {"aim_pixel_per_deg",      7.0f},  // pixels per degree of rotation (kmbox/makcu calibration)
        {"aim_hw_gain",            1.0f},  // hardware-path-only multiplier on pixel delta (decouples HW responsiveness from smoothness slider)
        {"aim_fov_radius",         200.f},
        {"aim_projectile_speed",   715.f}, // m/s - used when preset = Custom
        {"aim_trigger_fov",        3.0f},  // max angular distance (deg) before trigger bot fires


        // Player ESP
        {"player_font_size",        13.f},
        {"player_box_opacity",       1.f},
        {"player_skeleton_opacity",  1.f},
        {"player_box_fill_opacity",  0.15f},
        {"player_arrows_size",       12.f},
        {"player_arrows_radius",    150.f},
        {"menu_ui_scale",            1.0f},

        // Item ESP
        {"item_font_size",         12.f},
        {"item_price_min",          1000.f}, // price filter for loose loot
        {"item_price_color_min",    0.f},
        {"item_price_color_max",    50000.f},
        {"item_icon_size",         72.f},
        {"item_icon_focus_px",     88.f}, // screen-center radius for focus icon
        {"grenade_font_size",      13.f},

        // Quest Helper
        {"quest_font_size",        15.f},
        {"quest_arrow_size",       22.f},
        {"quest_arrow_margin",     48.f},
        {"quest_tracer_thickness",  1.5f},

        // World ESP font sizes
        {"vehicle_font_size",      12.f},
        {"vehicle_icon_size",      28.f},
        {"flag_font_size",         12.f},
        {"exfil_font_size",        12.f},
        {"fob_font_size",          12.f},
        {"rally_font_size",        12.f},

        // Recoil / sway (0 = none, 100 = full)
        // REMOVE percent: 0 = vanilla, 100 = fully removed
        {"robrun_recoil_amount",      100.f}, // 100 = full, 0 = none
        {"robrun_sway_amount",        100.f},

        // ---- Weapon Mods float values --------------------------------------
        {"wmod_moa",              0.5f},     // arc-minutes (lower = tighter)
        {"wmod_speed_modifier",   1.0f},     // bullet speed multiplier
        {"wmod_recoil_manip",     0.25f},    // manipulation modifier (lower = faster handling)
        {"wmod_fire_noise",       50.f},     // dB  (cosmetic)
        {"wmod_proj_velocity",    1200.f},   // m/s — overrides muzzle velocity on projectile
        {"wmod_proj_energy",      6000.f},   // joules — damage potential
        {"wmod_proj_weight",      8.f},      // grams
        {"wmod_proj_moa",         0.0f},     // added spread per shot (lower = tighter)

        // ---- Tier 2: attachment multipliers --------------------------------
        {"wmod_firerate_mult",    2.0f},     // gas block FireRateModifier
        // ---- Tier 3: player movement values --------------------------------
        {"pmod_walk_speed",       1500.f},   // cm/s on CharacterMovement (default ~600)
        {"pmod_jump_velocity",    1000.f},   // cm/s (default ~420)
        {"pmod_gravity_scale",    0.4f},     // 1.0 = normal, 0.2 = floaty
        {"pmod_air_control_val",  1.0f},     // 0..1 — strafe in air
        {"pmod_turn_rate",        540.f},    // deg/s — quick 360s

        // ---- Tier 5: sandbox values ----------------------------------------
        {"pmod_sprint_angle",     360.f},    // deg — SprintAngleTolerance (45 default; 360 = true omni)
        {"pmod_lean_value",       1.0f},     // -1..+1 — MovementLeanFactor pin
        {"pmod_prone_eye_height", 80.f},     // ProneEyeHeight (~28 default)
        {"pmod_crouch_factor",    0.35f},    // CrouchFactor/HeightFactor low-profile

        // ---- Tier 6: movement physics values -------------------------------
        {"pmod_max_accel",        99999.f},  // MaxAcceleration (~2048 default)
        {"pmod_crouched_halfh",   10.f},     // CrouchedHalfHeight (~40 default)

        // ---- Tier 7: enemy debuff values -----------------------------------
        {"emod_snail_speed",      50.f},     // cm/s for Snail Enemies

        // ---- Tier 4: armor -------------------------------------------------
        {"wmod_armor_joules",     999999.f}, // OverallJoules + MinimumDamageJoules

        // ---- World float values --------------------------------------------
        {"wmod_day_speed_mult",   1.0f},     // 0=frozen, 60=time-lapse
        {"wmod_heli_speed",       4000.f},   // cm/s replacement for helicopter speed
        {"wmod_fov_value",        100.f},    // PlayerCameraManager DefaultFOV
    };

    // ---------------------------------------------------------------- strings
    std::map<std::string, std::string> string_settings = {
        // Hardware mouse input credentials
        {"aim_kmbox_ip",    "192.168.2.188"}, // KMBox Net IP address
        {"aim_kmbox_mac",   "77065C53"},      // 8-hex-digit KMBox MAC / UUID
        {"aim_makcu_com",   "COM3"},          // Makcu serial COM port (e.g. "COM3" or "3"); blank = auto-detect via VID/PID
        {"aim_bpro_com",    ""},              // KMBox B-Pro serial COM port; blank = auto-detect
        {"aim_ferum_com",   ""},              // Ferrum serial COM port; blank = auto-detect via VID/PID
    };

    // ----------------------------------------------------------------- colors
    std::map<std::string, ImColor> color_settings = {
        // Player ESP
        {"player_box_color",           {1.f, 1.f, 1.f, 1.f}},
        {"player_skeleton_color",      {1.f, 1.f, 1.f, 1.f}},
        {"player_health_bar_color",    {0.f, 1.f, 0.f, 1.f}},
        {"player_health_bar_bg_color", {0.2f, 0.2f, 0.2f, 1.f}},
        {"player_name_color",          {0.78f, 0.84f, 0.90f, 1.f}}, // soft steel (fuser-safe)
        {"player_distance_color",      {1.f, 1.f, 1.f, 1.f}},
        {"player_tracers_color",       {1.f, 1.f, 1.f, 1.f}},
        {"player_head_dot_color",      {1.f, 0.f, 0.f, 1.f}},
        {"player_enemy_color",         {0.92f, 0.30f, 0.32f, 1.f}},
        {"player_ai_color",            {0.95f, 0.78f, 0.28f, 1.f}},   // AI gold
        {"player_ai_boss_color",       {0.72f, 0.38f, 0.95f, 1.f}},   // boss violet
        {"player_ai_guard_color",      {0.95f, 0.62f, 0.22f, 1.f}},   // guard amber
        {"player_ai_raider_color",     {0.35f, 0.82f, 0.95f, 1.f}},   // AI ice
        {"player_teammate_color",      {0.35f, 0.88f, 0.55f, 1.f}},
        {"player_corpse_color",        {0.55f, 0.55f, 0.58f, 1.f}},
        {"write_chams_color",            {0.10f, 1.00f, 0.25f, 1.f}}, // visible / glow
        {"write_chams_invisible_color",  {1.00f, 0.12f, 0.12f, 1.f}}, // occluded (VisCheck)

        // Item ESP
        {"item_loot_color",            {0.92f, 0.78f, 0.42f, 1.f}}, // muted gold
        {"item_container_color",       {0.55f, 0.78f, 0.92f, 1.f}}, // steel blue
        {"item_airdrop_color",         {0.95f, 0.48f, 0.28f, 1.f}}, // burnt orange
        {"item_dead_body_color",       {0.72f, 0.52f, 0.78f, 1.f}}, // muted violet
        {"item_quest_color",           {0.45f, 0.92f, 0.55f, 1.f}}, // soft green
        {"item_grenade_color",         {1.00f, 0.30f, 0.30f, 1.f}}, // red
        {"grenade_esp_color",          {1.00f, 0.25f, 0.15f, 1.f}},
        {"player_high_alert_color",    {1.00f, 0.15f, 0.15f, 1.f}},
        {"item_generic_color",         {0.80f, 0.80f, 0.80f, 1.f}}, // grey

        // Quest Helper per-subkind colors.
        {"quest_intel_color",          {0.20f, 0.80f, 1.00f, 1.f}}, // cyan = intel / docs
        {"quest_pickup_color",         {1.00f, 0.55f, 0.10f, 1.f}}, // orange = placed quest items
        {"quest_box_color",            {1.00f, 0.90f, 0.15f, 1.f}}, // gold = quest loot crates
        {"quest_tracer_color",         {0.75f, 0.85f, 0.95f, 0.6f}}, // soft steel tracer (fuser-safe)
        {"quest_arrow_color",          {0.78f, 0.88f, 0.96f, 0.9f}}, // screen-edge arrow

        // Item category colors (match item_category_color defaults)
        {"item_cat_weapon_color",     {1.00f, 0.30f, 0.30f, 1.f}},
        {"item_cat_mag_color",        {1.00f, 0.70f, 0.10f, 1.f}},
        {"item_cat_ammo_color",       {1.00f, 0.90f, 0.10f, 1.f}},
        {"item_cat_medical_color",    {0.20f, 1.00f, 0.40f, 1.f}},
        {"item_cat_attachment_color", {0.50f, 0.80f, 1.00f, 1.f}},
        {"item_cat_armor_color",      {0.35f, 0.70f, 1.00f, 1.f}},
        {"item_cat_backpack_color",   {0.80f, 0.50f, 1.00f, 1.f}},
        {"item_cat_food_color",       {1.00f, 0.75f, 0.40f, 1.f}},
        {"item_cat_misc_color",       {0.75f, 0.75f, 0.75f, 1.f}},

        // Player arrows
        {"player_arrows_color",        {1.f, 1.f, 1.f, 0.9f}},

        // Per-feature colors — AI (gold palette, not neon orange)
        {"ai_box_color",              {0.95f, 0.78f, 0.28f, 1.f}},
        {"ai_skeleton_color",         {0.95f, 0.78f, 0.28f, 1.f}},
        {"ai_health_bar_color",       {0.45f, 0.95f, 0.55f, 1.f}},
        {"ai_health_bar_bg_color",    {0.08f, 0.08f, 0.10f, 1.f}},
        {"ai_name_color",             {0.98f, 0.92f, 0.78f, 1.f}},
        {"ai_distance_color",         {0.80f, 0.82f, 0.86f, 1.f}},
        {"ai_head_dot_color",         {0.95f, 0.78f, 0.28f, 1.f}},
        {"ai_tracers_color",          {0.95f, 0.78f, 0.28f, 1.f}},
        {"ai_arrows_color",           {0.95f, 0.78f, 0.28f, 0.9f}},

        // Per-feature colors — Teammates (mint)
        {"teammate_box_color",        {0.35f, 0.88f, 0.55f, 1.f}},
        {"teammate_skeleton_color",   {0.35f, 0.88f, 0.55f, 1.f}},
        {"teammate_health_bar_color", {0.45f, 0.95f, 0.55f, 1.f}},
        {"teammate_health_bar_bg_color",{0.08f,0.08f,0.10f, 1.f}},
        {"teammate_name_color",       {0.85f, 0.98f, 0.88f, 1.f}},
        {"teammate_distance_color",   {0.80f, 0.82f, 0.86f, 1.f}},
        {"teammate_head_dot_color",   {0.35f, 0.88f, 0.55f, 1.f}},
        {"teammate_tracers_color",    {0.35f, 0.88f, 0.55f, 1.f}},
        {"teammate_arrows_color",     {0.35f, 0.88f, 0.55f, 0.9f}},

        // Aimbot
        {"aim_fov_color",              {1.f, 1.f, 1.f, 1.f}},
        {"aim_line_color",             {1.f, 0.2f, 0.2f, 0.9f}},

        // Misc
        {"crosshair_color",            {1.f, 1.f, 1.f, 0.9f}},
        {"ammo_hud_color",             {0.95f, 0.95f, 0.85f, 1.f}},
        {"item_money_color",           {0.2f, 1.f, 0.4f, 1.f}},

        // World ESP colors
        {"vehicle_color",              {1.f, 1.f, 1.f, 1.f}},
        {"vehicle_team1_color",        {0.2f, 0.5f, 1.f, 1.f}},
        {"vehicle_team2_color",        {1.f, 0.3f, 0.3f, 1.f}},
        {"flag_neutral_color",         {0.7f, 0.7f, 0.7f, 1.f}},
        {"exfil_open_color",           {0.15f, 0.95f, 0.35f, 1.f}},
        {"exfil_pending_color",        {0.95f, 0.85f, 0.20f, 1.f}},
        {"exfil_closed_color",         {0.55f, 0.55f, 0.55f, 1.f}},
        {"exfil_secret_color",         {0.75f, 0.35f, 0.95f, 1.f}},
        {"flag_team1_color",           {0.2f, 0.5f, 1.f, 1.f}},
        {"flag_team2_color",           {1.f, 0.3f, 0.3f, 1.f}},
        {"flag_contested_color",       {0.8f, 0.2f, 0.9f, 1.f}},
        {"flag_capturing_color",       {1.f, 0.85f, 0.f, 1.f}},
        {"fob_team1_color",            {0.2f, 0.5f, 1.f, 1.f}},
        {"fob_team2_color",            {1.f, 0.3f, 0.3f, 1.f}},
        {"rally_color",                {1.f, 0.85f, 0.f, 1.f}},
        {"rally_team1_color",          {0.2f, 0.5f, 1.f, 1.f}},
        {"rally_team2_color",          {1.f, 0.3f, 0.3f, 1.f}},
    };

public:
    // --- Getters ---------------------------------------------------------
    bool get_bool(const std::string& name) const {
        auto it = bool_settings.find(name);
        return (it != bool_settings.end()) ? it->second : false;
    }
    int get_int(const std::string& name) const {
        auto it = int_settings.find(name);
        return (it != int_settings.end()) ? it->second : 0;
    }
    // Distance (meters). Values <= 0 fall back to `fallback` (no unlimited mode).
    int get_distance_m(const std::string& name, int fallback) const {
        const int v = get_int(name);
        return (v > 0) ? v : fallback;
    }
    float get_float(const std::string& name) const {
        auto it = float_settings.find(name);
        return (it != float_settings.end()) ? it->second : 0.f;
    }
    ImColor get_color(const std::string& name) const {
        auto it = color_settings.find(name);
        return (it != color_settings.end()) ? it->second : ImColor(1.f, 1.f, 1.f, 1.f);
    }
    std::string get_string(const std::string& name) const {
        auto it = string_settings.find(name);
        return (it != string_settings.end()) ? it->second : std::string{};
    }

    // --- Setters ---------------------------------------------------------
    void set_bool  (const std::string& name, bool v)              { bool_settings  [name] = v; }
    void set_int   (const std::string& name, int v)               { int_settings   [name] = v; }
    void set_float (const std::string& name, float v)             { float_settings [name] = v; }
    void set_color (const std::string& name, const ImColor& v)    { color_settings [name] = v; }
    void set_string(const std::string& name, const std::string& v){ string_settings[name] = v; }

    // --- ImGui reference getters -----------------------------------------
    bool*        get_bool_ref  (const std::string& name) { return &bool_settings  [name]; }
    int*         get_int_ref   (const std::string& name) { return &int_settings   [name]; }
    float*       get_float_ref (const std::string& name) { return &float_settings [name]; }
    ImColor*     get_color_ref (const std::string& name) { return &color_settings [name]; }
    std::string* get_string_ref(const std::string& name) { return &string_settings[name]; }

    // --- Persistence (implemented in .cpp) -------------------------------
    void save_settings(const std::string& filename) const;
    void load_settings(const std::string& filename);
    bool validate_settings() const;
    void reset_to_defaults();
};

// Helper for legacy callers.
ImColor get_color(const std::string& setting_name);

// Global settings instance.
extern c_settings g_settings;
