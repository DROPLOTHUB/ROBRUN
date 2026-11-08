#include "pch.h"
#include "Memory.h"

#include <thread>
#include <iostream>
#include <algorithm>
#include <vector>
#include <cctype>
#include <cstring>
#include <string>

Memory::Memory()
{
	// Only initialize the key - libraries will be loaded in Init()
	this->key = std::make_shared<c_keys>();
}

Memory::~Memory()
{
	VMMDLL_Close(this->vHandle);
	DMA_INITIALIZED = false;
	PROCESS_INITIALIZED = false;
}

bool Memory::DumpMemoryMap(bool debug)
{
	LPCSTR args[] = {const_cast<LPCSTR>(""), const_cast<LPCSTR>("-device"), const_cast<LPCSTR>("fpga://algo=0"), const_cast<LPCSTR>(""), const_cast<LPCSTR>("")};
	int argc = 3;
	if (debug)
	{
		args[argc++] = const_cast<LPCSTR>("-v");
		args[argc++] = const_cast<LPCSTR>("-printf");
	}

	VMM_HANDLE handle = VMMDLL_Initialize(argc, args);
	if (!handle)
	{
		DMALIB_LOG("[!] Failed to open a VMM Handle\n");
		return false;
	}

	PVMMDLL_MAP_PHYSMEM pPhysMemMap = NULL;
	if (!VMMDLL_Map_GetPhysMem(handle, &pPhysMemMap))
	{
		DMALIB_LOG("[!] Failed to get physical memory map\n");
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->dwVersion != VMMDLL_MAP_PHYSMEM_VERSION)
	{
		DMALIB_LOG("[!] Invalid VMM Map Version\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}

	if (pPhysMemMap->cMap == 0)
	{
		printf("[!] Failed to get physical memory map\n");
		VMMDLL_MemFree(pPhysMemMap);
		VMMDLL_Close(handle);
		return false;
	}
	//Dump map to file
	std::stringstream sb;
	for (DWORD i = 0; i < pPhysMemMap->cMap; i++)
	{
		sb << std::setfill('0') << std::setw(4) << i << "  " << std::hex << pPhysMemMap->pMap[i].pa << "  -  " << (pPhysMemMap->pMap[i].pa + pPhysMemMap->pMap[i].cb - 1) << "  ->  " << pPhysMemMap->pMap[i].pa << std::endl;
	}

	auto temp_path = std::filesystem::temp_directory_path();
	std::ofstream nFile(temp_path.string() + "\\mmap.txt");
	nFile << sb.str();
	nFile.close();

	VMMDLL_MemFree(pPhysMemMap);
	DMALIB_LOG("Successfully dumped memory map to file!\n");
	//Little sleep to make sure it's written to file.
	Sleep(3000);
	VMMDLL_Close(handle);
	return true;
}

unsigned char abort2[4] = {0x10, 0x00, 0x10, 0x00};

bool Memory::SetFPGA()
{
	ULONG64 qwID = 0;
	ULONG64 qwVersionMajor = 0;
	ULONG64 qwVersionMinor = 0;
	if (!VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &qwID) && VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MAJOR, &qwVersionMajor) && VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_VERSION_MINOR, &qwVersionMinor))
	{
		DMALIB_LOG("[!] Failed to lookup FPGA device, Attempting to proceed\n\n");
		return false;
	}

	DMALIB_LOG("[+] VMMDLL_ConfigGet");
	DMALIB_LOG(" ID = %lli", qwID);
	DMALIB_LOG(" VERSION = %lli.%lli\n", qwVersionMajor, qwVersionMinor);

	if ((qwVersionMajor >= 4) && ((qwVersionMajor >= 5) || (qwVersionMinor >= 7)))
	{
		HANDLE handle;
		LC_CONFIG config = {.dwVersion = LC_CONFIG_VERSION, .szDevice = "existing"};
		handle = LcCreate(&config);
		if (!handle)
		{
			DMALIB_LOG("[!] Failed to create FPGA device\n");
			return false;
		}

		LcCommand(handle, LC_CMD_FPGA_CFGREGPCIE_MARKWR | 0x002, 4, reinterpret_cast<PBYTE>(&abort2), NULL, NULL);
		DMALIB_LOG("[-] Register auto cleared\n");
		LcClose(handle);
	}

	return true;
}

