#include "overlay.hpp"
#include "menu.hpp"
#include "../includes.hpp"
#include "../utils/settings.hpp"
#include "../DMALibrary/Memory/Memory.h"
#include <dxgi.h>
#include <algorithm>
#include <cmath>
#include <vector>
#include <comdef.h>
#include <corecrt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <cfloat>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static unsigned get_primary_refresh_hz()
{
	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 480)
		return dm.dmDisplayFrequency;
	return 60;
}

static unsigned refresh_hz_from_gdi_display_dc(const wchar_t* szDevice)
{
	if (!szDevice || !szDevice[0])
		return 0;
	HDC hdc = CreateDCW(szDevice, nullptr, nullptr, nullptr);
	if (!hdc)
		return 0;
	const int vr = GetDeviceCaps(hdc, VREFRESH);
	DeleteDC(hdc);
	if (vr >= 30 && vr <= 480)
		return static_cast<unsigned>(vr);
	return 0;
}

static unsigned refresh_hz_from_devmode(const wchar_t* szDevice)
{
	if (!szDevice || !szDevice[0])
		return 0;
	DEVMODEW dm{};
	dm.dmSize = sizeof(dm);
	if (!EnumDisplaySettingsW(szDevice, ENUM_CURRENT_SETTINGS, &dm))
		return 0;
	if (dm.dmDisplayFrequency >= 30 && dm.dmDisplayFrequency <= 480)
		return dm.dmDisplayFrequency;
	return 0;
}

static unsigned refresh_hz_from_dxgi(HMONITOR hMonitor)
{
	if (!hMonitor)
		return 0;

	IDXGIFactory* factory = nullptr;
	if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory))) || !factory)
		return 0;

	unsigned best_hz = 0;

	for (UINT ai = 0;; ++ai)
	{
		IDXGIAdapter* adapter = nullptr;
		HRESULT hr = factory->EnumAdapters(ai, &adapter);
		if (hr == DXGI_ERROR_NOT_FOUND)
			break;
		if (FAILED(hr) || !adapter)
			continue;

		for (UINT oi = 0;; ++oi)
		{
			IDXGIOutput* output = nullptr;
			hr = adapter->EnumOutputs(oi, &output);
			if (hr == DXGI_ERROR_NOT_FOUND)
				break;
			if (FAILED(hr) || !output)
				continue;

			DXGI_OUTPUT_DESC od{};
			if (SUCCEEDED(output->GetDesc(&od)) && od.Monitor == hMonitor)
			{
				const UINT w = static_cast<UINT>(od.DesktopCoordinates.right - od.DesktopCoordinates.left);
				const UINT h = static_cast<UINT>(od.DesktopCoordinates.bottom - od.DesktopCoordinates.top);
				const UINT flags = DXGI_ENUM_MODES_INTERLACED;
				const DXGI_FORMAT fmts[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM };
				for (DXGI_FORMAT fmt : fmts)
				{
					UINT n = 0;
					if (!SUCCEEDED(output->GetDisplayModeList(fmt, flags, &n, nullptr)) || n == 0)
						continue;
					std::vector<DXGI_MODE_DESC> modes(n);
					if (!SUCCEEDED(output->GetDisplayModeList(fmt, flags, &n, modes.data())))
						continue;
					for (UINT i = 0; i < n; ++i)
					{
						if (modes[i].Width != w || modes[i].Height != h)
							continue;
						const UINT num = modes[i].RefreshRate.Numerator;
						const UINT den = modes[i].RefreshRate.Denominator ? modes[i].RefreshRate.Denominator : 1u;
						const unsigned hz = static_cast<unsigned>((static_cast<unsigned long long>(num) + den / 2) / den);
						if (hz >= 30 && hz <= 480 && hz > best_hz)
							best_hz = hz;
					}
					if (best_hz != 0)
						break;
				}
				output->Release();
				adapter->Release();
				factory->Release();
				return best_hz;
			}
			output->Release();
		}
		adapter->Release();
	}

	factory->Release();
	return 0;
}

static unsigned query_refresh_hz_for_hmonitor(HMONITOR hMonitor)
{
	if (!hMonitor)
		return 0;

	MONITORINFOEXW mex{};
	mex.cbSize = sizeof(mex);
	if (!GetMonitorInfoW(hMonitor, reinterpret_cast<LPMONITORINFO>(&mex)))
		return 0;

	unsigned hz = refresh_hz_from_gdi_display_dc(mex.szDevice);
	if (hz == 0)
		hz = refresh_hz_from_devmode(mex.szDevice);
	if (hz == 0)
		hz = refresh_hz_from_dxgi(hMonitor);
	if (hz == 0)
		hz = 60;
	return hz;
}

BOOL CALLBACK c_overlay::monitor_enum_proc(HMONITOR hMonitor, HDC, LPRECT lprcMonitor, LPARAM dwData)
{
	auto* self = reinterpret_cast<c_overlay*>(dwData);
	if (!self || !lprcMonitor)
		return TRUE;

	OverlayMonitorInfo mi{};
	mi.rect = *lprcMonitor;
	mi.hMonitor = hMonitor;
	mi.refresh_hz = query_refresh_hz_for_hmonitor(hMonitor);

	self->monitors_.push_back(mi);
	return TRUE;
}

void c_overlay::detect_monitors()
{
	monitors_.clear();
	EnumDisplayMonitors(nullptr, nullptr, monitor_enum_proc, reinterpret_cast<LPARAM>(this));
	if (monitors_.empty())
	{
		OverlayMonitorInfo mi{};
		mi.rect.left = 0;
		mi.rect.top = 0;
		mi.rect.right = GetSystemMetrics(SM_CXSCREEN);
		mi.rect.bottom = GetSystemMetrics(SM_CYSCREEN);
		mi.hMonitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		mi.refresh_hz = mi.hMonitor ? query_refresh_hz_for_hmonitor(mi.hMonitor) : get_primary_refresh_hz();
		monitors_.push_back(mi);
	}
	if (monitor_index_ >= static_cast<int>(monitors_.size()))
		monitor_index_ = 0;
}

