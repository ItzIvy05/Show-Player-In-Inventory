#pragma once

namespace Settings
{
    inline constexpr const wchar_t* DLL_NAME = L"ShowPlayerInInventory.dll";
    inline constexpr const char* INI_NAME = "ShowPlayerInInventory.ini";

    inline bool loaded = false;
    inline bool enabled = true;
    inline float offsetX = -46.7f;
    inline float offsetY = -12.0f;
    inline float offsetZ = -20.0f;
    inline constexpr float distance = 145.0f;
    inline float fov = 60.0f;

    void SetDefaults();
    void Load();
    void Save();
    [[nodiscard]] std::filesystem::path GetINIPath();
    [[nodiscard]] bool IsWatchedMenu(const RE::BSFixedString& menuName);
}