bool Memory::InitVmmOnly(bool memMap, bool debug)
{
	if (!modules.VMM || !modules.FTD3XX || !modules.LEECHCORE)
	{
		DMALIB_LOG("loading libraries...\n");
		modules.VMM = LoadLibraryA("vmm.dll");
		modules.FTD3XX = LoadLibraryA("FTD3XX.dll");
		modules.LEECHCORE = LoadLibraryA("leechcore.dll");

		if (!modules.VMM || !modules.FTD3XX || !modules.LEECHCORE)
		{
			DMALIB_LOG("vmm: %p\n", static_cast<void*>(modules.VMM));
			DMALIB_LOG("ftd: %p\n", static_cast<void*>(modules.FTD3XX));
			DMALIB_LOG("leech: %p\n", static_cast<void*>(modules.LEECHCORE));
			DMALIB_LOG("[!] Could not load a library\n");
			return false;
		}

		DMALIB_LOG("Successfully loaded libraries!\n");
	}

	if (!DMA_INITIALIZED)
	{
	reinit:
		LPCSTR args[] = {const_cast<LPCSTR>("-norefresh"), const_cast<LPCSTR>("-device"), const_cast<LPCSTR>("fpga://algo=0"), const_cast<LPCSTR>(""), const_cast<LPCSTR>(""), const_cast<LPCSTR>(""), const_cast<LPCSTR>("")};
		DWORD argc = 3;
		if (debug)
		{
			args[argc++] = const_cast<LPCSTR>("-v");
			args[argc++] = const_cast<LPCSTR>("-printf");
		}

		std::string path = "";
		if (memMap)
		{
			auto temp_path = std::filesystem::temp_directory_path();
			path = (temp_path.string() + "\\mmap.txt");
			bool dumped = false;
			if (!std::filesystem::exists(path))
				dumped = this->DumpMemoryMap(debug);
			else
				dumped = true;
			DMALIB_LOG("dumping memory map to file...\n");
			if (!dumped)
			{
				DMALIB_LOG("[!] ERROR: Could not dump memory map!\n");
				DMALIB_LOG("Defaulting to no memory map!\n");
			}
			else
			{
				DMALIB_LOG("Dumped memory map!\n");

				args[argc++] = const_cast<LPSTR>("-memmap");
				args[argc++] = const_cast<LPSTR>(path.c_str());
			}
		}
		this->vHandle = VMMDLL_Initialize(argc, args);
		if (!this->vHandle)
		{
			if (memMap)
			{
				memMap = false;
				DMALIB_LOG("[!] Initialization failed with Memory map? Try without MMap\n");
				goto reinit;
			}
			DMALIB_LOG("[!] Initialization failed! Is the DMA in use or disconnected?\n");
			return false;
		}

		ULONG64 FPGA_ID, DEVICE_ID;

		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_FPGA_ID, &FPGA_ID);
		VMMDLL_ConfigGet(this->vHandle, LC_OPT_FPGA_DEVICE_ID, &DEVICE_ID);

		DMALIB_LOG("FPGA ID: %llu\n", FPGA_ID);
		DMALIB_LOG("DEVICE ID: %llu\n", DEVICE_ID);

		if (!this->SetFPGA())
		{
			DMALIB_LOG("[!] Could not set FPGA!\n");
			VMMDLL_Close(this->vHandle);
			return false;
		}

		DMA_INITIALIZED = TRUE;
	}
	else
		DMALIB_LOG("DMA already initialized!\n");

	return true;
}

bool Memory::AttachProcess(const std::string& process_name)
{
	PROCESS_INITIALIZED = FALSE;
	current_process = CurrentProcessInformation{};

	if (!DMA_INITIALIZED || !this->vHandle)
	{
		DMALIB_LOG("[!] AttachProcess: DMA device not initialized\n");
		return false;
	}

	current_process.PID = GetPidFromName(process_name);
	if (!current_process.PID)
	{
		DMALIB_LOG("[!] Could not get PID from name!\n");
		return false;
	}
	current_process.process_name = process_name;
	if (!this->FixCr3())
		std::cout << "[!] Failed to fix CR3" << std::endl;
	else
		std::cout << "[+] CR3 fixed" << std::endl;

	current_process.base_address = GetBaseDaddy(process_name);
	if (!current_process.base_address)
	{
		DMALIB_LOG("[!] Could not get base address!\n");
		return false;
	}

	current_process.base_size = GetBaseSize(process_name);
	if (!current_process.base_size)
	{
		DMALIB_LOG("[!] Could not get base size!\n");
		return false;
	}

	DMALIB_LOG("Process information of %s\n", process_name.c_str());
	DMALIB_LOG("PID: %i\n", current_process.PID);
	DMALIB_LOG("Base Address: 0x%llx\n", current_process.base_address);
	DMALIB_LOG("Base Size: 0x%llx\n", current_process.base_size);

	PROCESS_INITIALIZED = TRUE;

	return true;
}

