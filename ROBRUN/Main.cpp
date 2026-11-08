#include "includes.hpp"
#include "overlay/overlay.hpp"
#include "overlay/menu.hpp"
#include "game/robrun/basic_esp.hpp"

#include "DMALibrary/Memory/Memory.h"
#include "utils/robrun_log.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")

// =============================================================================
// ROB RUN DMA ESP - entry point
//
// Flow:
//   setup VMM -> attach "ROB RUN.exe" -> resolve UnityPlayer/GameAssembly ->
//   resolve the ECS world once -> start overlay -> worker thread refreshes
//   entities while the render thread draws.
//
// The ESP itself is header-only under game/robrun/.
// =============================================================================

// Keep the scheduler at 1 ms so the worker/render loops are not clamped to the
// default ~15.6 ms Windows quantum.
struct timer_resolution_guard
{
    bool acquired = false;
    timer_resolution_guard()  { acquired = (timeBeginPeriod(1) == TIMERR_NOERROR); }
    ~timer_resolution_guard() { if (acquired) timeEndPeriod(1); }
};

uintptr_t game_base = NULL;
std::string g_settings_path;

// Module bases resolved once at startup and read by the header-only SDK.
namespace sdk::unity
{
	uintptr_t unity_base = 0;
	uintptr_t game_assembly_base = 0;
}

int screen_get_x = GetSystemMetrics(SM_CXSCREEN);
int screen_get_y = GetSystemMetrics(SM_CYSCREEN);
Aspect::Vector2 screen_size(static_cast<float>(screen_get_x), static_cast<float>(screen_get_y));

// -----------------------------------------------------------------------------
// ESP state
// -----------------------------------------------------------------------------
static robrun::esp g_esp;

// Worker-owned world state. The render thread never takes this lock; it reads
// g_snapshot instead, so a slow DMA pass cannot freeze or flicker the menu.
static std::mutex g_esp_mutex;

// Render snapshot, swapped by the worker after every update. shared_ptr keeps
// the per-frame hand-off to a pointer copy (no entity/string copies at 500fps).
static std::mutex g_snap_mutex;
static std::shared_ptr<const robrun::esp_snapshot> g_snapshot;

// Set by the menu, consumed by the worker.
static std::atomic<bool> g_resolve_request{ false };

