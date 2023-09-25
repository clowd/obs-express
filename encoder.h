#pragma once
#include "windows.h"
#include "gdiplus.h"
#include "obs-studio/libobs/obs.h"

obs_encoder_t* create_and_configure_video_encoder(bool hwAccel, bool lowCpuMode, uint16_t crf, Gdiplus::SizeF& outputSize);