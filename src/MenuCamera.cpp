#include "MenuCamera.h"
#include "Obstructions.h"
#include "SettingsUI.h"

// windows.h macro
#undef GetObject

namespace
{
    using UEFlag = RE::ControlMap::UEFlag;

    // the controls locked while someone is shown. pov switch and wheel zoom would move the game's camera, and with skyrim souls, movement and
    // looking would move the player. allow movement leaves movement on, but looking stays locked, as right-drag already turns the player
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

    // if the player is going faster than this as the menu opens, they count as moving and the view stays behind them. walking is around 80
    constexpr float MOVING_SPEED = 50.0f;

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

    // head tracking off through the game's own switch (what papyrus SetHeadTracking sets), with any target dropped. each frame, as a head
    // reloading, e.g. a hair with physics, can pick a target up again. the graph's bHeadTracking only reports what it's doing
    void HoldHeadStill(RE::Actor* actor)
    {
        actor->AsActorState()->actorState2.headTracking = false;

        auto* process = actor->GetActorRuntimeData().currentProcess;
        auto* high = process ? process->high : nullptr;
        if (!high) {
            return;
        }

        using HeadTrack = RE::HighProcessData::HEAD_TRACK_TYPE;
        for (std::uint32_t i = 0; i < HeadTrack::kTotal; ++i) {
            if (high->headTrackTarget[i]) {
                high->ClearHeadtrackTarget(static_cast<HeadTrack>(i), false);
            }
        }
    }

    // the face as the menu caught it. expression mods run on papyrus, which stops in a paused menu, so a face caught mid-blink or mid-expression
    // would hold for the whole menu. it eases to neutral with the eyes open instead, and goes back as it was after. thank you to Menu Studio again
    struct SavedFace
    {
        std::vector<float> expression;
        std::vector<float> modifier;
        std::vector<float> phoneme;
        bool exprOverride = false;
        bool saved = false;
    };
    SavedFace savedFace;

    void CopyOut(const RE::BSFaceGenKeyframeMultiple& keyframe, std::vector<float>& out)
    {
        if (keyframe.values && keyframe.count > 0) {
            out.assign(keyframe.values, keyframe.values + keyframe.count);
        } else {
            out.clear();
        }
    }

    void CopyIn(RE::BSFaceGenKeyframeMultiple& keyframe, const std::vector<float>& in)
    {
        if (keyframe.values && keyframe.count == in.size() && !in.empty()) {
            std::copy(in.begin(), in.end(), keyframe.values);
            keyframe.isUpdated = true;
        }
    }

    void OpenEyes(RE::BSFaceGenAnimationData* face)
    {
        using Modifier = RE::BSFaceGenKeyframeMultiple::Modifier;
        if (auto& modifier = face->modifierKeyFrame; modifier.values && modifier.count > Modifier::BlinkRight) {
            modifier.values[Modifier::BlinkLeft] = 0.0f;
            modifier.values[Modifier::BlinkRight] = 0.0f;
        }
    }

    void NeutralFace(RE::Actor* actor)
    {
        auto* face = actor->GetFaceGenAnimationData();
        if (!face) {
            return;
        }

        CopyOut(face->expressionKeyFrame, savedFace.expression);
        CopyOut(face->modifierKeyFrame, savedFace.modifier);
        CopyOut(face->phenomeKeyFrame, savedFace.phoneme);
        savedFace.exprOverride = face->exprOverride;
        savedFace.saved = true;

        // an expression mod's override would stop the reset, so it's lifted for the menu and handed back after
        face->ClearExpressionOverride();
        face->Reset(0.25f, true, true, false, false);

        // the eyelids open at once rather than easing, and the next blink waits a moment
        OpenEyes(face);
        face->eyesBlinkingTimer = 3.0f;
    }

    void RestoreFace(RE::Actor* actor)
    {
        if (!savedFace.saved) {
            return;
        }
        savedFace.saved = false;

        auto* face = actor->GetFaceGenAnimationData();
        if (!face) {
            return;
        }

        CopyIn(face->expressionKeyFrame, savedFace.expression);
        CopyIn(face->modifierKeyFrame, savedFace.modifier);
        CopyIn(face->phenomeKeyFrame, savedFace.phoneme);

        // never a saved blink, which would leave the eyes half shut until the next one
        OpenEyes(face);
        face->exprOverride = savedFace.exprOverride;
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

    // the game doesn't tell the player's animation graph about an equip while paused, so the old weapon's idle would stay until the menu closed.
    // it's let through while the player is shown, as their animation runs here. other actors are always told. found by Menu Studio
    struct EquipNotifyHook
    {
        // the player's check before telling the graph, which is false while paused. CommonLib declares it void
        static bool CanNotify(RE::PlayerCharacter* a_this)
        {
            auto* camera = MenuCamera::GetSingleton();
            return (camera->IsActive() && camera->GetSubject() == a_this) || _CanNotify(a_this);
        }

        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
            _CanNotify = vtbl.write_vfunc(0xB3, CanNotify);
        }

        static inline REL::Relocation<decltype(CanNotify)> _CanNotify;
    };