void c_overlay::apply_current_monitor_geometry()
{
	if (!window_handle || monitors_.empty())
		return;

	monitor_index_ = (std::max)(0, (std::min)(monitor_index_, static_cast<int>(monitors_.size()) - 1));
	OverlayMonitorInfo& mon = monitors_[static_cast<size_t>(monitor_index_)];
	if (mon.hMonitor)
		mon.refresh_hz = query_refresh_hz_for_hmonitor(mon.hMonitor);
	const RECT& r = mon.rect;
	const int w = r.right - r.left;
	const int h = r.bottom - r.top;
	display_refresh_hz_ = mon.refresh_hz;

	SetWindowPos(window_handle, HWND_TOPMOST, r.left, r.top, w, h, SWP_SHOWWINDOW);
	window_width = static_cast<FLOAT>(w);
	window_height = static_cast<FLOAT>(h);
	::screen_size = Aspect::Vector2(static_cast<float>(w), static_cast<float>(h));
}

bool c_overlay::recreate_swap_chain_for_monitor()
{
	if (!window_handle || !d3d_device || monitors_.empty())
		return false;

	apply_current_monitor_geometry();

	const RECT& r = monitors_[static_cast<size_t>(monitor_index_)].rect;
	const int w = r.right - r.left;
	const int h = r.bottom - r.top;

	dest_render_target();
	if (swap_chain)
	{
		swap_chain->Release();
		swap_chain = nullptr;
	}

	DXGI_SWAP_CHAIN_DESC sd{};
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 2;
	sd.BufferDesc.Width = static_cast<UINT>(w);
	sd.BufferDesc.Height = static_cast<UINT>(h);
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = display_refresh_hz_;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = window_handle;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	IDXGIDevice* dxgi_device = nullptr;
	IDXGIAdapter* adapter = nullptr;
	IDXGIFactory* factory = nullptr;
	if (FAILED(d3d_device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device))))
		return false;
	if (FAILED(dxgi_device->GetAdapter(&adapter)))
	{
		dxgi_device->Release();
		return false;
	}
	if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory))))
	{
		adapter->Release();
		dxgi_device->Release();
		return false;
	}

	const HRESULT hr = factory->CreateSwapChain(d3d_device, &sd, &swap_chain);
	factory->Release();
	adapter->Release();
	dxgi_device->Release();

	if (FAILED(hr) || !swap_chain)
		return false;

	init_render_target();
	if (imgui_callbacks_ready_)
	{
		ImGui_ImplDX11_InvalidateDeviceObjects();
		ImGui_ImplDX11_CreateDeviceObjects();
	}
	return true;
}

void c_overlay::poll_monitor_switch_hotkey()
{
	if (monitors_.size() < 2u)
		return;

	const bool win = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);
	const bool shf = ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) || ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0);
	const bool left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
	const bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
	const bool combo = win && shf && ((left && !right) || (!left && right));

	if (!combo)
	{
		last_win_shift_arrow_combo_ = false;
		return;
	}
	if (last_win_shift_arrow_combo_)
		return;

	last_win_shift_arrow_combo_ = true;
	if (right)
		monitor_index_ = (monitor_index_ + 1) % static_cast<int>(monitors_.size());
	else
		monitor_index_ = (monitor_index_ + static_cast<int>(monitors_.size()) - 1) % static_cast<int>(monitors_.size());

	recreate_swap_chain_for_monitor();
}

// microsoft safe release of pointers
template <typename T>
inline VOID c_overlay::safe_release(T*& p) {
	if (NULL != p) {
		p->Release();
		p = NULL;
	}
}

BOOL c_overlay::init_device() {
	DXGI_SWAP_CHAIN_DESC vSwapChainDesc;
	ZeroMemory(&vSwapChainDesc, sizeof(vSwapChainDesc));
	vSwapChainDesc.BufferCount = 2;
	vSwapChainDesc.BufferDesc.Width = static_cast<UINT>(window_width);
	vSwapChainDesc.BufferDesc.Height = static_cast<UINT>(window_height);
	vSwapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	vSwapChainDesc.BufferDesc.RefreshRate.Numerator = display_refresh_hz_;
	vSwapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	vSwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	vSwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	vSwapChainDesc.OutputWindow = window_handle;
	vSwapChainDesc.SampleDesc.Count = 1;
	vSwapChainDesc.SampleDesc.Quality = 0;
	vSwapChainDesc.Windowed = TRUE;
	vSwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL vFeatureLevel;
	D3D_FEATURE_LEVEL vFeatureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };

	if (D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, vFeatureLevelArray, 2, D3D11_SDK_VERSION, &vSwapChainDesc, &swap_chain, &d3d_device, &vFeatureLevel, &device_context) != S_OK)
		return false;

	init_render_target();

	return true;
}

VOID c_overlay::dest_device() {
	dest_render_target();
	safe_release(swap_chain);
	safe_release(device_context);
	safe_release(d3d_device);
}

