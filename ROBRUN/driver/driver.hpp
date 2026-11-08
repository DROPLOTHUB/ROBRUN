#pragma once

#ifndef _WIN64
#error This ROBRUN build targets x64 with PCILeech / MemProcFS DMA. Switch the VS platform to x64.
#endif

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "../DMALibrary/Memory/Memory.h"

class cDriver
{
	std::string module_name_;
	uint32_t    process_id_ = 0;

public:
	cDriver() = default;

	bool setup()
	{
		return mem.InitVmmOnly(true, false);
	}

	bool target(const char* proc)
	{
		if (!proc || !proc[0])
			return false;
		module_name_ = proc;
		if (!mem.AttachProcess(module_name_))
		{
			process_id_ = 0;
			return false;
		}
		process_id_ = mem.GetAttachedPid();
		return process_id_ != 0;
	}

	bool target(uint32_t pid)
	{
		if (!pid)
			return false;
		process_id_ = pid;
		return true;
	}

	uint32_t target() const { return process_id_; }

	uint64_t getbase()
	{
		if (module_name_.empty())
			return 0;
		return mem.GetBaseDaddy(module_name_);
	}

	bool read(uint64_t address, PVOID buffer, SIZE_T size)
	{
		return mem.Read(address, buffer, size);
	}

	bool read(PVOID address, PVOID buffer, SIZE_T size)
	{
		return read(reinterpret_cast<uint64_t>(address), buffer, size);
	}

	bool read_virt(uint64_t address, PVOID buffer, unsigned long size)
	{
		return mem.Read(address, buffer, static_cast<size_t>(size));
	}

	template<typename T = uintptr_t>
	T read(const std::uint64_t& address)
	{
		return mem.Read<T>(address);
	}

	template<typename T = uintptr_t>
	T read(DWORD pid, const std::uint64_t& address)
	{
		(void)pid;
		return mem.Read<T>(address);
	}

	bool write(PVOID address, PVOID buffer, SIZE_T size)
	{
		return mem.Write(reinterpret_cast<uintptr_t>(address), buffer, size);
	}

	template<typename T>
	bool write(std::uint64_t address, T data)
	{
		return mem.Write(address, &data, sizeof(T));
	}

	bool read_chunked(uint64_t address, void* buffer, size_t size)
	{
		const size_t kMax = 0x1000;
		auto* buf = static_cast<uint8_t*>(buffer);
		size_t remaining = size;
		uint64_t addr = address;
		while (remaining > 0)
		{
			const size_t chunk = remaining > kMax ? kMax : remaining;
			if (!mem.Read(addr, buf, chunk))
				return false;
			buf += chunk;
			addr += chunk;
			remaining -= chunk;
		}
		return true;
	}

	template<typename T>
	bool read_array(uintptr_t address, T* buffer, size_t count)
	{
		return read_chunked(address, reinterpret_cast<void*>(buffer), sizeof(T) * count);
	}

	// ---- Scatter / gather (batch DMA reads) ---------------------------------
	VMMDLL_SCATTER_HANDLE create_scatter()
	{
		return mem.CreateScatterHandle();
	}

	void close_scatter(VMMDLL_SCATTER_HANDLE handle)
	{
		if (handle)
			mem.CloseScatterHandle(handle);
	}

	void add_scatter_read(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
	{
		if (handle && address && buffer && size)
			mem.AddScatterReadRequest(handle, address, buffer, size);
	}

	template<typename T>
	void add_scatter_read(VMMDLL_SCATTER_HANDLE handle, uint64_t address, T* out)
	{
		add_scatter_read(handle, address, out, sizeof(T));
	}

	void execute_scatter(VMMDLL_SCATTER_HANDLE handle)
	{
		if (handle)
			mem.ExecuteReadScatter(handle);
	}
};

inline cDriver* driver = new cDriver();