bool Memory::Init(std::string process_name, bool memMap, bool debug)
{
	if (!InitVmmOnly(memMap, debug))
		return false;
	return AttachProcess(process_name);
}

void Memory::RefreshDMA()
{
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_FREQ_TLB, 1);
	// Force module / process map rebuild — stale maps often lose GameAssembly.dll
	// after mid-raid DMA thrash while UnityPlayer.dll still resolves.
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_FREQ_MEM, 1);
	VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
}

DWORD Memory::GetPidFromName(std::string process_name)
{
	DWORD pid = 0;
	VMMDLL_PidGetFromName(this->vHandle, (LPSTR)process_name.c_str(), &pid);
	return pid;
}

std::vector<int> Memory::GetPidListFromName(std::string name)
{
	PVMMDLL_PROCESS_INFORMATION process_info = NULL;
	DWORD total_processes = 0;
	std::vector<int> list = { };

	if (!VMMDLL_ProcessGetInformationAll(this->vHandle, &process_info, &total_processes))
	{
		DMALIB_LOG("[!] Failed to get process list\n");
		return list;
	}

	for (DWORD i = 0; i < total_processes; i++)
	{
		auto process = process_info[i];
		if (strstr(process.szNameLong, name.c_str()))
			list.push_back(process.dwPID);
	}

	return list;
}

std::vector<std::string> Memory::GetModuleList(std::string process_name)
{
	std::vector<std::string> list = { };
	PVMMDLL_MAP_MODULE module_info;
	if (!VMMDLL_Map_GetModuleU(this->vHandle, current_process.PID, &module_info, VMMDLL_MODULE_FLAG_NORMAL))
	{
		DMALIB_LOG("[!] Failed to get module list\n");
		return list;
	}

	for (size_t i = 0; i < module_info->cMap; i++)
	{
		auto module = module_info->pMap[i];
		list.push_back(module.uszText);
	}

	return list;
}

VMMDLL_PROCESS_INFORMATION Memory::GetProcessInformation()
{
	VMMDLL_PROCESS_INFORMATION info;
	SIZE_T process_information = sizeof(VMMDLL_PROCESS_INFORMATION);
	ZeroMemory(&info, sizeof(VMMDLL_PROCESS_INFORMATION));
	info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;

	if (!VMMDLL_ProcessGetInformation(this->vHandle, current_process.PID, &info, &process_information))
	{
		DMALIB_LOG("[!] Failed to find process information\n");
		return { };
	}

	DMALIB_LOG("[+] Found process information\n");
	return info;
}

PEB Memory::GetProcessPeb()
{
	auto info = GetProcessInformation();
	if (info.win.vaPEB)
	{
		DMALIB_LOG("[+] Found process PEB ptr at 0x%p\n", reinterpret_cast<void*>(info.win.vaPEB));
		return Read<PEB>(info.win.vaPEB);
	}
	DMALIB_LOG("[!] Failed to find the processes PEB\n");
	return { };
}

size_t Memory::GetBaseDaddy(std::string module_name)
{
	std::wstring str(module_name.begin(), module_name.end());

	PVMMDLL_MAP_MODULEENTRY module_info = nullptr;
	if (VMMDLL_Map_GetModuleFromNameW(this->vHandle, current_process.PID,
		const_cast<LPWSTR>(str.c_str()), &module_info, VMMDLL_MODULE_FLAG_NORMAL)
		&& module_info)
	{
		return module_info->vaBase;
	}

	// Fallback: full module list + case-insensitive / partial match.
	// MemProcFS sometimes drops GameAssembly from the name index while the
	// module is still present in Map_GetModuleU after a TLB refresh.
	PVMMDLL_MAP_MODULE mods = nullptr;
	if (VMMDLL_Map_GetModuleU(this->vHandle, current_process.PID, &mods, VMMDLL_MODULE_FLAG_NORMAL)
		&& mods)
	{
		std::string want = module_name;
		for (char& c : want)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

		for (DWORD i = 0; i < mods->cMap; ++i)
		{
			const char* text = mods->pMap[i].uszText;
			if (!text) continue;
			std::string have = text;
			for (char& c : have)
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (have == want || have.find(want) != std::string::npos
				|| (want.find("gameassembly") != std::string::npos
					&& have.find("gameassembly") != std::string::npos))
			{
				const size_t base = static_cast<size_t>(mods->pMap[i].vaBase);
				VMMDLL_MemFree(mods);
				return base;
			}
		}
		VMMDLL_MemFree(mods);
	}

	// Rate-limit spam — Main wait-loop calls this every 2s.
	static std::string s_last;
	static int s_miss = 0;
	if (s_last != module_name) { s_last = module_name; s_miss = 0; }
	if ((++s_miss % 8) == 1)
		DMALIB_LOG("[!] Couldn't find Base Address for %s (miss=%d)\n",
			module_name.c_str(), s_miss);
	return 0;
}

