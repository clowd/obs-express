#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <iterator>
#include "windows.h"
#include "gdiplus.h"
#include "shellscalingapi.h"
#include "argh.h"
#include "getscreens.h"
#include "json.hpp"
#include "include/obs.h"


#pragma comment(lib, "user32.lib") 
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "obs.lib")

//#pragma comment(lib, "gdi32.lib")
//#pragma comment(lib, "shcore.lib")
//#pragma comment(lib, "ole32.lib")
//#pragma comment(lib, "shell32.lib")
//#pragma comment(lib, "msimg32.lib")
//#pragma comment(lib, "winmm.lib")

using namespace std;
using namespace Gdiplus;
using json = nlohmann::json;

bool cancelRequested = false;

BOOL WINAPI ctrl_handler(DWORD fdwCtrlType)
{
	cancelRequested = true;
	return TRUE; // indicate we have handled the signal and no further processing should happen

	//switch (fdwCtrlType)
	//{
	//	// Handle the CTRL-C signal.
	//case CTRL_C_EVENT:
	//	printf("Ctrl-C event\n\n");
	//	Beep(750, 300);
	//	return TRUE;

	//	// CTRL-CLOSE: confirm that the user wants to exit.
	//case CTRL_CLOSE_EVENT:
	//	Beep(600, 200);
	//	printf("Ctrl-Close event\n\n");
	//	return TRUE;

	//	// Pass other signals to the next handler.
	//case CTRL_BREAK_EVENT:
	//	Beep(900, 200);
	//	printf("Ctrl-Break event\n\n");
	//	return FALSE;

	//case CTRL_LOGOFF_EVENT:
	//	Beep(1000, 200);
	//	printf("Ctrl-Logoff event\n\n");
	//	return FALSE;

	//case CTRL_SHUTDOWN_EVENT:
	//	Beep(750, 500);
	//	printf("Ctrl-Shutdown event\n\n");
	//	return FALSE;

	//default:
	//	return FALSE;
	//}
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

void run(int argc, char* argv[])
{
	// handle command line arguments
	argh::parser cmdl;
	cmdl.add_params({ "adapter", "captureRegion", "speakers", "microphones", "fps", "cq", "maxOutputWidth", "maxOutputHeight", "outputFile" });
	cmdl.parse(argc, argv);

	bool pause = cmdl["pause"];

	uint16_t adapter, fps, cq, maxOutputWidth, maxOutputHeight;
	cmdl("adapter", 0) >> adapter;
	cmdl("fps", 120) >> fps;
	cmdl("cq", 24) >> cq;
	cmdl("maxOutputWidth", 0) >> maxOutputWidth;
	cmdl("maxOutputHeight", 0) >> maxOutputHeight;

	auto speakers = cmdl.params("speakers");
	auto microphones = cmdl.params("microphones");

	string tmpCaptureRegion, outputFile;
	cmdl("captureRegion") >> tmpCaptureRegion;
	cmdl("outputFile") >> outputFile;

	if (tmpCaptureRegion.empty() || outputFile.empty())
		throw std::exception("Required parameters: captureRegion, outputFile");

	Rect captureRegion = parse_rect(tmpCaptureRegion);

	// calculate ideal canvas size
	SizeF outputSize{ (float)captureRegion.Width, (float)captureRegion.Height };

	if (maxOutputWidth > 0 && outputSize.Width > maxOutputWidth) {
		float waspect = outputSize.Width / outputSize.Height;
		outputSize.Width = maxOutputWidth;
		outputSize.Height = round(maxOutputWidth / waspect);
	}

	if (maxOutputHeight > 0 && outputSize.Height > maxOutputHeight) {
		float haspect = outputSize.Height / outputSize.Width;
		outputSize.Width = round(maxOutputHeight / haspect);
		outputSize.Height = maxOutputHeight;
	}

	float dnsclperc = round((1 - ((outputSize.Width * outputSize.Height) / (captureRegion.Width * captureRegion.Height))) * 100);

	if (dnsclperc > 0) {
		cout << "Downscaling from " << captureRegion.Width << "x" << captureRegion.Height << " to " << outputSize.Width << "x" << outputSize.Height << " (-" << dnsclperc << ")" << std::endl;
	}

	// do obs setup.
	if (!obs_startup("en-US", nullptr, nullptr))
		throw std::exception("Unable to start OBS");

	obs_video_info vvi{};
	vvi.adapter = adapter;
	vvi.base_width = captureRegion.Width;
	vvi.base_height = captureRegion.Height;
	vvi.fps_num = fps;
	vvi.fps_den = 1;
	vvi.graphics_module = "libobs-d3d11";
	vvi.output_format = video_format::VIDEO_FORMAT_NV12;
	vvi.output_width = (uint32_t)outputSize.Width;
	vvi.output_height = (uint32_t)outputSize.Height;
	vvi.scale_type = obs_scale_type::OBS_SCALE_BICUBIC;
	vvi.colorspace = video_colorspace::VIDEO_CS_709;
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

	struct obs_module_failure_info mfi;
	obs_load_all_modules2(&mfi);
	obs_log_loaded_modules();
	obs_post_load_modules();

	if (!obs_initialized()) {
		throw std::exception("Unknown error initializing");
	}

	cout << "OBS version " + string(obs_get_version_string()) + " loaded successfully." << std::endl;

	// catch ctrl events and shut down obs gracefully
	SetConsoleCtrlHandler(ctrl_handler, TRUE);

	// create scene
	int channel = 0;
	auto scene = obs_scene_create("main");
	auto sceneSource = obs_scene_get_source(scene);
	obs_set_output_source(channel++, sceneSource);

	// audio capture sources
	for (auto& id : speakers)
	{
		auto opt = obs_data_create();
		obs_data_set_string(opt, "device_id", id.second.c_str());
		auto source = obs_source_create("wasapi_output_capture", "", opt, nullptr);
		obs_set_output_source(channel++, source);
		obs_data_release(opt);
	}

	for (auto& id : microphones)
	{
		auto opt = obs_data_create();
		obs_data_set_string(opt, "device_id", id.second.c_str());
		auto source = obs_source_create("wasapi_input_capture", "", opt, nullptr);
		obs_set_output_source(channel++, source);
		obs_data_release(opt);
	}

	// display capture sources
	auto displays = get_screen_info();
	for (int i = 0; i < displays.size(); i++)
	{
		auto& display = displays[i];

		Rect displayBounds{ display.x, display.y, display.width, display.height };

		if (displayBounds.IntersectsWith(captureRegion)) {
			auto opt = obs_data_create();
			obs_data_set_bool(opt, "capture_cursor", true);
			obs_data_set_int(opt, "monitor", i);
			obs_source_t* source = obs_source_create("monitor_capture", "", opt, nullptr);
			obs_data_release(opt);

			obs_sceneitem_t* sceneItem = obs_scene_add(scene, source);
			vec2 pos{ displayBounds.X - captureRegion.X , displayBounds.Y - captureRegion.Y };
			obs_sceneitem_set_pos(sceneItem, &pos);
		}
	}

	// video encoder
	vector<string> encoders{};
	string selectedEncoder;
	for (int i = 0; i < 20; i++)
	{
		const char* name;
		if (!obs_enum_encoder_types(i, &name))
			break;

		auto& sname = encoders.emplace_back(name);
		if (sname == "jim_nvenc")
		{
			selectedEncoder = "jim_nvenc";
		}
		else if (sname == "obs_x264" && selectedEncoder.empty())
		{
			selectedEncoder = "obs_x264";
		}
	}

	std::ostringstream imploded;
	std::copy(encoders.begin(), encoders.end(), std::ostream_iterator<std::string>(imploded, ", "));
	cout << "Available encoders: " << imploded.str() << std::endl;

	if (encoders.empty() || selectedEncoder.empty())
	{
		throw std::exception("No supported video encoders available.");
	}

	// create encoders
	auto videoOptions = obs_data_create();

	//if (selectedEncoder == "jim_nvenc")
	//{
	//	//bool lookahead = obs_data_get_bool(settings, "lookahead");
	//	obs_data_set_string(videoOptions, "rate_control", "CQP");
	//	obs_data_set_string(videoOptions, "profile", "high");
	//	obs_data_set_int(videoOptions, "cqp", cq);
	//	obs_data_set_int(videoOptions, "bitrate", 0);
	//	// below are perf related settings
	//	obs_data_set_string(videoOptions, "preset", "llhq");
	//	obs_data_set_bool(videoOptions, "lookahead", false);
	//}
	//else if (selectedEncoder == "obs_x264")
	//{
	//}

	auto encVideo = obs_video_encoder_create(selectedEncoder.c_str(), "video_encoder", videoOptions, nullptr);
	auto encAudio = obs_audio_encoder_create("ffmpeg_aac", "audio_encoder", nullptr, 0, nullptr);

	// create output muxer
	auto muxerOptions = obs_data_create();
	obs_data_set_string(muxerOptions, "path", outputFile.c_str());
	auto muxer = obs_output_create("ffmpeg_muxer", "main_output", muxerOptions, nullptr);

	obs_encoder_set_video(encVideo, obs_get_video());
	obs_encoder_set_audio(encAudio, obs_get_audio());
	obs_output_set_video_encoder(muxer, encVideo);
	obs_output_set_audio_encoder(muxer, encAudio, 0);

	if (pause) {
		cout << "Press any key to start recording" << std::endl;
		getchar();
	}

	obs_output_start(muxer);
	cout << "Started recording" << std::endl;

	json status;
	while (!cancelRequested) {
		Sleep(1000);

		double percent = 0;
		int totalDropped = obs_output_get_frames_dropped(muxer);
		int totalFrames = obs_output_get_total_frames(muxer);
		if (totalFrames == 0) { percent = 0.0; }
		else { percent = (double)totalDropped / (double)totalFrames * 100.0; }

		auto frameTime = (double)obs_get_average_frame_time_ns() / 1000000.0;

		status["droppedFrames"] = totalDropped;
		status["droppedFramesPerc"] = percent;
		status["fps"] = obs_get_active_fps();
		status["avgTimeToRenderFrame"] = frameTime;

		cout << status << std::endl;
	}

	cout << "Cancel requested. Starting Shutdown" << std::endl;

	obs_output_stop(muxer);
	obs_shutdown();

	cout << "Exiting" << std::endl;

}

int main(int argc, char* argv[])
{
	try
	{
		// per monitor dpi aware so winapi does not lie to us
		SetProcessDpiAwareness(PROCESS_DPI_AWARENESS::PROCESS_PER_MONITOR_DPI_AWARE);
		run(argc, argv);
		return 0;
	}
	catch (const std::exception& exc)
	{
		std::cerr << std::endl << exc.what();
		std::cerr << std::endl << "A fatal error has occurred. The application will exit." << std::endl;
		return -1;
	}
	catch (...)
	{
		std::cerr << std::endl << "An unknown error has occurred. The application will exit." << std::endl;
		return -1;
	}
}