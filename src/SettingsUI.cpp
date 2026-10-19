#include "SettingsUI.h"
#include "EventProcessor.h"
#include "MenuCamera.h"
#include "Settings.h"

#include <IconsFontAwesome6.h>

namespace
{
    constexpr float POPUP_WIDTH = 560.0f;

    constexpr std::uint32_t LEFT_CTRL = std::to_underlying(RE::BSWin32KeyboardDevice::Key::kLeftControl);
    constexpr std::uint32_t LEFT_SHIFT = std::to_underlying(RE::BSWin32KeyboardDevice::Key::kLeftShift);
    // the menus have no keyboard camera controls, so FUCK's help leaves them out
    constexpr auto HELP_FLAGS = FUCK::CameraSessionFlags::kNoKeyboard;
    constexpr std::array<std::uint32_t, 4> CLOSE_KEYS{ std::to_underlying(RE::BSWin32KeyboardDevice::Key::kEnter), std::to_underlying(RE::BSWin32KeyboardDevice::Key::kEscape), FUCK::Keys::kGP_A, FUCK::Keys::kGP_B };

    bool Checkbox(const char* label, bool& setting, const char* help)
    {
        const bool changed = FUCK::Checkbox(label, &setting);
        if (changed) {
            Settings::Save();
        }

        FUCK::SetTooltip(help);
        return changed;
    }

    struct Icon
    {
        ImTextureID texture = 0;
        ImVec2 size{};
        const char* name = "";
    };

    Icon KeyIcon(std::uint32_t key)
    {
        Icon icon;
        icon.texture = FUCK::GetIconForKey(key, &icon.size);
        if (!icon.texture) {
            icon.name = FUCK::GetKeyName(key);
        }

        return icon;
    }

    void DrawIcon(const Icon& icon)
    {
        if (!icon.texture) {
            FUCK::AlignTextToFramePadding();
            FUCK::TextUnformatted(icon.name);
            return;
        }

        FUCK::DrawImage(icon.texture, icon.size);
    }

    // the icons with a plus between each, the plus centred on the icons' height
    void Row(const char* action, std::initializer_list<Icon> icons)
    {
        FUCK::LeftLabel(action);

        float height = 0.0f;
        for (const auto& icon : icons) {
            height = std::max(height, icon.size.y);
        }

        const float top = FUCK::GetCursorPos().y;
        const float plusY = top + std::max(0.0f, (height - FUCK::GetTextLineHeight()) * 0.5f);
        for (const auto& icon : icons) {
            if (&icon != icons.begin()) {
                FUCK::SameLine();
                FUCK::SetCursorPosY(plusY);
                FUCK::TextDisabled("%s", ICON_FA_PLUS);
                FUCK::SameLine();
                FUCK::SetCursorPosY(top);
            }

            DrawIcon(icon);
        }
    }

    // FUCK-RACE's window pattern
    class ControlsWindow final : public FUCK::IWindow, public REX::Singleton<ControlsWindow>
    {
    public:
        const char* Id() const override
        {
            return "Controls";
        }

        const char* Title() const override
        {
            return "$SPII_Title"_T;
        }

