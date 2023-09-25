#pragma once
#include <vector>
#include <string>

#include "windows.h"
#include "gdiplus.h"

using namespace std;

uint64_t util_obs_get_time_ms();
double util_obs_get_cpu_utilisation();
void util_obs_cpu_usage_info_start();
vector<string> util_string_split(const string& input, char delimiter);
Gdiplus::Rect util_parse_rect(const string& input);
Gdiplus::Color util_parse_color(const string& input);
string get_obs_output_errorcode_string(uint32_t code);
std::string util_string_utf8_encode(const std::wstring& wstr);
