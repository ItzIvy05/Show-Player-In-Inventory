#pragma once

namespace Settings
{
    //default setting values
    struct Values
    {
        bool enabled          = true;
        bool hideItemPreviews = false;
        bool animateCharacter = true;
        bool turnCharacter    = false;
        bool rememberCamera   = false;
        bool rememberFacing   = false;
        bool hideObstructions = true;
        bool sneakVignette    = true;
        bool upperBody        = false;
        bool cameraControls   = true;
        bool tutorialSeen     = false;
        int version = 0;  // the settings' version, see VERSION in Settings.cpp
    };

    // as they stand, kept in FUCK's settings for this plugin (Data/FUCKs/ShowPlayerInInventory)
    inline Values values;

    void SetDefaults();
    void Load();
    void Save();
    [[nodiscard]] bool IsWatchedMenu(std::string_view menuName);

    // the follower whose inventory is open, shown in the player's place, or nullptr
    [[nodiscard]] RE::Actor* GetFollower();

    // whoever's being pickpocketed, shown in the player's place, or nullptr
    [[nodiscard]] RE::Actor* GetPickpocketed();

    // whether GoToBed is installed, which lies the player down in a bed. without it a bed goes straight to the sleep menu
    [[nodiscard]] bool CanLieDown();
}
