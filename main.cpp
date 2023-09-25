#include <string>
#include <iostream>
#include <vector>
#include <iterator>
#include <unordered_set>
#include <regex>

#include "windows.h"
#include "gdiplus.h"
#include "shellscalingapi.h"
#include "process.h"

#include "version.h"
#include "argh.h"
#include "getscreens.h"
#include "util.h"
#include "encoder.h"
#include "json.hpp"
#include "obs-studio/libobs/obs.h"

#pragma comment(lib, "user32.lib") 
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "obs.lib")

using namespace std;
using namespace Gdiplus;
using json = nlohmann::json;

HANDLE startHandle;
HANDLE cancelHandle;
bool cancelRequested = false;
obs_output_t* muxer;

BOOL WINAPI ctrl_handler(DWORD fdwCtrlType)
{
    cout << "Received exit signal." << std::endl;
    cancelRequested = true;
    SetEvent(cancelHandle);
    SetEvent(startHandle);
    return TRUE; // indicate we have handled the signal and no further processing should happen
}

Rect captureRegion;
obs_source_t* mouseFilter = 0;
obs_sceneitem_t* mouseSceneItem = 0;
void update_tracker(uint32_t x, uint32_t y, float opacity, float scale)
{
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
    auto time = util_obs_get_time_ms();
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

void handle_signal(void* data, calldata_t* cd)
{
    auto signal_name = (char*)data;
    json rec_start;
    rec_start["signal"] = std::string(signal_name);
    rec_start["type"] = "signal";
    cout << rec_start << std::endl;
}

uint64_t startTimeMs = 0;
void signal_started_recording(void* data, calldata_t* cd)
{
    startTimeMs = util_obs_get_time_ms();
    json rec_start;
    rec_start["type"] = "started_recording";
    cout << rec_start << std::endl;
}

void signal_stopped_recording(void* data, calldata_t* cd)
{
    obs_output_t* output = (obs_output_t*)calldata_ptr(cd, "output");
    int code = calldata_int(cd, "code");
    const char* output_error = obs_output_get_last_error(output);

    json rec_stop;
    rec_stop["type"] = "stopped_recording";
    rec_stop["code"] = code;
    rec_stop["message"] = get_obs_output_errorcode_string(code);
    if (output_error != nullptr) {
        rec_stop["error"] = output_error;
    }

    cout << rec_stop << std::endl;

    cout << "Exiting process" << std::endl;

    // obs_shutdown() actually crashes, probably because we're not cleaning up all the resources beforehand.
    // I don't really care to do this properly as this is a one-off recorder and the OS will clean up
    ExitProcess(code);
}

vector<obs_source_t*> spkDevices{};
vector<obs_source_t*> micDevices{};

unsigned int __stdcall read_input_proc(void* lpParam)
{
    while (!cancelRequested) {
        std::string str;
        std::getline(std::cin, str);

        // tolower the command
        std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });

        auto words = util_string_split(str, ' ');

        if (str == "q" || str == "quit" || str == "exit" || str == "stop") {
            cout << "Quit/stop command received." << std::endl;
            cancelRequested = true;
            SetEvent(cancelHandle);
            SetEvent(startHandle);
        }

        else if (str == "start") {
            cout << "Start command received." << std::endl;
            if (obs_output_paused(muxer)) {
                obs_output_pause(muxer, false);
            }
            else {
                // first start
                SetEvent(startHandle);
            }
        }

        else if (str == "pause") {
            cout << "Pause command received." << std::endl;
            obs_output_pause(muxer, true);
        }

        else if (words.size() == 3 && (words[0] == "mute" || words[0] == "unmute")) {
            bool isMuted = words[0] == "mute";
            if (words[1] == "s") {
                auto idx = stoi(words[2]);
                if (idx < spkDevices.size()) {
                    obs_source_set_muted(spkDevices[idx], isMuted);
                    cout << "Audio speaker device " << idx << ": " << words[0] << "d." << std::endl;
                }
                else {
                    cout << "Audio speaker device " << idx << " out of range." << std::endl;
                }
            }
            else if (words[1] == "m") {
                auto idx = stoi(words[2]);
                if (idx < micDevices.size()) {
                    obs_source_set_muted(micDevices[idx], isMuted);
                    cout << "Audio microphone device " << idx << ": " << words[0] << "d." << std::endl;
                }
                else {
                    cout << "Audio microphone device " << idx << " out of range." << std::endl;
                }
            }
            else {
                cout << "Unknown audio device type: " << words[1] << std::endl;
                continue;
            }
        }

        else if (!str.empty()) {
            cout << "Unknown command or invalid arguments: " << str << std::endl;
        }
    }

    cout << "stdin read thread has exited." << std::endl;
    return 0;
}

