#pragma once

namespace Settings
{
    // the smallest the item card and preview can be sized to. at nothing they can't be brought back
    inline constexpr float MIN_ITEM_SCALE = 0.01f;

    //default setting values
    struct Values
    {
        bool enabled           = true;
        bool animateCharacter  = true;
        bool turnCharacter     = false;
        bool rememberCamera    = false;
        bool rememberFacing    = false;
        bool hideObstructions  = true;
        bool sneakVignette     = true;
        bool upperBody         = false;
        bool cameraControls    = true;
        bool tutorialSeen      = false;

        // the item card and 3D item preview, in the movie's pixels (positive right and up) and as a scale, 0 and 1 leaving them as they are
        float itemCardX        = 0.0f;
        float itemCardY        = 0.0f;
        float itemCardScale    = 1.0f;
        float itemPreviewX     = 0.0f;
        float itemPreviewY     = 0.0f;
        float itemPreviewScale = 1.0f;
        float benchLabelX      = 0.0f;  // a crafting bench's description, across

        // how much the player's moved horizontally, away from the item list
        float frameShift       = 0.0f;

        // the field of view framed menus open at, narrower than the game's so the player isn't stretched at the screen's edge
        float startFov         = 50.0f;

        // the card and 3D item are placed and sized for the screen's shape, ignoring their sliders
        bool autoAdjust        = true;
        bool autoAdjustOwn     = false;  // on themed layouts

        // with Apparel Preview, the card and 3D item are hidden while a piece is previewed on the player
        bool hideAppPrevItems  = false;

        // the item menus' card spot as last laid out, on a screen of this shape, for a bench opened before any item menu
        float spotAspect       = 0.0f;
        float cardSpotX        = 0.0f;
        float cardSpotTop      = 0.0f;
        float cardSpotWidth    = 0.0f;

        // how far below the card's top a bench's requirements reach, as a share of the card's width, measured at the last bench
        float bottomLimit      = 0.0f;

        int version = 0;  // the settings' version, see VERSION in Settings.cpp
    };

    // as they stand, kept in FUCK's settings for this plugin (Data/FUCKs/ShowPlayerInInventory)
    inline Values values;

    void SetDefaults();
    void Load();
    void Save();
    [[nodiscard]] bool IsWatchedMenu(std::string_view menuName);

    // the staff enchanting mod's crafting menu, which is a class of its own
    inline constexpr std::string_view STAFF_CRAFTING_MENU = "StaffCraftingMenu";

    // whether it's the game's crafting menu or the staff enchanting mod's
    [[nodiscard]] bool IsCraftingMenu(std::string_view menuName);

    // hiding the card and 3D item during Apparel Preview, only on screens up to 16:9, where they cover the piece
    [[nodiscard]] bool CanHideApparelPreviewItems();

    // the follower whose inventory is open, shown in the player's place, or nullptr
    [[nodiscard]] RE::Actor* GetFollower();

    // whoever's being pickpocketed, shown in the player's place, or nullptr
    [[nodiscard]] RE::Actor* GetPickpocketed();

    // a mannequin, which opens its inventory as a follower does. framed like one, but left in its pose: not turned or animated
    [[nodiscard]] bool IsManakin(const RE::Actor* actor);

    // whether GoToBed is installed, which lies the player down in a bed. without it a bed goes straight to the sleep menu
    [[nodiscard]] bool CanLieDown();
}