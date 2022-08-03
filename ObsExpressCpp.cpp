#include <string>
#include <iostream>
#include <vector>

#include "windows.h"
#include "gdiplus.h"
#include "shellscalingapi.h"
#include "argh.h"
#include "include/obs.h"

#pragma comment(lib, "user32.lib") 
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
//#pragma comment(lib, "gdi32.lib")
//#pragma comment(lib, "shcore.lib")
//#pragma comment(lib, "ole32.lib")
//#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")
//#pragma comment(lib, "msimg32.lib")
//#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "obs.lib")

using namespace std;
using namespace Gdiplus;

//struct obs_recording_start_request {
//	RECT captureRegion;
//	vector<std::string> speakers;
//	vector<std::string> microphones;
//	uint16_t fps;
//	uint16_t cq;
//	uint16_t maxOutputWidth;
//	uint16_t maxOutputHeight;
//	BOOL hardwareAccelerated;
//	std::string outputFileName;
//};

//void start_recording(const obs_recording_start_request& request)
//{
//
//}

BOOL WINAPI ctrl_handler(DWORD fdwCtrlType)
{
	switch (fdwCtrlType)
	{
		// Handle the CTRL-C signal.
	case CTRL_C_EVENT:
		printf("Ctrl-C event\n\n");
		Beep(750, 300);
		return TRUE;

		// CTRL-CLOSE: confirm that the user wants to exit.
	case CTRL_CLOSE_EVENT:
		Beep(600, 200);
		printf("Ctrl-Close event\n\n");
		return TRUE;

		// Pass other signals to the next handler.
	case CTRL_BREAK_EVENT:
		Beep(900, 200);
		printf("Ctrl-Break event\n\n");
		return FALSE;

	case CTRL_LOGOFF_EVENT:
		Beep(1000, 200);
		printf("Ctrl-Logoff event\n\n");
		return FALSE;

	case CTRL_SHUTDOWN_EVENT:
		Beep(750, 500);
		printf("Ctrl-Shutdown event\n\n");
		return FALSE;

	default:
		return FALSE;
	}
}

vector<string> split_comma(const string& input) {
	stringstream ss(input);
	vector<string> result;

	while (ss.good())
	{
		string substr;
		getline(ss, substr, ',');
		result.push_back(substr);
	}

	return result;
}

Rect parse_rect(const string& input) {
	auto parts = split_comma(input);
	if (parts.size() != 4) {
		string message = "Not a rectangle: " + input;
		throw std::exception(message.c_str());
	}

	Rect r{};
	r.X = stoi(parts[0]);
	r.Y = stoi(parts[1]);
	r.Width = stoi(parts[2]);
	r.Height = stoi(parts[3]);

	return r;
}

Size parse_size(const string& input) {
	auto parts = split_comma(input);
	if (parts.size() != 2) {
		string message = "Not a size: " + input;
		throw std::exception(message.c_str());
	}

	Size r{};
	r.Width = stoi(parts[0]);
	r.Height = stoi(parts[1]);
	return r;
}

int run(int argc, char* argv[])
{
	// handle command line arguments
	argh::parser cmdl;
	cmdl.add_params({ "captureRegion", "speakers", "microphones", "fps", "cq", "maxOutputSize", "outputFile" });
	cmdl.parse(argc, argv);

	uint16_t fps, cq;
	cmdl("fps", 30) >> fps;
	cmdl("cq", 24) >> cq;

	auto speakers = cmdl.params("speakers");
	auto microphones = cmdl.params("microphones");

	string tmpCaptureRegion, tmpMaxOutputSize, outputFile;
	cmdl("captureRegion") >> tmpCaptureRegion;
	cmdl("maxOutputSize", "0,0") >> tmpMaxOutputSize;
	cmdl("outputFile") >> outputFile;

	if (tmpCaptureRegion.empty())
		throw std::exception("Required parameter: captureRegion");

	Rect captureRegion = parse_rect(tmpCaptureRegion);
	Size maxOutputSize = parse_size(tmpMaxOutputSize);

	// per monitor dpi aware so winapi does not lie to us
	SetProcessDpiAwareness(PROCESS_DPI_AWARENESS::PROCESS_PER_MONITOR_DPI_AWARE);

	// do obs setup.
	if (!obs_startup("en-US", nullptr, nullptr))
		throw std::exception("Unable to start OBS");

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
		throw std::exception("Unable to initialize video, error code: " + vr);

	obs_audio_info avi{};
	avi.samples_per_sec = 44100;
	avi.speakers = speaker_layout::SPEAKERS_STEREO;

	if (!obs_reset_audio(&avi))
		throw std::exception("Unable to initialize audio");

	obs_post_load_modules();

	if (!obs_initialized()) {
		throw std::exception("Unknown error initializing");
	}

	cout << "OBS version " + string(obs_get_version_string()) + " loaded successfully.";

	// catch ctrl events and shut down obs gracefully
	SetConsoleCtrlHandler(ctrl_handler, TRUE);



}

int main(int argc, char* argv[])
{
	try
	{
		run(argc, argv);
	}
	catch (const std::exception& exc)
	{
		std::cerr << std::endl << exc.what();
		std::cerr << std::endl << "A fatal error has occurred. The application will exit." << std::endl;
	}
	catch (...)
	{
		std::cerr << std::endl << "An unknown error has occurred. The application will exit." << std::endl;
	}
}