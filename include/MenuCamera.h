#pragma once

#include "Settings.h"

#include <FUCK_Camera.h>

// FUCK's camera over the third-person camera, this side gets the player ready and puts them back
class MenuCamera : public REX::Singleton<MenuCamera>, public FUCK::ICameraClient
{
public:
    static void InstallHooks();

    void Start();
    void Stop();
    void Update(float interval);
    void ResetCamera();
    void ApplyAnimation();
    static void ApplyItemPreviews();
    bool DrawPositionSliders();
    void OnPerspectiveUpdate(bool firstPerson);
    void SetCacheFrozen(bool frozen);
    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] bool HasCamera();

private:
    // FUCK::ICameraClient
    void OnCameraUpdate(FUCK::CameraControlState& state, float interval) override;
    void OnCameraTakenOver() override;

    void SetFrozen(bool freeze);

    REX::EnumSet<RE::ControlMap::UEFlag, std::uint32_t> disabledControls;

    float playerAngleX = 0.0f;
    float playerAngleZ = 0.0f;
    float worldFOV = 0.0f;

    bool active = false;
    bool cachedFirstPerson = false;
    bool cacheFrozen = false;
    bool headTrackingWasOn = false;
    bool allowAutoVanityMode = false;
    bool playerFrozen = false;

    // the cursor was on the item list when the mouse buttons went down, so they belong to the list
    bool mouseOnList = false;
};