unsigned int __stdcall output_status_proc(void* lpParam)
{
    while (!cancelRequested) {
        Sleep(1000);

        if (startTimeMs == 0 || obs_output_paused(muxer)) {
            continue;
        }

        auto currentTimeMs = util_obs_get_time_ms();

        double percent = 0;
        int totalDropped = obs_output_get_frames_dropped(muxer);
        int totalFrames = obs_output_get_total_frames(muxer);
        if (totalFrames == 0) { percent = 0.0; }
        else { percent = (double)totalDropped / (double)totalFrames * 100.0; }

        auto frameTime = (double)obs_get_average_frame_time_ns() / 1000000.0;

        json status;
        status["timeMs"] = currentTimeMs - startTimeMs;
        status["dropped"] = totalDropped;
        status["droppedPerc"] = percent;
        status["fps"] = obs_get_active_fps();
        status["frameTime"] = frameTime;
        status["cpu"] = util_obs_get_cpu_utilisation();
        status["type"] = "status";
        cout << status << std::endl;
    }
    return 0;
}

void preview_callback(void* displayPtr, uint32_t cx, uint32_t cy)
{
    obs_render_main_texture();
}

void run(vector<string> arguments)
{
    // handle command line arguments
    argh::parser cmdl;
    cmdl.add_params({ "adapter", "region", "speaker", "microphone", "fps", "crf", "maxWidth", "maxHeight", "output", "trackerColor", "preview" });
    cmdl.parse(arguments);

    cout << std::endl;
    cout << "obs-express v" << OBS_EXPRESS_VERSION << ", a command line screen recording utility" << std::endl;
    cout << "  bundled with obs-studio v" << obs_get_version_string() << std::endl;
    cout << "  created for Clowd (https://github.com/clowd/Clowd)" << std::endl;
    cout << std::endl;

    bool help = cmdl[{ "h", "help" }];
    if (help) {
        cout << "Global: " << std::endl;
        cout << "  --help                  Show this help text" << std::endl;
        cout << std::endl << "Required: " << std::endl;
        cout << "  --region {x,y,w,h}      The region of the desktop to capture" << std::endl;
        cout << "  --output {filePath}     The file for the generated recording" << std::endl;
        cout << std::endl << "Optional: " << std::endl;
        cout << "  --adapter {int}         The index of the graphics device to use" << std::endl;
        cout << "  --speaker {dev_id}      Output device ID to record (can be multiple)" << std::endl;
        cout << "  --microphone {dev_id}   Input device ID to record (can be multiple)" << std::endl;
        cout << "  --fps {int}             The target video framerate (default: 30)" << std::endl;
        cout << "  --crf {int}             Quality from 0-51, lower is better. (default: 24) " << std::endl;
        cout << "  --maxWidth {int}        Downscale output to a maximum width" << std::endl;
        cout << "  --maxHeight {int}       Downscale output to a maximum height" << std::endl;
        cout << "  --tracker               If the mouse click tracker should be rendered" << std::endl;
        cout << "  --trackerColor {r,g,b}  The color of the tracker (default: 255,0,0)" << std::endl;
        cout << "  --lowCpuMode            Maximize performance if using CPU encoding" << std::endl;
        cout << "  --hwAccel               Use hardware encoding if available" << std::endl;
        cout << "  --noCursor              Do not render mouse cursor in recording" << std::endl;
        cout << "  --pause                 Pause before recording until start command" << std::endl;
        cout << "  --preview {hwnd}        Render a recording preview to window handle" << std::endl;
        return;
    }

    bool pause = cmdl["pause"];
    bool trackerEnabled = cmdl["tracker"];
    bool lowCpuMode = cmdl["lowCpuMode"];
    bool hwAccel = cmdl["hwAccel"];
    bool noCursor = cmdl["noCursor"];

    uint16_t adapter, fps, crf, maxOutputWidth, maxOutputHeight;
    cmdl("adapter", 0) >> adapter;
    cmdl("fps", 30) >> fps;
    cmdl("crf", 24) >> crf;
    cmdl("maxWidth", 0) >> maxOutputWidth;
    cmdl("maxHeight", 0) >> maxOutputHeight;

    uint32_t previewWidth, previewHeight;
    void* previewHwnd = 0;
    std::string previewStr = cmdl("preview").str();
    if (!previewStr.empty()) {
        std::regex re_all_digits("^\\d+$");
        std::regex re_hexidecimal("^0x[0-9a-zA-Z]+$");
        if (std::regex_match(previewStr, re_hexidecimal)) {
            previewHwnd = (void*)std::stoul(previewStr, nullptr, 16);
        }
        else if (std::regex_match(previewStr, re_all_digits)) {
            previewHwnd = (void*)std::stoul(previewStr, nullptr, 10);
        }
        else {
            throw std::invalid_argument("Unknown window handle format, must be decimal or hexidecimal: " + previewStr);
        }

        RECT r;
        if (!GetWindowRect((HWND)previewHwnd, &r))
            throw std::invalid_argument("Unable to retrieve details for window handle '" + previewStr + "'. Is it a real window? Does this process have permission to access it?");

        previewWidth = r.right - r.left;
        previewHeight = r.bottom - r.top;
    }

    auto speakers = cmdl.params("speaker");
    auto microphones = cmdl.params("microphone");

    string tmpCaptureRegion, tmpTrackerColor, outputFile;
    tmpCaptureRegion = cmdl("region").str();
    tmpTrackerColor = cmdl("trackerColor", "255,0,0").str();
    outputFile = cmdl("output").str();

    if (tmpCaptureRegion.empty() || outputFile.empty())
        throw std::invalid_argument("Required parameters: region, output");

    captureRegion = util_parse_rect(tmpCaptureRegion);
    Color trackerColor = util_parse_color(tmpTrackerColor);

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
        cout << "Downscaling from " << captureRegion.Width << "x" << captureRegion.Height << " to " << outputSize.Width << "x" << outputSize.Height << " (-" << dnsclperc << "%)" << std::endl;
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
    if (vr != OBS_VIDEO_SUCCESS) {
        cout << "ERROR: Unable to initialize d3d11, error code: " << to_string(vr) << std::endl;
        // if we fail to initialise d3d11, let's try again with opengl
        vvi.graphics_module = "libobs-opengl";
        vr = obs_reset_video(&vvi);
        if (vr != OBS_VIDEO_SUCCESS) {
            cout << "ERROR: Unable to initialize open-gl, error code: " << to_string(vr) << std::endl;
            throw std::exception("Could not initialize video pipeline");
        }
    }

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

    // create scene
    int channel = 0;
    auto scene = obs_scene_create("main");
    auto sceneSource = obs_scene_get_source(scene);
    obs_set_output_source(channel++, sceneSource);

    // audio capture sources
    for (auto& id : speakers) {
        auto opt = obs_data_create();
        obs_data_set_string(opt, "device_id", id.second.c_str());
        auto source = obs_source_create("wasapi_output_capture", "", opt, nullptr);
        obs_set_output_source(channel++, source);
        obs_data_release(opt);
        spkDevices.push_back(source);
    }

    for (auto& id : microphones) {
        auto opt = obs_data_create();
        obs_data_set_string(opt, "device_id", id.second.c_str());
        auto source = obs_source_create("wasapi_input_capture", "", opt, nullptr);
        obs_set_output_source(channel++, source);
        obs_data_release(opt);
        micDevices.push_back(source);
    }

    // display capture sources
    auto displays = get_screen_info();
    for (int i = 0; i < displays.size(); i++) {
        auto& display = displays[i];

        Rect displayBounds{ display.x, display.y, display.width, display.height };

        if (displayBounds.IntersectsWith(captureRegion)) {
            auto opt = obs_data_create();
            obs_data_set_bool(opt, "capture_cursor", !noCursor);
            obs_data_set_int(opt, "monitor", i);
            // https://github.com/obsproject/obs-studio/pull/7049 switches the property from 'monitor' to 'monitor_id'
            obs_data_set_string(opt, "monitor_id", display.monitor_id);
            obs_source_t* source = obs_source_create("monitor_capture", "", opt, nullptr);
            obs_data_release(opt);

            obs_sceneitem_t* sceneItem = obs_scene_add(scene, source);
            vec2 pos{ displayBounds.X - captureRegion.X , displayBounds.Y - captureRegion.Y };
            obs_sceneitem_set_pos(sceneItem, &pos);
        }
    }

    // encoders & output muxer
    auto encVideo = create_and_configure_video_encoder(hwAccel, lowCpuMode, crf, outputSize);
    auto encAudio = obs_audio_encoder_create("ffmpeg_aac", "audio_encoder", nullptr, 0, nullptr);
    auto muxerOptions = obs_data_create();
    obs_data_set_string(muxerOptions, "path", outputFile.c_str());
    muxer = obs_output_create("ffmpeg_muxer", "main_output_muxer", muxerOptions, nullptr);

    obs_encoder_set_video(encVideo, obs_get_video());
    obs_encoder_set_audio(encAudio, obs_get_audio());
    obs_output_set_video_encoder(muxer, encVideo);
    obs_output_set_audio_encoder(muxer, encAudio, 0);

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

    // obs signals
    signal_handler_t* signals = obs_output_get_signal_handler(muxer);
    signal_handler_connect(signals, "start", signal_started_recording, nullptr);
    signal_handler_connect(signals, "stop", signal_stopped_recording, nullptr);
    signal_handler_connect(signals, "stop", handle_signal, (void*)"stop");
    signal_handler_connect(signals, "start", handle_signal, (void*)"start");
    signal_handler_connect(signals, "pause", handle_signal, (void*)"pause");
    signal_handler_connect(signals, "unpause", handle_signal, (void*)"unpause");
    signal_handler_connect(signals, "starting", handle_signal, (void*)"starting");
    signal_handler_connect(signals, "stopping", handle_signal, (void*)"stopping");

    startHandle = CreateEvent(NULL, TRUE, FALSE, NULL);
    cancelHandle = CreateEvent(NULL, TRUE, FALSE, NULL);

    // catch ctrl events and shut down obs gracefully
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    // read std-input for commands
    (HANDLE)_beginthreadex(NULL, 0, read_input_proc, nullptr, 0, nullptr);

    // start preview rendering
    if (previewHwnd) {
        gs_init_data display_init{};
        display_init.adapter = adapter;
        display_init.cx = captureRegion.Width;
        display_init.cy = captureRegion.Height;
        display_init.format = GS_BGRA;
        display_init.zsformat = GS_ZS_NONE;
        display_init.num_backbuffers = 1;
        display_init.window.hwnd = previewHwnd;
        auto hdisplay = obs_display_create(&display_init, 0x0);
        obs_display_add_draw_callback(hdisplay, preview_callback, 0);
    }

    json rec_init;
    rec_init["type"] = "initialized";
    cout << rec_init << std::endl;

    if (pause) {
        cout << ">>>> Type 'start' + Enter to start recording." << std::endl;
        WaitForSingleObject(startHandle, INFINITE);
    }

    if (cancelRequested) {
        cout << "Cancel requested. No output yet. Exiting process." << std::endl;
        ExitProcess(0);
    }

    cout << "Requesting output start" << std::endl;

    if (!obs_output_start(muxer))
        throw std::runtime_error(obs_output_get_last_error(muxer));

    // begin writing status to std out
    (HANDLE)_beginthreadex(NULL, 0, output_status_proc, nullptr, 0, nullptr);

    WaitForSingleObject(cancelHandle, INFINITE);

    //if (trackerEnabled) obs_remove_tick_callback(frame_tick, NULL);

    cout << "Cancel requested. Starting Shutdown" << std::endl;

    obs_output_stop(muxer);

    // cleanup is handled by signal_stopped_recording in a different thread
    for (int i = 0; i < 30; i++) {
        Sleep(1000);
    }
    ExitProcess(-1);
}

int wmain(int argc, wchar_t* argv[], wchar_t* envp[])
{
    try {
        // per monitor dpi aware so winapi does not lie to us
        SetProcessDpiAwareness(PROCESS_DPI_AWARENESS::PROCESS_PER_MONITOR_DPI_AWARE);

        // convert wchar arguments to utf8
        vector<string> utf8argv{};
        for (int i = 0; i < argc; i++) {
            utf8argv.emplace_back(util_string_utf8_encode(argv[i]));
        }

        run(utf8argv);
        return 0;
    }
    catch (const std::exception& exc) {
        std::cerr << std::endl << exc.what();
        std::cerr << std::endl << "A fatal error has occurred. The application will exit." << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << std::endl << "An unknown error has occurred. The application will exit." << std::endl;
        return -1;
    }
}