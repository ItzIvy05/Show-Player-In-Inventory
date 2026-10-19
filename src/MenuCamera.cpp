#include "MenuCamera.h"
#include "SettingsUI.h"

// windows.h macro
#undef GetObject

namespace
{
    using UEFlag = RE::ControlMap::UEFlag;

    // pov switch and wheel zoom would move the game's own camera, and movement and looking keep the player still while time runs (skyrim souls)
    constexpr std::array CONTROLS{ UEFlag::kPOVSwitch, UEFlag::kWheelZoom, UEFlag::kMovement, UEFlag::kLooking };

    // the body framed from the top of the screen nearly to the bottom, as -1 to 1 from the bottom up
    constexpr float FRAMING_TOP = 1.0f;
    constexpr float FRAMING_BOTTOM = -0.9f;

    // where the item list ends when it can't be read, as a share of the screen's width
    constexpr float DEFAULT_LIST_EDGE = 0.45f;

    // where skyui's list panel comes to rest in its menus once it has slid in (InventoryLists.swf: from -100 to 25 as the menu opens)
    constexpr float PANEL_REST_X = 25.0f;

    // how far the position sliders go, in game units
    constexpr float SLIDER_RANGE = 200.0f;

    // the field of view slider's range, in degrees
    constexpr float FOV_MIN = 20.0f;
    constexpr float FOV_MAX = 120.0f;