void setup_modern_theme()
{
	// Modern style by LousyBook-01 from ImThemes
	ImGuiStyle& style = ImGui::GetStyle();

	style.Alpha = 1.0f;
	//style.DisabledAlpha = 0.300000011920929f;
	style.WindowPadding = ImVec2(10.10000038146973f, 10.10000038146973f);
	style.WindowRounding = 10.30000019073486f;
	style.WindowBorderSize = 1.0f;
	style.WindowMinSize = ImVec2(20.0f, 32.0f);
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_Right;
	style.ChildRounding = 8.199999809265137f;
	style.ChildBorderSize = 1.0f;
	style.PopupRounding = 10.69999980926514f;
	style.PopupBorderSize = 1.0f;
	style.FramePadding = ImVec2(20.0f, 1.5f);
	style.FrameRounding = 4.800000190734863f;
	style.FrameBorderSize = 0.0f;
	style.ItemSpacing = ImVec2(9.699999809265137f, 5.300000190734863f);
	style.ItemInnerSpacing = ImVec2(5.400000095367432f, 9.300000190734863f);
	style.CellPadding = ImVec2(7.900000095367432f, 2.0f);
	style.IndentSpacing = 10.69999980926514f;
	style.ColumnsMinSpacing = 6.0f;
	style.ScrollbarSize = 12.10000038146973f;
	style.ScrollbarRounding = 20.0f;
	style.GrabMinSize = 10.0f;
	style.GrabRounding = 4.599999904632568f;
	style.TabRounding = 4.0f;
	style.TabBorderSize = 0.0f;
	style.TabMinWidthForCloseButton = 0.0f;
	style.ColorButtonPosition = ImGuiDir_Right;
	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

	style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(1.0f, 1.0f, 1.0f, 0.3991416096687317f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.03921568766236305f, 0.03921568766236305f, 0.03921568766236305f, 0.9399999976158142f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.05098039284348488f, 0.05098039284348488f, 0.05098039284348488f, 0.9399999976158142f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 0.5f);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.4206008315086365f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.1411764770746231f, 0.1411764770746231f, 0.1411764770746231f, 0.4000000059604645f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.2313725501298904f, 0.2313725501298904f, 0.2313725501298904f, 0.8626609444618225f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.09411764889955521f, 0.09411764889955521f, 0.09411764889955521f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.0f, 0.0f, 0.0f, 0.2918455004692078f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1372549086809158f, 0.1372549086809158f, 0.1372549086809158f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.01960784383118153f, 0.01960784383118153f, 0.01960784383118153f, 0.5299999713897705f);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3098039329051971f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.407843142747879f, 0.407843142747879f, 0.407843142747879f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.5098039507865906f, 0.5098039507865906f, 0.5098039507865906f, 1.0f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(0.9803921580314636f, 0.2588235437870026f, 0.2588235437870026f, 1.0f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9803921580314636f, 1.0f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.0f, 0.0f, 0.5793991088867188f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(1.0f, 0.2313725501298904f, 0.2313725501298904f, 1.0f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.0f, 0.0f, 0.0f, 0.454935610294342f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.1803921610116959f, 0.1803921610116959f, 0.1803921610116959f, 0.800000011920929f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.9764705896377563f, 0.2588235437870026f, 0.2588235437870026f, 1.0f);
	style.Colors[ImGuiCol_Separator] = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
	style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 0.7799999713897705f);
	style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
	style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.2000000029802322f);
	style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.6700000166893005f);
	style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 0.949999988079071f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.105882354080677f, 0.105882354080677f, 0.105882354080677f, 1.0f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(1.0f, 0.364705890417099f, 0.6745098233222961f, 0.800000011920929f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(1.0f, 0.2235294133424759f, 0.2235294133424759f, 1.0f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.1098039224743843f, 0.168627455830574f, 0.239215686917305f, 0.9724000096321106f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
	style.Colors[ImGuiCol_PlotLines] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.4274509847164154f, 0.3490196168422699f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.0f, 0.2156862765550613f, 0.2156862765550613f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 0.2156862765550613f, 0.6980392336845398f, 1.0f);
	style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(1.0f, 0.2352941185235977f, 0.2352941185235977f, 1.0f);
	style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(1.0f, 0.3176470696926117f, 0.3176470696926117f, 1.0f);
	style.Colors[ImGuiCol_TableBorderLight] = ImVec4(1.0f, 0.5647059082984924f, 0.5647059082984924f, 0.3690987229347229f);
	style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.7254902124404907f, 0.3372549116611481f, 1.0f, 0.0f);
	style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 0.2745098173618317f, 0.2745098173618317f, 0.1115880012512207f);
	style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.9764705896377563f, 0.2588235437870026f, 0.2588235437870026f, 1.0f);
	style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
	style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.0f, 0.0f, 0.0f, 0.6437768340110779f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.4678111672401428f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.733905553817749f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.7982832789421082f);
}

void setup_purple_comfy_theme()
{
	// Purple Comfy style by RegularLunar from ImThemes
	ImGuiStyle& style = ImGui::GetStyle();

	style.Alpha = 1.0f;
	//style.DisabledAlpha = 0.1000000014901161f;
	style.WindowPadding = ImVec2(8.0f, 8.0f);
	style.WindowRounding = 10.0f;
	style.WindowBorderSize = 0.0f;
	style.WindowMinSize = ImVec2(30.0f, 30.0f);
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_Right;
	style.ChildRounding = 5.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupRounding = 10.0f;
	style.PopupBorderSize = 0.0f;
	style.FramePadding = ImVec2(5.0f, 3.5f);
	style.FrameRounding = 5.0f;
	style.FrameBorderSize = 0.0f;
	style.ItemSpacing = ImVec2(5.0f, 4.0f);
	style.ItemInnerSpacing = ImVec2(5.0f, 5.0f);
	style.CellPadding = ImVec2(4.0f, 2.0f);
	style.IndentSpacing = 5.0f;
	style.ColumnsMinSpacing = 5.0f;
	style.ScrollbarSize = 15.0f;
	style.ScrollbarRounding = 9.0f;
	style.GrabMinSize = 15.0f;
	style.GrabRounding = 5.0f;
	style.TabRounding = 5.0f;
	style.TabBorderSize = 0.0f;
	style.TabMinWidthForCloseButton = 0.0f;
	style.ColorButtonPosition = ImGuiDir_Right;
	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

	style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(1.0f, 1.0f, 1.0f, 0.3605149984359741f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.55f, 1.0f, 0.5f);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.3803921639919281f, 0.4235294163227081f, 0.572549045085907f, 0.5490196347236633f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.55f, 1.0f, 0.3f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.09803921729326248f, 0.09803921729326248f, 0.09803921729326248f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.2588235437870026f, 0.2588235437870026f, 0.2588235437870026f, 0.0f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 0.0f);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.1568627506494522f, 0.1568627506494522f, 0.1568627506494522f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.2352941185235977f, 0.2352941185235977f, 0.2352941185235977f, 1.0f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.294117659330368f, 0.294117659330368f, 0.294117659330368f, 1.0f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 0.55f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.0f, 0.55f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0f, 0.55f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.0f, 0.55f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.0f, 0.55f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.0f, 0.55f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.0f, 0.55f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.0f, 0.55f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0f, 0.55f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_Separator] = ImVec4(0.0f, 0.55f, 1.0f, 0.5f);
	style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.0f, 0.55f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.0f, 0.55f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.55f, 1.0f, 0.5f);
	style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.0f, 0.55f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.0f, 0.55f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.0f, 0.55f, 1.0f, 0.5f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.0f, 0.55f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(0.0f, 0.55f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.0f, 0.4509803950786591f, 1.0f, 0.0f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 0.0f);
	style.Colors[ImGuiCol_PlotLines] = ImVec4(0.294117659330368f, 0.294117659330368f, 0.294117659330368f, 1.0f);
	style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.0f, 0.55f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.0f, 0.55f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.0f, 0.65f, 1.0f, 0.8f);
	style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
	style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.0f, 0.55f, 1.0f, 0.5f);
	style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.0f, 0.55f, 1.0f, 0.3f);
	style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03433477878570557f);
	style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.55f, 1.0f, 0.5f);
	style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 0.8999999761581421f);
	style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.699999988079071f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.2000000029802322f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.3499999940395355f);
}

