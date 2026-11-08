#pragma once

#include "Settings.h"

#include <FUCK_Camera.h>

// FUCK's camera over the third-person camera, this side gets the player ready and puts them back
class MenuCamera : public REX::Singleton<MenuCamera>, public FUCK::ICameraClient
{
public:
    static void InstallHooks();
    static void HookMenu(std::string_view menuName);

    void        Start(std::string_view menuName);
    void        Stop();
    void        Update(float interval);
    void        ResetCamera();
    void        ApplyFraming();
    void        ApplyAnimation();
    void        OnPerspectiveUpdate(bool firstPerson);
    void        SetCacheFrozen(bool frozen);
    
    [[nodiscard]] bool IsActive() const;
    [[nodiscard]] bool HasCamera();

    // the one shown: the player, or the follower whose inventory is open
    [[nodiscard]] RE::Actor* GetSubject() const;

private:
    // FUCK::ICameraClient
    void OnCameraUpdate(FUCK::CameraControlState& state, float interval) override;
    void OnCameraTakenOver() override;

    void SetFrozen(bool freeze);
    bool FollowApparelPreview(float interval);
    void HideItemCards(bool hide);
    void UpdateShade(float interval);
    void RemoveShade();
    void Restart();
    void HidePlayer();
    void ShowPlayer();
    
    [[nodiscard]] bool IsAnimatedHere(RE::Actor* actor) const;
    [[nodiscard]] std::vector<RE::Actor*> GetShown() const;

    REX::EnumSet<RE::ControlMap::UEFlag, std::uint32_t> disabledControls;

    float playerAngleX = 0.0f;
    float playerAngleZ = 0.0f;

    bool  pitchLevelled    = false;  // the player's look was levelled, to put back
    bool  merchantShown    = false;  // a merchant in barter, whose counter and face are left alone
    bool  hidingPlayer     = false;  // the player's kept hidden, from in front of a merchant
    bool  furnitureBase    = false;  // FUCK's controls over the furniture camera (a crafting menu), which is never left
    bool  benchFraming     = false;  // framed as at a bench, nearer the list (see GetFraming)
    float framingMargin    = 0.0f;   // and the room round it, 0 for FUCK's
    bool  benchPlaced      = false;  // a bench camera set by hand, so not framed
    float benchShiftScale  = 0.0f;   // and how far the frame shift slides it per unit, 0 when it can't
    float benchShiftPlaced = 0.0f;   // the shift it was placed with
    float benchShift       = 0.0f;   // and the shift it's slid to since
    bool  tutorialPending  = false;  // wait for initial framing updates
    int   tutorialFrames   = 0;

    std::vector<RE::NiPointer<RE::NiAVObject>> hiddenParts;  // and the parts of them hidden here, to show again

    // the menu shown in, and the player shown in a merchant's place while Apparel Preview has something on them
    std::string shownMenu;
    
    bool  previewShowsPlayer = false;
    float previewOffFor      = 0.0f;
    bool  restarting         = false;
    bool  cardHidden         = false;  // the item card hidden for it, to show again
    bool  moreHUDHidden      = false;  // moreHUD's item card hidden for it, to show again

    // the darkness round the menu while sneaking unseen
    bool  shadeAllowed = false;  // not in barter or a follower's inventory, where it's no secret
    bool  unseen       = false;
    float shade        = 0.0f;   // 0 to 1, easing towards whether they're unseen
    float shadeCheckIn = 0.0f;   // seconds until detection is checked again

    // a follower shown in the player's place, and how they faced, to put back
    RE::ActorHandle follower;
    float followerAngleZ = 0.0f;

    // the horse of the one shown while they ride
    RE::ActorHandle mount;

    // someone held by the one shown (carried, or riding behind them)
    struct Held
    {
        RE::ActorHandle actor;
        bool animated = true;                     // false for an animal another mod poses itself
        RE::NiPointer<RE::NiAVObject> carriedBy;  // what they move with: a horse's saddle, or the pelvis of whoever holds them
        RE::NiTransform offset;                   // where they are against it
    };
    std::vector<Held> held;

    // who the player's talking to, animated with the one shown in a paused menu
    RE::ActorHandle partner;
    float worldFOV = 0.0f;

    bool active            = false;
    bool cachedFirstPerson = false;
    bool cacheFrozen       = false;
    bool playerFrozen      = false;

    bool firstPersonAfterBench = false;  // back to first person once the player's left a workbench

    // the cursor was on the item list when the mouse buttons went down, so they belong to the list
    bool mouseOnList = false;
};