#include "getscreens.h"
#include "windows.h"
#include "shellscalingapi.h"

using namespace std;

static bool GetMonitorTarget(LPCWSTR device, DISPLAYCONFIG_TARGET_DEVICE_NAME* target)
{
    bool found = false;
    UINT32 numPath, numMode;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPath, &numMode) == ERROR_SUCCESS) {
        DISPLAYCONFIG_PATH_INFO* paths = (DISPLAYCONFIG_PATH_INFO*)malloc(numPath * sizeof(DISPLAYCONFIG_PATH_INFO));
        DISPLAYCONFIG_MODE_INFO* modes = (DISPLAYCONFIG_MODE_INFO*)malloc(numMode * sizeof(DISPLAYCONFIG_MODE_INFO));
        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPath, paths, &numMode, modes, NULL) == ERROR_SUCCESS) {
            for (size_t i = 0; i < numPath; ++i) {
                const DISPLAYCONFIG_PATH_INFO* const path = &paths[i];
                DISPLAYCONFIG_SOURCE_DEVICE_NAME source;
                source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                source.header.size = sizeof(source);
                source.header.adapterId = path->sourceInfo.adapterId;
                source.header.id = path->sourceInfo.id;
                if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS && wcscmp(device, source.viewGdiDeviceName) == 0) {
                    target->header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
                    target->header.size = sizeof(*target);
                    target->header.adapterId = path->sourceInfo.adapterId;
                    target->header.id = path->targetInfo.id;
                    found = DisplayConfigGetDeviceInfo(&target->header) == ERROR_SUCCESS;
                    break;
                }
            }
        }

        free(modes);
        free(paths);
    }

    return found;
}

static void GetMonitorName(HMONITOR handle, char* name, size_t count)
{
    MONITORINFOEXW mi;
    DISPLAYCONFIG_TARGET_DEVICE_NAME target;

    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(handle, (LPMONITORINFO)&mi) && GetMonitorTarget(mi.szDevice, &target)) {
        snprintf(name, count, "%ls", target.monitorFriendlyDeviceName);
    }
    else {
        strcpy_s(name, count, "[OBS: Unknown]");
    }
}

BOOL __cdecl EnumMonitorCallback(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
    std::vector<screen_info>& infos = *((std::vector<screen_info>*)dwData);

    MONITORINFOEXA mon_info{};
    mon_info.cbSize = sizeof(MONITORINFOEXA);
    GetMonitorInfoA(hMonitor, &mon_info);

    UINT dpiX, dpiY;
    GetDpiForMonitor(hMonitor, MDT_DEFAULT, &dpiX, &dpiY);

    // we are using the 'A' functions here instead of 'W' only because OBS also does this and we need
    // our id's to match what OBS is expecting.
    DISPLAY_DEVICEA device;
    device.cb = sizeof(device);
    EnumDisplayDevicesA(mon_info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME);

    char monitor_name[64];
    GetMonitorName(hMonitor, monitor_name, sizeof(monitor_name));

    infos.emplace_back(
        mon_info.rcMonitor.left,
        mon_info.rcMonitor.top,
        mon_info.rcMonitor.right - mon_info.rcMonitor.left,
        mon_info.rcMonitor.bottom - mon_info.rcMonitor.top,
        dpiX,
        device.DeviceID,
        mon_info.szDevice,
        monitor_name);

    return TRUE;
}

vector<screen_info> get_screen_info()
{
    std::vector<screen_info> screens{};
    HDC hdc = GetDC(NULL);
    EnumDisplayMonitors(hdc, NULL, EnumMonitorCallback, (LPARAM)&screens);
    return screens;
}

mouse_info get_mouse_info()
{
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