VOID c_overlay::init_imgui() {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.LogFilename = nullptr;
	io.IniFilename = nullptr;

	ImGui::StyleColorsDark();
	setup_purple_comfy_theme();
	{
		static const ImWchar glyph_ranges[] = {
			0x0020, 0x00FF,
			0x0400, 0x052F,
			0x2122, 0x2122,
			0,
		};
		io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf", 12.f, NULL, glyph_ranges);
		font->esp_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf", 12.f, NULL, glyph_ranges);
		font->menu_font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\bahnschrift.ttf", 16.f, NULL, glyph_ranges);
	}

	ImGui_ImplWin32_Init(window_handle);
	ImGui_ImplDX11_Init(d3d_device, device_context);
	imgui_callbacks_ready_ = true;

}

VOID c_overlay::dest_imgui() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

VOID c_overlay::init_render_target() {
	ID3D11Texture2D* back_buffer = nullptr;
	if (!swap_chain || FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer))) || !back_buffer)
		return;
	if (d3d_device)
		d3d_device->CreateRenderTargetView(back_buffer, nullptr, &render_target_view);
	back_buffer->Release();
}

VOID c_overlay::dest_render_target() {
	if (!render_target_view)
		return;

	render_target_view->Release();
	render_target_view = NULL;
}

void c_overlay::handle_wm_size(unsigned width, unsigned height)
{
	if (!d3d_device || !swap_chain || width == 0 || height == 0)
		return;

	dest_render_target();
	if (FAILED(swap_chain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0)))
		return;

	init_render_target();
	window_width = static_cast<FLOAT>(width);
	window_height = static_cast<FLOAT>(height);
	::screen_size = Aspect::Vector2(static_cast<float>(width), static_cast<float>(height));
}

void c_overlay::apply_menu_window_style()
{
	if (!window_handle)
		return;

	const bool menu = g_settings.get_bool("menu_open");
	if (menu)
		SetWindowLongPtr(window_handle, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TOPMOST);
	else
		SetWindowLongPtr(window_handle, GWL_EXSTYLE, WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST);

	if (menu != last_menu_open_for_cursor_)
	{
		ShowCursor(menu ? TRUE : FALSE);
		last_menu_open_for_cursor_ = menu;
	}
}

void c_overlay::sync_imgui_input()
{
	// Feed the local DMA-PC cursor directly into ImGui.
	// GetCursorPos returns screen coordinates; ScreenToClient converts to
	// window-client coordinates so ImGui gets the right position regardless
	// of which monitor the overlay is on.
	// Use AddMouse*Event (not raw MouseDown writes) so popups / color
	// pickers get proper click + drag transitions.
	ImGuiIO& io = ImGui::GetIO();

	POINT p{};
	::GetCursorPos(&p);
	if (window_handle)
		::ScreenToClient(window_handle, &p);
	io.AddMousePosEvent(static_cast<float>(p.x), static_cast<float>(p.y));

	const bool l = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool r = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	const bool m = (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
	io.AddMouseButtonEvent(0, l);
	io.AddMouseButtonEvent(1, r);
	io.AddMouseButtonEvent(2, m);
}

LRESULT CALLBACK c_overlay::static_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_NCCREATE)
	{
		const CREATESTRUCTA* const cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
		return DefWindowProcA(hwnd, msg, wParam, lParam);
	}

	c_overlay* const self = reinterpret_cast<c_overlay*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	// Mouse input is driven entirely by sync_imgui_input() (GetCursorPos +
	// GetAsyncKeyState), matching the Arc Raiders DMA approach. We do NOT
	// forward messages to ImGui_ImplWin32_WndProcHandler to avoid OS mouse
	// event conflicts with the manually-fed cursor position.

	switch (msg)
	{
	case WM_DISPLAYCHANGE:
		if (self && self->window_handle && self->d3d_device && self->swap_chain)
		{
			self->detect_monitors();
			self->recreate_swap_chain_for_monitor();
		}
		break;
	case WM_SIZE:
		if (self && wParam != SIZE_MINIMIZED)
			self->handle_wm_size(LOWORD(lParam), HIWORD(lParam));
		return 0;
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	case WM_MOUSEWHEEL:
		// Mouse wheel is not captured by sync_imgui_input() (GetAsyncKeyState
		// doesn't track wheel delta), so forward it manually to ImGui here.
		if (ImGui::GetCurrentContext())
			ImGui::GetIO().MouseWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
		return 0;
	case WM_MOUSEHWHEEL:
		if (ImGui::GetCurrentContext())
			ImGui::GetIO().MouseWheelH += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		break;
	}

	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

