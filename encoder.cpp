#include "encoder.h"

#include <vector>
#include <unordered_set>
#include <string>
#include <sstream>
#include <iostream>

using namespace std;

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

obs_encoder_t* create_and_configure_video_encoder(bool hwAccel, bool lowCpuMode, uint16_t crf, Gdiplus::SizeF& outputSize)
{
    unordered_set<string> encoders{};
    for (int i = 0; i < 100; i++) {
        const char* name;
        if (!obs_enum_encoder_types(i, &name))
            break;
        encoders.insert(name);
    }
    
    std::ostringstream imploded;
    std::copy(encoders.begin(), encoders.end(), std::ostream_iterator<std::string>(imploded, ", "));
    std::cout << "Available OBS encoders: " << imploded.str() << std::endl;

    obs_encoder_t* encVideo = nullptr;
    if (hwAccel) {
        auto adjustedCrf = CalcCRF(outputSize.Width, outputSize.Height, crf, false);
        cout << "Actual CRF: " << adjustedCrf << std::endl;

        if (encoders.find("jim_nvenc") != encoders.end()) {
            encVideo = obs_video_encoder_create("jim_nvenc", "enc_jim_nvenc", nullptr, nullptr);
            UpdateRecordingSettings_nvenc(encVideo, adjustedCrf);
        }
        else if (encoders.find("amd_amf_h264") != encoders.end()) {
            encVideo = obs_video_encoder_create("amd_amf_h264", "enc_amd_amf_h264", nullptr, nullptr);
            UpdateRecordingSettings_amd_cqp(encVideo, adjustedCrf);
        }
        else if (encoders.find("obs_qsv11") != encoders.end()) {
            encVideo = obs_video_encoder_create("obs_qsv11", "enc_obs_qsv11", nullptr, nullptr);
            UpdateRecordingSettings_qsv11(encVideo, adjustedCrf);
        }
    }

    if (encVideo == nullptr) {
        auto adjustedCrfForCpu = CalcCRF(outputSize.Width, outputSize.Height, crf, lowCpuMode);
        cout << "Actual CRF: " << adjustedCrfForCpu << std::endl;

        encVideo = obs_video_encoder_create("obs_x264", "enc_obs_x264", nullptr, nullptr);
        UpdateRecordingSettings_x264_crf(encVideo, adjustedCrfForCpu, lowCpuMode);
    }

    return encVideo;
}