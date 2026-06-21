#include "SettingsUI.h"
#include "EventProcessor.h"
#include "Settings.h"

namespace
{
    void ApplyCamera()
    {
        EventProcessor::GetSingleton().ApplyLiveSettings();
    }

    void HelpMarker(const char* text)
    {
        ImGuiMCP::SameLine();
        ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4{0.55f, 0.55f, 0.55f, 1.0f});
        ImGuiMCP::TextUnformatted("(?)");
        ImGuiMCP::PopStyleColor();

        if (ImGuiMCP::IsItemHovered()) {
            ImGuiMCP::BeginTooltip();
            ImGuiMCP::PushTextWrapPos(ImGuiMCP::GetFontSize() * 28.0f);
            ImGuiMCP::TextUnformatted(text);
            ImGuiMCP::PopTextWrapPos();
            ImGuiMCP::EndTooltip();
        }
    }

    bool Slider(const char* label, float& value, float min, float max, const char* format)
    {
        ImGuiMCP::TextUnformatted(label);
        ImGuiMCP::SameLine(130.0f);
        ImGuiMCP::SetNextItemWidth(260.0f);
        return ImGuiMCP::SliderFloat((std::string("##") + label).c_str(), &value, min, max, format);
    }
}

namespace SettingsUI
{
    void Register()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::info("[IvyShowPlayerInMenus] SKSE Menu Framework not installed. Settings menu skipped.");
            return;
        }

        SKSEMenuFramework::SetSection("Show Player In Inventory");
        SKSEMenuFramework::AddSectionItem("Settings", SettingsUI::Render);
        logger::info("[IvyShowPlayerInMenus] Registered SKSE Menu Framework settings.");
    }

    void __stdcall Render()
    {
        bool cameraChanged = false;

        ImGuiMCP::SetWindowFontScale(0.93f);
        ImGuiMCP::SeparatorText("GENERAL");

        bool enabled = Settings::enabled;
        if (ImGuiMCP::Checkbox("Enable", &enabled)) {
            Settings::enabled = enabled;
            cameraChanged = true;
        }
        HelpMarker("Toggles Show Player In Inventory ON and OFF.");

        ImGuiMCP::Spacing();
        ImGuiMCP::SeparatorText("CAMERA");

        if (Slider("Offset X", Settings::offsetX, -300.0f, 300.0f, "%.1f")) {
            cameraChanged = true;
        }
        HelpMarker("Moves the Camera left or right.");

        if (Slider("Offset Y", Settings::offsetY, -300.0f, 300.0f, "%.1f")) {
            cameraChanged = true;
        }
        HelpMarker("Moves the camera forward or backward around the character.");

        if (Slider("Offset Z", Settings::offsetZ, -150.0f, 150.0f, "%.1f")) {
            cameraChanged = true;
        }
        HelpMarker("Moves the Camera up or down.");

        if (Slider("FOV", Settings::fov, 20.0f, 100.0f, "%.1f")) {
            cameraChanged = true;
        }
        HelpMarker("Adjust Camera Field of View.");

        ImGuiMCP::Spacing();
        ImGuiMCP::SeparatorText("INI FILE");

        if (ImGuiMCP::Button("Save Settings")) {
            Settings::Save();
        }
        HelpMarker("Writes the current values to Data\\SKSE\\Plugins\\ShowPlayerInInventory.ini.");

        ImGuiMCP::SameLine(0.0f, 14.0f);

        if (ImGuiMCP::Button("Reset Defaults")) {
            Settings::SetDefaults();
            cameraChanged = true;
        }
        HelpMarker("Restores the built-in defaults. Use Save Settings if you want to write them to the INI.");

        ImGuiMCP::SetWindowFontScale(1.0f);

        if (cameraChanged) {
            ApplyCamera();
        }
    }
}
