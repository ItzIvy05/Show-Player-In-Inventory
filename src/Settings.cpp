#include "Settings.h"

namespace
{
    // the settings and their ini keys. defaults come from Values, and only values that differ are written
    const FUCK::INI::Fields<Settings::Values> FIELDS{ "General", {
        { "bEnable", &Settings::Values::enabled },
        { "bMagicMenu", &Settings::Values::magicMenu },
        { "bBarterMenu", &Settings::Values::barterMenu },
        { "bContainerMenu", &Settings::Values::containerMenu },
        { "bHideItemPreviews", &Settings::Values::hideItemPreviews },
        { "bAnimateCharacter", &Settings::Values::animateCharacter },
        { "bAllowMovement", &Settings::Values::allowMovement },
        { "bTurnCharacter", &Settings::Values::turnCharacter },
        { "bRememberCamera", &Settings::Values::rememberCamera },
        { "bCameraControls", &Settings::Values::cameraControls },
        { "bTutorialSeen", &Settings::Values::tutorialSeen },
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
        // the tutorial has been seen either way
        const bool seen = values.tutorialSeen;
        values = {};
        values.tutorialSeen = seen;
    }

    void Load()
    {
        GetINI().Load(FIELDS, values);
        logger::info("[Settings] enabled={} animateCharacter={}", values.enabled, values.animateCharacter);
    }

    // only what differs from the defaults is written
    void Save()
    {
        GetINI().Save(FIELDS, values);
    }

    bool IsWatchedMenu(std::string_view menuName)
    {
        if (menuName == RE::InventoryMenu::MENU_NAME) {
            return true;
        }

        if (menuName == RE::MagicMenu::MENU_NAME) {
            return values.magicMenu;
        }

        if (menuName == RE::BarterMenu::MENU_NAME) {
            return values.barterMenu;
        }

        // chests and other containers only. an actor's inventory is a body, a pickpocket or a follower, where showing the player isn't much use
        if (menuName == RE::ContainerMenu::MENU_NAME) {
            if (!values.containerMenu) {
                return false;
            }

            const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
            return target && !target->As<RE::Actor>();
        }

        return false;
    }
}