size_t Memory::GetBaseSize(std::string module_name)
{
	std::wstring str(module_name.begin(), module_name.end());

	PVMMDLL_MAP_MODULEENTRY module_info;
	auto bResult = VMMDLL_Map_GetModuleFromNameW(this->vHandle, current_process.PID, const_cast<LPWSTR>(str.c_str()), &module_info, VMMDLL_MODULE_FLAG_NORMAL);
	if (bResult)
	{
		return module_info->cbImageSize;
	}
	return 0;
}

uintptr_t Memory::GetExportTableAddress(std::string import, std::string process, std::string module)
{
	PVMMDLL_MAP_EAT eat_map = NULL;
	PVMMDLL_MAP_EATENTRY export_entry;
	bool result = VMMDLL_Map_GetEATU(mem.vHandle, mem.GetPidFromName(process) /*| VMMDLL_PID_PROCESS_WITH_KERNELMEMORY*/, const_cast<LPSTR>(module.c_str()), &eat_map);
	if (!result)
	{
		DMALIB_LOG("[!] Failed to get Export Table\n");
		return 0;
	}

	if (eat_map->dwVersion != VMMDLL_MAP_EAT_VERSION)
	{
		VMMDLL_MemFree(eat_map);
		eat_map = NULL;
		DMALIB_LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}

	uintptr_t addr = 0;
	for (DWORD i = 0; i < eat_map->cMap; i++)
	{
		export_entry = eat_map->pMap + i;
		if (strcmp(export_entry->uszFunction, import.c_str()) == 0)
		{
			addr = export_entry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(eat_map);
	eat_map = NULL;

	return addr;
}

uintptr_t Memory::GetImportTableAddress(std::string import, std::string process, std::string module)
{
	PVMMDLL_MAP_IAT iat_map = NULL;
	PVMMDLL_MAP_IATENTRY import_entry;
	bool result = VMMDLL_Map_GetIATU(mem.vHandle, mem.GetPidFromName(process) /*| VMMDLL_PID_PROCESS_WITH_KERNELMEMORY*/, const_cast<LPSTR>(module.c_str()), &iat_map);
	if (!result)
	{
		DMALIB_LOG("[!] Failed to get Import Table\n");
		return 0;
	}

	if (iat_map->dwVersion != VMMDLL_MAP_IAT_VERSION)
	{
		VMMDLL_MemFree(iat_map);
		iat_map = NULL;
		DMALIB_LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}

	uintptr_t addr = 0;
	for (DWORD i = 0; i < iat_map->cMap; i++)
	{
		import_entry = iat_map->pMap + i;
		if (strcmp(import_entry->uszFunction, import.c_str()) == 0)
		{
			addr = import_entry->vaFunction;
			break;
		}
	}

	VMMDLL_MemFree(iat_map);
	iat_map = NULL;

	return addr;
}

uint64_t cbSize = 0x80000;
//callback for VfsFileListU
VOID cbAddFile(_Inout_ HANDLE h, _In_ LPCSTR uszName, _In_ ULONG64 cb, _In_opt_ PVMMDLL_VFS_FILELIST_EXINFO pExInfo)
{
	if (strcmp(uszName, "dtb.txt") == 0)
		cbSize = cb;
}

struct Info
{
	uint32_t index;
	uint32_t process_id;
	uint64_t dtb;
	uint64_t kernelAddr;
	std::string name;
};

bool Memory::FixCr3()
{
	PVMMDLL_MAP_MODULEENTRY module_entry;
	bool result = VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID, const_cast<LPSTR>(current_process.process_name.c_str()), &module_entry, NULL);
	if (result)
		return true; //Doesn't need to be patched lol

	if (!VMMDLL_InitializePlugins(this->vHandle))
	{
		DMALIB_LOG("[-] Failed VMMDLL_InitializePlugins call\n");
		return false;
	}

	//have to sleep a little or we try reading the file before the plugin initializes fully
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	while (true)
	{
		BYTE bytes[4] = {0};
		DWORD i = 0;
		auto nt = VMMDLL_VfsReadW(this->vHandle, const_cast<LPWSTR>(L"\\misc\\procinfo\\progress_percent.txt"), bytes, 3, &i, 0);
		if (nt == VMMDLL_STATUS_SUCCESS && atoi(reinterpret_cast<LPSTR>(bytes)) == 100)
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	VMMDLL_VFS_FILELIST2 VfsFileList;
	VfsFileList.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
	VfsFileList.h = 0;
	VfsFileList.pfnAddDirectory = 0;
	VfsFileList.pfnAddFile = cbAddFile; //dumb af callback who made this system

	result = VMMDLL_VfsListU(this->vHandle, const_cast<LPSTR>("\\misc\\procinfo\\"), &VfsFileList);
	if (!result)
		return false;

	//read the data from the txt and parse it
	const size_t buffer_size = cbSize;
	std::unique_ptr<BYTE[]> bytes(new BYTE[buffer_size]);
	DWORD j = 0;
	auto nt = VMMDLL_VfsReadW(this->vHandle, const_cast<LPWSTR>(L"\\misc\\procinfo\\dtb.txt"), bytes.get(), static_cast<DWORD>(buffer_size - 1), &j, 0);
	if (nt != VMMDLL_STATUS_SUCCESS)
		return false;

	std::vector<uint64_t> possible_dtbs;
	std::string lines(reinterpret_cast<char*>(bytes.get()));
	std::istringstream iss(lines);
	std::string line;

	while (std::getline(iss, line))
	{
		Info info = { };

		std::istringstream info_ss(line);
		if (info_ss >> std::hex >> info.index >> std::dec >> info.process_id >> std::hex >> info.dtb >> info.kernelAddr >> info.name)
		{
			if (info.process_id == 0) //parts that lack a name or have a NULL pid are suspects
				possible_dtbs.push_back(info.dtb);
			if (current_process.process_name.find(info.name) != std::string::npos)
				possible_dtbs.push_back(info.dtb);
		}
	}

	//loop over possible dtbs and set the config to use it til we find the correct one
	for (size_t i = 0; i < possible_dtbs.size(); i++)
	{
		auto dtb = possible_dtbs[i];
		VMMDLL_ConfigSet(this->vHandle, VMMDLL_OPT_PROCESS_DTB | current_process.PID, dtb);
		result = VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID, const_cast<LPSTR>(current_process.process_name.c_str()), &module_entry, NULL);
		if (result)
		{
			DMALIB_LOG("[+] Patched DTB\n");
			return true;
		}
	}

	DMALIB_LOG("[-] Failed to patch module\n");
	return false;
}

bool Memory::DumpMemory(uintptr_t address, std::string path)
{
	DMALIB_LOG("[!] Memory dumping currently does not rebuild the IAT table, imports will be missing from the dump.\n");
	IMAGE_DOS_HEADER dos;
	Read(address, &dos, sizeof(IMAGE_DOS_HEADER));

	//Check if memory has a PE 
	if (dos.e_magic != 0x5A4D) //Check if it starts with MZ
	{
		DMALIB_LOG("[-] Invalid PE Header\n");
		return false;
	}

	IMAGE_NT_HEADERS64 nt;
	Read(address + dos.e_lfanew, &nt, sizeof(IMAGE_NT_HEADERS64));

	//Sanity check
	if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		DMALIB_LOG("[-] Failed signature check\n");
		return false;
	}
	//Shouldn't change ever. so const 
	const size_t target_size = nt.OptionalHeader.SizeOfImage;
	//Crashes if we don't make it a ptr :(
	auto target = std::unique_ptr<uint8_t[]>(new uint8_t[target_size]);

	//Read whole modules memory
	Read(address, target.get(), target_size);
	auto nt_header = (PIMAGE_NT_HEADERS64)(target.get() + dos.e_lfanew);
	auto sections = (PIMAGE_SECTION_HEADER)(target.get() + dos.e_lfanew + FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader);

	for (size_t i = 0; i < nt.FileHeader.NumberOfSections; i++, sections++)
	{
		//Rewrite the file offsets to the virtual addresses
		DMALIB_LOG("[!] Rewriting file offsets at 0x%p size 0x%p\n", reinterpret_cast<void*>(static_cast<uintptr_t>(sections->VirtualAddress)), reinterpret_cast<void*>(static_cast<uintptr_t>(sections->Misc.VirtualSize)));
		sections->PointerToRawData = sections->VirtualAddress;
		sections->SizeOfRawData = sections->Misc.VirtualSize;
	}

	//Find all modules used by this process
	//auto descriptor = Read<IMAGE_IMPORT_DESCRIPTOR>(address + ntHeader->OptionalHeader.DataDirectory[1].VirtualAddress);

	//int descriptor_count = 0;
	//int thunk_count = 0;

	/*std::vector<ModuleData> modulelist;
	while (descriptor.Name) {
		auto first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.FirstThunk);
		auto original_first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.OriginalFirstThunk);
		thunk_count = 0;

		char ModuleName[256];
		ReadMemory(moduleAddr + descriptor.Name, (void*)&ModuleName, 256);

		std::string DllName = ModuleName;

		ModuleData tmpModuleData;

		//if(std::find(modulelist.begin(), modulelist.end(), tmpModuleData) == modulelist.end())
		//	modulelist.push_back(tmpModuleData);
		while (original_first_thunk.u1.AddressOfData) {
			char name[256];
			ReadMemory(moduleAddr + original_first_thunk.u1.AddressOfData + 0x2, (void*)&name, 256);

			std::string str_name = name;
			auto thunk_offset{ thunk_count * sizeof(uintptr_t) };

			//if (str_name.length() > 0)
			//	imports[str_name] = moduleAddr + descriptor.FirstThunk + thunk_offset;

			++thunk_count;
			first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.FirstThunk + sizeof(IMAGE_THUNK_DATA) * thunk_count);
			original_first_thunk = Read<IMAGE_THUNK_DATA>(moduleAddr + descriptor.OriginalFirstThunk + sizeof(IMAGE_THUNK_DATA) * thunk_count);
		}

		++descriptor_count;
		descriptor = Read<IMAGE_IMPORT_DESCRIPTOR>(moduleAddr + ntHeader->OptionalHeader.DataDirectory[1].VirtualAddress + sizeof(IMAGE_IMPORT_DESCRIPTOR) * descriptor_count);
	}*/

	//Rebuild import table

	//DMALIB_LOG("[!] Creating new import section\n");

	//Create New Import Section

	//Build new import Table

	//Dump file
	const auto dumped_file = CreateFileW(std::wstring(path.begin(), path.end()).c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_COMPRESSED, NULL);
	if (dumped_file == INVALID_HANDLE_VALUE)
	{
		DMALIB_LOG("[!] Failed creating file: %lu\n", static_cast<unsigned long>(GetLastError()));
		return false;
	}

	if (!WriteFile(dumped_file, target.get(), static_cast<DWORD>(target_size), NULL, NULL))
	{
		DMALIB_LOG("[!] Failed writing file: %lu\n", static_cast<unsigned long>(GetLastError()));
		CloseHandle(dumped_file);
		return false;
	}

	DMALIB_LOG("[+] Successfully dumped memory at %s\n", path.c_str());
	CloseHandle(dumped_file);
	return true;
}