    // an equip (or a mod reacting to an item going on) sends the animation graph an event. while someone is shown in a paused menu it plays here,
    // then the game sends it again once the menu closes, so a repeat just after the menu is skipped. found by Inventory Reforged
    namespace Replays
    {
        constexpr auto WINDOW = std::chrono::milliseconds(1500);  // how long after the menu closes a repeat is caught
        constexpr std::size_t MOST = 32;

        struct Event
        {
            const RE::IAnimationGraphManagerHolder* holder;
            std::string name;
        };

        std::mutex lock;
        std::vector<Event> heard;
        std::chrono::steady_clock::time_point lastShown{};
        std::atomic<const RE::IAnimationGraphManagerHolder*> shown = nullptr;

        bool Notify(RE::IAnimationGraphManagerHolder* holder, const RE::BSFixedString& event, auto&& original)
        {
            const std::string_view name = event.c_str() ? event.c_str() : "";
            const bool listening = shown == holder && RE::UI::GetSingleton()->GameIsPaused();

            if (!name.empty() && !listening) {
                std::scoped_lock guard{ lock };
                const auto again = std::ranges::find_if(heard, [&](const Event& heardEvent) { return heardEvent.holder == holder && heardEvent.name == name; });
                if (again != heard.end() && std::chrono::steady_clock::now() < lastShown + WINDOW) {
                    heard.erase(again);
                    return true;
                }
            }

            const bool taken = original(holder, event);
            if (taken && !name.empty() && listening) {
                std::scoped_lock guard{ lock };
                if (heard.size() < MOST) {
                    heard.push_back({ holder, std::string(name) });
                }
            }
            return taken;
        }
    }

    // called each frame of the menu with whoever is shown while paused, or nullptr. a new showing starts with nothing heard
    void HoldReplayedEvents(RE::Actor* subject)
    {
        const RE::IAnimationGraphManagerHolder* holder = subject;
        std::scoped_lock guard{ Replays::lock };
        if (holder && holder != Replays::shown) {
            Replays::heard.clear();
        }
        if (holder) {
            Replays::lastShown = std::chrono::steady_clock::now();
        }
        Replays::shown = holder;
    }

    template <class T>
    struct AnimationEventHook
    {
        static bool Notify(RE::IAnimationGraphManagerHolder* a_this, const RE::BSFixedString& a_event)
        {
            return Replays::Notify(a_this, a_event, _Notify);
        }

        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[3] };
            _Notify = vtbl.write_vfunc(0x1, Notify);
        }

        static inline REL::Relocation<decltype(Notify)> _Notify;
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
    EquipNotifyHook::Install();
    AnimationEventHook<RE::PlayerCharacter>::Install();
    AnimationEventHook<RE::Character>::Install();
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

// the player, or the follower whose inventory is open
RE::Actor* MenuCamera::GetSubject() const
{
    if (const auto shown = follower.get()) {
        return shown.get();
    }
    return RE::PlayerCharacter::GetSingleton();
}

void MenuCamera::OnPerspectiveUpdate(bool firstPerson)
{
    if (!active && !cacheFrozen) {
        cachedFirstPerson = firstPerson;
    }
}