        void Draw() override
        {
            const bool appearing = FUCK::IsWindowAppearing();

            // FUCK's window background follows the user's transparency, this box stays solid
            const ImVec2 pos = FUCK::GetWindowPos();
            const ImVec2 size = FUCK::GetWindowSize();
            const float border = FUCK::GetStyleVar(ImGuiStyleVar_WindowBorderSize);
            ImVec4 background = FUCK::GetStyleColorVec4(ImGuiCol_WindowBg);
            background.w = 1.0f;
            FUCK::DrawRectFilled(ImVec2(pos.x + border, pos.y + border), ImVec2(pos.x + size.x - border, pos.y + size.y - border), background, FUCK::GetStyleVar(ImGuiStyleVar_WindowRounding));
            lastPos = pos;
            lastSize = size;

            const bool gamepad = FUCK::GetInputDevice() == FUCK::InputDevice::kGamepad;
            const Icon dragIcon = KeyIcon(FUCK::Keys::kIcon_MouseMove);
            const Icon wheelIcon = KeyIcon(FUCK::Keys::kIcon_Wheel);

            FUCK::Dummy(ImVec2(FUCK::Scale(POPUP_WIDTH), 0.0f));
            FUCK::PushFontScaled(FUCK::GetFont(FUCK::Font::kLarge), 1.0f);
            FUCK::CenteredText("$SPII_ControlsTitle"_T);
            FUCK::PopFont();
            FUCK::Separator();

            // with camera controls off only turning is left
            const bool moves = Settings::values.cameraControls;

            if (FUCK::BeginTabBar("Controls")) {
                if (FUCK::BeginTabItem(FUCK::IconText(ICON_FA_COMPUTER_MOUSE, "$SPII_TabKBM"_T), appearing && !gamepad ? ImGuiTabItemFlags_SetSelected : 0)) {
                    Row("$SPII_Turn"_T, { KeyIcon(FUCK::Keys::kMouse_Right), dragIcon });
                    if (moves) {
                        Row("$SPII_Move"_T, { KeyIcon(FUCK::Keys::kMouse_Middle), dragIcon });
                        Row("$SPII_Tilt"_T, { KeyIcon(LEFT_CTRL), KeyIcon(FUCK::Keys::kMouse_Middle), dragIcon });
                        Row("$SPII_Zoom"_T, { KeyIcon(LEFT_CTRL), wheelIcon });
                        Row("$SPII_FOV"_T, { KeyIcon(LEFT_CTRL), KeyIcon(LEFT_SHIFT), wheelIcon });
                    }
                    FUCK::EndTabItem();
                }

                if (FUCK::BeginTabItem(FUCK::IconText(ICON_FA_GAMEPAD, "$SPII_TabGamepad"_T), appearing && gamepad ? ImGuiTabItemFlags_SetSelected : 0)) {
                    const Icon hold = KeyIcon(FUCK::Keys::kGP_RB);
                    Row("$SPII_Turn"_T, { hold, KeyIcon(FUCK::Keys::kGP_X), KeyIcon(FUCK::Keys::kGP_Y) });
                    if (moves) {
                        Row("$SPII_Move"_T, { hold, KeyIcon(FUCK::Keys::kGP_LSX) });
                        Row("$SPII_OrbitZoom"_T, { hold, KeyIcon(FUCK::Keys::kGP_RSX) });
                        Row("$SPII_FOV"_T, { hold, KeyIcon(FUCK::Keys::kIcon_Triggers) });
                    }
                    FUCK::EndTabItem();
                }

                FUCK::EndTabBar();
            }

            // the rest of FUCK's camera controls, orbit, roll and the rest
            if (moves) {
                FUCK::LeftLabel("$SPII_MoreControls"_T);
                FUCK::DrawCameraHelpMarker(HELP_FLAGS);
            }

            FUCK::Spacing();

            const char* ok = "$SPII_OK"_T;
            const float width = FUCK::CalcTextSize(ok).x + FUCK::GetStyleVarVec(ImGuiStyleVar_FramePadding).x * 2.0f;
            FUCK::SetCursorPosX((size.x - width) * 0.5f);
            if (FUCK::Button(ok)) {
                SetOpen(false);
            }

            FUCK::SetItemDefaultFocus();
        }

        bool IsOpen() const override
        {
            return visible;
        }

        void SetOpen(bool a_open) override
        {
            visible = a_open;
            if (!a_open && !Settings::values.tutorialSeen) {
                Settings::values.tutorialSeen = true;
                Settings::Save();
            }
        }

        FUCK::WindowFlags GetFlags() const override
        {
            auto flags = FUCK::WindowFlags::kNoDecoration | FUCK::WindowFlags::kAutoResize | FUCK::WindowFlags::kNoResize | FUCK::WindowFlags::kCloseOnEsc;

            // FUCK-RACE's hover check, input only goes to the box while the cursor is on it
            const ImVec2 mouse = FUCK::GetMousePos();
            const bool hovered = mouse.x >= lastPos.x && mouse.x <= lastPos.x + lastSize.x && mouse.y >= lastPos.y && mouse.y <= lastPos.y + lastSize.y;
            if (!hovered && !FUCK::IsAnyItemActive()) {
                flags = flags | FUCK::WindowFlags::kPassInputToGame;
            }

            return flags;
        }

        ImVec2 GetDefaultPos() const override
        {
            return ImVec2((FUCK::GetDisplaySize().x - FUCK::Scale(POPUP_WIDTH)) * 0.5f, FUCK::Scale(100.0f));
        }

        bool OnAsyncInput(const void* a_events) override
        {
            for (const auto key : CLOSE_KEYS) {
                if (FUCK::IsInputPressed(a_events, key)) {
                    SetOpen(false);
                    return true;
                }
            }

            return false;
        }

