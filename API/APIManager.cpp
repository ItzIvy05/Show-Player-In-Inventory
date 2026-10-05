#include "APIManager.h"

void APIs::RegisterCallbacks()
{
    const bool registered = SmoothCamAPI::RegisterInterfaceLoaderCallback(SKSE::GetMessagingInterface(), [](void* interfaceInstance, SmoothCamAPI::InterfaceVersion interfaceVersion) {
        if (interfaceVersion == SmoothCamAPI::InterfaceVersion::V2 || interfaceVersion == SmoothCamAPI::InterfaceVersion::V3) {
            SmoothCam = reinterpret_cast<SmoothCamAPI::IVSmoothCam2*>(interfaceInstance);
            logger::info("[SmoothCam] Obtained SmoothCam API.");
        } else {
            logger::warn("[SmoothCam] Unsupported SmoothCam API version returned.");
        }
    });

    if (!registered) {
        logger::warn("[SmoothCam] Callback registration failed.");
    }
}

void APIs::RequestAPIs()
{
    if (!SmoothCamAPI::RequestInterface(SKSE::GetMessagingInterface(), SmoothCamAPI::InterfaceVersion::V2)) {
        logger::debug("[SmoothCam] Interface request dispatch failed. SmoothCam may not be installed.");
    }
}
