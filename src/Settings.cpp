#include "Settings.h"

namespace
{
    std::string Trim(std::string value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, last - first + 1);
    }

    std::string Upper(std::string value)
    {
        for (char& ch : value) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        }

        return value;
    }

    bool ParseBool(std::string value, bool fallback)
    {
        value = Upper(Trim(std::move(value)));

        if (value == "1" || value == "TRUE" || value == "YES" || value == "ON") {
            return true;
        }

        if (value == "0" || value == "FALSE" || value == "NO" || value == "OFF") {
            return false;
        }

        return fallback;
    }

    float ParseFloat(std::string value, float fallback)
    {
        value = Trim(std::move(value));

        if (value.empty()) {
            return fallback;
        }

        try {
            size_t index = 0;
            const float parsed = std::stof(value, &index);
            return index == value.size() ? parsed : fallback;
        } catch (...) {
            return fallback;
        }
    }

    void ApplyValue(std::string section, std::string key, std::string value)
    {
        section = Upper(Trim(std::move(section)));
        key = Upper(Trim(std::move(key)));
        value = Trim(std::move(value));

        if (section == "GENERAL") {
            if (key == "BENABLE") {
                Settings::enabled = ParseBool(value, Settings::enabled);
            }

            return;
        }

        if (section != "CAMERA") {
            return;
        }

        if (key == "FOFFSETX") {
            Settings::offsetX = ParseFloat(value, Settings::offsetX);
        } else if (key == "FOFFSETY") {
            Settings::offsetY = ParseFloat(value, Settings::offsetY);
        } else if (key == "FOFFSETZ") {
            Settings::offsetZ = ParseFloat(value, Settings::offsetZ);
        } else if (key == "FFOV") {
            Settings::fov = ParseFloat(value, Settings::fov);
        }
    }
}

namespace Settings
{
    std::filesystem::path GetINIPath()
    {
        wchar_t modulePath[MAX_PATH]{};

        const HMODULE module = GetModuleHandleW(Settings::DLL_NAME);
        if (!module) {
            return std::filesystem::path("Data/SKSE/Plugins") / Settings::INI_NAME;
        }

        const DWORD length = GetModuleFileNameW(module, modulePath, MAX_PATH);
        if (length == 0 || length == MAX_PATH) {
            return std::filesystem::path("Data/SKSE/Plugins") / Settings::INI_NAME;
        }

        return std::filesystem::path(modulePath).parent_path() / Settings::INI_NAME;
    }

    void SetDefaults()
    {
        enabled = true;
        offsetX = -46.7f;
        offsetY = -12.0f;
        offsetZ = -20.0f;
        fov = 60.0f;
    }

    void Load()
    {
        if (loaded) {
            return;
        }

        loaded = true;
        SetDefaults();

        const auto path = GetINIPath();
        std::ifstream file(path);

        if (!file.is_open()) {
            logger::warn("[Settings] Could not open {}. Defaults will be used.", path.string());
            return;
        }

        std::string section;
        std::string line;

        while (std::getline(file, line)) {
            const auto comment = line.find_first_of(";#");
            if (comment != std::string::npos) {
                line = line.substr(0, comment);
            }

            line = Trim(std::move(line));
            if (line.empty()) {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                continue;
            }

            const auto equals = line.find('=');
            if (equals == std::string::npos) {
                continue;
            }

            ApplyValue(section, line.substr(0, equals), line.substr(equals + 1));
        }

        logger::info(
            "[Settings] enabled={} offsetX={} offsetY={} offsetZ={} fov={}",
            enabled,
            offsetX,
            offsetY,
            offsetZ,
            fov);
    }

    void Save()
    {
        const auto path = GetINIPath();
        std::filesystem::create_directories(path.parent_path());

        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open()) {
            logger::warn("[Settings] Could not write {}.", path.string());
            return;
        }

        file << "[General]\n";
        file << "bEnable = " << (enabled ? 1 : 0) << "\n\n";
        file << "[Camera]\n";
        file << "fOffsetX = " << offsetX << "\n";
        file << "fOffsetY = " << offsetY << "\n";
        file << "fOffsetZ = " << offsetZ << "\n";
        file << "fFOV = " << fov << "\n";
    }

    bool IsWatchedMenu(const RE::BSFixedString& menuName)
    {
        return menuName == RE::InventoryMenu::MENU_NAME || menuName == RE::MagicMenu::MENU_NAME;
    }
}