void MenuCamera::Start(std::string_view menuName)
{
    if (active) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    // combat, riding, sitting, and lying down only as their settings allow, and never while sitting down or getting up. a rider counts as
    // sitting to the game, so riding is told apart from a seat, and lying down is told from the pose, as a bed can report either state
    const auto sitSleep = player->AsActorState()->GetSitSleepState();
    const bool mounted = player->IsOnMount();
    const bool inFurniture = !mounted && (sitSleep == RE::SIT_SLEEP_STATE::kIsSitting || sitSleep == RE::SIT_SLEEP_STATE::kIsSleeping);
    const bool lying = inFurniture && FUCK::IsLyingDown(player);
    const bool sitting = inFurniture && !lying;
    const bool settled = sitSleep == RE::SIT_SLEEP_STATE::kNormal || sitSleep == RE::SIT_SLEEP_STATE::kIsSitting || sitSleep == RE::SIT_SLEEP_STATE::kIsSleeping;
    const bool seatAllowed = settled && (!sitting || Settings::values.showSitting) && (!lying || (Settings::values.showSleeping && Settings::CanLieDown()));
    const bool allowed = seatAllowed && (!mounted || Settings::values.showOnHorseback) && (!player->IsInCombat() || Settings::values.showInCombat);
    // the furniture and free cameras aren't the player's own, so switching either to third person would break it
    const bool ownCamera = camera->IsInFreeCameraMode() || camera->currentState->id == RE::CameraState::kFurniture;
    if (!allowed || ownCamera) {
        return;
    }

    playerAngleX = player->GetAngleX();
    playerAngleZ = player->GetAngleZ();
    allowAutoVanityMode = camera->allowAutoVanityMode;

    // in a follower's inventory the follower is shown instead, through the player's camera
    auto* shownFollower = menuName == RE::ContainerMenu::MENU_NAME ? Settings::GetFollower() : nullptr;
    follower = shownFollower ? shownFollower->GetHandle() : RE::ActorHandle{};
    followerAngleZ = shownFollower ? shownFollower->GetAngleZ() : 0.0f;
    auto* subject = shownFollower ? shownFollower : static_cast<RE::Actor*>(player);

    // the one shown faces the camera, so their head would turn round to look where it looks
    headTrackingWasOn = subject->AsActorState()->actorState2.headTracking;
    HoldHeadStill(subject);
    NeutralFace(subject);

    bool moving = false;
    if (auto* process = player->GetActorRuntimeData().currentProcess) {
        process->ClearActionHeadtrackTarget(true);

        // zero the speed and play the game's own stop
        auto& speed = process->middleHigh->currentMovementSpeed;
        moving = speed > MOVING_SPEED;
        if (speed > 0.0f) {
            speed = 0.0f;
            RE::PlayerControls::GetSingleton()->data.autoMove = false;
            if (player->AsActorState()->IsSprinting()) {
                PerformAction(player, RE::DEFAULT_OBJECT::kActionSprintStop);
            }
            PerformAction(player, RE::DEFAULT_OBJECT::kActionMoveStop);
        }
    }

    // a ridden horse stops too, as it otherwise carries on turning
    RE::NiPointer<RE::Actor> ridden;
    if (mounted && player->GetMount(ridden) && ridden) {
        if (auto* process = ridden->GetActorRuntimeData().currentProcess; process && process->middleHigh) {
            auto& speed = process->middleHigh->currentMovementSpeed;
            moving = moving || speed > MOVING_SPEED;
            speed = 0.0f;
        }
        PerformAction(ridden.get(), RE::DEFAULT_OBJECT::kActionMoveStop);
    }

    auto* controlMap = RE::ControlMap::GetSingleton();
    for (const auto flag : CONTROLS) {
        if (controlMap->AreControlsEnabled(flag) && !(flag == UEFlag::kMovement && Settings::values.allowMovement)) {
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

    // how the one shown meets the camera:
    // - they turn to face it: a follower, or the player with Turn Character on
    // - it comes round to their front instead (looking down along them, lying down): with Turn Character off, and always when sitting,
    //   lying down, or riding, as turning them would take them off the seat or bed, and the game brings a rider round on their horse over
    //   several frames, seen as a spin
    // - looting or on the move, it stays behind them, as turning them away from the chest looks wrong and the view should follow them
    const bool looting = menuName == RE::ContainerMenu::MENU_NAME && !shownFollower;
    const bool seated = sitting || lying;
    const bool riding = subject->IsOnMount();
    const bool turnCharacter = !riding && (shownFollower != nullptr || (Settings::values.turnCharacter && !looting && !seated));
    const bool cameraToFront = !moving && !looting && !turnCharacter;

    // a rider's horse, animated with them in a paused menu
    mount = {};
    RE::NiPointer<RE::Actor> horse;
    if (riding && subject->GetMount(horse) && horse) {
        mount = horse->GetHandle();
    }

    // FUCK's camera goes on last, once the camera above is ready. resumable, so another plugin's camera pauses it rather than ending it
    FUCK::CameraSessionOptions options;
    options.client = this;
    options.base = FUCK::CameraBase::kThirdPerson;
    options.subject = shownFollower;
    // no keyboard camera keys, as the menu uses them for its list and tabs. smp cloth and hair keep moving in a paused menu
    options.flags = FUCK::CameraSessionFlags::kResumable | FUCK::CameraSessionFlags::kNoKeyboard | FUCK::CameraSessionFlags::kRunPhysics;
    if (!moving && turnCharacter) {
        options.flags |= FUCK::CameraSessionFlags::kFaceCamera;
    }
    if (cameraToFront) {
        options.flags |= FUCK::CameraSessionFlags::kCameraToFront;
    }

    // no turning at a chest, on a seat, in a bed or on a horse, where it would look wrong
    if (looting || seated || riding) {
        options.flags |= FUCK::CameraSessionFlags::kNoTurning;
    }

    // opens where the camera was left last time, otherwise from the default framing. not on the move, as that view is from behind and the
    // remembered one is from the front, nor in containers, riding, seated or lying down, as a remembered angle wouldn't suit the spot
    if (Settings::values.rememberCamera && !moving && !mounted && !seated && menuName != RE::ContainerMenu::MENU_NAME) {
        options.flags |= FUCK::CameraSessionFlags::kRememberCamera;
    }

    // an animation can kneel the player at a chest, or settle them into a seat or bed, so the framing follows
    if (looting || seated) {
        options.flags |= FUCK::CameraSessionFlags::kFollowPose;
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
    HoldReplayedEvents(nullptr);
    Obstructions::RestoreAll();

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    if (disabledControls) {
        RE::ControlMap::GetSingleton()->ToggleControls(disabledControls.get(), true, false);
        disabledControls.reset();
    }

    if (cachedFirstPerson) {
        camera->ForceFirstPerson();
    }

    // FUCK turns the one shown to face the camera, by their heading or their 3D, so this puts them back to how they were facing
    auto* subject = GetSubject();
    subject->AsActorState()->actorState2.headTracking = headTrackingWasOn;
    RestoreFace(subject);
    if (subject != player) {
        subject->data.angle.z = followerAngleZ;
        subject->Update3DPosition(true);
    }
    follower = {};

    mount = {};

    player->data.angle.x = playerAngleX;
    player->data.angle.z = playerAngleZ;

    camera->allowAutoVanityMode = allowAutoVanityMode;
    camera->worldFOV = worldFOV;
    camera->Update();
    player->Update3DPosition(true);
}

void MenuCamera::Update(float interval)
{
    if (!active) {
        return;
    }

    auto* subject = GetSubject();
    HoldHeadStill(subject);

    // FUCK-RACE's cap, so one long frame can't skip the idle ahead
    interval = std::min(interval, 0.1f);

    // a paused world skips the animation and scene update of the one shown and their horse, so it's done here. with skyrim souls the game
    // runs it itself. the horse first, as the rider sits on it
    const bool paused = RE::UI::GetSingleton()->GameIsPaused();
    if (!playerFrozen && paused) {
        const auto horse = mount.get();
        for (auto* actor : { horse.get(), subject }) {
            if (!actor) {
                continue;
            }

            actor->UpdateAnimation(interval);
            if (auto* root = actor->Get3D(false)) {
                RE::NiUpdateData data{};
                root->Update(data);
            }
        }
    }
    HoldReplayedEvents(paused ? subject : nullptr);

    // after the menu, so the camera frames this frame's pose. with skyrim souls FUCK also keeps it on top of the game's own camera
    FUCK::UpdateCamera(this, interval);

    // once the camera is placed, so it's what this frame sees from
    Obstructions::Update(interval, subject);
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

// lets movement through or locks it as the setting changes, while a menu is open
void MenuCamera::ApplyMovement()
{
    if (!active) {
        return;
    }

    auto* controlMap = RE::ControlMap::GetSingleton();
    const bool locked = disabledControls.any(UEFlag::kMovement);
    if (Settings::values.allowMovement && locked) {
        controlMap->ToggleControls(UEFlag::kMovement, true, false);
        disabledControls.reset(UEFlag::kMovement);
    } else if (!Settings::values.allowMovement && !locked && controlMap->AreControlsEnabled(UEFlag::kMovement)) {
        controlMap->ToggleControls(UEFlag::kMovement, false, false);
        disabledControls.set(UEFlag::kMovement);
    }
}

// freezes or thaws the one shown to match the setting, while a menu is open
void MenuCamera::ApplyAnimation()
{
    SetFrozen(active && !Settings::values.animateCharacter);
}

// FUCK holds the one shown still whether or not the game is paused (skyrim souls), eyes open
void MenuCamera::SetFrozen(bool freeze)
{
    if (freeze != playerFrozen) {
        FUCK::SetActorFrozen(GetSubject(), freeze);
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
