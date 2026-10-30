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

    bool BeginCheckboxes(const char* id)
    {
        return FUCK::BeginTable(id, 2, FUCK::TableFlags::kSizingStretchSame);
    }

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
                    Row("$SPII_ResetDoubleClick"_T, { KeyIcon(FUCK::Keys::kMouse_Middle) });
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
                    Row("$SPII_Reset"_T, { hold, KeyIcon(FUCK::Keys::kGP_R3) });
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

    // live in any item menu open underneath. whole numbers: the position dragged in the movie's pixels, over a whole screen either way,
    // and the size as a percentage, from 1
    void ItemSliders(const char* id, float& x, float& y, float& scale, const char* help)
    {
        constexpr float MOVIE_HEIGHT = 720.0f;  // the menus' height in the movie's pixels, its width going with the screen's shape
        constexpr float WIDE_SCREEN  = 16.0f / 9.0f;
        constexpr float DRAG_SPEED   = 1.0f;    // movie pixels for each pixel the mouse moves
        constexpr float LARGEST      = 2.0f;
        constexpr float PERCENT      = 100.0f;

        // a whole screen's width and height either way, so wherever the card or preview starts, any spot on the screen can be reached
        const ImVec2 screen = FUCK::GetDisplaySize();
        const float aspect  = screen.y > 0.0f ? screen.x / screen.y : WIDE_SCREEN;
        const float across  = MOVIE_HEIGHT * aspect;
        const float up      = MOVIE_HEIGHT;

        FUCK::PushID(id);
        bool changed = false;
        if (FUCK::DragFloat("$SPII_SliderX"_T, &x, DRAG_SPEED, -across, across, "%.0f")) {
            x = std::clamp(x, -across, across);  // a typed-in value isn't held to the range
            changed = true;
        }
        FUCK::SetTooltip(help);
        if (FUCK::DragFloat("$SPII_SliderZ"_T, &y, DRAG_SPEED, -up, up, "%.0f")) {
            y = std::clamp(y, -up, up);
            changed = true;
        }
        FUCK::SetTooltip(help);
        if (FUCK::ScaledSliderFloat("$SPII_SliderScale"_T, &scale, Settings::MIN_ITEM_SCALE, LARGEST, PERCENT)) {
            scale = std::max(scale, Settings::MIN_ITEM_SCALE);  // a typed-in value isn't held to the slider's range
            changed = true;
        }
        FUCK::SetTooltip(help);
        FUCK::PopID();
        if (changed) {
            Settings::Save();
        }
    }

    void BeginSection(const char* title)
    {
        FUCK::SeparatorText(title);
        FUCK::Indent();
    }

    void EndSection()
    {
        FUCK::Unindent();
    }

    void DrawSettings()
    {
        auto& values = Settings::values;

        const ImVec4 ON{ 0.2f, 0.8f, 0.2f, 1.0f };   // green
        const ImVec4 OFF{ 0.8f, 0.2f, 0.2f, 1.0f };  // red
        FUCK::PushStyleColor(ImGuiCol_Text, values.enabled ? ON : OFF);
        const bool toggled = FUCK::Checkbox("$SPII_Enable"_T, &values.enabled, false);
        FUCK::PopStyleColor();
        FUCK::SetTooltip("$SPII_EnableTT"_T);
        if (toggled) {
            Settings::Save();
            EventProcessor::GetSingleton()->ApplyLiveSettings();
        }

        FUCK::Spacing();

        auto* camera = MenuCamera::GetSingleton();
        if (BeginCheckboxes("##Settings")) {
            FUCK::TableNextColumn();
            BeginSection("$SPII_General"_T);
            Checkbox("$SPII_HideObstructions"_T,     values.hideObstructions, "$SPII_HideObstructionsTT"_T);
            if (Checkbox("$SPII_Animate"_T,          values.animateCharacter, "$SPII_AnimateTT"_T)) {
                camera->ApplyAnimation();
            }
            Checkbox("$SPII_SneakVignette"_T,        values.sneakVignette,    "$SPII_SneakVignetteTT"_T);
            // dragged, in half the screen's height, either way up to 4 of them. a mouse pixel moves it about a pixel and a half at 1440p
            constexpr float FRAME_SHIFT_RANGE = 4.0f;
            constexpr float FRAME_SHIFT_SPEED = 0.002f;
            if (FUCK::DragFloat("$SPII_FrameShift"_T, &values.frameShift, FRAME_SHIFT_SPEED, -FRAME_SHIFT_RANGE, FRAME_SHIFT_RANGE, "%.3f")) {
                values.frameShift = std::clamp(values.frameShift, -FRAME_SHIFT_RANGE, FRAME_SHIFT_RANGE);
                Settings::Save();
                camera->ApplyFraming();
            }
            FUCK::SetTooltip("$SPII_FrameShiftTT"_T);
            EndSection();

            FUCK::TableNextColumn();
            BeginSection("$SPII_Camera"_T);
            Checkbox("$SPII_UpperBody"_T,      values.upperBody,      "$SPII_UpperBodyTT"_T);
            Checkbox("$SPII_TurnCharacter"_T,  values.turnCharacter,  "$SPII_TurnCharacterTT"_T);
            Checkbox("$SPII_RememberCamera"_T, values.rememberCamera, "$SPII_RememberCameraTT"_T);

            FUCK::Indent();
            FUCK::BeginDisabled(!values.rememberCamera);
            Checkbox("$SPII_RememberFacing"_T, values.rememberFacing, "$SPII_RememberFacingTT"_T);
            FUCK::EndDisabled();
            FUCK::Unindent();
            EndSection();

            FUCK::EndTable();
        }

        FUCK::Spacing();

        if (BeginCheckboxes("##Controls")) {
            FUCK::TableNextColumn();
            BeginSection("$SPII_Controls"_T);
            Checkbox("$SPII_CameraControls"_T, values.cameraControls, "$SPII_CameraControlsTT"_T);
            if (FUCK::Button("$SPII_ShowControls"_T)) {
                SettingsUI::ShowControls();
            }
            FUCK::SetTooltip("$SPII_ShowControlsTT"_T);
            if (values.cameraControls) {
                FUCK::SameLine();
                FUCK::DrawCameraHelpMarker(HELP_FLAGS);
            }
            EndSection();

            BeginSection("$SPII_Items"_T);
            Checkbox("$SPII_AutoAdjust"_T, values.autoAdjust, "$SPII_AutoAdjustTT"_T);
            static const bool apparelPreview = GetModuleHandleW(L"ApparelPreview.dll") != nullptr;
            if (apparelPreview) {
                Checkbox("$SPII_HideApparelPreviewItems"_T, values.hideAppPrevItems, "$SPII_HideApparelPreviewItemsTT"_T);
            }
            // set by Auto-Adjustment while it's on
            if (FUCK::CollapsingHeader("$SPII_ItemPreview"_T)) {
                FUCK::Indent();
                FUCK::BeginDisabled(values.autoAdjust);
                ItemSliders("ItemPreview", values.itemPreviewX, values.itemPreviewY, values.itemPreviewScale, "$SPII_ItemPreviewTT"_T);
                FUCK::EndDisabled();
                FUCK::Unindent();
            }
            if (FUCK::CollapsingHeader("$SPII_ItemCard"_T)) {
                FUCK::Indent();
                FUCK::BeginDisabled(values.autoAdjust);
                ItemSliders("ItemCard", values.itemCardX, values.itemCardY, values.itemCardScale, "$SPII_ItemCardTT"_T);
                FUCK::EndDisabled();
                FUCK::Unindent();
            }
            EndSection();

            FUCK::TableNextColumn();
            BeginSection("$SPII_Adjust"_T);
            if (FUCK::CollapsingHeader("$SPII_Position"_T)) {
                FUCK::Indent();
                if (!camera->DrawPositionSliders()) {
                    FUCK::TextDisabledWrapped("%s", "$SPII_SlidersClosed"_T);
                }
                FUCK::Unindent();
            }
            if (FUCK::CollapsingHeader("$SPII_Advanced"_T)) {
                FUCK::Indent();
                FUCK::DrawCameraSettings(FUCK::CameraSettingsFlags::kSpeeds);
                FUCK::Unindent();
            }
            EndSection();

            BeginSection("$SPII_ResetSection"_T);
            FUCK::BeginDisabled(!camera->HasCamera());
            if (FUCK::Button("$SPII_ResetCamera"_T)) {
                camera->ResetCamera();
            }
            FUCK::EndDisabled();
            FUCK::SetTooltip("$SPII_ResetCameraTT"_T);

            FUCK::SameLine();
            if (FUCK::Button("$SPII_ResetDefaults"_T)) {
                Settings::SetDefaults();
                Settings::Save();
                EventProcessor::GetSingleton()->ApplyLiveSettings();
                camera->ApplyAnimation();
                camera->ApplyFraming();
            }
            FUCK::SetTooltip("$SPII_ResetDefaultsTT"_T);
            EndSection();

            FUCK::EndTable();
        }
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