c_overlay::c_overlay() :
	ready(false),
	exit(false),
	window_handle(nullptr),
	window_class({}),
	window_width((FLOAT)GetSystemMetrics(SM_CXSCREEN)),
	window_height((FLOAT)GetSystemMetrics(SM_CYSCREEN)),
	font(nullptr),
	d3d_device(nullptr),
	device_context(nullptr),
	swap_chain(nullptr),
	render_target_view(nullptr),
	draw_list(nullptr),
	breath(NULL),
	menu_ticks(NULL)
{
	const HINSTANCE hInst = GetModuleHandleA(nullptr);
	ZeroMemory(&window_class, sizeof(window_class));
	window_class.cbSize = sizeof(WNDCLASSEXA);
	window_class.style = CS_CLASSDC;
	window_class.lpfnWndProc = static_wnd_proc;
	window_class.hInstance = hInst;
	window_class.lpszClassName = k_overlay_class_name;

	if (!RegisterClassExA(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		return;

	detect_monitors();
	monitor_index_ = 0;
	display_refresh_hz_ = monitors_.front().refresh_hz;
	const RECT& r0 = monitors_.front().rect;
	const int sw = r0.right - r0.left;
	const int sh = r0.bottom - r0.top;
	window_width = static_cast<FLOAT>(sw);
	window_height = static_cast<FLOAT>(sh);
	::screen_size = Aspect::Vector2(static_cast<float>(sw), static_cast<float>(sh));

	window_handle = CreateWindowExA(
		WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
		k_overlay_class_name,
		"ROBRUN Overlay",
		WS_POPUP,
		r0.left,
		r0.top,
		sw,
		sh,
		nullptr,
		nullptr,
		hInst,
		this);

	if (!window_handle)
		return;

	owns_window_ = true;

	SetLayeredWindowAttributes(window_handle, RGB(0, 0, 0), 255, LWA_ALPHA);

	MARGINS margin = { -1 };
	DwmExtendFrameIntoClientArea(window_handle, &margin);

	ShowWindow(window_handle, SW_SHOWDEFAULT);
	UpdateWindow(window_handle);

	font = new s_font();

	if (!init_device())
	{
		DestroyWindow(window_handle);
		window_handle = nullptr;
		UnregisterClassA(k_overlay_class_name, hInst);
		owns_window_ = false;
		return;
	}

	init_imgui();

	last_menu_open_for_cursor_ = g_settings.get_bool("menu_open");
	ShowCursor(last_menu_open_for_cursor_ ? TRUE : FALSE);
}

c_overlay::~c_overlay() {
	exit = true;

	imgui_callbacks_ready_ = false;
	dest_imgui();
	dest_device();

	if (font)
	{
		delete font;
		font = nullptr;
	}

	if (owns_window_ && window_handle)
	{
		DestroyWindow(window_handle);
		window_handle = nullptr;
		UnregisterClassA(k_overlay_class_name, GetModuleHandleA(nullptr));
		owns_window_ = false;
	}
}

const void c_overlay::render(FLOAT width, FLOAT height) {
	if (exit) {
		ready = true;
		return;
	}
	static bool l_show_fps = true;
	//ImGui::SetNextWindowSize(ImVec2(275, 325));
	init_draw_list(); // we only do this once a loop

	if (l_show_fps) {
		std::string fps = std::to_string(static_cast<int>(ImGui::GetIO().Framerate));
		text({ window_width - ImGui::CalcTextSize(fps.c_str()).x, 0 }, fps);
	}

	render_callback();
}

BOOL c_overlay::msg_loop() {
	if (window_handle) {
		static LARGE_INTEGER qpc_freq{};
		static LARGE_INTEGER qpc_last_start{};
		LARGE_INTEGER qpc_now{};
		if (qpc_freq.QuadPart == 0)
			QueryPerformanceFrequency(&qpc_freq);
		QueryPerformanceCounter(&qpc_now);

		float imgui_dt = 1.f / 60.f;
		if (qpc_last_start.QuadPart != 0 && qpc_freq.QuadPart > 0)
			imgui_dt = static_cast<float>(qpc_now.QuadPart - qpc_last_start.QuadPart) / static_cast<float>(qpc_freq.QuadPart);
		qpc_last_start = qpc_now;

		if (imgui_dt <= 0.f)
			imgui_dt = 1.f / 60.f;
		else if (imgui_dt > 0.25f)
			imgui_dt = 0.25f;
		else if (imgui_dt < 1.f / 500.f)
			imgui_dt = 1.f / 500.f;

		MSG msg{ 0 };
		ImVec4 clear_clr = { 0,0,0,0 };

		bool quit = false;
		while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
			::TranslateMessage(&msg);
			::DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				quit = true;
		}
		if (quit)
			return false;

		poll_monitor_switch_hotkey();

		apply_menu_window_style();

		ImGuiIO& io = ImGui::GetIO();
		(void)io;

		ImGui_ImplDX11_NewFrame();
		// Prevent ImGui_ImplWin32_NewFrame from queuing its own mouse-pos event
		// (which is in client space from GetCursorPos+ScreenToClient) so it
		// doesn't fight with the position we set in sync_imgui_input below.
		// The flag is cleared before ImGui::NewFrame so the reset to -FLT_MAX
		// never fires.
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		ImGui_ImplWin32_NewFrame();
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		sync_imgui_input();
		io.DeltaTime = imgui_dt;
		ImGui::NewFrame();

		if (!clear_render)
		{
			render(window_width, window_height);
		}
		else
		{
			ImGui::GetIO().MouseDrawCursor = false;
			cleared_renderer = true;
		}

		ImGui::Render();
		device_context->OMSetRenderTargets(1, &render_target_view, NULL);
		device_context->ClearRenderTargetView(render_target_view, (float*)&clear_clr); // might break, if so then put cheat color of type ImVec4 { 0,0,0,0 }
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		const UINT sync_interval = g_settings.get_bool("vsync") ? 1u : 0u;
		swap_chain->Present(sync_interval, 0);

		// Proof for FPS tuning — mirrors the top-right counter into ROBRUN.log.
		{
			static LARGE_INTEGER s_last_log{};
			LARGE_INTEGER now_log{};
			QueryPerformanceCounter(&now_log);
			const double sec = (qpc_freq.QuadPart > 0)
				? double(now_log.QuadPart - s_last_log.QuadPart) / double(qpc_freq.QuadPart)
				: 0.0;
			if (s_last_log.QuadPart == 0 || sec >= 2.0)
			{
				s_last_log = now_log;
				const int fps = static_cast<int>(ImGui::GetIO().Framerate + 0.5f);
				std::cout << "[overlay] fps=" << fps
					<< " frame_ms=" << (imgui_dt * 1000.f)
					<< " menu=" << (g_settings.get_bool("menu_open") ? 1 : 0)
					<< std::endl;
			}
		}

		return true;
	}
	return false;
}

VOID c_overlay::init_draw_list() {
	draw_list = ImGui::GetBackgroundDrawList();
}

VOID c_overlay::crosshair(const FLOAT aSize, ImU32 color) {
	draw_list->AddLine({ window_width / 2,window_height / 2 - (aSize + 1) }, { window_width / 2 ,window_height / 2 + (aSize + 1) }, color, 2);
	draw_list->AddLine({ window_width / 2 - (aSize + 1),window_height / 2 }, { window_width / 2 + (aSize + 1), window_height / 2 }, color, 2);
}

VOID c_overlay::box(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color, const FLOAT line_width) {
	const float t = (line_width > 0.f) ? line_width : 1.f;
	const ImU32 outline = IM_COL32(0, 0, 0, 210);
	draw_list->AddRect(pos, ImVec2{ pos.x + width, pos.y + height }, outline, 0.f, 0, t + 1.6f);
	draw_list->AddRect(pos, ImVec2{ pos.x + width, pos.y + height }, color, 0.f, 0, t);
}

VOID c_overlay::boxfilled(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color) {
	draw_list->AddRectFilled(pos, ImVec2{ pos.x + width,pos.y + height }, color);
}

VOID c_overlay::corner_box(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color,
                           const FLOAT line_width, float corner_frac)
{
	const float t = (line_width > 0.f) ? line_width : 1.5f;
	float cl = (std::min)(width, height) * corner_frac;
	cl = (std::max)(6.f, (std::min)(cl, (std::min)(width, height) * 0.42f));

	const float x0 = pos.x, y0 = pos.y;
	const float x1 = pos.x + width, y1 = pos.y + height;
	const ImU32 outline = IM_COL32(0, 0, 0, 220);

	auto corner = [&](ImU32 col, float w) {
		// TL
		draw_list->AddLine({ x0, y0 }, { x0 + cl, y0 }, col, w);
		draw_list->AddLine({ x0, y0 }, { x0, y0 + cl }, col, w);
		// TR
		draw_list->AddLine({ x1, y0 }, { x1 - cl, y0 }, col, w);
		draw_list->AddLine({ x1, y0 }, { x1, y0 + cl }, col, w);
		// BL
		draw_list->AddLine({ x0, y1 }, { x0 + cl, y1 }, col, w);
		draw_list->AddLine({ x0, y1 }, { x0, y1 - cl }, col, w);
		// BR
		draw_list->AddLine({ x1, y1 }, { x1 - cl, y1 }, col, w);
		draw_list->AddLine({ x1, y1 }, { x1, y1 - cl }, col, w);
	};
	corner(outline, t + 1.7f);
	corner(color, t);
}

VOID c_overlay::bracket_box(const ImVec2& pos, const FLOAT width, const FLOAT height, ImU32 color,
                            const FLOAT line_width)
{
	const float t = (line_width > 0.f) ? line_width : 1.35f;
	// Short arms (~18% of min side) — reads as a frame, not a full box.
	float arm = (std::min)(width, height) * 0.18f;
	arm = (std::max)(5.f, (std::min)(arm, 22.f));
	const float tip = (std::max)(2.2f, t * 0.55f);

	const float x0 = pos.x, y0 = pos.y;
	const float x1 = pos.x + width, y1 = pos.y + height;
	const ImU32 outline = IM_COL32(0, 0, 0, 200);

	auto L = [&](float ax, float ay, float bx, float by, ImU32 col, float w) {
		draw_list->AddLine({ ax, ay }, { bx, by }, col, w);
	};
	auto draw = [&](ImU32 col, float w) {
		L(x0, y0, x0 + arm, y0, col, w); L(x0, y0, x0, y0 + arm, col, w);
		L(x1, y0, x1 - arm, y0, col, w); L(x1, y0, x1, y0 + arm, col, w);
		L(x0, y1, x0 + arm, y1, col, w); L(x0, y1, x0, y1 - arm, col, w);
		L(x1, y1, x1 - arm, y1, col, w); L(x1, y1, x1, y1 - arm, col, w);
		// Tiny accent dots at corner tips for depth without glow spam.
		draw_list->AddCircleFilled({ x0, y0 }, tip, col, 6);
		draw_list->AddCircleFilled({ x1, y0 }, tip, col, 6);
		draw_list->AddCircleFilled({ x0, y1 }, tip, col, 6);
		draw_list->AddCircleFilled({ x1, y1 }, tip, col, 6);
	};
	draw(outline, t + 1.55f);
	draw(color, t);
}

VOID c_overlay::line(const ImVec2& point1, const ImVec2 point2, ImU32 color, const FLOAT line_width) {
	draw_list->AddLine(point1, point2, color, line_width);
}

VOID c_overlay::line_outlined(const ImVec2& a, const ImVec2& b, ImU32 color, float width)
{
	const float w = (width > 0.f) ? width : 1.5f;
	draw_list->AddLine(a, b, IM_COL32(0, 0, 0, 200), w + 1.8f);
	draw_list->AddLine(a, b, color, w);
}

VOID c_overlay::bone_segment(const ImVec2& a, const ImVec2& b, ImU32 color, float width)
{
	const float w = (width > 0.f) ? width : 1.6f;
	draw_list->AddLine(a, b, IM_COL32(0, 0, 0, 210), w + 1.9f);
	draw_list->AddLine(a, b, color, w);
	const float jr = (std::max)(1.6f, w * 0.85f);
	draw_list->AddCircleFilled(a, jr + 1.1f, IM_COL32(0, 0, 0, 180), 8);
	draw_list->AddCircleFilled(b, jr + 1.1f, IM_COL32(0, 0, 0, 180), 8);
	draw_list->AddCircleFilled(a, jr, color, 8);
	draw_list->AddCircleFilled(b, jr, color, 8);
}

VOID c_overlay::circle(const ImVec2& point, const FLOAT radius, ImU32 color, const FLOAT width) {
	// 12 segments is plenty for ESP dots — 200 was nuking overlay FPS.
	draw_list->AddCircle(point, radius, IM_COL32(0, 0, 0, 200), 12, width + 1.4f);
	draw_list->AddCircle(point, radius, color, 12, width);
}

VOID c_overlay::circle_filled(const ImVec2& point, const FLOAT radius, ImU32 color) {
	draw_list->AddCircleFilled(point, radius, color, 14);
}

VOID c_overlay::text(const ImVec2& pos, const std::string& text, ImU32 color, bool outline, bool background, bool center_x, float font_size)
{
	this->text(pos, text, font_size, color, outline);
}

VOID c_overlay::text(const ImVec2& pos, const std::string& text, float font_size, ImU32 color, bool outline)
{
	if (font_size <= 0.0f)
		font_size = 14.0f;

	if (!font || !font->esp_font)
		return;

	// World→screen projections produce sub-pixel positions (e.g. 482.37) that
	// drift fractionally as the camera moves. Without snapping, ImGui's
	// per-glyph anti-aliasing slides between adjacent pixels every frame and
	// you see a colored / black shimmer crawling around the text.  Snapping
	// the base position to integer pixels locks the AA pattern to the pixel
	// grid so subsequent frames at the same logical position look identical.
	const ImVec2 draw_pos(std::floor(pos.x + 0.5f), std::floor(pos.y + 0.5f));

	if (outline)
	{
		// Full 8-direction 1-pixel outline. The old NSEW-only (4 pass) pattern
		// left the four diagonal corners of every glyph uncovered, so the
		// colored interior bled through at the corners as a faint halo —
		// most visible against bright text on a dark background. Adding the
		// 4 diagonal offsets closes the outline into a complete square shell.
		const ImFont* f  = font->esp_font;
		const char*   s  = text.c_str();
		draw_list->AddText(f, font_size, { draw_pos.x + 1.f, draw_pos.y       }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x - 1.f, draw_pos.y       }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x      , draw_pos.y + 1.f }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x      , draw_pos.y - 1.f }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x + 1.f, draw_pos.y + 1.f }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x - 1.f, draw_pos.y + 1.f }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x + 1.f, draw_pos.y - 1.f }, IM_COL32_BLACK, s);
		draw_list->AddText(f, font_size, { draw_pos.x - 1.f, draw_pos.y - 1.f }, IM_COL32_BLACK, s);
	}

	draw_list->AddText(font->esp_font, font_size, draw_pos, color, text.c_str());
}

