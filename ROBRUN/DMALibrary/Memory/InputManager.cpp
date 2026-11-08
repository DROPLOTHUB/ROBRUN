#include "pch.h"
#include "InputManager.h"
#include "Registry.h"
#include "Memory/Memory.h"

#include <iostream>

// KB_LOG writes to ROBRUN.log via the cout->file redirection Main.cpp sets up.
// DMALIB_LOG (printf) bypasses that redirection so only cout is reliable here.
#define KB_LOG(msg) do { std::cout << "[keyboard] " << msg << std::endl; } while (0)

//TODO: Restart winlogon.exe when it doesn't exist.
bool c_keys::InitKeyboard()
{
	std::string win = registry.QueryValue("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\CurrentBuild", e_registry_type::sz);
	int Winver = 0;
	if (!win.empty())
		Winver = std::stoi(win);
	else
	{
		KB_LOG("InitKeyboard: failed to read CurrentBuild from target registry (target PC unreachable via DMA?)");
		return false;
	}
	std::string ubr = registry.QueryValue("HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\UBR", e_registry_type::dword);
	int Ubr = 0;
	if (!ubr.empty())
		Ubr = std::stoi(ubr);
	else
	{
		KB_LOG("InitKeyboard: failed to read UBR from target registry");
		return false;
	}
	KB_LOG("InitKeyboard: target Windows build=" << Winver << " ubr=" << Ubr
	       << " (" << (Winver > 22000 ? "Win11 sig-scan path" : "Win10 EAT/PDB path") << ")");
	this->win_logon_pid = mem.GetPidFromName("winlogon.exe");
	if (Winver > 22000)
	{
		auto pids = mem.GetPidListFromName("csrss.exe");
		for (size_t i = 0; i < pids.size(); i++)
		{
			auto pid = pids[i];

			PVMMDLL_MAP_MODULEENTRY win32k_module_info;
			if (!VMMDLL_Map_GetModuleFromNameW(mem.vHandle, pid, const_cast<LPWSTR>(L"win32ksgd.sys"), &win32k_module_info, VMMDLL_MODULE_FLAG_NORMAL))
			{
				if (!VMMDLL_Map_GetModuleFromNameW(mem.vHandle, pid, const_cast<LPWSTR>(L"win32k.sys"), &win32k_module_info, VMMDLL_MODULE_FLAG_NORMAL))
				{
					DMALIB_LOG("failed to get module win32k info\n");
					return false;
				}
			}
			uintptr_t win32k_base = win32k_module_info->vaBase;
			size_t win32k_size = win32k_module_info->cbImageSize;
			//win32ksgd
			auto g_session_ptr = mem.FindSignature("48 8B 05 ? ? ? ? 48 8B 04 C8", win32k_base, win32k_base + win32k_size, pid);
			if (!g_session_ptr)
			{
				//win32k
				g_session_ptr = mem.FindSignature("48 8B 05 ? ? ? ? FF C9", win32k_base, win32k_base + win32k_size, pid);
				if (!g_session_ptr)
				{
					DMALIB_LOG("failed to find g_session_global_slots\n");
					return false;
				}
			}
			int relative = mem.Read<int>(g_session_ptr + 3, pid);
			uintptr_t g_session_global_slots = g_session_ptr + 7 + relative;
			uintptr_t user_session_state = 0;
			for (int i = 0; i < 4; i++)
			{
				user_session_state = mem.Read<uintptr_t>(mem.Read<uintptr_t>(mem.Read<uintptr_t>(g_session_global_slots, pid) + 8 * i, pid), pid);
				if (user_session_state > 0x7FFFFFFFFFFF)
					break;
			}

			PVMMDLL_MAP_MODULEENTRY win32kbase_module_info;
			if (!VMMDLL_Map_GetModuleFromNameW(mem.vHandle, pid, const_cast<LPWSTR>(L"win32kbase.sys"), &win32kbase_module_info, VMMDLL_MODULE_FLAG_NORMAL))
			{
				DMALIB_LOG("failed to get module win32kbase info\n");
				return false;
			}
			uintptr_t win32kbase_base = win32kbase_module_info->vaBase;
			size_t win32kbase_size = win32kbase_module_info->cbImageSize;

			uintptr_t ptr = mem.FindSignature("48 8D 90 ? ? ? ? E8 ? ? ? ? 0F 57 C0", win32kbase_base, win32kbase_base + win32kbase_size, pid);
			// PostUpdateKeyStateEvent
			uintptr_t ptr2 = mem.FindSignature("48 89 98 C8 4A ? ?", win32kbase_base, win32kbase_base + win32kbase_size, pid);
			// CMouseProcessor::RootCursorAsync::SynchronizeCursorPosition(void)
			uint32_t session_offset = 0x0;
			uint32_t session_offset2 = 0x0;
			if (ptr)
			{
				session_offset = mem.Read<uint32_t>(ptr + 3, pid);
				session_offset2 = mem.Read<uint32_t>(ptr2 + 3, pid);
				gafAsyncKeyStateExport = user_session_state + session_offset;
				gptCursorAsyncExport = user_session_state + session_offset2;
				KB_LOG("InitKeyboard (Win11): session_state=0x" << std::hex << user_session_state
				       << " key_off=0x" << session_offset
				       << " gafAsyncKeyState=0x" << gafAsyncKeyStateExport << std::dec);
			}
			else
			{
				DMALIB_LOG("failed to find offset for gafAyncKeyStateExport\n");
				KB_LOG("InitKeyboard (Win11): pattern scan failed for key-state offset in win32kbase.sys");
				return false;
			}

			if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF) break;
		}
		if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF)
		{
			KB_LOG("InitKeyboard: SUCCESS via Win11 sig-scan path, gafAsyncKeyState=0x"
			       << std::hex << gafAsyncKeyStateExport << std::dec);
			return true;
		}
		KB_LOG("InitKeyboard: FAILED — Win11 sig-scan path could not resolve gafAsyncKeyState");
		return false;
	}
	else
	{
		PVMMDLL_MAP_EAT eat_map = NULL;
		PVMMDLL_MAP_EATENTRY eat_map_entry;
		bool result = VMMDLL_Map_GetEATU(mem.vHandle, mem.GetPidFromName("winlogon.exe") | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, const_cast<LPSTR>("win32kbase.sys"), &eat_map);
		if (!result)
		{
			KB_LOG("InitKeyboard: VMMDLL_Map_GetEATU failed (winlogon.exe pid="
			       << this->win_logon_pid << ") — DMA link to target may be unstable");
			return false;
		}

		if (eat_map->dwVersion != VMMDLL_MAP_EAT_VERSION)
		{
			KB_LOG("InitKeyboard: EAT map version mismatch");
			VMMDLL_MemFree(eat_map);
			eat_map_entry = NULL;
			return false;
		}

		for (DWORD i = 0; i < eat_map->cMap; i++)
		{
			eat_map_entry = eat_map->pMap + i;
			if (strcmp(eat_map_entry->uszFunction, "gafAsyncKeyState") == 0)
			{
				gafAsyncKeyStateExport = eat_map_entry->vaFunction;
				break;
			}
			if (strcmp(eat_map_entry->uszFunction, "gptCursorAsync") == 0)
			{
				gptCursorAsyncExport = eat_map_entry->vaFunction;
			}
		}

		VMMDLL_MemFree(eat_map);
		eat_map = NULL;
		if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
		{
			KB_LOG("InitKeyboard: gafAsyncKeyState not in EAT, falling back to PDB symbol lookup");
			PVMMDLL_MAP_MODULEENTRY module_info;
			auto result = VMMDLL_Map_GetModuleFromNameW(mem.vHandle, mem.GetPidFromName("winlogon.exe") | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, static_cast<LPCWSTR>(L"win32kbase.sys"), &module_info, VMMDLL_MODULE_FLAG_NORMAL);
			if (!result)
			{
				DMALIB_LOG("failed to get module info\n");
				KB_LOG("InitKeyboard: failed to get win32kbase.sys module info from target");
				return false;
			}

			char str[32] = {};
			// BUG FIX (from CSGO source): VMMDLL_PdbLoad is attempted first.
			// If it succeeds the resolved symbol address is now correctly stored
			// in gafAsyncKeyStateExport (was previously lost in a local variable).
			// If it fails, the pattern-scan fallback below takes over so keyboard
			// still works without a symbol server or local PDB cache.
			if (!VMMDLL_PdbLoad(mem.vHandle, mem.GetPidFromName("winlogon.exe") | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, module_info->vaBase, str))
			{
				DMALIB_LOG("failed to load pdb\n");
				KB_LOG("InitKeyboard: VMMDLL_PdbLoad FAILED for win32kbase.sys @ 0x"
				       << std::hex << module_info->vaBase << std::dec
				       << " — check that symsrv.dll / dbghelp.dll / info.db sit next to vmm.dll "
				       << "and that this PC has internet access to msdl.microsoft.com (MS symbol server). "
				       << "Falling back to pattern scan...");
			}
			else
			{
				KB_LOG("InitKeyboard: PDB loaded OK, module='" << str << "'");

				uintptr_t gafAsyncKeyState = 0;
				if (!VMMDLL_PdbSymbolAddress(mem.vHandle, str, const_cast<LPSTR>("gafAsyncKeyState"), &gafAsyncKeyState))
				{
					DMALIB_LOG("failed to find gafAsyncKeyState\n");
					KB_LOG("InitKeyboard: PDB loaded but gafAsyncKeyState symbol lookup failed — falling back to pattern scan");
				}
				else if (gafAsyncKeyState > 0x7FFFFFFFFFFF)
				{
					// BUG FIX: assign to the class member, not just the local var
					gafAsyncKeyStateExport = gafAsyncKeyState;
					KB_LOG("InitKeyboard: PDB path resolved gafAsyncKeyState=0x"
					       << std::hex << gafAsyncKeyStateExport << std::dec);
				}

				if (gptCursorAsyncExport == 0)
				{
					uintptr_t gptCursorAsync = 0;
					if (VMMDLL_PdbSymbolAddress(mem.vHandle, str, const_cast<LPSTR>("gptCursorAsync"), &gptCursorAsync)
						&& gptCursorAsync > 0x7FFFFFFFFFFF)
					{
						gptCursorAsyncExport = gptCursorAsync;
					}
				}
			}

			// --- Pattern scan fallback (works without PDB / symbol server) ---
			// Scans win32kbase.sys for RIP-relative references to gafAsyncKeyState.
			// Covers Win10 19041 through 22000. Same approach as CSGO-External.
			if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
			{
				KB_LOG("InitKeyboard: trying pattern scan for gafAsyncKeyState in win32kbase.sys ...");
				uintptr_t base   = module_info->vaBase;
				size_t    sz     = module_info->cbImageSize;
				DWORD scan_pid   = mem.GetPidFromName("winlogon.exe") | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;

				const struct { const char* sig; int disp_off; } kPatterns[] = {
					{ "48 8D 0D ? ? ? ? 45 33 D2",           3 },   // Win10 20H2 / 21H1
					{ "4C 8D 05 ? ? ? ? 48 83 EC ? 41 FF",   3 },   // Win10 21H2
					{ "48 8B 3D ? ? ? ? 45 33 D2",           3 },   // Win10 22H2
					{ "48 8D 0D ? ? ? ? 4C 8D 05",           3 },   // Win11 22000
					{ "48 8B 35 ? ? ? ? 48 39 70",           3 },   // Alt Win10 variant
				};

				bool scan_ok = false;
				for (const auto& p : kPatterns)
				{
					uintptr_t ptr = mem.FindSignature(p.sig, base, base + sz, scan_pid);
					if (!ptr) continue;

					int32_t rel = mem.Read<int32_t>(ptr + p.disp_off, scan_pid);
					uintptr_t candidate = ptr + p.disp_off + 4 + rel;
					if (candidate > 0x7FFFFFFFFFFF)
					{
						gafAsyncKeyStateExport = candidate;
						KB_LOG("InitKeyboard: pattern scan SUCCESS — sig=\"" << p.sig
						       << "\" gafAsyncKeyState=0x" << std::hex << candidate << std::dec);
						scan_ok = true;
						break;
					}
				}

				if (!scan_ok)
				{
					DMALIB_LOG("keyboard: all methods failed — DMA key input disabled\n");
					KB_LOG("InitKeyboard: FAILED — EAT + PDB + pattern scan all returned nothing. "
					       "DMA keyboard input disabled. Check vmm.dll, symsrv.dll, dbghelp.dll versions.");
				}
			}
		}
		if (gafAsyncKeyStateExport > 0x7FFFFFFFFFFF)
		{
			KB_LOG("InitKeyboard: SUCCESS via " << (gptCursorAsyncExport ? "EAT" : "PDB/scan")
			       << " path, gafAsyncKeyState=0x" << std::hex << gafAsyncKeyStateExport << std::dec);
			return true;
		}
		KB_LOG("InitKeyboard: FAILED — gafAsyncKeyStateExport never resolved to a valid kernel address");
		return false;
	}
}

