#include "MenuCamera.h"

#include "APIManager.h"
#include "EventProcessor.h"
#include "Settings.h"

namespace
{
    constexpr float PI = 3.14159265358979323846f;
}

MenuCamera& MenuCamera::GetSingleton()
{
    static MenuCamera instance;
    return instance;
}

bool MenuCamera::CaptureINISettings()
{
    auto* ini = RE::INISettingCollection::GetSingleton();
    if (!ini) {
        return false;
    }

    overShoulderCombatPosX = ini->GetSetting("fOverShoulderCombatPosX:Camera");
    overShoulderCombatAddY = ini->GetSetting("fOverShoulderCombatAddY:Camera");
    overShoulderCombatPosZ = ini->GetSetting("fOverShoulderCombatPosZ:Camera");
    autoVanityModeDelay = ini->GetSetting("fAutoVanityModeDelay:Camera");
    overShoulderPosX = ini->GetSetting("fOverShoulderPosX:Camera");
    overShoulderPosZ = ini->GetSetting("fOverShoulderPosZ:Camera");
    vanityModeMinDist = ini->GetSetting("fVanityModeMinDist:Camera");
    vanityModeMaxDist = ini->GetSetting("fVanityModeMaxDist:Camera");
    mouseWheelZoomSpeed = ini->GetSetting("fMouseWheelZoomSpeed:Camera");
    togglePOVDelay = ini->GetSetting("fTogglePOVDelay:Controls");

    return overShoulderCombatPosX && overShoulderCombatAddY && overShoulderCombatPosZ && autoVanityModeDelay &&
           overShoulderPosX && overShoulderPosZ && vanityModeMinDist && vanityModeMaxDist && mouseWheelZoomSpeed &&
           togglePOVDelay;
}

bool MenuCamera::Start()
{
    if (active) {
        ApplySettings();
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    if (!player || !camera) {
        logger::warn("[MenuCamera] Could not start. Missing player or camera.");
        return false;
    }

    if (player->IsInCombat() || player->IsOnMount()) {
        logger::info("[MenuCamera] Skipped start. Player is in combat or on horseback.");
        return false;
    }

    if (!CaptureINISettings()) {
        logger::warn("[MenuCamera] Could not start. Missing INI camera settings.");
        return false;
    }

    auto* thirdState = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraState::kThirdPerson].get());
    if (!thirdState) {
        logger::warn("[MenuCamera] Could not start. Missing third person camera state.");
        return false;
    }

    APIs::RequestAPIs();

    if (g_SmoothCam && g_SmoothCam->IsCameraEnabled()) {
        const auto result = g_SmoothCam->RequestCameraControl(g_pluginHandle);

        if (result == SmoothCamAPI::APIResult::OK || result == SmoothCamAPI::APIResult::AlreadyGiven) {
            g_SmoothCam->RequestInterpolatorUpdates(g_pluginHandle, true);
            smoothCamControl = true;
            logger::info("[MenuCamera] SmoothCam camera control acquired.");
        } else {
            logger::warn("[MenuCamera] SmoothCam camera control request failed: {}", static_cast<std::uint8_t>(result));
        }
    } else {
        logger::debug("[MenuCamera] SmoothCam API not available or SmoothCam disabled.");
    }

    if (!CaptureState(player, camera, thirdState)) {
        ResetSavedState();
        return false;
    }

    active = true;
    ApplyCameraValues(player, camera, thirdState);
    logger::info("[MenuCamera] Started. offsetX={} offsetY={} offsetZ={} fov={}", Settings::offsetX, Settings::offsetY, Settings::offsetZ, Settings::fov);

    return true;
}

void MenuCamera::Stop()
{
    if (!active) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    if (!player || !camera) {
        ResetSavedState();
        return;
    }

    auto* thirdState = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraState::kThirdPerson].get());

    if (smoothCamControl && g_SmoothCam && g_SmoothCam->IsCameraEnabled()) {
        g_SmoothCam->ReleaseCameraControl(g_pluginHandle);
        logger::info("[MenuCamera] SmoothCam camera control released.");
    }

    if (wasFirstPerson) {
        camera->ForceFirstPerson();
    } else if (previousState) {
        camera->SetState(previousState);
    }

    player->data.angle.x = playerAngleX;
    player->data.angle.z = playerAngleZ;
    player->SetGraphVariableBool("IsNPC", headtrackingEnabled);
    player->SetGraphVariableBool("bHeadTrackSpine", headTrackSpineEnabled);
    player->SetGraphVariableBool("bUseEyeTracking", eyeTrackingEnabled);

    if (autoVanityModeDelay) {
        autoVanityModeDelay->data.f = savedAutoVanityModeDelay;
    }

    if (togglePOVDelay) {
        togglePOVDelay->data.f = savedTogglePOVDelay;
    }

    if (thirdState) {
        thirdState->toggleAnimCam = toggleAnimCam;
        thirdState->freeRotationEnabled = freeRotationEnabled;
        thirdState->targetZoomOffset = targetZoomOffset;
        thirdState->pitchZoomOffset = pitchZoomOffset;
        thirdState->freeRotation = freeRotation;
        thirdState->posOffsetExpected = thirdState->posOffsetActual = posOffsetExpected;
    }

    if (vanityModeMinDist) {
        vanityModeMinDist->data.f = savedVanityModeMinDist;
    }

    if (vanityModeMaxDist) {
        vanityModeMaxDist->data.f = savedVanityModeMaxDist;
    }

    if (overShoulderCombatPosX) {
        overShoulderCombatPosX->data.f = savedOverShoulderCombatPosX;
    }

    if (overShoulderCombatAddY) {
        overShoulderCombatAddY->data.f = savedOverShoulderCombatAddY;
    }

    if (overShoulderCombatPosZ) {
        overShoulderCombatPosZ->data.f = savedOverShoulderCombatPosZ;
    }

    if (overShoulderPosX) {
        overShoulderPosX->data.f = savedOverShoulderPosX;
    }

    if (overShoulderPosZ) {
        overShoulderPosZ->data.f = savedOverShoulderPosZ;
    }

    camera->cameraTarget = player;
    camera->worldFOV = worldFOV;
    camera->Update();
    player->Update3DPosition(true);

    if (mouseWheelZoomSpeed) {
        mouseWheelZoomSpeed->data.f = savedMouseWheelZoomSpeed;
    }

    ResetSavedState();
    logger::info("[MenuCamera] Stopped and restored camera state.");
}

