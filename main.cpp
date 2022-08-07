#include <string>
#include <iostream>
#include <vector>
#include <sstream>
#include <iterator>
#include <unordered_set>

#include "windows.h"
#include "gdiplus.h"
#include "shellscalingapi.h"

#include "argh.h"
#include "getscreens.h"
#include "json.hpp"
#include "obs-studio/libobs/obs.h"
#include "obs-studio/libobs/util/platform.h"

#pragma comment(lib, "user32.lib") 
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "obs.lib")

using namespace std;
using namespace Gdiplus;
using json = nlohmann::json;

bool cancelRequested = false;

BOOL WINAPI ctrl_handler(DWORD fdwCtrlType)
{
	cancelRequested = true;
	return TRUE; // indicate we have handled the signal and no further processing should happen
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
		string message = "Not a valid rectangle: " + input;
		throw std::exception(message.c_str());
	}

	Rect r{};
	r.X = stoi(parts[0]);
	r.Y = stoi(parts[1]);
	r.Width = stoi(parts[2]);
	r.Height = stoi(parts[3]);

	return r;
}

Color parse_color(const string& input) {
	auto parts = split_comma(input);
	if (parts.size() != 3) {
		string message = "Not a valid color: " + input;
		throw std::exception(message.c_str());
	}

	Color r{ (BYTE)stoi(parts[0]), (BYTE)stoi(parts[1]), (BYTE)stoi(parts[2]) };
	return r;
}

static bool icq_available(obs_encoder_t* encoder)
{
	obs_properties_t* props = obs_encoder_properties(encoder);
	obs_property_t* p = obs_properties_get(props, "rate_control");
	bool              icq_found = false;

	size_t num = obs_property_list_item_count(p);
	for (size_t i = 0; i < num; i++) {
		const char* val = obs_property_list_item_string(p, i);
		if (strcmp(val, "ICQ") == 0) {
			icq_found = true;
			break;
		}
	}

	obs_properties_destroy(props);
	return icq_found;
}

void UpdateRecordingSettings_qsv11(obs_encoder_t* videoRecordingEncoder, int crf)
{
	bool icq = icq_available(videoRecordingEncoder);

	obs_data_t* settings = obs_data_create();
	obs_data_set_string(settings, "profile", "high");

	if (icq) {
		obs_data_set_string(settings, "rate_control", "ICQ");
		obs_data_set_int(settings, "icq_quality", crf);
	}
	else {
		obs_data_set_string(settings, "rate_control", "CQP");
		obs_data_set_int(settings, "qpi", crf);
		obs_data_set_int(settings, "qpp", crf);
		obs_data_set_int(settings, "qpb", crf);
	}

	obs_encoder_update(videoRecordingEncoder, settings);
	obs_data_release(settings);
}

void UpdateRecordingSettings_nvenc(obs_encoder_t* videoRecordingEncoder, int cqp)
{
	obs_data_t* settings = obs_data_create();
	obs_data_set_string(settings, "rate_control", "CQP");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", "hq");
	obs_data_set_int(settings, "cqp", cqp);
	obs_data_set_int(settings, "bitrate", 0);
	obs_encoder_update(videoRecordingEncoder, settings);
	obs_data_release(settings);
}

void UpdateRecordingSettings_amd_cqp(obs_encoder_t* videoRecordingEncoder, int cqp)
{
	obs_data_t* settings = obs_data_create();

	// Static Properties
	obs_data_set_int(settings, "Usage", 0);
	obs_data_set_int(settings, "Profile", 100); // High

	// Rate Control Properties
	obs_data_set_int(settings, "RateControlMethod", 0);
	obs_data_set_int(settings, "QP.IFrame", cqp);
	obs_data_set_int(settings, "QP.PFrame", cqp);
	obs_data_set_int(settings, "QP.BFrame", cqp);
	obs_data_set_int(settings, "VBVBuffer", 1);
	obs_data_set_int(settings, "VBVBuffer.Size", 100000);

	// Picture Control Properties
	obs_data_set_double(settings, "KeyframeInterval", 2.0);
	obs_data_set_int(settings, "BFrame.Pattern", 0);

	// Update and release
	obs_encoder_update(videoRecordingEncoder, settings);
	obs_data_release(settings);
}

