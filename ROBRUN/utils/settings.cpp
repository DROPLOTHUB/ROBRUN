#include "settings.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <ctime>

// Global settings instance definition
c_settings g_settings;

// Implementation of c_settings methods
void c_settings::save_settings(const std::string& filename) const {
    try {
        // Extract directory path from filename
        size_t last_slash = filename.find_last_of("\\/");
        if (last_slash != std::string::npos) {
            std::string dir_path = filename.substr(0, last_slash);
            // Create directory if it doesn't exist
            CreateDirectoryA(dir_path.c_str(), NULL);
        }

        nlohmann::json j;

        // Save boolean settings
        for (const auto& [key, value] : bool_settings) {
            j["bools"][key] = value;
        }

        // Save integer settings
        for (const auto& [key, value] : int_settings) {
            j["ints"][key] = value;
        }

        // Save float settings
        for (const auto& [key, value] : float_settings) {
            j["floats"][key] = value;
        }

        // Save color settings
        for (const auto& [key, value] : color_settings) {
            j["colors"][key] = { value.Value.x, value.Value.y, value.Value.z, value.Value.w };
        }

        // Save string settings
        for (const auto& [key, value] : string_settings) {
            j["strings"][key] = value;
        }

        // Add version info for future compatibility
        j["version"] = "1.0";
        j["timestamp"] = std::time(nullptr);

        std::ofstream file(filename);
        if (file.is_open()) {
            file << j.dump(4);
            file.close();
        }
        else {
            // Log error or handle file creation failure
            std::cerr << "Failed to open file for writing: " << filename << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error saving settings: " << e.what() << std::endl;
    }
}

void c_settings::load_settings(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open settings file: " << filename << std::endl;
            return;
        }

        nlohmann::json j;
        file >> j;
        file.close();

        // Check if file is valid JSON
        if (j.is_null() || j.empty()) {
            std::cerr << "Settings file is empty or invalid JSON: " << filename << std::endl;
            return;
        }

        // Check version compatibility (optional, for future use)
        if (j.contains("version")) {
            std::string version = j["version"].get<std::string>();
            // For now, we accept any version, but this allows future version handling
            //std::cout << "Loading settings version: " << version << std::endl;
        }

        // Load boolean settings with validation
        if (j.contains("bools") && j["bools"].is_object()) {
            for (auto& [key, value] : j["bools"].items()) {
                try {
                    if (value.is_boolean()) {
                        bool_settings[key] = value.get<bool>();
                    }
                    else if (value.is_number_integer()) {
                        // Handle legacy integer booleans (0/1)
                        bool_settings[key] = (value.get<int>() != 0);
                    }
                    else {
                        std::cerr << "Invalid boolean value for key: " << key << std::endl;
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading boolean setting '" << key << "': " << e.what() << std::endl;
                }
            }
        }

        // Load integer settings with validation
        if (j.contains("ints") && j["ints"].is_object()) {
            for (auto& [key, value] : j["ints"].items()) {
                try {
                    if (value.is_number_integer()) {
                        int_settings[key] = value.get<int>();
                    }
                    else if (value.is_number_float()) {
                        // Handle legacy float values by rounding
                        int_settings[key] = static_cast<int>(std::round(value.get<float>()));
                    }
                    else {
                        std::cerr << "Invalid integer value for key: " << key << std::endl;
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading integer setting '" << key << "': " << e.what() << std::endl;
                }
            }
        }

        // Load float settings with validation
        if (j.contains("floats") && j["floats"].is_object()) {
            for (auto& [key, value] : j["floats"].items()) {
                try {
                    if (value.is_number_float()) {
                        float_settings[key] = value.get<float>();
                    }
                    else if (value.is_number_integer()) {
                        // Handle legacy integer values by converting to float
                        float_settings[key] = static_cast<float>(value.get<int>());
                    }
                    else {
                        std::cerr << "Invalid float value for key: " << key << std::endl;
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading float setting '" << key << "': " << e.what() << std::endl;
                }
            }
        }

        // Load color settings with comprehensive validation
        if (j.contains("colors") && j["colors"].is_object()) {
            for (auto& [key, value] : j["colors"].items()) {
                try {
                    if (value.is_array() && value.size() == 4) {
                        // Validate all array elements are numbers
                        bool valid = true;
                        for (size_t i = 0; i < 4; ++i) {
                            if (!value[i].is_number()) {
                                valid = false;
                                break;
                            }
                        }

                        if (valid) {
                            color_settings[key] = ImColor(
                                std::clamp(value[0].get<float>(), 0.0f, 1.0f),
                                std::clamp(value[1].get<float>(), 0.0f, 1.0f),
                                std::clamp(value[2].get<float>(), 0.0f, 1.0f),
                                std::clamp(value[3].get<float>(), 0.0f, 1.0f)
                            );
                        }
                        else {
                            std::cerr << "Invalid color array for key: " << key << std::endl;
                        }
                    }
                    else if (value.is_array() && value.size() == 3) {
                        // Handle legacy RGB colors (without alpha)
                        color_settings[key] = ImColor(
                            std::clamp(value[0].get<float>(), 0.0f, 1.0f),
                            std::clamp(value[1].get<float>(), 0.0f, 1.0f),
                            std::clamp(value[2].get<float>(), 0.0f, 1.0f),
                            1.0f  // Default alpha
                        );
                    }
                    else if (value.is_number_integer()) {
                        // Handle legacy integer color format (0-255 range)
                        int color_int = value.get<int>();
                        color_settings[key] = ImColor(
                            ((color_int >> 16) & 0xFF) / 255.0f,
                            ((color_int >> 8) & 0xFF) / 255.0f,
                            (color_int & 0xFF) / 255.0f,
                            1.0f
                        );
                    }
                    else {
                        std::cerr << "Invalid color format for key: " << key << std::endl;
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading color setting '" << key << "': " << e.what() << std::endl;
                }
            }
        }

        // Load string settings
        if (j.contains("strings") && j["strings"].is_object()) {
            for (auto& [key, value] : j["strings"].items()) {
                try {
                    if (value.is_string())
                        string_settings[key] = value.get<std::string>();
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading string setting '" << key << "': " << e.what() << std::endl;
                }
            }
        }

        // Migrate legacy "0 = unlimited" distance values.
        auto fix_dist = [this](const char* key, int fallback) {
            auto it = int_settings.find(key);
            if (it != int_settings.end() && it->second <= 0)
                it->second = fallback;
        };
        fix_dist("player_max_distance", 1000);
        fix_dist("player_enemy_max_distance", 1000);
        fix_dist("player_ai_max_distance", 1000);
        fix_dist("player_teammate_max_distance", 1000);
        fix_dist("player_corpse_max_distance", 400);
        fix_dist("player_skeleton_max_distance", 150);
        fix_dist("ai_skeleton_max_distance", 150);
        fix_dist("teammate_skeleton_max_distance", 300);
        fix_dist("vehicle_max_distance", 2000);
        fix_dist("flag_max_distance", 2000);
        fix_dist("fob_max_distance", 2000);
        fix_dist("rally_max_distance", 500);

    }
    catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parse error in settings file: " << e.what() << std::endl;
    }
    catch (const nlohmann::json::type_error& e) {
        std::cerr << "JSON type error in settings file: " << e.what() << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading settings: " << e.what() << std::endl;
    }
}

bool c_settings::validate_settings() const {
    bool valid = true;

    // Validate color ranges (should be 0.0-1.0)
    for (const auto& [key, color] : color_settings) {
        if (color.Value.x < 0.0f || color.Value.x > 1.0f ||
            color.Value.y < 0.0f || color.Value.y > 1.0f ||
            color.Value.z < 0.0f || color.Value.z > 1.0f ||
            color.Value.w < 0.0f || color.Value.w > 1.0f) {
            std::cerr << "Invalid color range for key: " << key << std::endl;
            valid = false;
        }
    }

    // Validate integer ranges (basic sanity checks)
    for (const auto& [key, value] : int_settings) {
        if (key.find("distance") != std::string::npos && (value < 0 || value > 10000)) {
            std::cerr << "Invalid distance value for key: " << key << " (value: " << value << ")" << std::endl;
            valid = false;
        }
        if (key.find("fov") != std::string::npos && (value < 0 || value > 360)) {
            std::cerr << "Invalid FOV value for key: " << key << " (value: " << value << ")" << std::endl;
            valid = false;
        }
        if (key.find("smooth") != std::string::npos && (value < 0 || value > 100)) {
            std::cerr << "Invalid smooth value for key: " << key << " (value: " << value << ")" << std::endl;
            valid = false;
        }
    }

    // Validate float ranges. Opacity / smoothness / font_size stay bounded,
    // but projectile speed (m/s) and fov radius (px) can legitimately be
    // large, so only flag clearly nonsensical negatives / huge values.
    for (const auto& [key, value] : float_settings) {
        const bool is_opacity   = key.find("opacity")    != std::string::npos;
        const bool is_smoothing = key.find("smooth")     != std::string::npos;
        if ((is_opacity || is_smoothing) && (value < 0.0f || value > 10.0f)) {
            std::cerr << "Invalid float value for key: " << key << " (value: " << value << ")" << std::endl;
            valid = false;
        }
        else if (value < 0.0f || value > 100000.0f) {
            std::cerr << "Invalid float value for key: " << key << " (value: " << value << ")" << std::endl;
            valid = false;
        }
    }

    return valid;
}

void c_settings::reset_to_defaults() {
    bool_settings = {
        {"menu_open",           true},
        {"vsync",               false},
        {"debug_actors",        false},

        // Write features
        {"write_features_confirmed", false},
        {"no_recoil",           false},
        {"always_day",          false},

        {"player_box",          true},
        {"player_skeleton",     true},
        {"player_health_bar",   true},
        {"player_name",         true},
        {"player_distance",     true},
        {"player_head_dot",     false},
        {"player_tracers",      false},
        {"player_enemy_highlight",    true},
        {"player_ai_highlight",       true},
        {"player_teammate_highlight", false},
        {"player_corpse_highlight",   true},
        {"corpse_attacker_line",      true},

        {"aim_enabled",         false},
        {"aim_fov_enabled",     false},
        {"aim_at_closest",          false},
        {"aim_target_players",      true},
        {"aim_target_ai",           true},
        {"aim_prediction_enabled",  true},
        {"aim_weapon_auto",         true},
        {"aim_zeroing_enabled",     true},
        {"aim_zeroing_auto_match",  true},
        {"aim_line",                false},
        {"combat_mode",             false},



        // World ESP
        {"vehicle_esp_enabled",          false},
        {"vehicle_show_distance",        true},
        {"vehicle_show_team",            true},
        {"flag_esp_enabled",             false},
        {"flag_show_distance",           true},
        {"fob_esp_enabled",              false},
        {"fob_show_distance",            true},
        {"rally_esp_enabled",            false},
        {"rally_show_distance",          true},
        {"minimap_enabled",              false},

        // Player ESP additions
        {"player_box_filled",         false},
        {"player_arrows",             false},
        {"player_weapon",             true},

        // Per-category — AI/Bots
        {"ai_box",true},{"ai_skeleton",true},{"ai_health_bar",true},{"ai_name",true},
        {"ai_distance",true},{"ai_head_dot",true},{"ai_tracers",false},
        {"ai_weapon",true},{"ai_arrows",true},{"ai_box_filled",false},

        // Per-category — Teammates
        {"teammate_box",false},{"teammate_skeleton",false},{"teammate_health_bar",true},
        {"teammate_name",true},{"teammate_distance",true},{"teammate_head_dot",false},
        {"teammate_tracers",false},{"teammate_weapon",true},{"teammate_arrows",false},
        {"teammate_box_filled",false},
        {"player_show_dead_tag",      true},
        {"player_show_gear_value",    true},
        {"ai_show_gear_value",        false},
        {"teammate_show_gear_value",  false},
        {"player_show_coma_tag",      true},
        {"player_show_armor_tag",     true},

        // Item ESP additions
        {"item_show_money",           true},
        {"item_show_price",           false},
        {"item_show_corpse_value",    true},
        {"item_show_level",           false},
        {"item_scale_with_dist",      false},

        // Misc
        {"crosshair_enabled",         false},
        {"show_esp_fps",              false},
    };

    int_settings = {
        {"aim_key",             VK_XBUTTON1},
        {"combat_key",          VK_CAPITAL},
        {"aim_bone",            133},   // head
        {"aim_max_distance",    300},
        {"aim_weapon_preset",   4},      // AKM default

        {"player_max_distance",          1000},
        {"player_enemy_max_distance",    1000},
        {"player_ai_max_distance",       1000},
        {"player_teammate_max_distance", 1000},
        {"player_corpse_max_distance",   400},
        {"player_skeleton_max_distance",   150},
        {"ai_skeleton_max_distance",       150},
        {"teammate_skeleton_max_distance", 300},
        {"minimap_size",                 180},
        {"minimap_range",                300},
        {"player_box_style",             0},

        // World ESP distances
        {"vehicle_max_distance",         2000},
        {"flag_max_distance",            2000},
        {"fob_max_distance",             2000},
        {"rally_max_distance",           500},
        {"minimap_size",                 180},
        {"minimap_range",                300},
    };

    float_settings = {
        {"aim_smoothness",         0.15f},
        {"aim_smoothness_y",       0.15f},
        {"aim_hw_gain",            1.0f},
        {"aim_fov_radius",         200.f},
        {"aim_projectile_speed",   715.f},

        {"player_font_size",       13.f},
        {"player_box_opacity",      1.f},
        {"player_skeleton_opacity", 1.f},
        {"player_box_fill_opacity", 0.15f},

        {"item_font_size",          12.f},
        {"item_price_min",          0.f},
        {"item_price_color_min",    0.f},
        {"item_price_color_max",    50000.f},

        {"player_arrows_size",      12.f},
        {"player_arrows_radius",   150.f},
        {"menu_ui_scale",           1.0f},
        {"player_box_fill_opacity", 0.15f},

        // Weapon feel (0 = vanilla, 100 = fully removed)
        {"robrun_recoil_amount",       100.f},
        {"robrun_sway_amount",         100.f},

        // World ESP font sizes
        {"vehicle_font_size",       12.f},
        {"flag_font_size",          12.f},
        {"fob_font_size",           12.f},
        {"rally_font_size",         12.f},
    };

    color_settings = {
        {"player_box_color",           {1.f, 1.f, 1.f, 1.f}},
        {"player_skeleton_color",      {1.f, 1.f, 1.f, 1.f}},
        {"player_health_bar_color",    {0.f, 1.f, 0.f, 1.f}},
        {"player_health_bar_bg_color", {0.2f, 0.2f, 0.2f, 1.f}},
        {"player_name_color",          {1.f, 1.f, 1.f, 1.f}},
        {"player_distance_color",      {1.f, 1.f, 1.f, 1.f}},
        {"player_tracers_color",       {1.f, 1.f, 1.f, 1.f}},
        {"player_head_dot_color",      {1.f, 0.f, 0.f, 1.f}},
        {"player_enemy_color",         {1.f, 0.1f, 0.1f, 1.f}},
        {"player_ai_color",             {1.f, 0.4f, 0.f, 1.f}},
        {"player_teammate_color",      {0.2f, 1.f, 0.3f, 1.f}},
        {"player_corpse_color",        {0.6f, 0.6f, 0.6f, 1.f}},


        // World ESP colors
        {"vehicle_color",              {1.f, 1.f, 1.f, 1.f}},       // unclaimed/unknown
        {"vehicle_team1_color",        {0.2f, 0.5f, 1.f, 1.f}},     // team 1 blue
        {"vehicle_team2_color",        {1.f, 0.3f, 0.3f, 1.f}},     // team 2 red
        {"flag_neutral_color",         {0.7f, 0.7f, 0.7f, 1.f}},
        {"flag_team1_color",           {0.2f, 0.5f, 1.f, 1.f}},
        {"flag_team2_color",           {1.f, 0.3f, 0.3f, 1.f}},
        {"flag_contested_color",       {0.8f, 0.2f, 0.9f, 1.f}},
        {"flag_capturing_color",       {1.f, 0.85f, 0.f, 1.f}},
        {"fob_team1_color",            {0.2f, 0.5f, 1.f, 1.f}},
        {"fob_team2_color",            {1.f, 0.3f, 0.3f, 1.f}},
        {"rally_color",                {1.f, 0.85f, 0.f, 1.f}},     // yellow (neutral/unknown)
        {"rally_team1_color",          {0.2f, 0.5f, 1.f, 1.f}},     // team 1 blue
        {"rally_team2_color",          {1.f, 0.3f, 0.3f, 1.f}},     // team 2 red

        {"aim_fov_color",              {1.f, 1.f, 1.f, 1.f}},
        {"aim_line_color",             {1.f, 0.2f, 0.2f, 0.9f}},


        {"player_arrows_color",        {1.f, 1.f, 1.f, 0.9f}},

        {"ai_box_color",{1.f,0.4f,0.f,1.f}},{"ai_skeleton_color",{1.f,0.4f,0.f,1.f}},
        {"ai_health_bar_color",{0.f,1.f,0.f,1.f}},{"ai_health_bar_bg_color",{0.2f,0.2f,0.2f,1.f}},
        {"ai_name_color",{1.f,0.4f,0.f,1.f}},{"ai_distance_color",{1.f,0.4f,0.f,1.f}},
        {"ai_head_dot_color",{1.f,0.f,0.f,1.f}},{"ai_tracers_color",{1.f,0.4f,0.f,1.f}},
        {"ai_arrows_color",{1.f,0.4f,0.f,0.9f}},

        {"teammate_box_color",{0.2f,1.f,0.3f,1.f}},{"teammate_skeleton_color",{0.2f,1.f,0.3f,1.f}},
        {"teammate_health_bar_color",{0.f,1.f,0.f,1.f}},{"teammate_health_bar_bg_color",{0.2f,0.2f,0.2f,1.f}},
        {"teammate_name_color",{0.2f,1.f,0.3f,1.f}},{"teammate_distance_color",{0.2f,1.f,0.3f,1.f}},
        {"teammate_head_dot_color",{0.2f,1.f,0.3f,1.f}},{"teammate_tracers_color",{0.2f,1.f,0.3f,1.f}},
        {"teammate_arrows_color",{0.2f,1.f,0.3f,0.9f}},
        {"crosshair_color",            {1.f, 1.f, 1.f, 0.9f}},

    };
}

// Helper function to convert old namespace calls to new settings system
ImColor get_color(const std::string& setting_name) {
    ImColor color = g_settings.get_color(setting_name);
    return ImColor(color.Value.x, color.Value.y, color.Value.z, color.Value.w);
}
