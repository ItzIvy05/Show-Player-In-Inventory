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

        // the item card and 3D item preview, in the movie's pixels (positive right and up) and as a scale, 0 and 1 leaving them as they are
        float itemCardX        = 0.0f;
        float itemCardY        = 0.0f;
        float itemCardScale    = 1.0f;
        float itemPreviewX     = 0.0f;
        float itemPreviewY     = 0.0f;
        float itemPreviewScale = 1.0f;

        // on a screen wider than 16:9, how much of the extra width the player's moved right into, away from the item list
        float wideScreenShift  = 0.7f;

        // with Apparel Preview, the card and 3D item are hidden while a piece is previewed on the player
        bool hideApparelPreviewItems = true;

        // the item menus' card spot and rotation area as last laid out, on a screen of this shape, for a bench opened before any item menu
        float spotAspect     = 0.0f;
        float cardSpotX      = 0.0f;
        float cardSpotTop    = 0.0f;
        float cardSpotWidth  = 0.0f;
        float areaSpotX      = 0.0f;
        float areaSpotY      = 0.0f;
        float areaSpotWidth  = 0.0f;
        float areaSpotHeight = 0.0f;

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