void UpdateRecordingSettings_x264_crf(obs_encoder_t* videoRecordingEncoder, int crf, bool lowCPUx264)
{
	obs_data_t* settings = obs_data_create();
	obs_data_set_int(settings, "crf", crf);
	obs_data_set_bool(settings, "use_bufsize", true);
	obs_data_set_string(settings, "rate_control", "CRF");
	obs_data_set_string(settings, "profile", "high");
	obs_data_set_string(settings, "preset", lowCPUx264 ? "ultrafast" : "veryfast");
	obs_encoder_update(videoRecordingEncoder, settings);
	obs_data_release(settings);
}

#define CROSS_DIST_CUTOFF 2000.0
int CalcCRF(int outputX, int outputY, int crf, bool lowCPUx264)
{
	double fCX = double(outputX);
	double fCY = double(outputY);

	if (lowCPUx264)
		crf -= 2;

	double crossDist = sqrt(fCX * fCX + fCY * fCY);
	double crfResReduction = fmin(CROSS_DIST_CUTOFF, crossDist) / CROSS_DIST_CUTOFF;
	crfResReduction = (1.0 - crfResReduction) * 10.0;

	return crf - int(crfResReduction);
}

Rect captureRegion;
obs_source_t* mouseFilter = 0;
obs_sceneitem_t* mouseSceneItem = 0;
void update_tracker(uint32_t x, uint32_t y, float opacity, float scale) {

	if (mouseFilter == nullptr || mouseSceneItem == nullptr) {
		return;
	}

	vec2 pos{ x - captureRegion.X , y - captureRegion.Y };
	obs_sceneitem_set_pos(mouseSceneItem, &pos);
	vec2 vscale{ scale, scale };
	obs_sceneitem_set_scale(mouseSceneItem, &vscale);

	auto opt_opacity = obs_data_create();
	obs_data_set_int(opt_opacity, "opacity", (int32_t)opacity);
	obs_source_update(mouseFilter, opt_opacity);
	obs_data_release(opt_opacity);
};

uint64_t lastMouseClick;
mouse_info lastMouseClickPosition;
bool mouseVisible;
#define ANIMATION_DURATION ((double)400) // ms * ns
void frame_tick(void* priv, float seconds)
{
	auto time = os_gettime_ns() / 1000000;
	auto mouseData = get_mouse_info();
	if (mouseData.pressed) {
		lastMouseClickPosition = mouseData;
		lastMouseClick = time;
	}

	auto lastClickAgo = time - lastMouseClick;
	if (lastClickAgo < ANIMATION_DURATION) {
		mouseVisible = true;
		// max 85% opacity
		float opacity = (1 - (lastClickAgo / ANIMATION_DURATION)) * 85;
		float mouseZoom = lastMouseClickPosition.dpi / 96;

		// radius: min 15, will grow +35 to max of 50 (*dpi)
		float radius = (10 + ((lastClickAgo / ANIMATION_DURATION) * 30)) * mouseZoom;

		// scale: intendedRenderedSize/actualImageSize - the tracker.png is 100x100
		float scale = radius / 50;

		auto x = lastMouseClickPosition.x - radius;
		auto y = lastMouseClickPosition.y - radius;

		update_tracker(x, y, opacity, scale);
		//cout << "mouse visible: " << opacity << std::endl;
	}
	else if (mouseVisible) {
		mouseVisible = false;
		update_tracker(0, 0, 0, 1);
		//cout << "mouse removed..." << std::endl;
	}
}