static const char* hexdigits =
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\001\002\003\004\005\006\007\010\011\000\000\000\000\000\000"
	"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
	"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";

static uint8_t GetByte(const char* hex)
{
	return static_cast<uint8_t>((hexdigits[hex[0]] << 4) | (hexdigits[hex[1]]));
}

// MemSearch filter: only walk private (non image / non file backed) memory.
// The managed heap lives in private VAD regions, so skipping images keeps the
// scan from reading the ~1 GB of mapped DLLs for no reason.
static BOOL mem_search_vad_filter(PVMMDLL_MEM_SEARCH_CONTEXT, PVMMDLL_MAP_PTEENTRY, PVMMDLL_MAP_VADENTRY vad)
{
	if (vad && (vad->fImage || vad->fFile))
		return FALSE;
	return TRUE;
}

bool Memory::MemSearch(const std::vector<uint64_t>& values, uint64_t va_min, uint64_t va_max,
                       std::vector<uint64_t>& out, size_t max_results, uint64_t* bytes_read,
                       bool force_pte)
{
	out.clear();
	if (bytes_read) *bytes_read = 0;
	if (!this->vHandle || values.empty())
		return false;

	// vmm 5.10 moved the search entries from an inline array to a pointer, so
	// the entries must outlive the VMMDLL_MemSearch call.
	constexpr DWORD kMaxSearchEntries = 16;
	std::vector<VMMDLL_MEM_SEARCH_CONTEXT_SEARCHENTRY> searches(
		(std::min)(values.size(), static_cast<size_t>(kMaxSearchEntries)));

	VMMDLL_MEM_SEARCH_CONTEXT ctx{};
	ctx.dwVersion = VMMDLL_MEM_SEARCH_VERSION;
	ctx.cMaxResult = static_cast<DWORD>((std::max)(max_results, static_cast<size_t>(1)));
	ctx.cSearch = static_cast<DWORD>(searches.size());
	ctx.pSearch = searches.data();
	for (DWORD i = 0; i < ctx.cSearch; ++i)
	{
		VMMDLL_MEM_SEARCH_CONTEXT_SEARCHENTRY& e = searches[i];
		e.cbAlign = 8;
		e.cb = 8;
		std::memcpy(e.pb, &values[i], 8);
		std::memset(e.pbSkipMask, 0, sizeof(e.pbSkipMask));
	}

	ctx.vaMin = va_min & ~0xFFFULL;
	ctx.vaMax = va_max & ~0xFFFULL;
	ctx.fForcePTE = force_pte ? TRUE : FALSE;
	ctx.fForceVAD = force_pte ? FALSE : TRUE;
	// The VAD filter drops image/file-backed ranges (the managed heap is
	// private). The PTE walk only visits present pages, so it needs no filter.
	ctx.pfnFilterOptCB = force_pte ? nullptr : mem_search_vad_filter;
	ctx.ReadFlags = VMMDLL_FLAG_NOCACHE;

	PQWORD ppva = nullptr;
	DWORD cva = 0;
	const BOOL ok = VMMDLL_MemSearch(this->vHandle,
		static_cast<DWORD>(current_process.PID), &ctx, &ppva, &cva);
	if (ppva)
	{
		if (cva)
			out.assign(ppva, ppva + cva);
		VMMDLL_MemFree(ppva);
	}
	if (bytes_read) *bytes_read = ctx.cbReadTotal;
	return ok != FALSE;
}