    // FUCK-RACE's prompt lock, plus quantity and book menus
    bool IsPromptOpen()
    {
        auto* ui = RE::UI::GetSingleton();
        return ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME) || ui->IsMenuOpen(RE::InterfaceStrings::GetSingleton()->quantityMenu) || ui->IsMenuOpen(RE::BookMenu::MENU_NAME);
    }

    template <class T>
    bool IsCursorOverList(RE::UI* ui)
    {
        const auto menu = ui->GetMenu<T>();
        if (!menu || !menu->uiMovie || !menu->GetRuntimeData().itemList) {
            return false;
        }

        // menu alpha 0 means the list is hidden, so it never blocks the turn
        RE::GFxValue alpha;
        if (menu->uiMovie->GetVariable(&alpha, "_root._alpha") && alpha.IsNumber() && alpha.GetNumber() == 0.0) {
            return false;
        }

        RE::GFxValue x;
        RE::GFxValue y;
        menu->uiMovie->GetVariable(&x, "_root._xmouse");
        menu->uiMovie->GetVariable(&y, "_root._ymouse");

        const RE::GFxValue args[3]{ x, y, RE::GFxValue(false) };
        RE::GFxValue hit;
        return menu->GetRuntimeData().itemList->root.Invoke("hitTest", &hit, args, 3) && hit.GetBool();
    }

    // the item list's right edge where it comes to rest, as a share of the screen's width. skyui slides the list's panel in as the menu opens,
    // so the edge is moved by however far the panel still has to go
    template <class T>
    std::optional<float> GetMenuListEdge(RE::UI* ui)
    {
        const auto menu = ui->GetMenu<T>();
        if (!menu || !menu->uiMovie || !menu->GetRuntimeData().itemList) {
            return std::nullopt;
        }

        auto& list = menu->GetRuntimeData().itemList->root;

        RE::GFxValue root;
        RE::GFxValue bounds;
        RE::GFxValue xMax;
        if (!menu->uiMovie->GetVariable(&root, "_root")) {
            return std::nullopt;
        }

        list.Invoke("getBounds", &bounds, &root, 1);
        if (!bounds.IsObject() || !bounds.GetMember("xMax", &xMax) || !xMax.IsNumber()) {
            return std::nullopt;
        }

        float stageEdge = static_cast<float>(xMax.GetNumber());

        RE::GFxValue panel;
        RE::GFxValue panelName;
        RE::GFxValue panelX;
        if (list.GetMember("_parent", &panel) && panel.IsDisplayObject() && panel.GetMember("_name", &panelName) && panelName.IsString() &&
            std::string_view(panelName.GetString()) == "panelContainer" && panel.GetMember("_x", &panelX) && panelX.IsNumber()) {
            stageEdge += PANEL_REST_X - static_cast<float>(panelX.GetNumber());
        }

        // as long as it's somewhere sensible, as a theme may lay it out differently
        const float width = FUCK::GetDisplaySize().x;
        const float edge = FUCK::TranslateScaleformToScreen(stageEdge, 0.0f).x;
        if (width <= 0.0f || edge < width * 0.15f || edge > width * 0.6f) {
            return std::nullopt;
        }

        return edge / width;
    }

    // the left edge of the space for the player, right of the item list
    float GetListEdge()
    {
        auto* ui = RE::UI::GetSingleton();
        return GetMenuListEdge<RE::InventoryMenu>(ui)
            .or_else([&] { return GetMenuListEdge<RE::MagicMenu>(ui); })
            .or_else([&] { return GetMenuListEdge<RE::BarterMenu>(ui); })
            .or_else([&] { return GetMenuListEdge<RE::ContainerMenu>(ui); })
            .value_or(DEFAULT_LIST_EDGE);
    }

    // the body framed halfway between the item list's edge and the screen's right edge, as FUCK-RACE's Quick Zoom frames right of its slider list
    FUCK::CameraScreenRect GetFraming(float listEdge)
    {
        // -1 to 1 across the screen, so halfway to the right edge (1) is the average
        const float leftX = listEdge * 2.0f - 1.0f;
        return { (leftX + 1.0f) * 0.5f, (FRAMING_TOP + FRAMING_BOTTOM) * 0.5f, (FRAMING_TOP - FRAMING_BOTTOM) * 0.5f };
    }

    bool IsCursorOverList()
    {
        auto* ui = RE::UI::GetSingleton();
        return IsCursorOverList<RE::InventoryMenu>(ui) || IsCursorOverList<RE::MagicMenu>(ui) || IsCursorOverList<RE::BarterMenu>(ui) || IsCursorOverList<RE::ContainerMenu>(ui);
    }

    void PerformAction(RE::Actor* actor, RE::DEFAULT_OBJECT id)
    {
        std::unique_ptr<RE::TESActionData> data{ RE::TESActionData::Create() };
        data->source = RE::NiPointer<RE::TESObjectREFR>(actor);
        data->action = RE::BGSDefaultObjectManager::GetSingleton()->GetObject<RE::BGSAction>(id);
        data->Process();
    }

    // turns the third-person camera round the player to their front, so they're seen face on without turning. FUCK's base holds the camera from there
    void MoveCameraToFront(RE::PlayerCharacter* player, RE::PlayerCamera* camera)
    {
        auto* root = camera->cameraRoot.get();
        if (!root) {
            return;
        }

        // headings as the game measures them, from +y towards +x. the camera looks down its own y axis
        auto& rotate = root->local.rotate;
        const float cameraHeading = std::atan2(rotate.entry[0][1], rotate.entry[1][1]);
        const float turn = player->GetAngleZ() + RE::NI_PI - cameraHeading;

        // turns a direction's heading by the angle above, leaving height alone
        const float c = std::cos(turn);
        const float s = std::sin(turn);
        const auto turned = [&](const RE::NiPoint3& v) { return RE::NiPoint3{ v.x * c + v.y * s, v.y * c - v.x * s, v.z }; };

        const RE::NiPoint3 pivot = player->GetPosition();
        const RE::NiPoint3 offset = turned(root->local.translate - pivot);
        root->local.translate = { pivot.x + offset.x, pivot.y + offset.y, root->local.translate.z };

        // each axis is a column
        for (int column = 0; column < 3; ++column) {
            const auto axis = turned({ rotate.entry[0][column], rotate.entry[1][column], rotate.entry[2][column] });
            rotate.entry[0][column] = axis.x;
            rotate.entry[1][column] = axis.y;
            rotate.entry[2][column] = axis.z;
        }

        RE::NiUpdateData data{};
        root->Update(data);
    }

    template <class T>
    struct PerspectiveHook
    {
        static void Update(T* a_this, RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
        {
            _Update(a_this, a_nextState);
            MenuCamera::GetSingleton()->OnPerspectiveUpdate(std::is_same_v<T, RE::FirstPersonState>);
        }

        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
            _Update = vtbl.write_vfunc(0x3, Update);
        }

        static inline REL::Relocation<decltype(Update)> _Update;
    };

    // FUCK-RACE's RaceSexMenu hook: runs every frame, even while the game is paused
    template <class T>
    struct AdvanceMovieHook
    {
        static void AdvanceMovie(T* a_this, float a_interval, std::uint32_t a_currentTime)
        {
            _AdvanceMovie(a_this, a_interval, a_currentTime);
            MenuCamera::GetSingleton()->Update(a_interval);
        }

        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
            _AdvanceMovie = vtbl.write_vfunc(0x5, AdvanceMovie);
        }

        static inline REL::Relocation<decltype(AdvanceMovie)> _AdvanceMovie;
    };

    // thank you myztikrice, from Show Player in Menus
    // replaces the item preview load, so the preview stays empty while the player is shown. installed only once the option is turned on
    struct LoadInventoryItemHook
    {
        static void LoadInventoryItem(RE::Inventory3DManager* a_this, RE::InventoryEntryData* a_entry)
        {
            auto* object = a_entry ? a_entry->GetObject() : nullptr;
            if (!object || (Settings::values.hideItemPreviews && MenuCamera::GetSingleton()->IsActive())) {
                a_this->UnloadInventoryItem();
                return;
            }

            auto* extra = a_entry->extraLists && !a_entry->extraLists->empty() ? a_entry->extraLists->front() : nullptr;
            a_this->LoadInventoryItem(object, extra);
        }

        static void Install()
        {
            static bool installed = false;
            if (installed) {
                return;
            }

            REL::Relocation<std::uintptr_t> target{ REL::VariantID(50884, 51757, 50884) };
            SKSE::GetTrampoline().write_branch<5>(target.address(), LoadInventoryItem);
            installed = true;
            logger::info("[MenuCamera] Installed item preview hook.");
        }
    };
}