// -----------------------------------------------------------------------------
// Overlay frame callback
// -----------------------------------------------------------------------------
void c_overlay::render_callback()
{
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// INSERT toggles the menu. Read the game PC keyboard via the DMA helper and
	// also the local one so the menu can never get stuck closed.
	{
		static bool s_insert_prev = false;
		const bool insert_now =
			is_keybind_down(VK_INSERT) ||
			((GetAsyncKeyState(VK_INSERT) & 0x8000) != 0);
		if (insert_now && !s_insert_prev)
			g_settings.set_bool("menu_open", !g_settings.get_bool("menu_open"));
		s_insert_prev = insert_now;
	}

	// Grab the latest worker snapshot. This lock is only ever held for a
	// shared_ptr copy, never across DMA reads.
	std::shared_ptr<const robrun::esp_snapshot> snap;
	{
		std::lock_guard<std::mutex> lock(g_snap_mutex);
		snap = g_snapshot;
	}
	if (!snap)
		return;

	// Always render: the menu must never depend on the worker's progress.
	g_esp.draw(*snap);

	// Styled menu. It reads the same snapshot and handles its own visibility
	// (menu_open), so it is safe to call every frame.
	bool re_resolve = false;
	render_menu(g_esp.cfg, *snap, re_resolve);
	if (re_resolve)
		g_resolve_request.store(true, std::memory_order_release);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
static int real_main()
{
	timer_resolution_guard _timer_res_guard;

	// Logs/settings live next to the exe no matter which drive is mapped.
	char exe_path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
	std::string exe_dir(exe_path);
	const auto slash = exe_dir.find_last_of("\\/");
	if (slash != std::string::npos)
		exe_dir = exe_dir.substr(0, slash + 1);

	const std::string log_path = exe_dir + "ROBRUN.log";
	g_settings_path            = exe_dir + "ROBRUN.json";

	{
		std::ifstream cfg(g_settings_path);
		if (cfg.good())
			g_settings.load_settings(g_settings_path);
	}

	// Dedicated log file. robrun_log owns the handle; stdout/stderr are redirected
	// to the same buffer so keyboard / DMALibrary / overlay prints also land in
	// the .log. robrun_log::write() additionally falls back to the file handle if
	// the redirect ever fails.
	{
		std::remove(log_path.c_str());
		const bool log_ok = robrun_log::init(log_path);
		if (log_ok)
		{
			std::cout.rdbuf(robrun_log::stream().rdbuf());
			std::cerr.rdbuf(robrun_log::stream().rdbuf());
			std::cout << std::unitbuf;
			std::cerr << std::unitbuf;
		}
	}

	ROBRUN_LOG("==========================================");
	ROBRUN_LOG("ROB RUN DMA ESP");
	ROBRUN_LOG("Log file: " << log_path);
	ROBRUN_LOG("Settings: " << g_settings_path);
	ROBRUN_LOG("Build: " << __DATE__ << " " << __TIME__);
	ROBRUN_LOG("==========================================");

	ROBRUN_LOG("DMA setup starting (FPGA / vmm)...");
	if (!driver->setup())
	{
		ROBRUN_LOG("FATAL: Failed to initialize DMA (device, mmap, or vmm.dll)!");
		return 1;
	}
	ROBRUN_LOG("DMA device ready.");
	Beep(800, 200);

	ROBRUN_LOG("Waiting for ROB RUN.exe...");
	while (!driver->target("ROB RUN.exe"))
	{
		ROBRUN_LOG("Game not found, retrying in 2 seconds...");
		std::this_thread::sleep_for(std::chrono::seconds(2));
	}
	ROBRUN_LOG("Found ROB RUN.exe! PID=" << driver->target());

	if (mem.GetKeyboard()->InitKeyboard())
		ROBRUN_LOG("DMA keyboard helper initialized.");
	else
		ROBRUN_LOG("WARN: win32k DMA keyboard unavailable (INSERT may need local focus).");

	ROBRUN_LOG("Waiting 5s for Unity modules to load...");
	std::this_thread::sleep_for(std::chrono::seconds(5));

	auto resolve_modules = [&]() {
		game_base                    = driver->getbase();
		sdk::unity::unity_base       = mem.GetBaseDaddy("UnityPlayer.dll");
		sdk::unity::game_assembly_base = mem.GetBaseDaddy("GameAssembly.dll");
	};

	resolve_modules();
	for (int tries = 0; tries < 30 && (!game_base || !sdk::unity::game_assembly_base); ++tries)
	{
		ROBRUN_LOG("Waiting for modules... exe=0x" << std::hex << game_base
		        << " unity=0x" << sdk::unity::unity_base
		        << " ga=0x" << sdk::unity::game_assembly_base << std::dec);
		std::this_thread::sleep_for(std::chrono::seconds(2));
		if (tries % 3 == 0)
		{
			mem.RefreshDMA();
			mem.FixCr3();
			driver->target("ROB RUN.exe");
		}
		resolve_modules();
	}

	ROBRUN_LOG("Modules OK");
	ROBRUN_LOG("  ROB RUN.exe      = 0x" << std::hex << game_base << std::dec);
	ROBRUN_LOG("  UnityPlayer.dll  = 0x" << std::hex << sdk::unity::unity_base << std::dec);
	ROBRUN_LOG("  GameAssembly.dll = 0x" << std::hex << sdk::unity::game_assembly_base << std::dec);

	if (!sdk::unity::game_assembly_base)
	{
		ROBRUN_LOG("FATAL: GameAssembly.dll is required (all TypeInfo RVAs live there).");
		return 2;
	}

	// First resolve before the overlay exists - no frame is blocked by the BFS.
	ROBRUN_LOG("Resolving ROB RUN world...");
	g_esp.cfg.enabled = true;
	g_esp.cfg.show_debug_status = true;
	g_esp.initialize();
	{
		auto s = std::make_shared<const robrun::esp_snapshot>(g_esp.make_snapshot());
		std::lock_guard<std::mutex> lock(g_snap_mutex);
		g_snapshot = std::move(s);
	}

	ROBRUN_LOG("Creating overlay...");
	overlay = new c_overlay();
	if (!overlay || reinterpret_cast<uintptr_t>(overlay->get_hwnd()) == 0)
	{
		ROBRUN_LOG("FATAL: Overlay failed to initialize (no HWND).");
		return 4;
	}
	ROBRUN_LOG("Overlay ready. INSERT toggles the menu, END quits.");

	std::atomic<bool> running{ true };
	std::thread worker([&] {
		using namespace std::chrono;
		while (running.load(std::memory_order_acquire))
		{
			if (g_resolve_request.exchange(false, std::memory_order_acq_rel))
			{
				std::lock_guard<std::mutex> lock(g_esp_mutex);
				try { g_esp.request_resolve(); } catch (...) {}
			}

			{
				std::lock_guard<std::mutex> lock(g_esp_mutex);
				try { g_esp.update(); } catch (...) {}
				auto s = std::make_shared<const robrun::esp_snapshot>(g_esp.make_snapshot());
				{
					std::lock_guard<std::mutex> snap_lock(g_snap_mutex);
					g_snapshot = std::move(s);
				}
				// Bones after the publish. The render thread is already drawing
				// the new positions while this scatter is in flight.
				try { g_esp.refresh_visuals(); } catch (...) {}
			}
			// Position scatter is the only thing on the hot path now. 1 ms
			// keeps the worker from spinning the FPGA without capping the
			// boxes at the old ~60 Hz.
			std::this_thread::sleep_for(milliseconds(1));
		}
	});

	while (running.load(std::memory_order_acquire))
	{
		if (GetAsyncKeyState(VK_END) & 0x8000)
			break;

		try
		{
			if (overlay && reinterpret_cast<uintptr_t>(overlay->get_hwnd()) != 0)
			{
				if (!overlay->msg_loop())
					break;
			}
		}
		catch (...) {}

		// Without vsync, Present(0) spins the loop at several hundred FPS and
		// burns CPU that the DMA worker needs. Cap it to ~240 FPS instead of a
		// pure yield; the ESP is redrawn far faster than the 60-90 Hz snapshot
		// anyway.
		if (g_settings.get_bool("vsync"))
			; // Present already paced
		else
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
	}

	running.store(false, std::memory_order_release);
	if (worker.joinable())
		worker.join();

	g_settings.save_settings(g_settings_path);
	ROBRUN_LOG("Shutting down.");
	return 0;
}

int main()
{
	return real_main();
}
