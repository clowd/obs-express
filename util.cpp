#include "util.h"

#include <sstream>


#include "obs-studio/libobs/obs.h"
#include "obs-studio/libobs/util/platform.h"

using namespace std;
using namespace Gdiplus;

os_cpu_usage_info_t* cpuUsageInfo = nullptr;

double getCPU_Percentage()
{
    double cpuPercentage = os_cpu_usage_info_query(cpuUsageInfo);
    cpuPercentage *= 10;
    cpuPercentage = trunc(cpuPercentage);
    cpuPercentage /= 10;
    return cpuPercentage;
}

uint64_t util_obs_get_time_ms()
{
    return os_gettime_ns() / 1000000;
}

double util_obs_get_cpu_utilisation()
{
    return getCPU_Percentage();
}

void util_obs_cpu_usage_info_start()
{
    if (cpuUsageInfo)
        os_cpu_usage_info_destroy(cpuUsageInfo);

    cpuUsageInfo = os_cpu_usage_info_start();
}

vector<string> util_string_split(const string& input, char delimiter)
{
    stringstream ss(input);
    vector<string> result;

    while (ss.good()) {
        string substr;
        getline(ss, substr, delimiter);
        result.push_back(substr);
    }

    return result;
}

Rect util_parse_rect(const string& input)
{
    auto parts = util_string_split(input, ',');
    if (parts.size() != 4) {
        string message = "Not a valid rectangle: " + input;
        throw std::invalid_argument(message.c_str());
    }

    Rect r{};
    r.X = stoi(parts[0]);
    r.Y = stoi(parts[1]);
    r.Width = stoi(parts[2]);
    r.Height = stoi(parts[3]);

    return r;
}

Color util_parse_color(const string& input)
{
    auto parts = util_string_split(input, ',');
    if (parts.size() != 3) {
        string message = "Not a valid color: " + input;
        throw std::invalid_argument(message.c_str());
    }

    Color r{ (BYTE)stoi(parts[0]), (BYTE)stoi(parts[1]), (BYTE)stoi(parts[2]) };
    return r;
}

string get_obs_output_errorcode_string(int code)
{
    switch (code) {
    case OBS_OUTPUT_SUCCESS:
        return "Successfully stopped";
    case OBS_OUTPUT_BAD_PATH:
        return "The specified path was invalid";
    case OBS_OUTPUT_CONNECT_FAILED:
        return "Failed to connect to a server";
    case OBS_OUTPUT_INVALID_STREAM:
        return "Invalid stream path";
    case OBS_OUTPUT_ERROR:
        return "Generic error";
    case OBS_OUTPUT_DISCONNECTED:
        return "Unexpectedly disconnected";
    case OBS_OUTPUT_UNSUPPORTED:
        return "The settings, video/audio format, or codecs are unsupported by this output";
    case OBS_OUTPUT_NO_SPACE:
        return "Ran out of disk space";
    case OBS_OUTPUT_ENCODE_ERROR:
        return "Encoder error";
    }
}

std::string util_string_utf8_encode(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}