void MenuCamera::InstallHooks()
{
    PerspectiveHook<RE::FirstPersonState>::Install();
    PerspectiveHook<RE::ThirdPersonState>::Install();
    AdvanceMovieHook<RE::InventoryMenu>::Install();
    AdvanceMovieHook<RE::MagicMenu>::Install();
    AdvanceMovieHook<RE::BarterMenu>::Install();
    AdvanceMovieHook<RE::ContainerMenu>::Install();
    ApplyItemPreviews();

    logger::info("[MenuCamera] Installed camera state and menu hooks.");
}

// clears a preview already showing. turned off, previews return from the next item selected
void MenuCamera::ApplyItemPreviews()
{
    if (!Settings::values.hideItemPreviews) {
        return;
    }

    LoadInventoryItemHook::Install();
    if (GetSingleton()->IsActive()) {
        RE::Inventory3DManager::GetSingleton()->UnloadInventoryItem();
    }
}

void MenuCamera::SetCacheFrozen(bool frozen)
{
    cacheFrozen = frozen;
}

bool MenuCamera::IsActive() const
{
    return active;
}

// false while another plugin's camera has ours paused
bool MenuCamera::HasCamera()
{
    return active && FUCK::IsCameraSessionActive(this);
}

void MenuCamera::OnPerspectiveUpdate(bool firstPerson)
{
    if (!active && !cacheFrozen) {
        cachedFirstPerson = firstPerson;
    }
}