void run(vector<string> arguments)
{
	// handle command line arguments
	argh::parser cmdl;
	cmdl.add_params({ "adapter", "captureRegion", "speakers", "microphones", "fps", "crf", "maxOutputWidth", "maxOutputHeight", "output", "trackerColor"});
	cmdl.parse(arguments);

	bool help = cmdl[{ "h", "help" }];
	if (help) {

		cout << "OBS Express, a light weight command line screen recorder." << std::endl;
		cout << "  --help             Show this help" << std::endl;
		cout << "  --adapter          The numerical index of the graphics device to use" << std::endl;
		cout << "  --captureRegion    The region of the desktop to record (eg. 'x,y,w,h') " << std::endl;
		cout << "  --speakers         Output device ID to record (can be multiple)" << std::endl;
		cout << "  --microphones      Input device ID to record (can be multiple)" << std::endl;
		cout << "  --fps              The target video framerate" << std::endl;
		cout << "  --crf              The contant rate factor (0-51, lower is better) " << std::endl;
		cout << "  --maxOutputWidth   Downscale to a maximum output width" << std::endl;
		cout << "  --maxOutputHeight  Downscale to a maximum output height" << std::endl;
		cout << "  --trackerEnabled   If the mouse click tracker should be rendered" << std::endl;
		cout << "  --trackerColor     The color of the tracker (eg. 'r,g,b')" << std::endl;
		cout << "  --lowCpuMode       Maximize performance if using CPU encoding" << std::endl;
		cout << "  --hwAccel          Use hardware encoding if available" << std::endl;
		cout << "  --pause            Wait for key-press before recording" << std::endl;
		cout << "  --output           The file name of the generated recording" << std::endl;
		return;
	}

	bool pause = cmdl["pause"];
	bool trackerEnabled = cmdl["trackerEnabled"];
	bool lowCpuMode = cmdl["lowCpuMode"];
	bool hwAccel = cmdl["hwAccel"];

	uint16_t adapter, fps, crf, maxOutputWidth, maxOutputHeight;
	cmdl("adapter", 0) >> adapter;
	cmdl("fps", 30) >> fps;
	cmdl("crf", 24) >> crf;
	cmdl("maxOutputWidth", 0) >> maxOutputWidth;
	cmdl("maxOutputHeight", 0) >> maxOutputHeight;

	auto speakers = cmdl.params("speakers");
	auto microphones = cmdl.params("microphones");

	string tmpCaptureRegion, tmpTrackerColor, outputFile;
	cmdl("captureRegion") >> tmpCaptureRegion;
	cmdl("trackerColor", "255,0,0") >> tmpTrackerColor;
	cmdl("output") >> outputFile;

	if (tmpCaptureRegion.empty() || outputFile.empty())
		throw std::exception("Required parameters: captureRegion, output");

	captureRegion = parse_rect(tmpCaptureRegion);
	Color trackerColor = parse_color(tmpTrackerColor);

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
	unordered_set<string> encoders{};
	for (int i = 0; i < 20; i++)
	{
		const char* name;
		if (!obs_enum_encoder_types(i, &name))
			break;
		encoders.insert(name);
	}

	std::ostringstream imploded;
	std::copy(encoders.begin(), encoders.end(), std::ostream_iterator<std::string>(imploded, ", "));
	cout << "Available OBS encoders: " << imploded.str() << std::endl;

	obs_encoder_t* encVideo = nullptr;
	if (hwAccel) 
	{
		auto adjustedCrf = CalcCRF(outputSize.Width, outputSize.Height, crf, false);
		cout << "Actual CRF: " << adjustedCrf << std::endl;

		if (encoders.find("jim_nvenc") != encoders.end()) 
		{
			encVideo = obs_video_encoder_create("jim_nvenc", "enc_jim_nvenc", nullptr, nullptr);
			UpdateRecordingSettings_nvenc(encVideo, adjustedCrf);
		} 
		else if (encoders.find("amd_amf_h264") != encoders.end()) 
		{
			encVideo = obs_video_encoder_create("amd_amf_h264", "enc_amd_amf_h264", nullptr, nullptr);
			UpdateRecordingSettings_amd_cqp(encVideo, adjustedCrf);
		} 
		else if (encoders.find("obs_qsv11") != encoders.end()) 
		{
			encVideo = obs_video_encoder_create("obs_qsv11", "enc_obs_qsv11", nullptr, nullptr);
			UpdateRecordingSettings_qsv11(encVideo, adjustedCrf);
		}
	}

	if (encVideo == nullptr) 
	{
		auto adjustedCrfForCpu = CalcCRF(outputSize.Width, outputSize.Height, crf, lowCpuMode);
		cout << "Actual CRF: " << adjustedCrfForCpu << std::endl;

		encVideo = obs_video_encoder_create("obs_x264", "enc_obs_x264", nullptr, nullptr);
		UpdateRecordingSettings_x264_crf(encVideo, adjustedCrfForCpu, lowCpuMode);
	}

	auto encAudio = obs_audio_encoder_create("ffmpeg_aac", "audio_encoder", nullptr, 0, nullptr);

	// create output muxer
	auto muxerOptions = obs_data_create();
	obs_data_set_string(muxerOptions, "path", outputFile.c_str());
	auto muxer = obs_output_create("ffmpeg_muxer", "main_output_muxer", muxerOptions, nullptr);

	obs_encoder_set_video(encVideo, obs_get_video());
	obs_encoder_set_audio(encAudio, obs_get_audio());
	obs_output_set_video_encoder(muxer, encVideo);
	obs_output_set_audio_encoder(muxer, encAudio, 0);

	if (pause) {
		cout << "Press any key to start recording" << std::endl;
		getchar();
	}

	if (trackerEnabled) // tracker
	{
		auto opt = obs_data_create();
		obs_data_set_bool(opt, "unload", true);
		obs_data_set_string(opt, "file", ".\\tracker.png");
		obs_source_t* source = obs_source_create("image_source", "mouse_highlight", opt, nullptr);
		obs_data_release(opt);

		auto fopt = obs_data_create();
		obs_data_set_int(fopt, "opacity", 0);
		obs_data_set_int(fopt, "color", RGB(trackerColor.GetR(), trackerColor.GetG(), trackerColor.GetB()));
		obs_source_t* filter = obs_source_create("color_filter", "mouse_color_correction", fopt, nullptr);
		obs_data_release(fopt);

		obs_source_filter_add(source, filter);
		obs_sceneitem_t* sceneItem = obs_scene_add(scene, source);
		obs_sceneitem_addref(sceneItem);

		mouseFilter = filter;
		mouseSceneItem = sceneItem;

		obs_add_tick_callback(frame_tick, NULL);
	}

	obs_output_start(muxer);
	cout << "Started recording" << std::endl;

	while (!cancelRequested) {
		Sleep(1000);

		double percent = 0;
		int totalDropped = obs_output_get_frames_dropped(muxer);
		int totalFrames = obs_output_get_total_frames(muxer);
		if (totalFrames == 0) { percent = 0.0; }
		else { percent = (double)totalDropped / (double)totalFrames * 100.0; }

		auto frameTime = (double)obs_get_average_frame_time_ns() / 1000000.0;

		json status;
		status["droppedFrames"] = totalDropped;
		status["droppedFramesPerc"] = percent;
		status["fps"] = obs_get_active_fps();
		status["avgTimeToRenderFrame"] = frameTime;
		cout << status << std::endl;
	}

	if (trackerEnabled) obs_remove_tick_callback(frame_tick, NULL);

	cout << "Cancel requested. Starting Shutdown" << std::endl;

	obs_output_stop(muxer);
	Sleep(1000);
	obs_shutdown();

	cout << "Exiting" << std::endl;
}

std::string utf8_encode(const std::wstring& wstr)
{
	if (wstr.empty()) return std::string();
	int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	std::string strTo(size_needed, 0);
	WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
	return strTo;
}

int wmain(int argc, wchar_t* argv[], wchar_t* envp[])
{
	try
	{
		// per monitor dpi aware so winapi does not lie to us
		SetProcessDpiAwareness(PROCESS_DPI_AWARENESS::PROCESS_PER_MONITOR_DPI_AWARE);

		// convert wchar arguments to utf8
		vector<string> utf8argv{};
		for (int i = 0; i < argc; i++) {
			utf8argv.emplace_back(utf8_encode(argv[i]));
		}

		run(utf8argv);
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