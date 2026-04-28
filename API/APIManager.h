#pragma once

#define SMOOTHCAM_API_COMMONLIB
#include "SmoothCamAPI.h"

struct APIs
{
    static inline SmoothCamAPI::IVSmoothCam2* SmoothCam{ nullptr };
    static void RegisterCallbacks();
    static void RequestAPIs();
};

extern SmoothCamAPI::IVSmoothCam2* g_SmoothCam;
