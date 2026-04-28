#include "APIManager.h"

SmoothCamAPI::IVSmoothCam2* g_SmoothCam = nullptr;

namespace
{
    bool callbackRegistered = false;
}

void APIs::RegisterCallbacks()
{
    if (callbackRegistered) {
        return;
    }

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::warn("[SmoothCam] SKSE messaging interface was unavailable.");
        return;
    }

    callbackRegistered = SmoothCamAPI::RegisterInterfaceLoaderCallback(
        messaging,
        [](void* interfaceInstance, SmoothCamAPI::InterfaceVersion interfaceVersion) {
            if (interfaceVersion == SmoothCamAPI::InterfaceVersion::V2 ||
                interfaceVersion == SmoothCamAPI::InterfaceVersion::V3) {
                SmoothCam = reinterpret_cast<SmoothCamAPI::IVSmoothCam2*>(interfaceInstance);
                g_SmoothCam = SmoothCam;
                logger::info("[SmoothCam] Obtained SmoothCam API.");
            } else {
                logger::warn("[SmoothCam] Unsupported SmoothCam API version returned.");
            }
        });

    if (!callbackRegistered) {
        logger::warn("[SmoothCam] Callback registration failed.");
    }
}

void APIs::RequestAPIs()
{
    RegisterCallbacks();

    if (SmoothCam) {
        g_SmoothCam = SmoothCam;
        return;
    }

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !callbackRegistered) {
        return;
    }

    if (!SmoothCamAPI::RequestInterface(messaging, SmoothCamAPI::InterfaceVersion::V2)) {
        logger::debug("[SmoothCam] Interface request dispatch failed. SmoothCam may not be installed or loaded yet.");
    }
}
