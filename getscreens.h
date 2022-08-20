#pragma once

#include <vector>

struct screen_info
{
    screen_info(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t dpi, char id[128], char name[64])
        : x(x), y(y), width(width), height(height), dpi(dpi), monitor_id(""), monitor_name("")
    {
        strcpy_s(monitor_id, _countof(monitor_id), id);
        strcpy_s(monitor_name, _countof(monitor_name), name);
    }
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t dpi;
    char monitor_id[128];
    char monitor_name[64];
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