void MenuCamera::Start()
{
    if (active) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    // sitting, sleeping and riding have their own cameras, and the free camera isn't the player's, so switching to third person would break them
    const bool sitSleep = player->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal;
    const bool ownCamera = camera->IsInFreeCameraMode() || camera->currentState->id == RE::CameraState::kFurniture;
    if (player->IsInCombat() || player->IsOnMount() || sitSleep || ownCamera) {
        logger::info("[MenuCamera] Skipped start. Player is in combat, on horseback, sitting or sleeping, or in the free camera.");
        return;
    }

    playerAngleX = player->GetAngleX();
    playerAngleZ = player->GetAngleZ();
    allowAutoVanityMode = camera->allowAutoVanityMode;

    // the player faces the camera, so their head would turn round to look where it looks
    player->GetGraphVariableBool("IsNPC", headtrackingEnabled);
    player->GetGraphVariableBool("bHeadTrackSpine", headTrackSpineEnabled);
    player->GetGraphVariableBool("bUseEyeTracking", eyeTrackingEnabled);
    player->SetGraphVariableBool("IsNPC", false);
    player->SetGraphVariableBool("bHeadTrackSpine", false);
    player->SetGraphVariableBool("bUseEyeTracking", false);

    if (auto* process = player->GetActorRuntimeData().currentProcess) {
        process->ClearActionHeadtrackTarget(true);

        // zero the speed and play the game's own stop
        auto& speed = process->middleHigh->currentMovementSpeed;
        if (speed > 0.0f) {
            speed = 0.0f;
            RE::PlayerControls::GetSingleton()->data.autoMove = false;
            if (player->AsActorState()->IsSprinting()) {
                PerformAction(player, RE::DEFAULT_OBJECT::kActionSprintStop);
            }
            PerformAction(player, RE::DEFAULT_OBJECT::kActionMoveStop);
        }
    }

    auto* controlMap = RE::ControlMap::GetSingleton();
    for (const auto flag : CONTROLS) {
        if (controlMap->AreControlsEnabled(flag)) {
            disabledControls.set(flag);
        }
    }

    if (disabledControls) {
        controlMap->ToggleControls(disabledControls.get(), false, false);
    }

    camera->allowAutoVanityMode = false;

    // EnterThirdPerson minus its zoom reset, fov read after the tab camera restores it
    if (camera->currentState->id < RE::CameraState::kThirdPerson) {
        camera->SetState(camera->cameraStates[RE::CameraState::kThirdPerson].get());
        camera->Update();
    }

    worldFOV = camera->worldFOV;
    player->data.angle.x = 0.1f;
    player->Update3DPosition(true);

    // either the player turns to face the camera, or the camera comes round to face them
    if (!Settings::values.turnCharacter) {
        MoveCameraToFront(player, camera);
    }

    // FUCK's camera goes on last, once the camera above is ready.
    // resumable, so another plugin's camera pauses this one rather than ending it
    FUCK::CameraSessionOptions options;
    options.client = this;
    options.base = FUCK::CameraBase::kThirdPerson;
    // no keyboard camera keys, as the menu uses them for its list and tabs
    options.flags = FUCK::CameraSessionFlags::kResumable | FUCK::CameraSessionFlags::kNoKeyboard;
    if (Settings::values.turnCharacter) {
        options.flags |= FUCK::CameraSessionFlags::kFaceCamera;
    }

    // otherwise it starts from the default framing every time
    if (Settings::values.rememberCamera) {
        options.flags |= FUCK::CameraSessionFlags::kRememberCamera;
    }
    options.startFramed = true;
    options.framingRect = GetFraming(GetListEdge());

    if (!FUCK::BeginCameraSession(options)) {
        logger::warn("[MenuCamera] FUCK's camera wouldn't start.");
    }

    active = true;
    ApplyAnimation();

    if (!Settings::values.tutorialSeen) {
        SettingsUI::ShowControls();
    }

    logger::info("[MenuCamera] Started.");
}

void MenuCamera::Stop()
{
    if (!active) {
        return;
    }

    active = false;
    mouseOnList = false;
    SetFrozen(false);

    // FUCK's camera comes off first, then everything Start changed goes back
    FUCK::EndCameraSession(this);

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    if (disabledControls) {
        RE::ControlMap::GetSingleton()->ToggleControls(disabledControls.get(), true, false);
        disabledControls.reset();
    }

    if (cachedFirstPerson) {
        camera->ForceFirstPerson();
    }

    // FUCK turns the player by their heading to face the camera, so this puts them back to how they were facing
    player->data.angle.x = playerAngleX;
    player->data.angle.z = playerAngleZ;
    player->SetGraphVariableBool("IsNPC", headtrackingEnabled);
    player->SetGraphVariableBool("bHeadTrackSpine", headTrackSpineEnabled);
    player->SetGraphVariableBool("bUseEyeTracking", eyeTrackingEnabled);

    camera->allowAutoVanityMode = allowAutoVanityMode;
    camera->worldFOV = worldFOV;
    camera->Update();
    player->Update3DPosition(true);

    logger::info("[MenuCamera] Stopped and restored camera state.");
}

