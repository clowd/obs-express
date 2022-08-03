#include "getscreens.h"
#include "windows.h";
#include "shellscalingapi.h"

using namespace std;

BOOL __cdecl EnumMonitorCallback(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	std::vector<screen_info>& infos = *((std::vector<screen_info>*)dwData);

	MONITORINFOEX mon_info{};
	mon_info.cbSize = sizeof(MONITORINFOEX);
	GetMonitorInfo(hMonitor, &mon_info);

	UINT dpiX, dpiY;
	GetDpiForMonitor(hMonitor, MDT_DEFAULT, &dpiX, &dpiY);

	infos.emplace_back(
		mon_info.rcMonitor.left,
		mon_info.rcMonitor.top,
		mon_info.rcMonitor.right - mon_info.rcMonitor.left,
		mon_info.rcMonitor.bottom - mon_info.rcMonitor.top,
		dpiX);

	return TRUE;
}

vector<screen_info> get_screen_info() {
	std::vector<screen_info> screens{};
	HDC hdc = GetDC(NULL);
	EnumDisplayMonitors(hdc, NULL, EnumMonitorCallback, (LPARAM)&screens);
	return screens;
}

mouse_info get_mouse_info() {
	// get mouse state & position
	POINT p;
	GetCursorPos(&p);
	int32_t x = p.x;
	int32_t y = p.y;
	bool leftkeydown = GetAsyncKeyState(VK_LBUTTON) & 0x8000;
	bool rightkeydown = GetAsyncKeyState(VK_RBUTTON) & 0x8000;
	bool pressed = leftkeydown || rightkeydown;

	// get dpi of monitor mouse is located on
	HMONITOR hMon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
	UINT dpiX, dpiY;
	GetDpiForMonitor(hMon, MDT_DEFAULT, &dpiX, &dpiY);

	mouse_info info{ x, y, pressed, dpiX };
	return info;
}