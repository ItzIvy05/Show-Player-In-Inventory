#include "SettingsUI.h"
#include "EventProcessor.h"
#include "Settings.h"

#include <FUCK_API.h>

namespace
{
    constexpr std::uint32_t NO_KEY = SKSE::InputMap::kMaxMacros;
    constexpr auto BIND_POPUP = "Bind Rotate Button";

    std::atomic_bool listening = false;
    std::atomic<std::uint32_t> heldKey = NO_KEY;

    void Help(const char* text)
    {
        FUCK::SameLine();
        FUCK::HelpMarker(text);
    }

    bool Checkbox(const char* label, REX::INI::Bool<>& setting, const char* help)
    {
        bool value = setting.GetValue();
        const bool changed = FUCK::Checkbox(label, &value);
        setting.SetValue(value);
        Help(help);
        return changed;
    }

    bool Slider(const char* label, REX::INI::F32<>& setting, float min, float max, const char* help)
    {
        float value = setting.GetValue();
        const bool changed = FUCK::SliderFloat(label, &value, min, max, "%.1f");
        setting.SetValue(value);
        Help(help);
        return changed;
    }

    [[nodiscard]] bool IsBindable(std::uint32_t code)
    {
        return code < SKSE::InputMap::kMacro_MouseWheelOffset || (code >= SKSE::InputMap::kMacro_GamepadOffset && code < SKSE::InputMap::kMaxMacros);
    }

    class SettingsPage final : public FUCK::ITool, public REX::Singleton<SettingsPage>
    {
    public:
        const char* Name() const override
        {
            return "Settings";
        }

        const char* Group() const override
        {
            return "Show Player In Inventory";
        }

        void Draw() override
        {
            bool cameraChanged = false;

            FUCK::SeparatorText("GENERAL");

            cameraChanged |= Checkbox("Enable", Settings::enabled, "Toggles Show Player In Inventory ON and OFF.");
            Checkbox("Barter Menu", Settings::barterEnabled, "Shows your character in the barter menu.");

            if (Checkbox("Enable Logging", Settings::loggingEnabled, "Writes detailed activity to ShowPlayerInInventory.log. Leave off unless troubleshooting. Warnings and errors are always logged.")) {
                Settings::ApplyLogLevel();
            }

            FUCK::Spacing();
            FUCK::SeparatorText("CAMERA");

            cameraChanged |= Slider("Offset X", Settings::offsetX, -300.0f, 300.0f, "Moves the Camera left or right.");
            cameraChanged |= Slider("Offset Y", Settings::offsetY, -300.0f, 300.0f, "Moves the camera forward or backward around the character.");
            cameraChanged |= Slider("Offset Z", Settings::offsetZ, -150.0f, 150.0f, "Moves the Camera up or down.");
            cameraChanged |= Slider("FOV", Settings::fov, 20.0f, 100.0f, "Adjust Camera Field of View.");

            FUCK::Spacing();
            FUCK::SeparatorText("CONTROLS");

            std::string keyName = SKSE::InputMap::GetKeyName(Settings::rotateKey.GetValue());
            if (keyName.empty()) {
                keyName = std::to_string(Settings::rotateKey.GetValue());
            }

            FUCK::LeftLabel("Rotate Button");
            if (FUCK::Button((keyName + "###RotateKeyRemap").c_str())) {
                FUCK::OpenPopup(BIND_POPUP);
                listening = true;
            }
            Help("Hold this button and drag the mouse, or use the right thumbstick on a controller, to rotate your character.");

            if (FUCK::BeginPopupModal(BIND_POPUP)) {
                FUCK::TextUnformatted("Press any key, mouse button, or controller button to bind.");
                FUCK::TextUnformatted("(Esc binds Escape.)");

                if (!listening) {
                    FUCK::CloseCurrentPopup();
                }

                FUCK::EndPopup();
            }

            FUCK::Spacing();
            FUCK::SeparatorText("INI FILE");

            if (FUCK::Button("Save Settings")) {
                Settings::Save();
            }
            Help("Writes the current values to Data\\SKSE\\Plugins\\ShowPlayerInInventory.ini.");

            FUCK::SameLine(0.0f, 14.0f);

            if (FUCK::Button("Reset Defaults")) {
                Settings::SetDefaults();
                cameraChanged = true;
            }
            Help("Restores the built-in defaults. Use Save Settings if you want to write them to the INI.");

            if (cameraChanged) {
                EventProcessor::GetSingleton()->ApplyLiveSettings();
            }
        }

        void OnClose() override
        {
            listening = false;
            heldKey = NO_KEY;
        }

        bool OnAsyncInput(const void* a_events) override
        {
            if (!a_events || (!listening && heldKey == NO_KEY)) {
                return false;
            }

            for (auto* current = *static_cast<const RE::InputEvent* const*>(a_events); current; current = current->next) {
                const auto* button = current->AsButtonEvent();
                if (!button) {
                    continue;
                }

                const auto code = EventProcessor::GetKeyCode(button);
                if (button->IsUp() && code == heldKey) {
                    heldKey = NO_KEY;
                } else if (listening && button->IsDown() && IsBindable(code)) {
                    Settings::rotateKey.SetValue(code);
                    heldKey = code;
                    listening = false;
                }
            }

            return true;
        }
    };
}

namespace SettingsUI
{
    void Register()
    {
        if (!FUCK::Connect(SKSE::GetPluginName().data())) {
            logger::info("[IvyShowPlayerInMenus] FLICK not found. Settings menu skipped.");
            return;
        }

        FUCK::RegisterTool(SettingsPage::GetSingleton());
        logger::info("[IvyShowPlayerInMenus] Registered FLICK settings.");
    }
}
