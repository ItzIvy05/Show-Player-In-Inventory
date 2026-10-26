#include "Settings.h"

namespace
{
    // the settings and their ini keys. defaults come from Values, and only values that differ are written
    const FUCK::INI::Fields<Settings::Values> FIELDS{ "General", {
        { "bEnable",            &Settings::Values::enabled },
        { "bMagicMenu",         &Settings::Values::magicMenu },
        { "bBarterMenu",        &Settings::Values::barterMenu },
        { "bShowMerchant",      &Settings::Values::showMerchant },
        { "bContainerMenu",     &Settings::Values::containerMenu },
        { "bFollowerInventory", &Settings::Values::followerInventory },
        { "bShowInCombat",      &Settings::Values::showInCombat },
        { "bShowOnHorseback",   &Settings::Values::showOnHorseback },
        { "bShowSitting",       &Settings::Values::showSitting },
        { "bShowSleeping",      &Settings::Values::showSleeping },
        { "bHideItemPreviews",  &Settings::Values::hideItemPreviews },
        { "bAnimateCharacter",  &Settings::Values::animateCharacter },
        { "bAllowMovement",     &Settings::Values::allowMovement },
        { "bTurnCharacter",     &Settings::Values::turnCharacter },
        { "bRememberCamera",    &Settings::Values::rememberCamera },
        { "bRememberFacing",    &Settings::Values::rememberFacing },
        { "bHideObstructions",  &Settings::Values::hideObstructions },
        { "bUpperBody",         &Settings::Values::upperBody },
        { "bCameraControls",    &Settings::Values::cameraControls },
        { "bTutorialSeen",      &Settings::Values::tutorialSeen },
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

        // always, from the front with Barter Menu on, otherwise from behind as when looting (MenuCamera::Start)
        if (menuName == RE::BarterMenu::MENU_NAME) {
            return true;
        }

        // chests, bodies and other containers. a living actor's inventory is only watched when it's a follower's, so pickpocketing is left alone
        if (menuName == RE::ContainerMenu::MENU_NAME) {
            const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
            if (!target) {
                return false;
            }

            const auto* actor = target->As<RE::Actor>();
            if (actor && !actor->IsDead()) {
                return values.followerInventory && GetFollower();
            }
            return values.containerMenu;
        }

        return false;
    }

    bool CanLieDown()
    {
        static const bool installed = GetModuleHandleW(L"GoToBed.dll") != nullptr;
        return installed;
    }

    // the container menu's NPC mode is a follower's "let's trade", or a mod opening someone's inventory. bodies and pickpocketing have modes of their own
    RE::Actor* GetFollower()
    {
        const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
        auto* actor = target ? target->As<RE::Actor>() : nullptr;
        if (!actor || actor->IsDead() || RE::ContainerMenu::GetContainerMode() != RE::ContainerMenu::ContainerMode::kNPCMode) {
            return nullptr;
        }

        return actor;
    }
}
