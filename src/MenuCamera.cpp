#include "MenuCamera.h"
#include "APIManager.h"
#include "Settings.h"

namespace
{
    RE::ThirdPersonState* GetThirdPersonState(RE::PlayerCamera* camera)
    {
        return static_cast<RE::ThirdPersonState*>(camera->cameraStates[RE::CameraState::kThirdPerson].get());
    }

    struct ThirdPersonUpdateHook
    {
        static void Update(RE::ThirdPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
        {
            _Update(a_this, a_nextState);
            MenuCamera::GetSingleton()->OnPerspectiveUpdate(false);
        }
        static inline REL::Relocation<decltype(Update)> _Update;
    };

    struct FirstPersonUpdateHook
    {
        static void Update(RE::FirstPersonState* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
        {
            _Update(a_this, a_nextState);
            MenuCamera::GetSingleton()->OnPerspectiveUpdate(true);
        }
        static inline REL::Relocation<decltype(Update)> _Update;
    };
}

void MenuCamera::InstallHook()
{
    REL::Relocation<std::uintptr_t> thirdVtbl{ RE::ThirdPersonState::VTABLE[0] };
    ThirdPersonUpdateHook::_Update = thirdVtbl.write_vfunc(0x3, ThirdPersonUpdateHook::Update);

    REL::Relocation<std::uintptr_t> firstVtbl{ RE::FirstPersonState::VTABLE[0] };
    FirstPersonUpdateHook::_Update = firstVtbl.write_vfunc(0x3, FirstPersonUpdateHook::Update);

    logger::info("[MenuCamera] Installed first and third person camera state hooks.");
}

void MenuCamera::SetCacheFrozen(bool frozen)
{
    cacheFrozen = frozen;
}

void MenuCamera::OnPerspectiveUpdate(bool firstPerson)
{
    if (!active && !cacheFrozen) {
        cachedFirstPerson = firstPerson;
    }

    if (active && !firstPerson) {
        EnforceCameraValues(RE::PlayerCamera::GetSingleton());
    }
}

void MenuCamera::EnforceCameraValues(RE::PlayerCamera* camera)
{
    auto* thirdState = GetThirdPersonState(camera);
    thirdState->toggleAnimCam = true;
    thirdState->freeRotationEnabled = true;
    thirdState->applyOffsets = true;
    thirdState->targetZoomOffset = 0.0f;
    thirdState->currentZoomOffset = 0.0f;
    thirdState->savedZoomOffset = 0.0f;
    thirdState->pitchZoomOffset = 0.1f;
    thirdState->posOffsetExpected = thirdState->posOffsetActual = RE::NiPoint3(Settings::offsetX.GetValue(), Settings::offsetY.GetValue(), Settings::offsetZ.GetValue());
    camera->worldFOV = Settings::fov.GetValue();
}

bool MenuCamera::CaptureINISettings()
{
    if (iniCaptured) {
        return true;
    }

    auto* ini = RE::INISettingCollection::GetSingleton();
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

    iniCaptured = overShoulderCombatPosX && overShoulderCombatAddY && overShoulderCombatPosZ && autoVanityModeDelay && overShoulderPosX && overShoulderPosZ && vanityModeMinDist && vanityModeMaxDist && mouseWheelZoomSpeed && togglePOVDelay;
    return iniCaptured;
}

void MenuCamera::Start()
{
    if (active) {
        ApplySettings();
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player->IsInCombat() || player->IsOnMount()) {
        logger::info("[MenuCamera] Skipped start. Player is in combat or on horseback.");
        return;
    }

    if (!CaptureINISettings()) {
        logger::warn("[MenuCamera] Could not start. Missing INI camera settings.");
        return;
    }

    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* thirdState = GetThirdPersonState(camera);

    if (APIs::SmoothCam && APIs::SmoothCam->IsCameraEnabled()) {
        const auto result = APIs::SmoothCam->RequestCameraControl(SKSE::GetPluginHandle());

        if (result == SmoothCamAPI::APIResult::OK || result == SmoothCamAPI::APIResult::AlreadyGiven) {
            APIs::SmoothCam->RequestInterpolatorUpdates(SKSE::GetPluginHandle(), true);
            smoothCamControl = true;
            logger::info("[MenuCamera] SmoothCam camera control acquired.");
        } else {
            logger::warn("[MenuCamera] SmoothCam camera control request failed: {}", static_cast<std::uint8_t>(result));
        }
    } else {
        logger::debug("[MenuCamera] SmoothCam API not available or SmoothCam disabled.");
    }

    CaptureState(player, camera, thirdState);

    active = true;
    ApplyCameraValues(player, camera, thirdState);
    logger::info("[MenuCamera] Started. offsetX={} offsetY={} offsetZ={} fov={}", Settings::offsetX.GetValue(), Settings::offsetY.GetValue(), Settings::offsetZ.GetValue(), Settings::fov.GetValue());
}

void MenuCamera::Stop()
{
    if (!active) {
        return;
    }

    active = false;

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();
    auto* thirdState = GetThirdPersonState(camera);

    if (smoothCamControl && APIs::SmoothCam && APIs::SmoothCam->IsCameraEnabled()) {
        APIs::SmoothCam->ReleaseCameraControl(SKSE::GetPluginHandle());
        logger::info("[MenuCamera] SmoothCam camera control released.");
    }

    if (cachedFirstPerson) {
        camera->SetState(camera->cameraStates[RE::CameraState::kFirstPerson].get());
    }

    player->data.angle.x = playerAngleX;
    player->data.angle.z = playerAngleZ;
    player->SetGraphVariableBool("IsNPC", headtrackingEnabled);
    player->SetGraphVariableBool("bHeadTrackSpine", headTrackSpineEnabled);
    player->SetGraphVariableBool("bUseEyeTracking", eyeTrackingEnabled);

    if (autoVanityModeDelay) {
        autoVanityModeDelay->SetFloat(savedAutoVanityModeDelay);
    }

    if (togglePOVDelay) {
        togglePOVDelay->SetFloat(savedTogglePOVDelay);
    }

    thirdState->toggleAnimCam = toggleAnimCam;
    thirdState->freeRotationEnabled = freeRotationEnabled;
    thirdState->applyOffsets = applyOffsets;
    thirdState->targetZoomOffset = targetZoomOffset;
    thirdState->currentZoomOffset = currentZoomOffset;
    thirdState->savedZoomOffset = savedZoomOffset;
    thirdState->pitchZoomOffset = pitchZoomOffset;
    thirdState->freeRotation = freeRotation;
    thirdState->posOffsetExpected = thirdState->posOffsetActual = posOffsetExpected;

    if (vanityModeMinDist) {
        vanityModeMinDist->SetFloat(savedVanityModeMinDist);
    }

    if (vanityModeMaxDist) {
        vanityModeMaxDist->SetFloat(savedVanityModeMaxDist);
    }

    if (overShoulderCombatPosX) {
        overShoulderCombatPosX->SetFloat(savedOverShoulderCombatPosX);
    }

    if (overShoulderCombatAddY) {
        overShoulderCombatAddY->SetFloat(savedOverShoulderCombatAddY);
    }

    if (overShoulderCombatPosZ) {
        overShoulderCombatPosZ->SetFloat(savedOverShoulderCombatPosZ);
    }

    if (overShoulderPosX) {
        overShoulderPosX->SetFloat(savedOverShoulderPosX);
    }

    if (overShoulderPosZ) {
        overShoulderPosZ->SetFloat(savedOverShoulderPosZ);
    }

    camera->cameraTarget = player;
    camera->worldFOV = worldFOV;
    camera->Update();
    player->Update3DPosition(true);

    if (mouseWheelZoomSpeed) {
        mouseWheelZoomSpeed->SetFloat(savedMouseWheelZoomSpeed);
    }

    ResetSavedState();
    logger::info("[MenuCamera] Stopped and restored camera state.");
}

void MenuCamera::ApplySettings()
{
    if (!active) {
        return;
    }

    auto* camera = RE::PlayerCamera::GetSingleton();
    ApplyCameraValues(RE::PlayerCharacter::GetSingleton(), camera, GetThirdPersonState(camera));
    logger::info("[MenuCamera] Applied live settings. offsetX={} offsetY={} offsetZ={} fov={}", Settings::offsetX.GetValue(), Settings::offsetY.GetValue(), Settings::offsetZ.GetValue(), Settings::fov.GetValue());
}

void MenuCamera::Rotate(float deltaX)
{
    if (!active) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();
    const float delta = deltaX * 0.01f;
    player->data.angle.z -= delta;
    GetThirdPersonState(camera)->freeRotation.x += delta;
    camera->Update();
    player->Update3DPosition(true);
}

bool MenuCamera::IsActive() const
{
    return active;
}

void MenuCamera::CaptureState(RE::PlayerCharacter* player, RE::PlayerCamera* camera, RE::ThirdPersonState* thirdState)
{
    camera->cameraTarget = player;

    playerAngleX = player->GetAngleX();
    playerAngleZ = player->GetAngleZ();
    freeRotation = thirdState->freeRotation;
    posOffsetExpected = thirdState->posOffsetExpected;
    targetZoomOffset = thirdState->targetZoomOffset;
    currentZoomOffset = thirdState->currentZoomOffset;
    savedZoomOffset = thirdState->savedZoomOffset;
    pitchZoomOffset = thirdState->pitchZoomOffset;
    worldFOV = camera->worldFOV;
    toggleAnimCam = thirdState->toggleAnimCam;
    freeRotationEnabled = thirdState->freeRotationEnabled;
    applyOffsets = thirdState->applyOffsets;

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
}

void MenuCamera::ApplyCameraValues(RE::PlayerCharacter* player, RE::PlayerCamera* camera, RE::ThirdPersonState* thirdState)
{
    camera->cameraTarget = player;
    camera->ForceThirdPerson();

    thirdState->freeRotation.x = RE::NI_PI - 0.5f;
    thirdState->freeRotation.y = 0.0f;

    autoVanityModeDelay->SetFloat(10800.0f);
    togglePOVDelay->SetFloat(10800.0f);
    overShoulderCombatPosX->SetFloat(Settings::offsetX.GetValue());
    overShoulderCombatAddY->SetFloat(Settings::offsetY.GetValue());
    overShoulderCombatPosZ->SetFloat(Settings::offsetZ.GetValue());
    overShoulderPosX->SetFloat(Settings::offsetX.GetValue());
    overShoulderPosZ->SetFloat(Settings::offsetZ.GetValue());
    vanityModeMinDist->SetFloat(Settings::distance);
    vanityModeMaxDist->SetFloat(Settings::distance);
    mouseWheelZoomSpeed->SetFloat(10000.0f);

    player->data.angle.x = 0.1f;

    EnforceCameraValues(camera);

    camera->Update();
    player->Update3DPosition(true);
}

void MenuCamera::ResetSavedState()
{
    active = false;
    smoothCamControl = false;
}
