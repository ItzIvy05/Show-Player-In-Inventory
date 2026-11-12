#include "Settings.h"

namespace
{
    // raised when a release should start everyone's settings afresh. 3 is 3.0.0, which reworked them
    constexpr int  VERSION = 3;
    constexpr auto SECTION = "General";

    // the settings and their ini keys. defaults come from Values, and only values that differ are written
    const FUCK::INI::Fields<Settings::Values> FIELDS{ SECTION, {
        { "bEnable",           &Settings::Values::enabled },
        { "bAnimateCharacter", &Settings::Values::animateCharacter },
        { "bTurnCharacter",    &Settings::Values::turnCharacter },
        { "bRememberCamera",   &Settings::Values::rememberCamera },
        { "bRememberFacing",   &Settings::Values::rememberFacing },
        { "bHideObstructions", &Settings::Values::hideObstructions },
        { "bSneakVignette",    &Settings::Values::sneakVignette },
        { "bUpperBody",        &Settings::Values::upperBody },
        { "bCameraControls",   &Settings::Values::cameraControls },
        { "bTutorialSeen",     &Settings::Values::tutorialSeen },
        { "bShowInventory",    &Settings::Values::showInventory },
        { "bShowMagic",        &Settings::Values::showMagic },
        { "bShowContainers",   &Settings::Values::showContainers },
        { "bShowFollowers",    &Settings::Values::showFollowers },
        { "bShowPickpocket",   &Settings::Values::showPickpocket },
        { "bShowBarter",       &Settings::Values::showBarter },
        { "bShowMerchant",     &Settings::Values::showMerchant },
        { "bShowCrafting",     &Settings::Values::showCrafting },
        { "fItemCardX",        &Settings::Values::itemCardX },
        { "fItemCardY",        &Settings::Values::itemCardY },
        { "fItemCardScale",    &Settings::Values::itemCardScale },
        { "fItemPreviewX",     &Settings::Values::itemPreviewX },
        { "fItemPreviewY",     &Settings::Values::itemPreviewY },
        { "fItemPreviewScale", &Settings::Values::itemPreviewScale },
        { "fBenchLabelX",      &Settings::Values::benchLabelX },
        { "bAutoAdjust",       &Settings::Values::autoAdjust },
        { "bAutoAdjustOwn",    &Settings::Values::autoAdjustOwn },
        { "bHideAppPrevItems", &Settings::Values::hideAppPrevItems },
        { "bHideItemCard",     &Settings::Values::hideItemCard },
        { "bHideItemPreview",  &Settings::Values::hideItemPreview },
        { "fFrameShift",       &Settings::Values::frameShift },
        { "fStartFov",         &Settings::Values::startFov },
        { "fSpotAspect",       &Settings::Values::spotAspect },
        { "fCardSpotX",        &Settings::Values::cardSpotX },
        { "fCardSpotTop",      &Settings::Values::cardSpotTop },
        { "fCardSpotWidth",    &Settings::Values::cardSpotWidth },
        { "fBottomLimit",      &Settings::Values::bottomLimit },
        { "iVersion",          &Settings::Values::version },
    } };

    FUCK::PluginSettings& GetINI()
    {
        static FUCK::PluginSettings ini;
        return ini;
    }
}

namespace Settings
{
    void SetDefaults()
    {
        // the tutorial has been seen either way, and the version, the item menus' spots and the bench's requirements stay
        const Values kept = values;
        values = {};
        values.version        = VERSION;
        values.tutorialSeen   = kept.tutorialSeen;
        values.spotAspect     = kept.spotAspect;
        values.cardSpotX      = kept.cardSpotX;
        values.cardSpotTop    = kept.cardSpotTop;
        values.cardSpotWidth  = kept.cardSpotWidth;
        values.bottomLimit    = kept.bottomLimit;
    }

    // settings from an older version are cleared
    void Load()
    {
        GetINI().Load(FIELDS, values);
        if (values.version >= VERSION) {
            return;
        }

        GetINI().Save([](CSimpleIniA& ini) { ini.Delete(SECTION, nullptr); });
        values = {};
        GetINI().Load(FIELDS, values);
        values.version = VERSION;
        Save();
    }

    // only what differs from the defaults is written
    void Save()
    {
        GetINI().Save(FIELDS, values);
    }

    bool IsWatchedMenu(std::string_view menuName)
    {
        if (menuName == RE::InventoryMenu::MENU_NAME) {
            return values.showInventory;
        }
        if (menuName == RE::MagicMenu::MENU_NAME) {
            return values.showMagic;
        }
        if (menuName == RE::BarterMenu::MENU_NAME) {
            return values.showBarter;
        }
        if (IsCraftingMenu(menuName)) {
            return values.showCrafting;
        }

        // chests, bodies and other containers, and a living actor's inventory when it's a follower's or being pickpocketed. anyone else's (a
        // mod opening it) is left alone
        if (menuName == RE::ContainerMenu::MENU_NAME) {
            const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
            if (!target) {
                return false;
            }

            const auto* actor = target->As<RE::Actor>();
            if (!actor || actor->IsDead()) {
                return values.showContainers;
            }
            if (GetFollower()) {
                return values.showFollowers;
            }
            return GetPickpocketed() && values.showPickpocket;
        }

        return false;
    }

    bool IsCraftingMenu(std::string_view menuName)
    {
        return menuName == RE::CraftingMenu::MENU_NAME || menuName == STAFF_CRAFTING_MENU;
    }

    bool CanHideApparelPreviewItems()
    {
        const auto size = FUCK::GetDisplaySize();
        return size.y > 0.0f && size.x / size.y <= 16.0f / 9.0f + 0.001f;
    }

    bool CanLieDown()
    {
        static const bool installed = GetModuleHandleW(L"GoToBed.dll") != nullptr;
        return installed;
    }

    // the living actor whose inventory the container menu has open in this mode, or nullptr
    RE::Actor* GetContainerActor(RE::ContainerMenu::ContainerMode mode)
    {
        const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
        auto* actor = target ? target->As<RE::Actor>() : nullptr;
        if (!actor || actor->IsDead() || RE::ContainerMenu::GetContainerMode() != mode) {
            return nullptr;
        }

        return actor;
    }

    // the container menu's NPC mode is a follower's "let's trade", or a mod opening someone's inventory. bodies and pickpocketing have modes of their own
    RE::Actor* GetFollower()
    {
        return GetContainerActor(RE::ContainerMenu::ContainerMode::kNPCMode);
    }

    RE::Actor* GetPickpocketed()
    {
        return GetContainerActor(RE::ContainerMenu::ContainerMode::kPickpocket);
    }

    bool IsManakin(const RE::Actor* actor)
    {
        constexpr RE::FormID MANAKIN_RACE = 0x0010760A;  // Skyrim.esm's ManakinRace
        const auto* race = actor ? actor->GetRace() : nullptr;
        return race && race->GetFormID() == MANAKIN_RACE;
    }
}
