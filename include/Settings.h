#pragma once

namespace Settings
{
    inline constexpr const char* INI_PATH = "Data/SKSE/Plugins/ShowPlayerInInventory.ini";

    inline REX::INI::Bool<> enabled{ "General", "bEnable", true };
    inline REX::INI::Bool<> barterEnabled{ "General", "bBarterMenu", false };
    inline REX::INI::Bool<> loggingEnabled{ "General", "bEnableLogging", false };
    inline REX::INI::F32<> offsetX{ "Camera", "fOffsetX", -46.7f };
    inline REX::INI::F32<> offsetY{ "Camera", "fOffsetY", -12.0f };
    inline REX::INI::F32<> offsetZ{ "Camera", "fOffsetZ", -20.0f };
    inline constexpr float distance = 145.0f;
    inline REX::INI::F32<> fov{ "Camera", "fFOV", 60.0f };
    inline REX::INI::U32<> rotateKey{ "Controls", "iRotateKey", 258 };

    void SetDefaults();
    void ApplyLogLevel();
    void Load();
    void Save();
    [[nodiscard]] bool IsWatchedMenu(const RE::BSFixedString& menuName);
}