void MenuCamera::ApplySettings()
{
    if (!active) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!player || !camera) {
        return;
    }

    auto* thirdState = static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraState::kThirdPerson].get());
    if (!thirdState) {
        return;
    }

    ApplyCameraValues(player, camera, thirdState);
    logger::info("[MenuCamera] Applied live settings. offsetX={} offsetY={} offsetZ={} fov={}", Settings::offsetX, Settings::offsetY, Settings::offsetZ, Settings::fov);
}

bool MenuCamera::IsActive() const
{
    return active;
}

bool MenuCamera::CaptureState(RE::PlayerCharacter* player, RE::PlayerCamera* camera, RE::ThirdPersonState* thirdState)
{
    if (!player || !camera || !thirdState) {
        return false;
    }

    camera->cameraTarget = player;
    previousState = camera->currentState.get();
    wasFirstPerson = camera->IsInFirstPerson();

    playerAngleX = player->data.angle.x;
    playerAngleZ = player->data.angle.z;
    freeRotation = thirdState->freeRotation;
    posOffsetExpected = thirdState->posOffsetExpected;
    targetZoomOffset = thirdState->targetZoomOffset;
    pitchZoomOffset = thirdState->pitchZoomOffset;
    worldFOV = camera->worldFOV;
    toggleAnimCam = thirdState->toggleAnimCam;
    freeRotationEnabled = thirdState->freeRotationEnabled;

    player->GetGraphVariableBool("IsNPC", headtrackingEnabled);
    player->GetGraphVariableBool("bHeadTrackSpine", headTrackSpineEnabled);
    player->GetGraphVariableBool("bUseEyeTracking", eyeTrackingEnabled);

    savedOverShoulderCombatPosX = overShoulderCombatPosX->GetFloat();
    savedOverShoulderCombatAddY = overShoulderCombatAddY->GetFloat();
    savedOverShoulderCombatPosZ = overShoulderCombatPosZ->GetFloat();
    savedAutoVanityModeDelay = autoVanityModeDelay->GetFloat();
    savedOverShoulderPosX = overShoulderPosX->GetFloat();
    savedOverShoulderPosZ = overShoulderPosZ->GetFloat();
    savedVanityModeMinDist = vanityModeMinDist->GetFloat();
    savedVanityModeMaxDist = vanityModeMaxDist->GetFloat();
    savedMouseWheelZoomSpeed = mouseWheelZoomSpeed->GetFloat();
    savedTogglePOVDelay = togglePOVDelay->GetFloat();

    player->SetGraphVariableBool("IsNPC", false);
    player->SetGraphVariableBool("bHeadTrackSpine", false);
    player->SetGraphVariableBool("bUseEyeTracking", false);

    if (auto* process = player->GetActorRuntimeData().currentProcess) {
        process->ClearActionHeadtrackTarget(true);
    }

    return true;
}

void MenuCamera::ApplyCameraValues(RE::PlayerCharacter* player, RE::PlayerCamera* camera, RE::ThirdPersonState* thirdState)
{
    if (!player || !camera || !thirdState) {
        return;
    }

    camera->cameraTarget = player;
    camera->SetState(thirdState);

    thirdState->toggleAnimCam = true;
    thirdState->freeRotationEnabled = true;
    thirdState->freeRotation.x = PI - 0.5f;
    thirdState->freeRotation.y = 0.0f;
    thirdState->targetZoomOffset = 0.0f;
    thirdState->pitchZoomOffset = 0.1f;

    autoVanityModeDelay->data.f = 10800.0f;
    togglePOVDelay->data.f = 10800.0f;
    overShoulderCombatPosX->data.f = Settings::offsetX;
    overShoulderCombatAddY->data.f = Settings::offsetY;
    overShoulderCombatPosZ->data.f = Settings::offsetZ;
    overShoulderPosX->data.f = Settings::offsetX;
    overShoulderPosZ->data.f = Settings::offsetZ;
    vanityModeMinDist->data.f = Settings::distance;
    vanityModeMaxDist->data.f = Settings::distance;
    mouseWheelZoomSpeed->data.f = 10000.0f;

    player->data.angle.x = 0.1f;
    thirdState->posOffsetExpected = thirdState->posOffsetActual = RE::NiPoint3(Settings::offsetX, Settings::offsetY,
                                                                              Settings::offsetZ);

    camera->worldFOV = Settings::fov;
    camera->Update();
    player->Update3DPosition(true);
}

void MenuCamera::ResetSavedState()
{
    overShoulderCombatPosX = nullptr;
    overShoulderCombatAddY = nullptr;
    overShoulderCombatPosZ = nullptr;
    autoVanityModeDelay = nullptr;
    overShoulderPosX = nullptr;
    overShoulderPosZ = nullptr;
    vanityModeMinDist = nullptr;
    vanityModeMaxDist = nullptr;
    mouseWheelZoomSpeed = nullptr;
    togglePOVDelay = nullptr;
    previousState = nullptr;
    active = false;
    smoothCamControl = false;
    wasFirstPerson = false;
}
