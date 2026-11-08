#include "menu.hpp"
#include "../includes.hpp"
#include "../utils/settings.hpp"
#include "../game/robrun/basic_esp.hpp"

#include <cstdio>
#include <cstring>

static void ColorEdit(const char* label, unsigned int* packed)
{
    float c[4] = {
        float(*packed & 0xFF) / 255.f,
        float((*packed >> 8) & 0xFF) / 255.f,
        float((*packed >> 16) & 0xFF) / 255.f,
        float((*packed >> 24) & 0xFF) / 255.f
    };
    if (ImGui::ColorEdit4(label, c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar))
    {
        auto ch = [](float v)
        {
            int i = static_cast<int>(v * 255.f + 0.5f);
            if (i < 0) i = 0;
            if (i > 255) i = 255;
            return i;
        };
        *packed = robrun::esp_col::rgba(ch(c[0]), ch(c[1]), ch(c[2]), ch(c[3]));
    }
}

static void DrawVisuals(robrun::esp_settings& cfg)
{
    if (ImGui::CollapsingHeader("Draw", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("ESP enabled", &cfg.enabled);
        ImGui::Checkbox("Box", &cfg.draw_box);
        ImGui::Checkbox("Skeleton", &cfg.draw_skeleton);
        ImGui::Checkbox("Name", &cfg.draw_name);
        ImGui::Checkbox("Role", &cfg.draw_role);
        ImGui::Checkbox("Stance", &cfg.draw_stance);
        ImGui::Checkbox("Distance", &cfg.draw_distance);
        ImGui::Checkbox("Weapon", &cfg.draw_weapon);
        ImGui::Checkbox("Armor", &cfg.draw_armor);
        ImGui::Checkbox("KO / cuffs", &cfg.draw_status);
        ImGui::Checkbox("Health", &cfg.draw_health);
    }

    if (ImGui::CollapsingHeader("Players", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Checkbox("Show local", &cfg.show_local);
        ImGui::Checkbox("Show dead", &cfg.show_dead);
        ImGui::Checkbox("Players only", &cfg.only_players);
        ImGui::Checkbox("Exclude group", &cfg.exclude_group);
    }

    if (ImGui::CollapsingHeader("Range"))
    {
        ImGui::SliderFloat("Max distance", &cfg.max_distance, 10.f, 1500.f, "%.0f m");
        ImGui::Checkbox("Approach warning", &cfg.approach_warning);
        ImGui::SliderFloat("Approach radius", &cfg.approach_distance, 5.f, 300.f, "%.0f m");
    }

    if (ImGui::CollapsingHeader("Box"))
    {
        ImGui::Checkbox("Capsule bounds", &cfg.use_capsule_bounds);
        ImGui::Checkbox("Ground anchor", &cfg.use_ground_anchor);
        ImGui::Checkbox("Position is centre", &cfg.position_is_center);
        ImGui::SliderFloat("Standing height", &cfg.box_height, 1.0f, 2.4f, "%.2f m");
        ImGui::SliderFloat("Crouch height", &cfg.box_height_crouch, 0.6f, 2.0f, "%.2f m");
        ImGui::SliderFloat("Box offset", &cfg.box_offset, -0.5f, 0.5f, "%+.2f m");
    }

    if (ImGui::CollapsingHeader("Colours"))
    {
        ColorEdit("Policeman", &cfg.col_police);
        ColorEdit("Robber", &cfg.col_robber);
        ColorEdit("Civilian", &cfg.col_civilian);
        ColorEdit("Prisoner", &cfg.col_prisoner);
        ColorEdit("Group member", &cfg.col_group);
        ColorEdit("Approach", &cfg.col_warn);
        ColorEdit("Default box", &cfg.col_box);
        ColorEdit("Skeleton", &cfg.col_skeleton);
        ColorEdit("Local", &cfg.col_local);
        ColorEdit("Dead", &cfg.col_box_dead);
        ColorEdit("Text", &cfg.col_text);
    }

    ImGui::Checkbox("On-screen debug status", &cfg.show_debug_status);
}

static void DrawLoot(robrun::esp_settings& cfg, const robrun::esp_snapshot& snap)
{
    ImGui::Checkbox("Loot ESP", &cfg.loot_enabled);
    ImGui::SliderFloat("Loot range", &cfg.loot_max_distance, 10.f, 800.f, "%.0f m");
    ImGui::Checkbox("Robbery points", &cfg.loot_robbery_points);
    ImGui::Checkbox("Cash points", &cfg.loot_cash_points);
    ImGui::Checkbox("Weights", &cfg.loot_weights);
    ImGui::Checkbox("Discarded bags", &cfg.loot_dropped_bags);
    ImGui::Checkbox("Police award box", &cfg.loot_police_award);
    ImGui::Checkbox("Safes and lockers", &cfg.loot_safes);
    ImGui::Checkbox("Show lock condition", &cfg.loot_show_lock_status);
    ImGui::Checkbox("Already robbed", &cfg.loot_robbed_hideouts);

    if (ImGui::CollapsingHeader("Colours"))
    {
        ColorEdit("Safe", &cfg.col_loot_safe);
        ColorEdit("Cash register", &cfg.col_loot_register);
        ColorEdit("Shelf", &cfg.col_loot_shelf);
        ColorEdit("Weight", &cfg.col_loot_weight);
        ColorEdit("Bag", &cfg.col_loot_bag);
        ColorEdit("Award box", &cfg.col_loot_award);
        ColorEdit("Cash", &cfg.col_loot_cash);
    }

    ImGui::Text("Loot in snapshot: %zu", snap.loot.size());
}

static void DrawTransport(robrun::esp_settings& cfg, const robrun::esp_snapshot& snap)
{
    ImGui::Checkbox("Transport ESP", &cfg.transport_enabled);
    ImGui::Checkbox("Show parked", &cfg.transport_show_parked);
    ImGui::Checkbox("Passengers", &cfg.transport_show_passengers);
    ImGui::Checkbox("Siren", &cfg.transport_show_siren);
    ImGui::SliderFloat("Transport range", &cfg.transport_max_distance, 10.f, 1500.f, "%.0f m");
    ImGui::Text("Vehicles in snapshot: %zu", snap.vehicles.size());
}

static void DrawAim(robrun::esp_settings& cfg, const robrun::esp_snapshot& snap)
{
    ImGui::Checkbox("Aimbot", &cfg.aim_enabled);
    ImGui::Checkbox("Hardware device", &cfg.aim_hardware);

    static char com_buf[16] = "COM1";
    static bool com_init = false;
    if (!com_init)
    {
        com_init = true;
        if (!cfg.aim_com.empty())
            strncpy_s(com_buf, cfg.aim_com.c_str(), _TRUNCATE);
    }
    if (ImGui::InputText("COM port", com_buf, sizeof(com_buf)))
        cfg.aim_com = com_buf;

    ImGui::Checkbox("Exclude group from aim", &cfg.aim_ignore_group);
    bool aim_head = cfg.aim_target_bone != "Torso";
    ImGui::Checkbox("Aim at head", &aim_head);
    cfg.aim_target_bone = aim_head ? "Head" : "Torso";

    ImGui::SliderFloat("FOV", &cfg.aim_fov, 10.f, 500.f, "%.0f px");
    ImGui::SliderFloat("Smooth", &cfg.aim_smooth, 0.02f, 1.f, "%.2f");

    ImGui::Separator();
    ImGui::Checkbox("Triggerbot", &cfg.aim_triggerbot);
    ImGui::Checkbox("RCS", &cfg.aim_rcs);
    ImGui::SliderFloat("RCS strength", &cfg.aim_rcs_strength, 0.f, 20.f, "%.1f px");
    ImGui::Text("Device: %s", cfg.aim_link);

    int groups = 0;
    for (const auto& e : snap.entities)
        if (e.group_member) ++groups;
    ImGui::Text("Group members: %d", groups);
}

static void DrawDiagnostics(const robrun::esp_snapshot& snap, bool& re_resolve)
{
    ImGui::Text("Resolved: %s", snap.resolved ? "yes" : "no");
    ImGui::Text("Ready: %s", snap.ready ? "yes" : "no");
    ImGui::Text("Entities: %zu", snap.entities.size());
    ImGui::Text("Loot: %zu", snap.loot.size());
    ImGui::Text("Vehicles: %zu", snap.vehicles.size());

    int groups = 0;
    for (const auto& e : snap.entities)
        if (e.group_member) ++groups;
    ImGui::Text("Group members: %d", groups);

    const char* cam = snap.camera.valid
        ? (snap.camera.from_brain ? "brain" : "battle")
        : "none";
    ImGui::Text("Camera: %s", cam);

    ImGui::Separator();
    ImGui::TextWrapped("%s", snap.status.empty() ? "no status" : snap.status.c_str());
    ImGui::Separator();
    if (ImGui::Button("Re-resolve world"))
        re_resolve = true;
    ImGui::TextDisabled("Resolver output goes to ROBRUN.log");
}

void render_menu(robrun::esp_settings& cfg,
                 const robrun::esp_snapshot& snap,
                 bool& re_resolve)
{
    re_resolve = false;
    if (!g_settings.get_bool("menu_open"))
        return;

    ImGui::SetNextWindowSize(ImVec2(460.f, 560.f), ImGuiCond_FirstUseEver);
    ImGui::Begin("ROBRUN", nullptr);

    if (ImGui::BeginTabBar("##robrun_tabs"))
    {
        if (ImGui::BeginTabItem("Visuals"))
        {
            DrawVisuals(cfg);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Loot"))
        {
            DrawLoot(cfg, snap);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Transport"))
        {
            DrawTransport(cfg, snap);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Aim"))
        {
            DrawAim(cfg, snap);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Diagnostics"))
        {
            DrawDiagnostics(snap, re_resolve);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::TextDisabled("INSERT toggles this menu. END closes the overlay.");
    ImGui::End();
}