    private:
        ImVec2 lastPos{};
        ImVec2 lastSize{};
        std::atomic_bool visible = false;
    };

    // FUCK calls OnClose when the page is left or the menu shuts, the controls box goes with it
    class SettingsTool final : public FUCK::SimpleTool
    {
    public:
        using SimpleTool::SimpleTool;

        void OnClose() override
        {
            ControlsWindow::GetSingleton()->SetOpen(false);
        }
    };

    void DrawSettings()
    {
        auto& values = Settings::values;

        FUCK::SeparatorText("$SPII_General"_T);

        if (Checkbox("$SPII_Enable"_T, values.enabled, "$SPII_EnableTT"_T)) {
            EventProcessor::GetSingleton()->ApplyLiveSettings();
        }

        Checkbox("$SPII_MagicMenu"_T, values.magicMenu, "$SPII_MagicMenuTT"_T);
        Checkbox("$SPII_BarterMenu"_T, values.barterMenu, "$SPII_BarterMenuTT"_T);
        Checkbox("$SPII_ContainerMenu"_T, values.containerMenu, "$SPII_ContainerMenuTT"_T);

        if (Checkbox("$SPII_HideItemPreviews"_T, values.hideItemPreviews, "$SPII_HideItemPreviewsTT"_T)) {
            MenuCamera::ApplyItemPreviews();
        }

        if (Checkbox("$SPII_Animate"_T, values.animateCharacter, "$SPII_AnimateTT"_T)) {
            MenuCamera::GetSingleton()->ApplyAnimation();
        }


        FUCK::Spacing();
        FUCK::SeparatorText("$SPII_Camera"_T);

        Checkbox("$SPII_TurnCharacter"_T, values.turnCharacter, "$SPII_TurnCharacterTT"_T);
        Checkbox("$SPII_RememberCamera"_T, values.rememberCamera, "$SPII_RememberCameraTT"_T);
        Checkbox("$SPII_CameraControls"_T, values.cameraControls, "$SPII_CameraControlsTT"_T);

        auto* camera = MenuCamera::GetSingleton();
        if (FUCK::CollapsingHeader("$SPII_Position"_T) && !camera->DrawPositionSliders()) {
            FUCK::TextDisabled("%s", "$SPII_SlidersClosed"_T);
        }

        // the camera keeps where it was left, so there's only a way back to the start
        FUCK::BeginDisabled(!camera->HasCamera());
        if (FUCK::Button("$SPII_ResetCamera"_T)) {
            camera->ResetCamera();
        }
        FUCK::EndDisabled();
        FUCK::SetTooltip("$SPII_ResetCameraTT"_T);

        if (FUCK::CollapsingHeader("$SPII_Advanced"_T)) {
            FUCK::DrawCameraSettings(FUCK::CameraSettingsFlags::kSpeeds);
        }

        FUCK::Spacing();
        FUCK::SeparatorText("$SPII_Controls"_T);

        if (FUCK::Button("$SPII_ShowControls"_T)) {
            SettingsUI::ShowControls();
        }
        FUCK::SetTooltip("$SPII_ShowControlsTT"_T);
        if (values.cameraControls) {
            FUCK::SameLine();
            FUCK::DrawCameraHelpMarker(HELP_FLAGS);
        }

        FUCK::Spacing();

        if (FUCK::Button("$SPII_ResetDefaults"_T)) {
            Settings::SetDefaults();
            Settings::Save();
            EventProcessor::GetSingleton()->ApplyLiveSettings();
            MenuCamera::GetSingleton()->ApplyAnimation();
        }
        FUCK::SetTooltip("$SPII_ResetDefaultsTT"_T);
    }
}

namespace SettingsUI
{
    void Register()
    {
        // Connect() logs why it failed
        if (!FUCK::Connect(SKSE::GetPluginName().data())) {
            return;
        }

        // FUCK's ready-made page at the top level under the mod's name, made after Connect() so it's translated. kept for the session, as FUCK holds onto it
        static SettingsTool settings("$SPII_Title"_T, "", DrawSettings);
        FUCK::RegisterTool(&settings);
        FUCK::RegisterWindow(ControlsWindow::GetSingleton());
    }

    void ShowControls()
    {
        ControlsWindow::GetSingleton()->SetOpen(true);
    }
}