uint64_t Memory::FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID)
{
	if (!signature || signature[0] == '\0' || range_start >= range_end)
		return 0;

	if (PID == 0)
		PID = current_process.PID;

	// Parse IDA-style pattern into bytes + mask once.
	std::vector<uint8_t> pat;
	std::vector<uint8_t> mask; // 1 = compare, 0 = wildcard
	for (const char* p = signature; *p; )
	{
		while (*p == ' ') ++p;
		if (!*p) break;
		if (*p == '?')
		{
			pat.push_back(0);
			mask.push_back(0);
			++p;
			if (*p == '?') ++p;
		}
		else
		{
			pat.push_back(GetByte(p));
			mask.push_back(1);
			p += 2;
		}
	}
	if (pat.empty())
		return 0;

	constexpr size_t kChunk = 0x100000; // 1 MiB
	const size_t overlap = pat.size() > 1 ? pat.size() - 1 : 0;
	std::vector<uint8_t> buffer(kChunk);

	for (uint64_t chunk_start = range_start; chunk_start < range_end; )
	{
		const size_t to_read = static_cast<size_t>((std::min)(range_end - chunk_start, (uint64_t)kChunk));
		DWORD cb = 0;
		std::fill(buffer.begin(), buffer.begin() + to_read, 0);
		if (!VMMDLL_MemReadEx(this->vHandle, PID, chunk_start, buffer.data(), static_cast<DWORD>(to_read), &cb, VMMDLL_FLAG_NOCACHE)
			|| cb < pat.size())
		{
			if (chunk_start + to_read >= range_end)
				break;
			chunk_start += (kChunk > overlap) ? (kChunk - overlap) : kChunk;
			continue;
		}

		// Skip chunks that look like failed DMA (entirely zero) — avoids false lea hits.
		bool any_nonzero = false;
		for (DWORD i = 0; i < cb; ++i)
		{
			if (buffer[i]) { any_nonzero = true; break; }
		}
		if (!any_nonzero)
		{
			if (chunk_start + to_read >= range_end)
				break;
			chunk_start += (kChunk > overlap) ? (kChunk - overlap) : kChunk;
			continue;
		}

		const size_t limit = cb - pat.size() + 1;
		for (size_t off = 0; off < limit; ++off)
		{
			bool ok = true;
			for (size_t j = 0; j < pat.size(); ++j)
			{
				if (mask[j] && buffer[off + j] != pat[j])
				{
					ok = false;
					break;
				}
			}
			if (ok)
				return chunk_start + off;
		}

		if (chunk_start + to_read >= range_end)
			break;
		chunk_start += (kChunk > overlap) ? (kChunk - overlap) : kChunk;
	}

	return 0;
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size) const
{
	if (!VMMDLL_MemWrite(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size)))
	{
		// Rate-limit: weapon-feel used to spam thousands of lines when hitting
		// cooked (read-only) USQWeaponStaticInfo pages.
		static ULONG64 s_last_tick = 0;
		static unsigned s_suppressed = 0;
		const ULONG64 now = GetTickCount64();
		if (now - s_last_tick >= 2000)
		{
			if (s_suppressed)
				DMALIB_LOG("[!] Failed to write Memory at 0x%p (+%u similar)\n",
					reinterpret_cast<void*>(address), s_suppressed);
			else
				DMALIB_LOG("[!] Failed to write Memory at 0x%p\n", reinterpret_cast<void*>(address));
			s_last_tick = now;
			s_suppressed = 0;
		}
		else
			++s_suppressed;
		return false;
	}
	return true;
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size, int pid) const
{
	if (!VMMDLL_MemWrite(this->vHandle, pid, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size)))
	{
		static ULONG64 s_last_tick = 0;
		static unsigned s_suppressed = 0;
		const ULONG64 now = GetTickCount64();
		if (now - s_last_tick >= 2000)
		{
			if (s_suppressed)
				DMALIB_LOG("[!] Failed to write Memory at 0x%p (+%u similar)\n",
					reinterpret_cast<void*>(address), s_suppressed);
			else
				DMALIB_LOG("[!] Failed to write Memory at 0x%p\n", reinterpret_cast<void*>(address));
			s_last_tick = now;
			s_suppressed = 0;
		}
		else
			++s_suppressed;
		return false;
	}
	return true;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size) const
{
	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &read_size, VMMDLL_FLAG_NOCACHE))
	{
		DMALIB_LOG("[!] Failed to read Memory at 0x%p\n", reinterpret_cast<void*>(address));
		return false;
	}

	return (read_size == size);
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid) const
{
	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(this->vHandle, pid, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size), &read_size, VMMDLL_FLAG_NOCACHE))
	{
		DMALIB_LOG("[!] Failed to read Memory at 0x%p\n", reinterpret_cast<void*>(address));
		return false;
	}
	return (read_size == size);
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle() const
{
	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, current_process.PID, VMMDLL_FLAG_NOCACHE);
	if (!ScatterHandle)
		DMALIB_LOG("[!] Failed to create scatter handle\n");
	return ScatterHandle;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid) const
{
	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, pid, VMMDLL_FLAG_NOCACHE);
	if (!ScatterHandle)
		DMALIB_LOG("[!] Failed to create scatter handle\n");
	return ScatterHandle;
}

