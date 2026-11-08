#pragma once

#include <d3d11.h>
#include <string>
#include <vector>
#include <map>
#include <windows.h>
#include "../includes.hpp"
#include "../libs/imgui/imgui.h"
#include "../libs/imgui/backends/imgui_impl_dx11.h"
#include "../libs/imgui/backends/imgui_impl_win32.h"
#include "../libs/imgui/imgui_internal.h"

#ifndef OVERLAY_H_
#define OVERLAY_H_

constexpr int menu_anim_time = 350;
constexpr int breath_anim_time = 1000;

// Fuser RGB-keys near-black from the 2nd PC (clear must stay black).
// Plate fills need enough luminance to survive keying; pure white (255³)
// also punches through on some key levels — use soft steel instead.
namespace fuser_col {
	inline ImU32 plate(int a = 210) { return IM_COL32(34, 38, 52, a); }
	inline ImU32 edge(int a = 90)   { return IM_COL32(150, 175, 200, a); }
	inline ImU32 meta(int a = 255)  { return IM_COL32(190, 205, 220, a); }
	inline ImU32 soft(int a = 255)  { return IM_COL32(200, 215, 230, a); }
}

class c_overlay {
private:
	HWND								window_handle;
	WNDCLASSEX							window_class;
	ID3D11Device* d3d_device;
	ID3D11DeviceContext* device_context;
	IDXGISwapChain* swap_chain;
	ID3D11RenderTargetView* render_target_view;
	ImDrawList* draw_list;

	struct s_font {
		ImFont* esp_font;
		ImFont* menu_font;
		ImFont* ico = nullptr;
		ImFont* ico_two = nullptr;
		ImFont* ico_three = nullptr;
		ImFont* Jaka = nullptr;
		FLOAT font_size;
	};

private:
	template <typename T>
	inline VOID safe_release(T*& p);

	BOOL init_device();
	VOID dest_device();

	VOID init_imgui();
	VOID dest_imgui();

	VOID init_render_target();
	VOID dest_render_target();
	const VOID render(FLOAT width, FLOAT height);

	void sync_imgui_input();
	void apply_menu_window_style();
	void handle_wm_size(unsigned width, unsigned height);
	static LRESULT CALLBACK static_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	static constexpr const char* k_overlay_class_name = "RobRunOverlayWnd";
	bool owns_window_ = false;
	bool last_menu_open_for_cursor_ = false;
	bool imgui_callbacks_ready_ = false;

	struct OverlayMonitorInfo {
		RECT     rect{};
		HMONITOR hMonitor = nullptr;
		unsigned refresh_hz = 60;
	};
	std::vector<OverlayMonitorInfo> monitors_;
	int      monitor_index_ = 0;
	unsigned display_refresh_hz_ = 60;
	bool     last_win_shift_arrow_combo_ = false;

	void detect_monitors();
	void apply_current_monitor_geometry();
	bool recreate_swap_chain_for_monitor();
	void poll_monitor_switch_hotkey();

	static BOOL CALLBACK monitor_enum_proc(HMONITOR hMonitor, HDC hdc, LPRECT lprcMonitor, LPARAM dwData);
public:
	c_overlay();
	~c_overlay();

	BOOL msg_loop();
	VOID render_callback();

	VOID init_draw_list();
	VOID crosshair(const FLOAT aSize, ImU32 color);
	VOID box(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color, const FLOAT line_width = 1.f);
	VOID boxfilled(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color);
	VOID corner_box(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color,
	                 const FLOAT line_width = 1.5f, float corner_frac = 0.28f);
	// Slim tactical brackets — shorter arms, accent tips (silhouette model).
	VOID bracket_box(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color,
	                 const FLOAT line_width = 1.35f);
	VOID line(const ImVec2& point1, const ImVec2 point2, ImU32 color, const FLOAT line_width = 1.f);
	VOID line_outlined(const ImVec2& a, const ImVec2& b, ImU32 color, float width = 1.5f);
	// Bone segment + joint dots for a readable body silhouette.
	VOID bone_segment(const ImVec2& a, const ImVec2& b, ImU32 color, float width = 1.6f);
	VOID circle(const ImVec2& point, const FLOAT radius, ImU32 color, const FLOAT width = 1.f);
	VOID circle_filled(const ImVec2& point, const FLOAT radius, ImU32 color);
	VOID text(const ImVec2& pos, const std::string& text, ImU32 color = IM_COL32(200, 215, 230, 255), bool outline = true, bool background = true, bool center_x = false, float font_size = 10.0f);
	VOID text(const ImVec2& pos, const std::string& text, float font_size, ImU32 color, bool outline = true);
	// Centered name/distance chip with soft plate behind text.
	VOID text_chip(const ImVec2& center_top, const std::string& text, float font_size, ImU32 text_color,
	               ImU32 plate = IM_COL32(34, 38, 52, 170));
	// Two-line identity plate (name / meta) — one composition under or over the pawn.
	VOID identity_plate(const ImVec2& center_top, const std::string& title, const std::string& meta,
	                    float font_size, ImU32 title_color);
	// Ground loot pin: kind glyph + optional soft halo (silhouette loot model).
	// style: 0=loose diamond, 1=container square, 2=corpse cross, 3=quest pin, 4=airdrop chevron
	VOID loot_pin(const ImVec2& center, ImU32 color, int style = 0, float size = 4.f,
	              bool halo = false);
	VOID radial_gradient(const ImVec2& center, float radius, ImU32 col_in, ImU32 col_out);

	BOOL in_screen(const ImVec2& pos);
	HWND get_hwnd() { return window_handle; };
	ID3D11Device* get_device() { return d3d_device; }
public:
	FLOAT window_width, window_height;
	bool exit, ready, show_menu = true;
	bool clear_render, cleared_renderer = false;
	INT breath, menu_ticks;
	s_font* font;
};


inline c_overlay* overlay = nullptr;

#endif