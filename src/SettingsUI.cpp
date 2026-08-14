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

    bool captureListening = false;
    bool captureArmed = false;

    int PollCapturedInput()
    {
        struct KeyMap
        {
            int key;
            int dik;
        };
        static const KeyMap keys[] = {
            { ImGuiMCP::ImGuiKey_Escape, 1 }, { ImGuiMCP::ImGuiKey_1, 2 }, { ImGuiMCP::ImGuiKey_2, 3 }, { ImGuiMCP::ImGuiKey_3, 4 },
            { ImGuiMCP::ImGuiKey_4, 5 }, { ImGuiMCP::ImGuiKey_5, 6 }, { ImGuiMCP::ImGuiKey_6, 7 }, { ImGuiMCP::ImGuiKey_7, 8 },
            { ImGuiMCP::ImGuiKey_8, 9 }, { ImGuiMCP::ImGuiKey_9, 10 }, { ImGuiMCP::ImGuiKey_0, 11 }, { ImGuiMCP::ImGuiKey_Minus, 12 },
            { ImGuiMCP::ImGuiKey_Equal, 13 }, { ImGuiMCP::ImGuiKey_Backspace, 14 }, { ImGuiMCP::ImGuiKey_Tab, 15 },
            { ImGuiMCP::ImGuiKey_Q, 16 }, { ImGuiMCP::ImGuiKey_W, 17 }, { ImGuiMCP::ImGuiKey_E, 18 }, { ImGuiMCP::ImGuiKey_R, 19 },
            { ImGuiMCP::ImGuiKey_T, 20 }, { ImGuiMCP::ImGuiKey_Y, 21 }, { ImGuiMCP::ImGuiKey_U, 22 }, { ImGuiMCP::ImGuiKey_I, 23 },
            { ImGuiMCP::ImGuiKey_O, 24 }, { ImGuiMCP::ImGuiKey_P, 25 }, { ImGuiMCP::ImGuiKey_LeftBracket, 26 }, { ImGuiMCP::ImGuiKey_RightBracket, 27 },
            { ImGuiMCP::ImGuiKey_Enter, 28 }, { ImGuiMCP::ImGuiKey_LeftCtrl, 29 }, { ImGuiMCP::ImGuiKey_A, 30 }, { ImGuiMCP::ImGuiKey_S, 31 },
            { ImGuiMCP::ImGuiKey_D, 32 }, { ImGuiMCP::ImGuiKey_F, 33 }, { ImGuiMCP::ImGuiKey_G, 34 }, { ImGuiMCP::ImGuiKey_H, 35 },
            { ImGuiMCP::ImGuiKey_J, 36 }, { ImGuiMCP::ImGuiKey_K, 37 }, { ImGuiMCP::ImGuiKey_L, 38 }, { ImGuiMCP::ImGuiKey_Semicolon, 39 },
            { ImGuiMCP::ImGuiKey_Apostrophe, 40 }, { ImGuiMCP::ImGuiKey_GraveAccent, 41 }, { ImGuiMCP::ImGuiKey_LeftShift, 42 }, { ImGuiMCP::ImGuiKey_Backslash, 43 },
            { ImGuiMCP::ImGuiKey_Z, 44 }, { ImGuiMCP::ImGuiKey_X, 45 }, { ImGuiMCP::ImGuiKey_C, 46 }, { ImGuiMCP::ImGuiKey_V, 47 },
            { ImGuiMCP::ImGuiKey_B, 48 }, { ImGuiMCP::ImGuiKey_N, 49 }, { ImGuiMCP::ImGuiKey_M, 50 }, { ImGuiMCP::ImGuiKey_Comma, 51 },
            { ImGuiMCP::ImGuiKey_Period, 52 }, { ImGuiMCP::ImGuiKey_Slash, 53 }, { ImGuiMCP::ImGuiKey_RightShift, 54 }, { ImGuiMCP::ImGuiKey_KeypadMultiply, 55 },
            { ImGuiMCP::ImGuiKey_LeftAlt, 56 }, { ImGuiMCP::ImGuiKey_Space, 57 }, { ImGuiMCP::ImGuiKey_CapsLock, 58 },
            { ImGuiMCP::ImGuiKey_F1, 59 }, { ImGuiMCP::ImGuiKey_F2, 60 }, { ImGuiMCP::ImGuiKey_F3, 61 }, { ImGuiMCP::ImGuiKey_F4, 62 },
            { ImGuiMCP::ImGuiKey_F5, 63 }, { ImGuiMCP::ImGuiKey_F6, 64 }, { ImGuiMCP::ImGuiKey_F7, 65 }, { ImGuiMCP::ImGuiKey_F8, 66 },
            { ImGuiMCP::ImGuiKey_F9, 67 }, { ImGuiMCP::ImGuiKey_F10, 68 }, { ImGuiMCP::ImGuiKey_NumLock, 69 }, { ImGuiMCP::ImGuiKey_ScrollLock, 70 },
            { ImGuiMCP::ImGuiKey_Keypad7, 71 }, { ImGuiMCP::ImGuiKey_Keypad8, 72 }, { ImGuiMCP::ImGuiKey_Keypad9, 73 }, { ImGuiMCP::ImGuiKey_KeypadSubtract, 74 },
            { ImGuiMCP::ImGuiKey_Keypad4, 75 }, { ImGuiMCP::ImGuiKey_Keypad5, 76 }, { ImGuiMCP::ImGuiKey_Keypad6, 77 }, { ImGuiMCP::ImGuiKey_KeypadAdd, 78 },
            { ImGuiMCP::ImGuiKey_Keypad1, 79 }, { ImGuiMCP::ImGuiKey_Keypad2, 80 }, { ImGuiMCP::ImGuiKey_Keypad3, 81 }, { ImGuiMCP::ImGuiKey_Keypad0, 82 },
            { ImGuiMCP::ImGuiKey_KeypadDecimal, 83 }, { ImGuiMCP::ImGuiKey_F11, 87 }, { ImGuiMCP::ImGuiKey_F12, 88 }, { ImGuiMCP::ImGuiKey_KeypadEnter, 156 },
            { ImGuiMCP::ImGuiKey_RightCtrl, 157 }, { ImGuiMCP::ImGuiKey_KeypadDivide, 181 }, { ImGuiMCP::ImGuiKey_PrintScreen, 183 }, { ImGuiMCP::ImGuiKey_RightAlt, 184 },
            { ImGuiMCP::ImGuiKey_Pause, 197 }, { ImGuiMCP::ImGuiKey_Home, 199 }, { ImGuiMCP::ImGuiKey_UpArrow, 200 }, { ImGuiMCP::ImGuiKey_PageUp, 201 },
            { ImGuiMCP::ImGuiKey_LeftArrow, 203 }, { ImGuiMCP::ImGuiKey_RightArrow, 205 }, { ImGuiMCP::ImGuiKey_End, 207 }, { ImGuiMCP::ImGuiKey_DownArrow, 208 },
            { ImGuiMCP::ImGuiKey_PageDown, 209 }, { ImGuiMCP::ImGuiKey_Insert, 210 }, { ImGuiMCP::ImGuiKey_Delete, 211 },
            { ImGuiMCP::ImGuiKey_GamepadDpadUp, 266 }, { ImGuiMCP::ImGuiKey_GamepadDpadDown, 267 },
            { ImGuiMCP::ImGuiKey_GamepadDpadLeft, 268 }, { ImGuiMCP::ImGuiKey_GamepadDpadRight, 269 },
            { ImGuiMCP::ImGuiKey_GamepadStart, 270 }, { ImGuiMCP::ImGuiKey_GamepadBack, 271 },
            { ImGuiMCP::ImGuiKey_GamepadL3, 272 }, { ImGuiMCP::ImGuiKey_GamepadR3, 273 },
            { ImGuiMCP::ImGuiKey_GamepadL1, 274 }, { ImGuiMCP::ImGuiKey_GamepadR1, 275 },
            { ImGuiMCP::ImGuiKey_GamepadFaceDown, 276 }, { ImGuiMCP::ImGuiKey_GamepadFaceRight, 277 },
            { ImGuiMCP::ImGuiKey_GamepadFaceLeft, 278 }, { ImGuiMCP::ImGuiKey_GamepadFaceUp, 279 },
            { ImGuiMCP::ImGuiKey_GamepadL2, 280 }, { ImGuiMCP::ImGuiKey_GamepadR2, 281 }
        };

        for (const auto& m : keys) {
            if (ImGuiMCP::IsKeyPressed(static_cast<ImGuiMCP::ImGuiKey>(m.key), false)) {
                return m.dik;
            }
        }

        if (ImGuiMCP::IsMouseClicked(ImGuiMCP::ImGuiMouseButton_Left, false)) {
            return 256;
        }

        if (ImGuiMCP::IsMouseClicked(ImGuiMCP::ImGuiMouseButton_Right, false)) {
            return 257;
        }

        if (ImGuiMCP::IsMouseClicked(ImGuiMCP::ImGuiMouseButton_Middle, false)) {
            return 258;
        }

        if (ImGuiMCP::IsMouseClicked(3, false)) {
            return 259;
        }

        if (ImGuiMCP::IsMouseClicked(4, false)) {
            return 260;
        }

        return -1;
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

        bool barterEnabled = Settings::barterEnabled;
        if (ImGuiMCP::Checkbox("Barter Menu", &barterEnabled)) {
            Settings::barterEnabled = barterEnabled;
        }
        HelpMarker("Shows your character in the barter menu.");

        bool loggingEnabled = Settings::loggingEnabled;
        if (ImGuiMCP::Checkbox("Enable Logging", &loggingEnabled)) {
            Settings::loggingEnabled = loggingEnabled;
            Settings::ApplyLogLevel();
        }
        HelpMarker("Writes detailed activity to ShowPlayerInInventory.log. Leave off unless troubleshooting. Warnings and errors are always logged.");

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
        ImGuiMCP::SeparatorText("CONTROLS");

        ImGuiMCP::TextUnformatted("Rotate Button");
        ImGuiMCP::SameLine(130.0f);

        std::string keyName = SKSE::InputMap::GetKeyName(Settings::rotateKey);
        if (keyName.empty()) {
            keyName = std::to_string(Settings::rotateKey);
        }

        if (ImGuiMCP::Button((keyName + "###RotateKeyRemap").c_str())) {
            ImGuiMCP::OpenPopup("Bind Rotate Button");
            captureListening = true;
            captureArmed = false;
        }
        HelpMarker("Hold this button and drag the mouse, or use the right thumbstick on a controller, to rotate your character.");

        if (ImGuiMCP::BeginPopupModal("Bind Rotate Button")) {
            ImGuiMCP::TextUnformatted("Press any key, mouse button, or controller button to bind.");
            ImGuiMCP::TextUnformatted("(Esc binds Escape.)");

            if (captureListening) {
                if (!captureArmed) {
                    captureArmed = true;
                } else {
                    const int code = PollCapturedInput();
                    if (code >= 0) {
                        Settings::rotateKey = static_cast<std::uint32_t>(code);
                        captureListening = false;
                    }
                }
            }

            if (!captureListening) {
                ImGuiMCP::CloseCurrentPopup();
            }

            ImGuiMCP::EndPopup();
        }

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