void MenuCamera::Update(float interval)
{
    if (!active) {
        return;
    }

    // FUCK-RACE's cap, so one long frame can't skip the idle ahead
    interval = std::min(interval, 0.1f);

    // a paused world skips the player's animation and scene update, so it's done here. with skyrim souls the game runs it itself
    if (!playerFrozen && RE::UI::GetSingleton()->GameIsPaused()) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        player->UpdateAnimation(interval);

        if (auto* root = player->Get3D()) {
            RE::NiUpdateData data{};
            root->Update(data);
        }
    }

    // after the menu, so the camera frames this frame's pose. with skyrim souls FUCK also keeps it on top of the game's own camera
    FUCK::UpdateCamera(this, interval);
}

// FUCK's offsets on the framing, as right, forward and up, and the field of view, for the settings' sliders
bool MenuCamera::DrawPositionSliders()
{
    FUCK::CameraOffsets offsets;
    if (!HasCamera() || !FUCK::GetCameraOffsets(this, offsets)) {
        return false;
    }

    bool changed = false;
    changed |= FUCK::SliderFloat("$SPII_SliderX"_T, &offsets.translate.x, -SLIDER_RANGE, SLIDER_RANGE, "%.0f");
    changed |= FUCK::SliderFloat("$SPII_SliderY"_T, &offsets.translate.y, SLIDER_RANGE, -SLIDER_RANGE, "%.0f");
    changed |= FUCK::SliderFloat("$SPII_SliderZ"_T, &offsets.translate.z, -SLIDER_RANGE, SLIDER_RANGE, "%.0f");

    // 0 until FUCK knows the game's field of view
    if (offsets.fov > 0.0f) {
        changed |= FUCK::SliderFloat("$SPII_SliderFOV"_T, &offsets.fov, FOV_MIN, FOV_MAX, "%.0f");
    }

    if (changed) {
        FUCK::SetCameraOffsets(this, offsets);
    }

    return true;
}

// freezes or thaws the player to match the setting, while a menu is open
void MenuCamera::ApplyAnimation()
{
    SetFrozen(active && !Settings::values.animateCharacter);
}

// FUCK holds the player still whether or not the game is paused (skyrim souls), and keeps their eyes open
void MenuCamera::SetFrozen(bool freeze)
{
    if (freeze != playerFrozen) {
        FUCK::SetActorFrozen(RE::PlayerCharacter::GetSingleton(), freeze);
        playerFrozen = freeze;
    }
}

// back to the starting framing, swooping there from where the camera is
void MenuCamera::ResetCamera()
{
    if (!HasCamera()) {
        return;
    }

    FUCK::ResetCamera(this);
    FUCK::SetCameraBodyFraming(this, true, GetFraming(GetListEdge()));
}

void MenuCamera::OnCameraUpdate(FUCK::CameraControlState& state, float)
{
    // prompts take the input, so the camera waits for them
    state.inputAllowed = !IsPromptOpen();

    // FUCK's own menu is driven by the controller while it's open
    state.gamepadBlocked = FUCK::IsMenuOpen();

    // off, only the settings' sliders move the camera, and turning stays
    state.cameraMoves = Settings::values.cameraControls;

    // a press on the item list is the list's, so it doesn't turn or move anything. decided as the buttons go down, and kept while they're held
    const bool held = FUCK::IsInputDown(FUCK::Keys::kMouse_Right) || FUCK::IsInputDown(FUCK::Keys::kMouse_Middle);
    if (!held) {
        mouseOnList = IsCursorOverList();
    }
    state.mouseLocked = mouseOnList;
}

void MenuCamera::OnCameraTakenOver()
{
    logger::info("[MenuCamera] Another plugin took the camera.");
}
