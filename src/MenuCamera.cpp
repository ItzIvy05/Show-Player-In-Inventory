#include "MenuCamera.h"
#include "Obstructions.h"
#include "SettingsUI.h"

// windows.h macro
#undef GetObject

namespace
{
    using UEFlag = RE::ControlMap::UEFlag;

    // the controls locked while someone is shown. pov switch and wheel zoom would move the game's camera, and with skyrim souls, looking would
    // turn the player, which right-drag already does. movement stays on, so they can walk with skyrim souls
    constexpr std::array CONTROLS{ UEFlag::kPOVSwitch, UEFlag::kWheelZoom, UEFlag::kLooking };

    // the body framed from the top of the screen nearly to the bottom, as -1 to 1 from the bottom up
    constexpr float FRAMING_TOP = 1.0f;
    constexpr float FRAMING_BOTTOM = -0.9f;

    // where the item list ends when it can't be read, as a share of the screen's width
    constexpr float DEFAULT_LIST_EDGE = 0.45f;

    // at a workbench with its own camera, how far across from the list the player's framed (0 at its edge, 1 at the screen's), leaving the
    // station room
    constexpr float BENCH_ACROSS = 0.1f;

    // where skyui's list panel comes to rest in its menus once it has slid in (InventoryLists.swf: from -100 to 25 as the menu opens)
    constexpr float PANEL_REST_X = 25.0f;

    // how far the position sliders go, in game units
    constexpr float SLIDER_RANGE = 200.0f;

    // the field of view slider's range, in degrees
    constexpr float FOV_MIN = 20.0f;
    constexpr float FOV_MAX = 120.0f;

    // someone held by the one shown (carried, or riding behind them) has their 3D closer to them, or to their horse, than two people can
    // stand. measured across the ground, as someone carried sits up at the carrier's arm or shoulder, and only so far up or down, so
    // someone on the floor above or below isn't counted. a 3D further than HELD_REACH from its own actor is posed there by another mod
    constexpr float HELD_REACH = 50.0f;
    constexpr float HELD_HEIGHT = 250.0f;

    // an animal posed in your arms by another mod sits further out in front of you, so it's looked for this far away
    constexpr float POSED_REACH = 120.0f;

    // the room round the body when they're holding someone, so whoever's riding behind or carried fits in too (FUCK's own is 1.15)
    constexpr float HOLDING_MARGIN = 1.4f;
    constexpr float CLOSE_WORK_MARGIN = 1.4f;  // room round the upper body at a desk's alchemy lab or enchanter

    // if the player is going faster than this as the menu opens, they count as moving and the view stays behind them. walking is around 80
    constexpr float MOVING_SPEED = 50.0f;

    // FUCK-RACE's prompt lock, plus quantity and book menus
    bool IsPromptOpen()
    {
        auto* ui = RE::UI::GetSingleton();
        return ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME)                      ||
               ui->IsMenuOpen(RE::InterfaceStrings::GetSingleton()->quantityMenu) ||
               ui->IsMenuOpen(RE::BookMenu::MENU_NAME)                            ;
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

    // the body framed the given share of the way from the item list's edge to the screen's right edge, halfway by default
    FUCK::CameraScreenRect GetFraming(float listEdge, float across = 0.5f)
    {
        // -1 to 1 across the screen
        const float leftX = listEdge * 2.0f - 1.0f;
        return { leftX + (1.0f - leftX) * across, (FRAMING_TOP + FRAMING_BOTTOM) * 0.5f, (FRAMING_TOP - FRAMING_BOTTOM) * 0.5f };
    }