void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	VMMDLL_Scatter_CloseHandle(handle);
}

void Memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	if (!VMMDLL_Scatter_PrepareEx(handle, address, static_cast<DWORD>(size), static_cast<PBYTE>(buffer), NULL))
	{
		DMALIB_LOG("[!] Failed to prepare scatter read at 0x%p\n", reinterpret_cast<void*>(static_cast<uintptr_t>(address)));
	}
}

void Memory::AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	if (!VMMDLL_Scatter_PrepareWrite(handle, address, static_cast<PBYTE>(buffer), static_cast<DWORD>(size)))
	{
		DMALIB_LOG("[!] Failed to prepare scatter write at 0x%p\n", reinterpret_cast<void*>(static_cast<uintptr_t>(address)));
	}
}

void Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	if (pid == 0)
		pid = current_process.PID;

	if (!VMMDLL_Scatter_ExecuteRead(handle))
	{
		//DMALIB_LOG("[-] Failed to Execute Scatter Read\n");
	}
	//Clear after using it
	if (!VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		DMALIB_LOG("[-] Failed to clear Scatter\n");
	}
}

void Memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	if (pid == 0)
		pid = current_process.PID;

	if (!VMMDLL_Scatter_Execute(handle))
	{
		DMALIB_LOG("[-] Failed to Execute Scatter Read\n");
	}
	//Clear after using it
	if (!VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		DMALIB_LOG("[-] Failed to clear Scatter\n");
	}
}