void c_keys::UpdateKeys()
{
	uint8_t previous_key_state_bitmap[64] = { 0 };
	memcpy(previous_key_state_bitmap, state_bitmap, 64);

	VMMDLL_MemReadEx(mem.vHandle, this->win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY, gafAsyncKeyStateExport, reinterpret_cast<PBYTE>(&state_bitmap), 64, NULL, VMMDLL_FLAG_NOCACHE);
	for (int vk = 0; vk < 256; ++vk)
		if ((state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2) && !(previous_key_state_bitmap[(vk * 2 / 8)] & 1 << vk % 4 * 2))
			previous_state_bitmap[vk / 8] |= 1 << vk % 8;
}

bool c_keys::IsKeyDown(uint32_t virtual_key_code)
{
	if (gafAsyncKeyStateExport < 0x7FFFFFFFFFFF)
		return false;
	// Light refresh for game-PC keys. Capped so Present never hammers the
	// FPGA (entity worker also refreshes). 50 ms is fine for hold-binds.
	if (std::chrono::system_clock::now() - start > std::chrono::milliseconds(50))
	{
		UpdateKeys();
		start = std::chrono::system_clock::now();
	}
	return state_bitmap[(virtual_key_code * 2 / 8)] & 1 << virtual_key_code % 4 * 2;
}

bool c_keys::IsKeyboardWorking() const
{
	return gafAsyncKeyStateExport > 0x7FFFFFFFFFFF;
}

TagPoint c_keys::GetCursorPosition()
{
	TagPoint cursor_position = { 0, 0 };

	if (gptCursorAsyncExport < 0x7FFFFFFFFFFF)
		return cursor_position;

	VMMDLL_MemReadEx(mem.vHandle,
		this->win_logon_pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY,
		gptCursorAsyncExport,
		reinterpret_cast<PBYTE>(&cursor_position),
		sizeof(TagPoint),
		NULL,
		VMMDLL_FLAG_NOCACHE);

	return cursor_position;
}