    bool IsCursorOverList()
    {
        auto* ui = RE::UI::GetSingleton();
        return IsCursorOverList<RE::InventoryMenu>(ui) ||
               IsCursorOverList<RE::MagicMenu>(ui)     ||
               IsCursorOverList<RE::BarterMenu>(ui)    ||
               IsCursorOverList<RE::ContainerMenu>(ui) ;
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

    void Clear(RE::BSFaceGenKeyframeMultiple& keyframe)
    {
        if (keyframe.values && keyframe.count > 0) {
            std::fill_n(keyframe.values, keyframe.count, 0.0f);
            keyframe.isUpdated = true;
        }
    }

    // someone else's face straight to neutral in a paused menu, dropping a line they're partway through, as one more frame of the game's
    // face update runs as the menu opens and none after it. not put back after, as the line would stick. with skyrim souls they finish it
    void SettleFace(RE::Actor* actor)
    {
        auto* face = actor->GetFaceGenAnimationData();
        if (!face) {
            return;
        }

        face->ClearExpressionOverride();
        face->Reset(0.0f, true, true, false, false);
        RE::BSSpinLockGuard guard{ face->lock };
        for (auto* keyframe : { &face->expressionKeyFrame, &face->modifierKeyFrame, &face->phenomeKeyFrame, &face->expression3, &face->modifier1,
                 &face->modifier3, &face->phoneme1, &face->phoneme3 }) {
            Clear(*keyframe);
        }
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

    // places the third-person camera looking at a point along the heading (from +y towards +x), tilted down by the pitch, this far back.
    // FUCK's framing then sets the distance
    // the camera at eye, looking the given way (heading as the player's, pitch down)
    void PlaceCamera(RE::PlayerCamera* camera, const RE::NiPoint3& eye, float heading, float pitch)
    {
        auto* root = camera->cameraRoot.get();
        if (!root) {
            return;
        }

        // each axis is a column: right, forward (the way the camera looks), then up the screen
        const float c = std::cos(pitch);
        const float s = std::sin(pitch);
        const RE::NiPoint3 along{ std::sin(heading), std::cos(heading), 0.0f };
        const RE::NiPoint3 right{ along.y, -along.x, 0.0f };
        const RE::NiPoint3 forward{ along.x * c, along.y * c, -s };
        const RE::NiPoint3 up{ along.x * s, along.y * s, c };
        auto& rotate = root->local.rotate;
        for (int row = 0; row < 3; ++row) {
            rotate.entry[row][0] = right[row];
            rotate.entry[row][1] = forward[row];
            rotate.entry[row][2] = up[row];
        }
        root->local.translate = eye;

        RE::NiUpdateData data{};
        root->Update(data);
    }

    void AimCamera(RE::PlayerCamera* camera, const RE::NiPoint3& at, float heading, float pitch, float back)
    {
        const RE::NiPoint3 forward{ std::sin(heading) * std::cos(pitch), std::cos(heading) * std::cos(pitch), -std::sin(pitch) };
        PlaceCamera(camera, at - forward * back, heading, pitch);
    }

    // behind a horse on the move, a little above, looking down along the way it heads, so the framing starts from the same view however
    // the camera was aimed or the ground sloped
    void MoveCameraBehindMount(RE::Actor* mount, RE::Actor* rider, RE::PlayerCamera* camera)
    {
        auto* model = rider->Get3D(false);
        if (auto* head = model ? model->GetObjectByName("NPC Head [Head]") : nullptr) {
            AimCamera(camera, head->world.translate, mount->GetAngleZ(), 12.0f * RE::NI_PI / 180.0f, 400.0f);
        }
    }

    constexpr float FRONT_REACH  = 170.0f;                      // how far in front must be clear, about where a chest-up framing stands
    constexpr float FRONT_CORNER = RE::NI_PI / 4.0f;            // a front corner, from straight ahead
    constexpr float FRONT_PITCH  = 6.0f * RE::NI_PI / 180.0f;   // looking down at the chest, as FUCK's own front view does
    constexpr float FRONT_BACK   = 300.0f;                      // where the camera starts, before the framing sets the distance

    // between the head and pelvis, or nullopt without either
    std::optional<RE::NiPoint3> ChestOf(RE::Actor* actor)
    {
        auto* model = actor->Get3D(false);
        auto* head = model ? model->GetObjectByName("NPC Head [Head]") : nullptr;
        auto* pelvis = model ? model->GetObjectByName("NPC Pelvis [Pelv]") : nullptr;
        if (!head || !pelvis) {
            return std::nullopt;
        }
        return (head->world.translate + pelvis->world.translate) * 0.5f;
    }

    // which way out from the chest is clear of walls and the like: straight ahead, else a front corner, the given side first, or nullopt if
    // neither is
    std::optional<float> ClearFront(RE::Actor* actor, const RE::NiPoint3& chest, float facing, float side, std::initializer_list<const RE::TESObjectREFR*> ignore)
    {
        for (const float offset : { 0.0f, FRONT_CORNER * side, -FRONT_CORNER * side }) {
            const float out = facing + offset;
            const RE::NiPoint3 reach = chest + RE::NiPoint3{ std::sin(out), std::cos(out), 0.0f } * FRONT_REACH;
            if (Obstructions::IsLineClear(actor, chest, reach, ignore)) {
                return out;
            }
        }
        return std::nullopt;
    }

    // 1 when a point is round to the actor's left of where they face, else -1
    float SideOf(const RE::NiPoint3& chest, float facing, const RE::NiPoint3& point)
    {
        return std::remainder(std::atan2(point.x - chest.x, point.y - chest.y) - facing, 2.0f * RE::NI_PI) >= 0.0f ? 1.0f : -1.0f;
    }

    // the merchant from in front, or a clear front corner, the player's side first (at a tanning rack or a forge their front is up against
    // it), else past the player as they see them. the framing then sets the distance
    void AimAtMerchant(RE::Actor* merchant, RE::PlayerCharacter* player, RE::PlayerCamera* camera)
    {
        const auto chest = ChestOf(merchant);
        if (!chest) {
            return;
        }

        const RE::NiPoint3 from = player->GetPosition();
        const float facing = merchant->GetAngleZ();
        if (const auto out = ClearFront(merchant, *chest, facing, SideOf(*chest, facing, from), { merchant, player })) {
            AimCamera(camera, *chest, *out + RE::NI_PI, FRONT_PITCH, FRONT_BACK);
            return;
        }

        AimCamera(camera, *chest, std::atan2(chest->x - from.x, chest->y - from.y), FRONT_PITCH, std::hypot(chest->x - from.x, chest->y - from.y) + 100.0f);
    }

    // whoever's pickpocketed, side on to the pair so the player stands beside them rather than in the way: the side the mark faces first,
    // else the other, else a corner behind the player, else straight past them. the framing then fits both
    void AimAtMark(RE::Actor* mark, RE::PlayerCharacter* player, RE::PlayerCamera* camera)
    {
        constexpr float SIDE   = RE::NI_PI / 2.0f;
        constexpr float CORNER = 35.0f * RE::NI_PI / 180.0f;
        constexpr float PITCH  = 10.0f * RE::NI_PI / 180.0f;

        const auto chest = ChestOf(mark);
        if (!chest) {
            return;
        }

        const RE::NiPoint3 from = player->GetPosition();
        const float toPlayer = std::atan2(from.x - chest->x, from.y - chest->y);
        const float frontSide = std::remainder(mark->GetAngleZ() - toPlayer, 2.0f * RE::NI_PI) >= 0.0f ? 1.0f : -1.0f;

        for (const float offset : { SIDE * frontSide, -SIDE * frontSide, CORNER * frontSide, -CORNER * frontSide, 0.0f }) {
            const float out = toPlayer + offset;
            const RE::NiPoint3 reach = *chest + RE::NiPoint3{ std::sin(out), std::cos(out), 0.0f } * FRONT_REACH;
            if (offset == 0.0f || Obstructions::IsLineClear(mark, *chest, reach, { mark, player })) {
                AimCamera(camera, *chest, out + RE::NI_PI, PITCH, FRONT_BACK);
                return;
            }
        }
    }

    // a bench camera set by hand, from the player's chest and facing: right, forward and up, then round (clockwise from above) and down in
    // degrees
    struct BenchShot
    {
        float right;
        float forward;
        float up;
        float round;
        float down;
    };

    constexpr BenchShot WORKBENCH_SHOT  { 112.0f, -66.2f, 57.2f, -66.4f, 30.0f };  // also the anvil, and any bench without its own camera
    constexpr BenchShot GRINDSTONE_SHOT { 102.2f, -47.0f, 57.7f, -77.1f, 30.0f };
    constexpr BenchShot ALCHEMY_LAB_SHOT{ 130.4f, -66.6f, 34.9f, -70.8f, 21.5f };  // free-standing, not on a desk
    constexpr BenchShot ENCHANTER_SHOT  { 96.1f, -124.7f, 10.3f, -45.0f, 10.2f };  // free-standing, not on a desk

    RE::TESFurniture* GetWorkbench(RE::Actor* actor, RE::NiPointer<RE::TESObjectREFR>* ref = nullptr)
    {
        auto bench = actor->GetOccupiedFurniture().get();
        auto* base = bench ? bench->GetBaseObject() : nullptr;
        auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
        if (!furniture || furniture->workBenchData.benchType.underlying() == 0) {
            return nullptr;
        }
        if (ref) {
            *ref = bench;
        }
        return furniture;
    }

    // how a workbench is shown: the grindstone, a free-standing alchemy lab or enchanter, and any bench without its own camera get one set
    // by hand (shot), unframed. any other starts from its own camera turned 45 degrees anticlockwise round the player (25 at a desk's
    // alchemy lab or enchanter, framed chest up) and eases into the framing, near the list
    struct BenchView
    {
        RE::NiTransform game;  // the game's camera as the menu opened
        std::optional<BenchShot> shot;
        bool ownCamera = false;  // the bench animation's
        bool desk = false;       // an alchemy lab or enchanter on a desk
    };

    BenchView GetBenchView(RE::PlayerCharacter* player, RE::PlayerCamera* camera)
    {
        using BenchType = RE::TESFurniture::WorkBenchData::BenchType;

        // a desk's alchemy lab or enchanter sits up on it, its bottom at least this far above the player's feet
        constexpr float ON_A_DESK = 30.0f;

        BenchView view;
        view.game = camera->cameraRoot->world;
        const auto state = camera->currentState->id;
        view.ownCamera = state == RE::CameraState::kAnimated || state == RE::CameraState::kPCTransition;

        RE::NiPointer<RE::TESObjectREFR> bench;
        if (auto* furniture = GetWorkbench(player, &bench)) {
            const auto type = static_cast<BenchType>(furniture->workBenchData.benchType.underlying());
            const bool alchemy = type == BenchType::kAlchemy || type == BenchType::kAlchemyExperiment;
            const bool enchanting = type == BenchType::kEnchanting || type == BenchType::kEnchantingExperiment;
            const float bottom = bench->GetPositionZ() + static_cast<float>(furniture->boundData.boundMin.z) * bench->GetScale();
            const bool onFloor = bottom - player->GetPositionZ() < ON_A_DESK;

            view.desk = (alchemy || enchanting) && !onFloor;
            if (alchemy && onFloor) {
                view.shot = ALCHEMY_LAB_SHOT;
            } else if (enchanting && onFloor) {
                view.shot = ENCHANTER_SHOT;
            } else if (type == BenchType::kSmithingWeapon) {
                view.shot = GRINDSTONE_SHOT;
            }
        }
        if (!view.shot && !view.ownCamera) {
            view.shot = WORKBENCH_SHOT;
        }
        return view;
    }

    void PlaceBenchCamera(RE::PlayerCharacter* player, RE::PlayerCamera* camera, const BenchView& view)
    {
        auto* root = camera->cameraRoot.get();
        const auto chest = ChestOf(player);
        if (!root || !chest) {
            return;
        }

        const float facing = player->GetAngleZ();
        if (const auto& shot = view.shot) {
            constexpr float RADIANS = RE::NI_PI / 180.0f;
            const RE::NiPoint3 ahead{ std::sin(facing), std::cos(facing), 0.0f };
            const RE::NiPoint3 right{ std::cos(facing), -std::sin(facing), 0.0f };
            const RE::NiPoint3 eye = *chest + right * shot->right + ahead * shot->forward + RE::NiPoint3{ 0.0f, 0.0f, shot->up };
            PlaceCamera(camera, eye, facing + shot->round * RADIANS, shot->down * RADIANS);
            return;
        }

        // anticlockwise round the chest from above
        const float angle = (view.desk ? 25.0f : 45.0f) * RE::NI_PI / 180.0f;
        RE::NiMatrix3 turn;
        turn.entry[0][0] = std::cos(angle);
        turn.entry[0][1] = -std::sin(angle);
        turn.entry[0][2] = 0.0f;
        turn.entry[1][0] = std::sin(angle);
        turn.entry[1][1] = std::cos(angle);
        turn.entry[1][2] = 0.0f;
        turn.entry[2][0] = 0.0f;
        turn.entry[2][1] = 0.0f;
        turn.entry[2][2] = 1.0f;

        root->local.rotate = turn * view.game.rotate;
        root->local.translate = *chest + turn * (view.game.translate - *chest);
        RE::NiUpdateData data{};
        root->Update(data);
    }

    // who the player's talking to, or nullptr
    RE::Actor* GetConversationPartner()
    {
        auto* topics = RE::MenuTopicManager::GetSingleton();
        auto ref = topics ? topics->speaker.get() : RE::NiPointer<RE::TESObjectREFR>{};
        if (!ref && topics) {
            ref = topics->lastSpeaker.get();
        }
        return ref ? ref->As<RE::Actor>() : nullptr;
    }

    bool HasApparelPreview()
    {
        static const bool installed = GetModuleHandleW(L"ApparelPreview.dll") != nullptr;
        return installed;
    }

    // Apparel Preview puts a piece on the player without equipping it, so a slot showing armour they aren't wearing (nor their skin) is
    // a preview. only with ApparelPreview.dll loaded
    bool IsApparelPreviewing()
    {
        if (!HasApparelPreview()) {
            return false;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        const auto& biped = player->GetBiped(false);
        if (!biped) {
            return false;
        }
        auto* skin = player->GetSkin();
        constexpr std::uint32_t ARMOR_SLOTS = 32;
        for (std::uint32_t i = 0; i < ARMOR_SLOTS; ++i) {
            auto* item = biped->objects[i].item;
            auto* armor = item ? item->As<RE::TESObjectARMO>() : nullptr;
            if (armor && armor != skin && armor != player->GetWornArmor(static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1u << i))) {
                return true;
            }
        }
        return false;
    }

    constexpr const char* SHADE_PATH = "_root.SPII_Shade";

    // dark edges round the menu, clear in the middle, under everything the menu draws
    void CreateShade(RE::GFxMovieView* view)
    {
        RE::GFxValue root;
        if (!view->GetVariable(&root, "_root") || !root.IsDisplayObject()) {
            return;
        }

        RE::GFxValue shade;
        const std::array<RE::GFxValue, 2> args{ RE::GFxValue("SPII_Shade"), RE::GFxValue(-16384.0) };
        if (!root.Invoke("createEmptyMovieClip", &shade, args) || !shade.IsDisplayObject()) {
            return;
        }

        const auto   frame  = view->GetVisibleFrameRect();
        const double width  = frame.right - frame.left;
        const double height = frame.bottom - frame.top;

        RE::GFxValue colors;
        RE::GFxValue alphas;
        RE::GFxValue ratios;
        view->CreateArray(&colors);
        view->CreateArray(&alphas);
        view->CreateArray(&ratios);
        for (const auto [alpha, ratio] : { std::pair{ 0.0, 110.0 }, std::pair{ 85.0, 255.0 } }) {
            colors.PushBack(RE::GFxValue(0.0));
            alphas.PushBack(RE::GFxValue(alpha));
            ratios.PushBack(RE::GFxValue(ratio));
        }

        RE::GFxValue box;
        view->CreateObject(&box);
        box.SetMember("matrixType", RE::GFxValue("box"));
        box.SetMember("x", RE::GFxValue(static_cast<double>(frame.left)));
        box.SetMember("y", RE::GFxValue(static_cast<double>(frame.top)));
        box.SetMember("w", RE::GFxValue(width));
        box.SetMember("h", RE::GFxValue(height));
        box.SetMember("r", RE::GFxValue(0.0));

        shade.Invoke("beginGradientFill", std::array<RE::GFxValue, 5>{ RE::GFxValue("radial"), colors, alphas, ratios, box });
        shade.Invoke("moveTo", std::array<RE::GFxValue, 2>{ RE::GFxValue(static_cast<double>(frame.left)),  RE::GFxValue(static_cast<double>(frame.top)) });
        shade.Invoke("lineTo", std::array<RE::GFxValue, 2>{ RE::GFxValue(static_cast<double>(frame.right)), RE::GFxValue(static_cast<double>(frame.top)) });
        shade.Invoke("lineTo", std::array<RE::GFxValue, 2>{ RE::GFxValue(static_cast<double>(frame.right)), RE::GFxValue(static_cast<double>(frame.bottom)) });
        shade.Invoke("lineTo", std::array<RE::GFxValue, 2>{ RE::GFxValue(static_cast<double>(frame.left)),  RE::GFxValue(static_cast<double>(frame.bottom)) });
        shade.Invoke("lineTo", std::array<RE::GFxValue, 2>{ RE::GFxValue(static_cast<double>(frame.left)),  RE::GFxValue(static_cast<double>(frame.top)) });
        shade.Invoke("endFill");
    }

    // nobody near has noticed the player, by the game's own detection (what papyrus IsDetectedBy reads). followers don't count, as they
    // always know where the player is
    bool IsUnseen(RE::PlayerCharacter* player)
    {
        auto* processes = RE::ProcessLists::GetSingleton();
        if (!processes) {
            return true;
        }
        for (const auto& handle : processes->highActorHandles) {
            const auto actor = handle.get();
            if (actor && !actor->IsDead() && !actor->IsPlayerTeammate() && actor->RequestDetectionLevel(player) > 0) {
                return false;
            }
        }
        return true;
    }

    // who the barter menu is with, if they're in view to show
    RE::Actor* GetMerchant()
    {
        const auto ref = RE::TESObjectREFR::LookupByHandle(RE::BarterMenu::GetTargetRefHandle());
        auto* merchant = ref ? ref->As<RE::Actor>() : nullptr;
        if (!merchant) {
            merchant = GetConversationPartner();
        }
        return merchant && merchant->Get3D(false) ? merchant : nullptr;
    }

    // the game's own field of view, from its ini
    float GetDefaultWorldFOV(float fallback)
    {
        for (auto* ini : { static_cast<RE::INISettingCollection*>(RE::INIPrefSettingCollection::GetSingleton()), RE::INISettingCollection::GetSingleton() }) {
            if (auto* setting = ini ? ini->GetSetting("fDefaultWorldFOV:Display") : nullptr) {
                return setting->GetFloat();
            }
        }
        return fallback;
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
    // replaces the item preview load, so the preview stays empty while the player is shown. installed only once the option is turned on,
    // or with Apparel Preview, whose pieces are shown on the player
    struct LoadInventoryItemHook
    {
        static void LoadInventoryItem(RE::Inventory3DManager* a_this, RE::InventoryEntryData* a_entry)
        {
            auto* object = a_entry ? a_entry->GetObject() : nullptr;
            auto* camera = MenuCamera::GetSingleton();
            if (!object || (Settings::values.hideItemPreviews && camera->IsActive()) || camera->IsPreviewShowingPlayer()) {
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
}

void MenuCamera::InstallHooks()
{
    PerspectiveHook<RE::FirstPersonState>::Install();
    PerspectiveHook<RE::ThirdPersonState>::Install();
    AdvanceMovieHook<RE::InventoryMenu>::Install();
    AdvanceMovieHook<RE::MagicMenu>::Install();
    AdvanceMovieHook<RE::BarterMenu>::Install();
    AdvanceMovieHook<RE::ContainerMenu>::Install();
    AdvanceMovieHook<RE::CraftingMenu>::Install();
    EquipNotifyHook::Install();
    ApplyItemPreviews();
    if (HasApparelPreview()) {
        LoadInventoryItemHook::Install();
    }

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

// not the third person the game switches to for a workbench's animation, so a crafting menu opened from first person goes back to it,
// once the player's left the bench
void MenuCamera::OnPerspectiveUpdate(bool firstPerson)
{
    if (active) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    const bool atWorkbench = GetWorkbench(player) != nullptr;
    if (firstPersonAfterBench && (firstPerson || !atWorkbench)) {
        firstPersonAfterBench = false;
        if (!firstPerson) {
            SKSE::GetTaskInterface()->AddTask([] {
                auto* camera = RE::PlayerCamera::GetSingleton();
                if (camera->currentState && camera->currentState->id == RE::CameraState::kThirdPerson) {
                    camera->ForceFirstPerson();
                }
            });
        }
    }

    if (!cacheFrozen && (firstPerson || !atWorkbench)) {
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

    // not while sitting down or getting up, and lying down only with GoToBed. a rider counts as sitting to the game, so riding is told apart
    // from a seat, and lying down is told from the pose, as a bed can report either state
    const auto sitSleep = player->AsActorState()->GetSitSleepState();
    const bool mounted = player->IsOnMount();
    const bool inFurniture = !mounted && (sitSleep == RE::SIT_SLEEP_STATE::kIsSitting || sitSleep == RE::SIT_SLEEP_STATE::kIsSleeping);
    const bool lying = inFurniture && FUCK::IsLyingDown(player);
    const bool sitting = inFurniture && !lying;
    const bool settled = sitSleep == RE::SIT_SLEEP_STATE::kNormal || sitSleep == RE::SIT_SLEEP_STATE::kIsSitting || sitSleep == RE::SIT_SLEEP_STATE::kIsSleeping;
    const bool allowed = settled && (!lying || Settings::CanLieDown());
    // the furniture and free cameras aren't the player's own, so switching either to third person would break it. a crafting menu keeps the
    // furniture camera, with FUCK over it (furnitureBase)
    const bool crafting = menuName == RE::CraftingMenu::MENU_NAME;
    const bool atFurnitureCamera = camera->currentState->id == RE::CameraState::kFurniture;
    const bool ownCamera = camera->IsInFreeCameraMode() || (atFurnitureCamera && !crafting);
    if (!allowed || ownCamera) {
        return;
    }
    furnitureBase = atFurnitureCamera;

    // at a workbench (see BenchView)
    const auto bench = crafting && !furnitureBase && camera->cameraRoot ? std::optional{ GetBenchView(player, camera) } : std::nullopt;
    benchPlaced = bench && bench->shot;
    framingAcross = bench && bench->ownCamera ? BENCH_ACROSS : 0.5f;

    playerAngleX = player->GetAngleX();
    playerAngleZ = player->GetAngleZ();
    shownMenu = menuName;

    // in a follower's inventory the follower is shown instead, through the player's camera, in barter the merchant, and pickpocketing,
    // whoever's being robbed
    const bool container = menuName == RE::ContainerMenu::MENU_NAME;
    auto* shownFollower = container ? Settings::GetFollower() : nullptr;
    shadeAllowed = menuName != RE::BarterMenu::MENU_NAME && !shownFollower;
    auto* shownMerchant = menuName == RE::BarterMenu::MENU_NAME && !previewShowsPlayer ? GetMerchant() : nullptr;
    auto* shownVictim = container ? Settings::GetPickpocketed() : nullptr;
    auto* shownOther = shownFollower ? shownFollower : shownMerchant ? shownMerchant : shownVictim;
    follower = shownOther ? shownOther->GetHandle() : RE::ActorHandle{};
    followerAngleZ = shownOther ? shownOther->GetAngleZ() : 0.0f;
    auto* subject = shownOther ? shownOther : static_cast<RE::Actor*>(player);

    // the player out of the way of a merchant, as they stand right in front. not pickpocketing, where they're seen sneaking up, and can
    // follow someone walking off with skyrim souls
    merchantShown = shownMerchant != nullptr;
    hidingPlayer = merchantShown;
    if (hidingPlayer) {
        HidePlayer();
    }

    // nothing's hidden round a merchant, whose counter is part of the view, nor whoever's pickpocketed, as the player's crept up to them
    // where they can be seen
    keepObstructions = merchantShown || shownVictim || crafting;

    // the one shown faces the camera, so their head would turn round to look where it looks
    headTrackingWasOn = subject->AsActorState()->actorState2.headTracking;
    HoldHeadStill(subject);
    // the player's face eases to neutral and goes back after. anyone else's is settled in a paused menu, along with anyone held or talked
    // to below, and otherwise left to the game
    const auto menu = RE::UI::GetSingleton()->GetMenu(menuName);
    const bool pausing = menu && menu->PausesGame();
    if (subject == player) {
        NeutralFace(subject);
    } else if (pausing) {
        SettleFace(subject);
    }

    bool moving = false;
    if (auto* process = player->GetActorRuntimeData().currentProcess) {
        process->ClearActionHeadtrackTarget(true);

        // they carry on moving, as with skyrim souls they can walk on through the menu, and a paused menu keeps their run going
        moving = process->middleHigh->currentMovementSpeed > MOVING_SPEED;
    }

    // a horse on the move counts too. a flying mount (a dragon) is left out: it flies itself and is always moving, so counting it would
    // keep the view behind the rider every time
    RE::NiPointer<RE::Actor> ridden;
    const auto* riddenRace = mounted && player->GetMount(ridden) && ridden ? ridden->GetRace() : nullptr;
    if (riddenRace && riddenRace->data.flags.none(RE::RACE_DATA::Flag::kFlies)) {
        if (auto* process = ridden->GetActorRuntimeData().currentProcess; process && process->middleHigh) {
            moving = moving || process->middleHigh->currentMovementSpeed > MOVING_SPEED;
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

    // EnterThirdPerson minus its zoom reset, fov read after the tab camera restores it
    if (!furnitureBase && camera->currentState->id < RE::CameraState::kThirdPerson) {
        camera->SetState(camera->cameraStates[RE::CameraState::kThirdPerson].get());
        camera->Update();
    }

    worldFOV = camera->worldFOV;

    // a rider's horse is animated with them in a paused menu
    const bool riding = subject->IsOnMount();
    mount = {};
    RE::NiPointer<RE::Actor> horse;
    if (riding && subject->GetMount(horse) && horse) {
        mount = horse->GetHandle();
    }

    // anyone the one shown is holding (carried, or riding behind them) is placed against them each frame by the mod doing the holding,
    // which a paused menu skips, so they're animated here as well, and kept where they are against the saddle or the holder's pelvis, so
    // they move with them. they're found by their 3D, as Cat Carry parks a held animal's actor behind you while posing its 3D in your
    // arms. that mod poses the animal and your arms round it itself, so neither is animated or moved here (see IsAnimatedHere)
    auto* horseModel = horse ? horse->Get3D(false) : nullptr;
    auto* subjectModel = subject->Get3D(false);
    RE::NiAVObject* carrier = horseModel ? horseModel->GetObjectByName("SaddleBone") : nullptr;
    if (!carrier && subjectModel) {
        carrier = subjectModel->GetObjectByName("NPC Pelvis [Pelv]");
    }
    held.clear();
    auto* processes = keepObstructions ? nullptr : RE::ProcessLists::GetSingleton();  // not round a merchant or victim, where it'd catch the player
    if (processes) {
        for (const auto& handle : processes->highActorHandles) {
            const auto actor = handle.get();
            auto* model = actor ? actor->Get3D(false) : nullptr;
            if (!model || actor.get() == subject || actor.get() == horse.get()) {
                continue;
            }

            const RE::NiPoint3 at = model->world.translate;
            const RE::NiPoint3 actorAt = actor->GetPosition();
            const bool posed = std::hypot(at.x - actorAt.x, at.y - actorAt.y) > HELD_REACH;
            const float reach = posed ? POSED_REACH : HELD_REACH;
            const auto closeTo = [&](const RE::NiPoint3& to) {
                return std::hypot(at.x - to.x, at.y - to.y) < reach && std::abs(at.z - to.z) < HELD_HEIGHT;
            };
            if (closeTo(subject->GetPosition()) || (horse && closeTo(horse->GetPosition()))) {
                Held one{ actor->GetHandle(), !posed };
                if (!posed && carrier) {
                    one.carriedBy = RE::NiPointer<RE::NiAVObject>(carrier);
                    one.offset = carrier->world.Invert() * model->world;
                }
                held.push_back(std::move(one));
            }
        }
    }
    const bool holding = !held.empty();

    // how the one shown meets the camera:
    // - they turn to face it: a follower, or the player with Turn Character on
    // - it comes round to their front instead (looking down along them, lying down): with Turn Character off, and always when sitting,
    //   lying down, riding, or holding someone. turning them would take them off the seat or bed, and the game or a mod only brings a
    //   horse or whoever they hold round with them over a few frames, so it'd be seen catching up
    // - looting, or on the move, it stays behind them, as turning them away from the chest looks wrong and the view should follow them.
    //   pickpocketing too, side on to the pair (AimAtMark), and never turned
    // - in a conversation the same, but a follower shown already faces the player, so the view comes from the player's side
    // - a merchant shown is seen from their front, or wherever's clear (AimAtMerchant), chest up and never turned
    const bool looting = menuName == RE::ContainerMenu::MENU_NAME && !shownFollower;
    const bool fromBehind = looting;
    const bool seated = sitting || lying;
    const bool talking = RE::UI::GetSingleton()->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
    auto* talkingTo = talking ? GetConversationPartner() : nullptr;
    partner = talkingTo && talkingTo != subject ? talkingTo->GetHandle() : RE::ActorHandle{};
    if (pausing) {
        for (const auto& one : held) {
            if (const auto actor = one.actor.get(); actor && actor.get() != player) {
                SettleFace(actor.get());
            }
        }
        if (const auto other = partner.get(); other && other.get() != player) {
            SettleFace(other.get());
        }
    }

    const bool turnCharacter = !shownMerchant && !crafting && !riding && !holding && (shownFollower != nullptr || (Settings::values.turnCharacter && !fromBehind && !seated));
    const bool cameraToFront = !shownMerchant && !crafting && !moving && !fromBehind && (!turnCharacter || (talking && shownFollower));

    // a fresh camera for a front view, so it doesn't vary with how the player was looking, and in a conversation, whose camera isn't the
    // game's. otherwise the view carries on from the game's camera
    const bool freshBase = !shownMerchant && (cameraToFront || talking);

    // a front view up against a wall (a shelf, a bookcase) comes round to a clear front corner instead, the camera's side first, aimed here
    // as the merchant's is. straight ahead stays FUCK's own front view. not lying down or on a dragon, whose views FUCK places itself
    const bool flying = riddenRace && riddenRace->data.flags.any(RE::RACE_DATA::Flag::kFlies);
    bool frontCorner = false;
    if (const auto chest = cameraToFront && !lying && !flying && !furnitureBase ? ChestOf(subject) : std::nullopt) {
        auto* cameraRoot = camera->cameraRoot.get();
        const RE::NiPoint3 eye = cameraRoot ? cameraRoot->world.translate : player->GetPosition();
        const float facing = subject->GetAngleZ();
        const auto out = ClearFront(subject, *chest, facing, SideOf(*chest, facing, eye), { subject, player, horse.get() });
        if (out && *out != facing) {
            AimCamera(camera, *chest, *out + RE::NI_PI, FRONT_PITCH, FRONT_BACK);
            frontCorner = true;
        }
    }

    // the player's look is levelled, so they don't hold their head looking up or down. not in a conversation, whose camera aims with it
    pitchLevelled = !talking;
    if (pitchLevelled) {
        player->data.angle.x = 0.1f;
        player->Update3DPosition(true);
    }

    // on horseback on the move, the view behind starts from the same place each time, rather than wherever the camera was aimed
    if (shownMerchant) {
        AimAtMerchant(shownMerchant, player, camera);
    }
    if (shownVictim) {
        AimAtMark(shownVictim, player, camera);
    }
    if (bench) {
        PlaceBenchCamera(player, camera, *bench);
    }
    if (moving && horse) {
        MoveCameraBehindMount(horse.get(), subject, camera);
    }

    // the game's field of view while they're shown, as a conversation camera can leave a narrow lens that frames them from far off.
    // Stop puts it back
    if (talking) {
        camera->worldFOV = GetDefaultWorldFOV(camera->worldFOV);
    }

    // FUCK's camera goes on last, once the camera above is ready. resumable, so another plugin's camera pauses it rather than ending it
    FUCK::CameraSessionOptions options;
    options.client = this;
    options.base = furnitureBase ? FUCK::CameraBase::kOffset : FUCK::CameraBase::kThirdPerson;
    options.subject = shownOther;
    // pickpocketing, the player's framed beside the mark
    if (shownVictim) {
        options.alsoFramed = player;
    }
    // no keyboard camera keys, as the menu uses them for its list and tabs. smp cloth and hair keep moving in a paused menu
    options.flags = FUCK::CameraSessionFlags::kResumable | FUCK::CameraSessionFlags::kNoKeyboard | FUCK::CameraSessionFlags::kRunPhysics;
    if (!moving && turnCharacter) {
        options.flags |= FUCK::CameraSessionFlags::kFaceCamera;
    }
    if (cameraToFront && !frontCorner && !furnitureBase) {
        options.flags |= FUCK::CameraSessionFlags::kCameraToFront;
    }
    if (freshBase && !frontCorner && !furnitureBase) {
        options.flags |= FUCK::CameraSessionFlags::kFreshBase;
    }

    // no turning from behind, on a seat, in a bed, on a horse, holding someone, or a merchant, where it would look wrong
    if (fromBehind || seated || riding || holding || shownMerchant || crafting) {
        options.flags |= FUCK::CameraSessionFlags::kNoTurning;
    }

    // opens where the camera was left last time, otherwise from the default framing. not on the move, as that view is from behind and the
    // remembered one is from the front, nor in containers, on a horse, seated, or in a conversation, as a remembered angle wouldn't suit the
    // spot, nor for Apparel Preview, which frames the whole body
    if (Settings::values.rememberCamera && !moving && !mounted && !seated && !talking && !shownMerchant && !previewShowsPlayer && !crafting &&
        menuName != RE::ContainerMenu::MENU_NAME) {
        options.flags |= FUCK::CameraSessionFlags::kRememberCamera;

        // and how they were turned, which wins over turning them to face the camera. not holding someone, who'd be seen catching up
        if (Settings::values.rememberFacing && !holding) {
            options.flags |= FUCK::CameraSessionFlags::kRememberFacing;
        }
    }


    // the obstruction hiding goes by the same part of the body. not on a dragon, whose framing already shows the rider with some of it.
    // a merchant always, as across a counter they're seen from the chest up, and a desk's alchemy lab or enchanter. never the player shown
    // for Apparel Preview, so the piece is seen whole, nor at other benches
    const bool onDragon = riddenRace && riddenRace->data.flags.any(RE::RACE_DATA::Flag::kFlies);
    upperBody = shownMerchant || (bench && bench->desk) || (Settings::values.upperBody && !onDragon && !previewShowsPlayer && !crafting);
    if (upperBody) {
        options.flags |= FUCK::CameraSessionFlags::kFrameUpperBody;
    }

    // an animation can kneel the player at a chest, or settle them into a seat or bed, so the framing follows. framed from the chest up,
    // it follows drawing a weapon or crouching too, as idle sway doesn't change the upper body's measure
    if (looting || seated || upperBody) {
        options.flags |= FUCK::CameraSessionFlags::kFollowPose;
    }
    if (holding) {
        options.framingMargin = HOLDING_MARGIN;
    }
    if (bench && bench->desk) {
        options.framingMargin = CLOSE_WORK_MARGIN;
    }
    framingMargin = options.framingMargin;
    options.startFramed = !bench;
    options.framingRect = GetFraming(GetListEdge(), framingAcross);

    if (!FUCK::BeginCameraSession(options)) {
        logger::warn("[MenuCamera] FUCK's camera wouldn't start.");
    }
    if (bench && !bench->shot) {
        FUCK::SetCameraBodyFraming(this, true, options.framingRect, options.framingMargin);
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
    Obstructions::RestoreAll();
    RemoveShade();
    ShowPlayer();

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* camera = RE::PlayerCamera::GetSingleton();

    if (disabledControls) {
        RE::ControlMap::GetSingleton()->ToggleControls(disabledControls.get(), true, false);
        disabledControls.reset();
    }

    // not between the merchant and the player on a restart, which goes straight back to third person, nor from the furniture camera,
    // which was never left
    if (cachedFirstPerson && !restarting && !furnitureBase) {
        // at a workbench, once the player's left it (see OnPerspectiveUpdate), as switching while they get up breaks Improved Camera
        if (GetWorkbench(player)) {
            firstPersonAfterBench = true;
        } else {
            camera->ForceFirstPerson();
        }
    }
    furnitureBase = false;
    benchPlaced = false;
    if (!restarting) {
        previewShowsPlayer = false;
        previewOffFor = 0.0f;
        moreHUDHidden = false;
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
    merchantShown    = false;
    hidingPlayer     = false;
    keepObstructions = false;

    mount = {};

    // anyone held goes back to where the game has them, for the mod holding them to place again
    for (const auto& one : held) {
        if (const auto actor = one.actor.get()) {
            actor->Update3DPosition(true);
        }
    }
    held.clear();
    partner = {};

    // their look only if it was levelled, as a conversation camera aims with it
    if (pitchLevelled) {
        player->data.angle.x = playerAngleX;
    }
    player->data.angle.z = playerAngleZ;

    camera->worldFOV = worldFOV;
    camera->Update();
    player->Update3DPosition(true);
}

void MenuCamera::Update(float interval)
{
    if (!active) {
        return;
    }

    if (FollowApparelPreview(interval)) {
        return;
    }

    auto* subject = GetSubject();
    HoldHeadStill(subject);

    // a mod can give back controls locked here (a paraglider landing), so they're locked again
    if (disabledControls) {
        auto* controlMap = RE::ControlMap::GetSingleton();
        for (const auto flag : CONTROLS) {
            if (disabledControls.any(flag) && controlMap->AreControlsEnabled(flag)) {
                controlMap->ToggleControls(flag, false, false);
            }
        }
    }

    // FUCK-RACE's cap, so one long frame can't skip the idle ahead
    interval = std::min(interval, 0.1f);

    // a paused world skips the animation and scene update of the one shown, their horse, and anyone they hold, so it's done here (see
    // IsAnimatedHere for who's left out). with skyrim souls the game runs it itself. the horse first, as the rider sits on it
    const bool paused = RE::UI::GetSingleton()->GameIsPaused();
    const auto shown = GetShown();
    if (!playerFrozen && paused) {
        for (auto* actor : shown) {
            if (!IsAnimatedHere(actor)) {
                continue;
            }

            actor->UpdateAnimation(interval);
            if (auto* root = actor->Get3D(false)) {
                RE::NiUpdateData data{};
                root->Update(data);
            }
        }

        // anyone held moves with what carries them, now it's moved this frame
        for (const auto& one : held) {
            const auto actor = one.actor.get();
            auto* root = actor ? actor->Get3D(false) : nullptr;
            if (!root || !one.carriedBy) {
                continue;
            }
            const RE::NiTransform at = one.carriedBy->world * one.offset;
            root->local = root->parent ? root->parent->world.Invert() * at : at;
            RE::NiUpdateData data{};
            root->Update(data);
        }

        // and who they're talking to, as across a counter they're in view too
        if (const auto other = partner.get(); other && std::ranges::find(shown, other.get()) == shown.end()) {
            other->UpdateAnimation(interval);
            if (auto* root = other->Get3D(false)) {
                RE::NiUpdateData data{};
                root->Update(data);
            }
        }
    }

    // after the menu, so the camera frames this frame's pose. with skyrim souls FUCK also keeps it on top of the game's own camera
    FUCK::UpdateCamera(this, interval);


    // once the camera is placed, so it's what this frame sees from. a hidden player is hidden again after, in case anything shows them
    if (!keepObstructions) {
        Obstructions::Update(interval, subject, shown, upperBody);
    }
    if (hidingPlayer) {
        HidePlayer();
    }

    UpdateShade(interval);
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

// freezes or thaws the one shown to match the setting, while a menu is open
void MenuCamera::ApplyAnimation()
{
    SetFrozen(active && !Settings::values.animateCharacter);
}

// everyone in view as part of the one shown: their horse, then them, then anyone they hold
std::vector<RE::Actor*> MenuCamera::GetShown() const
{
    std::vector<RE::Actor*> shown;
    if (const auto horse = mount.get()) {
        shown.push_back(horse.get());
    }
    shown.push_back(GetSubject());
    for (const auto& one : held) {
        if (const auto actor = one.actor.get()) {
            shown.push_back(actor.get());
        }
    }
    return shown;
}

// in barter, the player is shown in the merchant's place while Apparel Preview has a piece on them, to see it, and the merchant again once
// nothing's been previewed for a moment, so passing over rows that can't be previewed doesn't swap back and forth. true if it restarted
bool MenuCamera::FollowApparelPreview(float interval)
{
    constexpr float BACK_AFTER = 1.0f;  // seconds without a preview before the merchant's shown again

    if (shownMenu != RE::BarterMenu::MENU_NAME || (!merchantShown && !previewShowsPlayer)) {
        return false;
    }

    const bool previewing = IsApparelPreviewing();
    if (previewing && merchantShown) {
        previewShowsPlayer = true;
        previewOffFor = 0.0f;
        Restart();
        if (auto* models = RE::Inventory3DManager::GetSingleton()) {
            models->UnloadInventoryItem();
        }
        HideItemCards(true);
        return true;
    }

    previewOffFor = previewing ? 0.0f : previewOffFor + interval;
    if (previewShowsPlayer && previewOffFor >= BACK_AFTER) {
        previewShowsPlayer = false;
        HideItemCards(false);
        Restart();
        return true;
    }

    // each frame, after the menu's own update, as the card fades itself back in as the selection changes
    if (previewShowsPlayer) {
        HideItemCards(true);
    }
    return false;
}

// the item cards out of the way of the piece being previewed. the menu's own fade is used, so the next row highlighted brings it back
void MenuCamera::HideItemCards(bool hide)
{
    const auto view = RE::UI::GetSingleton()->GetMovieView(shownMenu);
    if (!view) {
        moreHUDHidden = false;
        return;
    }

    RE::GFxValue card;
    const bool found = (view->GetVariable(&card, "_root.Menu_mc.itemCard") && card.IsDisplayObject()) ||
                       (view->GetVariable(&card, "_root.Menu_mc.ItemCard_mc") && card.IsDisplayObject());
    if (found && hide) {
        card.Invoke("FadeOutCard");
        RE::GFxValue faded{ false };
        view->SetVariable("_root.Menu_mc._bItemCardFadedIn", faded, RE::GFxMovie::SetVarType::kNormal);
    }

    RE::GFxValue moreHUD;
    if (view->GetVariable(&moreHUD, "_root.AHZItemCardContainer") && moreHUD.IsDisplayObject()) {
        RE::GFxValue::DisplayInfo info;
        moreHUD.GetDisplayInfo(&info);
        if (hide && info.GetVisible()) {
            info.SetVisible(false);
            moreHUD.SetDisplayInfo(info);
            moreHUDHidden = true;
        } else if (!hide && moreHUDHidden) {
            info.SetVisible(true);
            moreHUD.SetDisplayInfo(info);
            moreHUDHidden = false;
        }
    }
}

// shows someone else in the same menu: everything goes back, then is set up again, staying in third person between. the first-person cache
// holds still, as putting the camera back would otherwise record third person in it
void MenuCamera::Restart()
{
    const std::string menuName = shownMenu;
    const bool wasCacheFrozen = cacheFrozen;
    cacheFrozen = true;
    restarting = true;
    Stop();
    Start(menuName);
    restarting = false;
    cacheFrozen = wasCacheFrozen;

    // if it can't start again (combat began), first person comes back as a close would
    if (!active && cachedFirstPerson) {
        RE::PlayerCamera::GetSingleton()->ForceFirstPerson();
    }
}

// the menu's edges darken while the player sneaks unseen and clear as they're noticed, so it shows whether a theft would be seen. not in
// barter or a follower's inventory. a paused menu keeps how it was as it opened, as nobody can notice them till it closes
void MenuCamera::UpdateShade(float interval)
{
    constexpr float EASE  = 0.4f;   // seconds to darken or clear
    constexpr float CHECK = 0.25f;  // seconds between detection checks

    auto* player = RE::PlayerCharacter::GetSingleton();
    const bool sneaking = shadeAllowed && Settings::values.sneakVignette && player->IsSneaking();
    if (sneaking) {
        shadeCheckIn -= interval;
        if (shadeCheckIn <= 0.0f) {
            shadeCheckIn = CHECK;
            unseen = IsUnseen(player);
        }
    }

    const float target = sneaking && unseen ? 1.0f : 0.0f;
    const float step = interval / EASE;
    shade = target > shade ? std::min(target, shade + step) : std::max(target, shade - step);

    const auto view = RE::UI::GetSingleton()->GetMovieView(shownMenu);
    if (!view) {
        return;
    }

    RE::GFxValue clip;
    if (!view->GetVariable(&clip, SHADE_PATH) || !clip.IsDisplayObject()) {
        if (shade <= 0.0f) {
            return;
        }
        CreateShade(view.get());
        if (!view->GetVariable(&clip, SHADE_PATH) || !clip.IsDisplayObject()) {
            return;
        }
    }

    RE::GFxValue::DisplayInfo info;
    clip.GetDisplayInfo(&info);
    info.SetAlpha(shade * 100.0);
    clip.SetDisplayInfo(info);
}

void MenuCamera::RemoveShade()
{
    shade = 0.0f;
    shadeCheckIn = 0.0f;
    unseen = false;
    const auto view = RE::UI::GetSingleton()->GetMovieView(shownMenu);
    RE::GFxValue clip;
    if (view && view->GetVariable(&clip, SHADE_PATH) && clip.IsDisplayObject()) {
        clip.Invoke("removeMovieClip");
    }
}

// the player out of the way of whoever's shown in front of them, by each part under their own 3D (not first person's), as outdoors the
// game shows the 3D itself again every frame. only parts that were drawn, so only those are put back
void MenuCamera::HidePlayer()
{
    auto* model = RE::PlayerCharacter::GetSingleton()->Get3D(false);
    auto* node = model ? model->AsNode() : nullptr;
    if (!node) {
        return;
    }
    for (auto& part : node->GetChildren()) {
        if (part && !part->GetAppCulled()) {
            part->SetAppCulled(true);
            hiddenParts.emplace_back(part);
        }
    }
}

void MenuCamera::ShowPlayer()
{
    for (auto& part : hiddenParts) {
        if (part) {
            part->SetAppCulled(false);
        }
    }
    hiddenParts.clear();
}

// whether this frame's animation is run here for someone shown: not for a held animal another mod poses itself, nor for the one holding
// it, as that mod poses their arms round it too
bool MenuCamera::IsAnimatedHere(RE::Actor* actor) const
{
    const auto posedBy = [&](const Held& one) { return !one.animated && (actor == GetSubject() || one.actor.get().get() == actor); };
    return std::ranges::none_of(held, posedBy);
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
    if (!benchPlaced) {
        FUCK::SetCameraBodyFraming(this, true, GetFraming(GetListEdge(), framingAcross), framingMargin);
    }
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
    const bool buttonsDown = FUCK::IsInputDown(FUCK::Keys::kMouse_Right) || FUCK::IsInputDown(FUCK::Keys::kMouse_Middle);
    if (!buttonsDown) {
        mouseOnList = IsCursorOverList();
    }
    state.mouseLocked = mouseOnList;
}

void MenuCamera::OnCameraTakenOver()
{
    logger::info("[MenuCamera] Another plugin took the camera.");
}
