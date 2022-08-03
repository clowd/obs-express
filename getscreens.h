#pragma once

#include <vector>

struct screen_info {
	screen_info(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t dpi) : x(x), y(y), width(width), height(height), dpi(dpi) {}
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
	uint32_t dpi;
};

struct mouse_info {
	int32_t x;
	int32_t y;
	bool pressed;
	uint32_t dpi;
};

std::vector<screen_info> get_screen_info();

mouse_info get_mouse_info();