VOID c_overlay::text_chip(const ImVec2& center_top, const std::string& text, float font_size,
                          ImU32 text_color, ImU32 plate)
{
	if (font_size <= 0.f) font_size = 13.f;
	if (!font || !font->esp_font || text.empty()) return;

	const ImVec2 sz = font->esp_font->CalcTextSizeA(font_size, FLT_MAX, 0.f, text.c_str());
	const float pad_x = 5.f;
	const float pad_y = 2.f;
	const float x = std::floor(center_top.x - sz.x * 0.5f - pad_x + 0.5f);
	const float y = std::floor(center_top.y - sz.y - pad_y * 2.f + 0.5f);
	const ImVec2 mn(x, y);
	const ImVec2 mx(x + sz.x + pad_x * 2.f, y + sz.y + pad_y * 2.f);
	draw_list->AddRectFilled(mn, mx, plate, 2.f);
	draw_list->AddRect(mn, mx, fuser_col::edge(55), 2.f, 0, 1.f);
	this->text(ImVec2(x + pad_x, y + pad_y), text, font_size, text_color, true);
}

VOID c_overlay::identity_plate(const ImVec2& center_top, const std::string& title, const std::string& meta,
                               float font_size, ImU32 title_color)
{
	if (!font || !font->esp_font) return;
	if (title.empty() && meta.empty()) return;
	if (font_size <= 0.f) font_size = 13.f;

	const float title_sz = font_size;
	const float meta_sz = (std::max)(10.5f, font_size * 0.82f);
	const ImVec2 tsz = title.empty() ? ImVec2{}
		: font->esp_font->CalcTextSizeA(title_sz, FLT_MAX, 0.f, title.c_str());
	const ImVec2 msz = meta.empty() ? ImVec2{}
		: font->esp_font->CalcTextSizeA(meta_sz, FLT_MAX, 0.f, meta.c_str());

	const float pad_x = 6.f;
	const float pad_y = 3.f;
	const float gap = (!title.empty() && !meta.empty()) ? 1.f : 0.f;
	const float inner_w = (std::max)(tsz.x, msz.x);
	const float inner_h = tsz.y + msz.y + gap;
	const float box_w = inner_w + pad_x * 2.f;
	const float box_h = inner_h + pad_y * 2.f;
	const float x = std::floor(center_top.x - box_w * 0.5f + 0.5f);
	const float y = std::floor(center_top.y - box_h + 0.5f);

	// Fuser-safe plate (not near-black — that keys out as a hole).
	draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + box_w, y + box_h),
	                         fuser_col::plate(210), 2.f);
	// Force full alpha on accent + title — faded colors look washed at range.
	const ImU32 solid = (title_color & 0x00FFFFFF) | IM_COL32(0, 0, 0, 255);
	draw_list->AddRectFilled(ImVec2(x, y), ImVec2(x + 2.f, y + box_h), solid, 1.f);
	draw_list->AddRect(ImVec2(x, y), ImVec2(x + box_w, y + box_h),
	                   fuser_col::edge(70), 2.f, 0, 1.f);

	float ty = y + pad_y;
	if (!title.empty())
	{
		const float tx = std::floor(x + (box_w - tsz.x) * 0.5f + 0.5f);
		this->text(ImVec2(tx, ty), title, title_sz, solid, true);
		ty += tsz.y + gap;
	}
	if (!meta.empty())
	{
		const float mx = std::floor(x + (box_w - msz.x) * 0.5f + 0.5f);
		this->text(ImVec2(mx, ty), meta, meta_sz, fuser_col::meta(), true);
	}
}

