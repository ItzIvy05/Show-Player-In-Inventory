#include "Settings.h"

namespace Settings
{
    void SetDefaults()
    {
        enabled.SetValue(true);
        barterEnabled.SetValue(false);
        loggingEnabled.SetValue(false);
        offsetX.SetValue(-46.7f);
        offsetY.SetValue(-12.0f);
        offsetZ.SetValue(-20.0f);
        fov.SetValue(60.0f);
        rotateKey.SetValue(258);
    }

    void ApplyLogLevel()
    {
        const auto level = loggingEnabled.GetValue() ? spdlog::level::info : spdlog::level::warn;
        spdlog::set_level(level);
        spdlog::flush_on(level);
    }

    void Load()
    {
        if (!std::filesystem::exists(INI_PATH)) {
            logger::warn("[Settings] Could not open {}. Defaults will be used.", INI_PATH);
        }

        const auto ini = REX::INI::SettingStore::GetSingleton();
        ini->Init(INI_PATH, "");
        ini->Load();

        ApplyLogLevel();

        logger::info("[Settings] enabled={} offsetX={} offsetY={} offsetZ={} fov={}", enabled.GetValue(), offsetX.GetValue(), offsetY.GetValue(), offsetZ.GetValue(), fov.GetValue());
    }

    void Save()
    {
        REX::INI::SettingStore::GetSingleton()->Save();
    }

    bool IsWatchedMenu(const RE::BSFixedString& menuName)
    {
        return menuName == RE::InventoryMenu::MENU_NAME || menuName == RE::MagicMenu::MENU_NAME || (barterEnabled.GetValue() && menuName == RE::BarterMenu::MENU_NAME);
    }
}
