#include <iostream>
#include "windows.h"
#include "include/obs.h"

#pragma comment(lib, "user32.lib") 
#pragma comment(lib, "dwmapi.lib")
//#pragma comment(lib, "gdi32.lib")
//#pragma comment(lib, "shcore.lib")
//#pragma comment(lib, "ole32.lib")
//#pragma comment(lib, "shell32.lib")
//#pragma comment(lib, "gdiplus.lib")
//#pragma comment(lib, "msimg32.lib")
//#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "obs.lib")

using namespace std;

int main()
{
	if (!obs_startup("en-US", nullptr, nullptr))
		throw new std::exception("Unable to start OBS");

	obs_load_all_modules();
	obs_log_loaded_modules();

	obs_video_info vvi{};
	vvi.adapter = 0;
	vvi.base_width = 100;
	vvi.base_height = 100;
	vvi.fps_num = 30;
	vvi.fps_den = 30;
	vvi.graphics_module = "libobs-d3d11";
	vvi.output_format = video_format::VIDEO_FORMAT_NV12;
	vvi.output_width = 100;
	vvi.output_height = 100;
	vvi.scale_type = obs_scale_type::OBS_SCALE_BILINEAR;
	vvi.colorspace = video_colorspace::VIDEO_CS_DEFAULT;
	vvi.gpu_conversion = true;
	vvi.range = video_range_type::VIDEO_RANGE_PARTIAL;

	auto vr = obs_reset_video(&vvi);
	if (vr != OBS_VIDEO_SUCCESS)
		throw new std::exception("Unable to initialize video, error code: " + vr);

	obs_audio_info avi{};
	avi.samples_per_sec = 44100;
	avi.speakers = speaker_layout::SPEAKERS_STEREO;

	if (!obs_reset_audio(&avi))
		throw new std::exception("Unable to initialize audio");

	obs_post_load_modules();




}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