VOID c_overlay::loot_pin(const ImVec2& center, ImU32 color, int style, float size, bool halo)
{
	if (size < 2.f) size = 2.f;
	const float s = size;
	const ImVec2 c = center;

	if (halo)
	{
		const float r = s * 2.4f;
		draw_list->AddCircleFilled(c, r, (color & 0x00FFFFFF) | IM_COL32(0, 0, 0, 55), 16);
		draw_list->AddCircle(c, r * 0.85f, (color & 0x00FFFFFF) | IM_COL32(0, 0, 0, 90), 16, 1.2f);
	}

	// Soft drop stem — anchors the glyph to the ground point.
	draw_list->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + s * 1.35f),
	                   IM_COL32(0, 0, 0, 160), 2.0f);
	draw_list->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + s * 1.35f),
	                   (color & 0x00FFFFFF) | IM_COL32(0, 0, 0, 200), 1.0f);

	auto fill_outline = [&](const ImVec2* pts, int n) {
		// Black underlay keys out on Fuser — intentional; colored fill stays.
		draw_list->AddConvexPolyFilled(pts, n, IM_COL32(0, 0, 0, 200));
		draw_list->AddConvexPolyFilled(pts, n, color);
		draw_list->AddPolyline(pts, n, fuser_col::edge(70), ImDrawFlags_Closed, 1.0f);
	};

	switch (style)
	{
	case 1: // container — rounded square
	{
		const float h = s * 0.85f;
		draw_list->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h),
		                         IM_COL32(0, 0, 0, 200), 1.5f);
		draw_list->AddRectFilled(ImVec2(c.x - h + 0.6f, c.y - h + 0.6f),
		                         ImVec2(c.x + h - 0.6f, c.y + h - 0.6f), color, 1.2f);
		draw_list->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h),
		                   fuser_col::edge(80), 1.5f, 0, 1.0f);
		break;
	}
	case 2: // corpse — X
	{
		const float t = (std::max)(1.4f, s * 0.35f);
		draw_list->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s),
		                   IM_COL32(0, 0, 0, 220), t + 1.4f);
		draw_list->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s),
		                   IM_COL32(0, 0, 0, 220), t + 1.4f);
		draw_list->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), color, t);
		draw_list->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), color, t);
		break;
	}
	case 3: // quest — teardrop pin
	{
		const ImVec2 pts[3] = {
			{ c.x, c.y - s * 1.15f },
			{ c.x + s * 0.85f, c.y + s * 0.35f },
			{ c.x - s * 0.85f, c.y + s * 0.35f },
		};
		fill_outline(pts, 3);
		draw_list->AddCircleFilled(c, s * 0.28f, fuser_col::plate(220), 8);
		break;
	}
	case 4: // airdrop — down chevron
	{
		const ImVec2 pts[3] = {
			{ c.x - s * 1.1f, c.y - s * 0.55f },
			{ c.x + s * 1.1f, c.y - s * 0.55f },
			{ c.x, c.y + s * 1.05f },
		};
		fill_outline(pts, 3);
		break;
	}
	default: // loose — diamond
	{
		const ImVec2 pts[4] = {
			{ c.x, c.y - s }, { c.x + s, c.y },
			{ c.x, c.y + s }, { c.x - s, c.y },
		};
		fill_outline(pts, 4);
		break;
	}
	}
}

VOID c_overlay::radial_gradient(const ImVec2& center, float radius, ImU32 col_in, ImU32 col_out) {
	if (((col_in | col_out) & IM_COL32_A_MASK) == 0 || radius < 0.5f)
		return;

	//draw_list->_PathArcToFastEx(center, radius, 0, IM_DRAWLIST_ARCFAST_SAMPLE_MAX, 0);
	const int count = draw_list->_Path.Size - 1;

	unsigned int vtx_base = draw_list->_VtxCurrentIdx;
	draw_list->PrimReserve(count * 3, count + 1);

	const ImVec2 uv = draw_list->_Data->TexUvWhitePixel;
	draw_list->PrimWriteVtx(center, uv, col_in);
	for (int n = 0; n < count; n++)
		draw_list->PrimWriteVtx(draw_list->_Path[n], uv, col_out);

	for (int n = 0; n < count; n++) {
		draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base));
		draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + n));
		draw_list->PrimWriteIdx((ImDrawIdx)(vtx_base + 1 + ((n + 1) % count)));
	}
	draw_list->_Path.Size = 0;
}

BOOL c_overlay::in_screen(const ImVec2& pos) {
	return !(pos.x > window_width || pos.x<0 || pos.y>window_height || pos.y < 0);
}