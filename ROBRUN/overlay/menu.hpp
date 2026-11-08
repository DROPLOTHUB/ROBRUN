#pragma once

#include "overlay.hpp"
#include "../libs/imgui/imgui.h"

// Forward declarations: the menu is bound to the ROB RUN ESP snapshot, but it
// does not need the whole world/SDK headers to compile.
namespace robrun
{
    struct esp_settings;
    struct esp_snapshot;
}

// Called once per frame from the overlay render callback.
//   cfg        - live ESP settings, edited in place
//   snap       - last immutable snapshot published by the worker
//   re_resolve - set to true when the user clicks "Re-resolve world"
void render_menu(robrun::esp_settings& cfg,
                 const robrun::esp_snapshot& snap,
                 bool& re_resolve);
