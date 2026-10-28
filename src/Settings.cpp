#include "Settings.h"

namespace
{
    // raised when a release should start everyone's settings afresh. 3 is 3.0.0, which reworked them
    constexpr int  VERSION = 3;
    constexpr auto SECTION = "General";

    // the settings and their ini keys. defaults come from Values, and only values that differ are written
    const FUCK::INI::Fields<Settings::Values> FIELDS{ SECTION, {
        { "bEnable",           &Settings::Values::enabled },
        { "bHideItemPreviews", &Settings::Values::hideItemPreviews },
        { "bAnimateCharacter", &Settings::Values::animateCharacter },
        { "bTurnCharacter",    &Settings::Values::turnCharacter },
        { "bRememberCamera",   &Settings::Values::rememberCamera },
        { "bRememberFacing",   &Settings::Values::rememberFacing },
        { "bHideObstructions", &Settings::Values::hideObstructions },
        { "bSneakVignette",    &Settings::Values::sneakVignette },
        { "bUpperBody",        &Settings::Values::upperBody },
        { "bCameraControls",   &Settings::Values::cameraControls },
        { "bTutorialSeen",     &Settings::Values::tutorialSeen },
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
        // the tutorial has been seen either way, and the version stays
        const bool seen = values.tutorialSeen;
        values = {};
        values.tutorialSeen = seen;
        values.version = VERSION;
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
        if (menuName == RE::InventoryMenu::MENU_NAME ||
            menuName == RE::MagicMenu::MENU_NAME     ||
            menuName == RE::BarterMenu::MENU_NAME)   {
            return true;
        }

        // chests, bodies and other containers, and a living actor's inventory when it's a follower's or being pickpocketed. anyone else's (a
        // mod opening it) is left alone
        if (menuName == RE::ContainerMenu::MENU_NAME) {
            const auto target = RE::TESObjectREFR::LookupByHandle(RE::ContainerMenu::GetTargetRefHandle());
            if (!target) {
                return false;
            }

            const auto* actor = target->As<RE::Actor>();
            return !actor || actor->IsDead() || GetFollower() || GetPickpocketed();
        }

        return false;
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
}
