#pragma once

#include <vector>

struct screen_info
{
    screen_info(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t dpi, char id[128], char name[64], char friendly_name[64])
        : x(x), y(y), width(width), height(height), dpi(dpi), monitor_id(""), monitor_device_name(""), monitor_friendly_name("")
    {
        strcpy_s(monitor_id, _countof(monitor_id), id);
        strcpy_s(monitor_device_name, _countof(monitor_device_name), name);
        strcpy_s(monitor_friendly_name, _countof(monitor_friendly_name), friendly_name);
    }
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t dpi;
    char monitor_id[128];
    char monitor_device_name[64];
    char monitor_friendly_name[64];
};

struct mouse_info
{
    int32_t x;
    int32_t y;
    bool pressed;
    uint32_t dpi;
};

std::vector<screen_info> get_screen_info();

mouse_info get_mouse_info();