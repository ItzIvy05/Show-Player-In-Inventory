#pragma once

namespace Settings
{
    struct Values
    {
        bool enabled = true;
        bool magicMenu = true;
        bool barterMenu = false;
        bool containerMenu = true;
        bool followerInventory = true;
        bool hideItemPreviews = false;
        bool animateCharacter = true;
        bool allowMovement = false;
        bool turnCharacter = false;
        bool rememberCamera = true;
        bool cameraControls = true;
        bool tutorialSeen = false;
    };

    // as they stand, kept in FUCK's settings for this plugin (Data/FUCKs/ShowPlayerInInventory)
    inline Values values;

    void SetDefaults();
    void Load();
    void Save();
    [[nodiscard]] bool IsWatchedMenu(std::string_view menuName);

    // the follower whose inventory is open, shown in the player's place, or nullptr
    [[nodiscard]] RE::Actor* GetFollower();
}
