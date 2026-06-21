#pragma once

class MenuCamera
{
public:
    static MenuCamera& GetSingleton();

    bool Start();
    void Stop();
    void ApplySettings();
    [[nodiscard]] bool IsActive() const;

private:
    RE::Setting* overShoulderCombatPosX = nullptr;
    RE::Setting* overShoulderCombatAddY = nullptr;
    RE::Setting* overShoulderCombatPosZ = nullptr;
    RE::Setting* autoVanityModeDelay = nullptr;
    RE::Setting* overShoulderPosX = nullptr;
    RE::Setting* overShoulderPosZ = nullptr;
    RE::Setting* vanityModeMinDist = nullptr;
    RE::Setting* vanityModeMaxDist = nullptr;
    RE::Setting* mouseWheelZoomSpeed = nullptr;
    RE::Setting* togglePOVDelay = nullptr;

    RE::TESCameraState* previousState = nullptr;
    RE::NiPoint2 freeRotation{};
    RE::NiPoint3 posOffsetExpected{};

    float playerAngleX = 0.0f;
    float playerAngleZ = 0.0f;
    float targetZoomOffset = 0.0f;
    float pitchZoomOffset = 0.0f;
    float worldFOV = 0.0f;

    float savedOverShoulderCombatPosX = 0.0f;
    float savedOverShoulderCombatAddY = 0.0f;
    float savedOverShoulderCombatPosZ = 0.0f;
    float savedAutoVanityModeDelay = 0.0f;
    float savedOverShoulderPosX = 0.0f;
    float savedOverShoulderPosZ = 0.0f;
    float savedVanityModeMinDist = 0.0f;
    float savedVanityModeMaxDist = 0.0f;
    float savedMouseWheelZoomSpeed = 0.0f;
    float savedTogglePOVDelay = 0.0f;

    bool active = false;
    bool smoothCamControl = false;
    bool wasFirstPerson = false;
    bool headtrackingEnabled = false;
    bool headTrackSpineEnabled = false;
    bool eyeTrackingEnabled = false;
    bool toggleAnimCam = false;
    bool freeRotationEnabled = false;

    [[nodiscard]] bool CaptureINISettings();
    [[nodiscard]] bool CaptureState(RE::PlayerCharacter* player, RE::PlayerCamera* camera, RE::ThirdPersonState* thirdState);
    void ApplyCameraValues(RE::PlayerCharacter* player, RE::PlayerCamera* camera, RE::ThirdPersonState* thirdState);
    void ResetSavedState();
};
