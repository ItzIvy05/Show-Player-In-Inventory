#include "ItemDisplay.h"
#include "Settings.h"

namespace
{
    // the preview's offsets are in the movie's pixels, as the card's are. one unit of the game's preview position is 8 of them at 1280x720
    constexpr float PIXELS_PER_UNIT = 8.0f;

    // each thing moved here is kept as last set. anything else found there is the menu's own placement (a new menu, SkyUI placing it, a
    // load), taken as the new base, so the offsets always go on top of wherever the menu or another mod puts it

    struct Rect
    {
        float left   = 0.0f;
        float top    = 0.0f;
        float right  = 0.0f;
        float bottom = 0.0f;
    };

    struct Card
    {
        RE::GFxMovieView* view = nullptr;
        bool  placed    = false;
        float baseX     =   0.0f;
        float baseY     =   0.0f;
        float baseScale = 100.0f;
        float x         =   0.0f;
        float y         =   0.0f;
        float scale     = 100.0f;
    };

    struct Area
    {
        RE::GFxMovieView* view = nullptr;
        float baseX      = 0.0f;
        float baseY      = 0.0f;
        float baseWidth  = 0.0f;
        float baseHeight = 0.0f;
        float x          = 0.0f;
        float y          = 0.0f;
        float width      = 0.0f;
        float height     = 0.0f;
    };

    struct Value
    {
        bool  valid = false;
        float base  = 0.0f;
        float last  = 0.0f;
    };

    Card card;
    Area rotationArea;

    // the card's spot in the item menus as they lay it out, before the offsets, which a bench's card is put on so the sliders place it
    // the same everywhere
    struct CardSpot
    {
        bool valid    = false;
        float centreX = 0.0f;
        float top     = 0.0f;
        float width   = 0.0f;
    };

    CardSpot itemMenuCard;

    // and the rotation area's, which a bench's goes on with its preview
    struct AreaSpot
    {
        bool valid   = false;
        float x      = 0.0f;
        float y      = 0.0f;
        float width  = 0.0f;
        float height = 0.0f;
    };

    AreaSpot itemMenuArea;

    // the game's preview settings, read as an item loads, and its preview manager's own copy for the menu open
    enum Axis { kX, kZ, kScale };
    struct PreviewSetting
    {
        const char* name;
        Axis axis;
        Value value;
    };

    std::array<PreviewSetting, 12> previewSettings{ {
        { "fInventory3DItemPosX:Interface", kX },
        { "fInventory3DItemPosXWide:Interface", kX },
        { "fMagic3DItemPosX:Interface", kX },
        { "fMagic3DItemPosXWide:Interface", kX },
        { "fInventory3DItemPosZ:Interface", kZ },
        { "fInventory3DItemPosZWide:Interface", kZ },
        { "fMagic3DItemPosZ:Interface", kZ },
        { "fMagic3DItemPosZWide:Interface", kZ },
        { "fInventory3DItemPosScale:Interface", kScale },
        { "fInventory3DItemPosScaleWide:Interface", kScale },
        { "fMagic3DItemPosScale:Interface", kScale },
        { "fMagic3DItemPosScaleWide:Interface", kScale },
    } };
    std::array<Value, 6> managerValues;  // itemPos x and z, itemScale, and their copies

    bool GetNumber(const RE::GFxValue& clip, const char* member, float& out)
    {
        RE::GFxValue value;
        if (!clip.GetMember(member, &value) || !value.IsNumber()) {
            return false;
        }
        out = static_cast<float>(value.GetNumber());
        return true;
    }

    // the base taken afresh if the value isn't what was last set, then the target set. false if it was already there
    bool Apply(float& current, Value& value, float offset, bool scale)
    {
        if (!value.valid || current != value.last) {
            value = { true, current, current };
        }
        const float target = scale ? value.base * offset : value.base + offset;
        if (target == current) {
            return false;
        }
        current = target;
        value.last = target;
        return true;
    }

    bool IsCardSet()
    {
        const auto& values = Settings::values;
        return values.itemCardX != 0.0f || values.itemCardY != 0.0f || values.itemCardScale != 1.0f;
    }

    bool IsPreviewSet()
    {
        const auto& values = Settings::values;
        return values.itemPreviewX != 0.0f || values.itemPreviewY != 0.0f || values.itemPreviewScale != 1.0f;
    }

    bool IsCraftingMenu(RE::GFxMovieView* view)
    {
        RE::GFxValue bench;
        return view->GetVariable(&bench, "_root.Menu.ItemInfoHolder") && bench.IsDisplayObject();
    }

    // where a clip is drawn on the menu
    bool GetBounds(RE::GFxMovieView* view, RE::GFxValue& clip, Rect& out)
    {
        RE::GFxValue root;
        RE::GFxValue bounds;
        return view->GetVariable(&root, "_root") && clip.Invoke("getBounds", &bounds, &root, 1) && bounds.IsObject() &&
               GetNumber(bounds, "xMin", out.left) && GetNumber(bounds, "yMin", out.top) && GetNumber(bounds, "xMax", out.right) &&
               GetNumber(bounds, "yMax", out.bottom) && out.right > out.left;
    }

    bool GetBounds(RE::GFxMovieView* view, RE::GFxValue& clip, float& left, float& top, float& right)
    {
        Rect rect;
        if (!GetBounds(view, clip, rect)) {
            return false;
        }
        left  = rect.left;
        top   = rect.top;
        right = rect.right;
        return true;
    }

    // SkyUI places the card and rotation area once its config has loaded, with the overrides its MCM sends as the menu opens (up to a
    // few seconds), setting where they go across but adding its offset to where the card is up and down. until then the menu's
    // _config isn't set
    bool IsSkyUIWaiting(RE::GFxMovieView* view)
    {
        RE::GFxValue manager;
        if (!view->GetVariable(&manager, "_global.skyui.util.ConfigManager") || manager.IsUndefined()) {
            return false;
        }
        RE::GFxValue config;
        return !(view->GetVariable(&config, "_root.Menu_mc._config") && config.IsObject()) &&
               !(view->GetVariable(&config, "_root.Menu._config") && config.IsObject());
    }

    // what SkyUI places (the card's holder), the vanilla menus' card, or the crafting menu's holder
    bool GetCard(RE::GFxMovieView* view, RE::GFxValue& clip)
    {
        RE::GFxValue skyui;
        if (view->GetVariable(&skyui, "_root.Menu_mc.itemCard") && skyui.IsDisplayObject() && skyui.GetMember("_parent", &clip) &&
            clip.IsDisplayObject()) {
            return true;
        }
        for (const char* path : { "_root.Menu_mc.ItemCard_mc", "_root.Menu.ItemInfoHolder" }) {
            if (view->GetVariable(&clip, path) && clip.IsDisplayObject()) {
                return true;
            }
        }
        return false;
    }

    float Aspect()
    {
        const ImVec2 display = FUCK::GetDisplaySize();
        return display.y > 0.0f ? display.x / display.y : 0.0f;
    }

    // the item menus' spots kept in the settings, so a bench opened before any item menu still has them. only on the screen they were
    // taken on, as the menus lay out by its shape
    void SaveSpots()
    {
        auto& values = Settings::values;
        const float aspect = Aspect();
        const bool same = values.spotAspect == aspect && values.cardSpotX == itemMenuCard.centreX && values.cardSpotTop == itemMenuCard.top &&
                          values.cardSpotWidth == itemMenuCard.width && values.areaSpotX == itemMenuArea.x && values.areaSpotY == itemMenuArea.y &&
                          values.areaSpotWidth == itemMenuArea.width && values.areaSpotHeight == itemMenuArea.height;
        if (same || !itemMenuCard.valid || !itemMenuArea.valid) {
            return;
        }
        values.spotAspect     = aspect;
        values.cardSpotX      = itemMenuCard.centreX;
        values.cardSpotTop    = itemMenuCard.top;
        values.cardSpotWidth  = itemMenuCard.width;
        values.areaSpotX      = itemMenuArea.x;
        values.areaSpotY      = itemMenuArea.y;
        values.areaSpotWidth  = itemMenuArea.width;
        values.areaSpotHeight = itemMenuArea.height;
        Settings::Save();
    }

    void LoadSpots()
    {
        const auto& values = Settings::values;
        if (values.spotAspect <= 0.0f || values.spotAspect != Aspect()) {
            return;
        }
        if (!itemMenuCard.valid && values.cardSpotWidth > 0.0f) {
            itemMenuCard = { true, values.cardSpotX, values.cardSpotTop, values.cardSpotWidth };
        }
        if (!itemMenuArea.valid && values.areaSpotWidth > 0.0f) {
            itemMenuArea = { true, values.areaSpotX, values.areaSpotY, values.areaSpotWidth, values.areaSpotHeight };
        }
    }

    // the top of the menu's bottom bar (SkyUI's, the crafting menu's) where it's shown, which the card is kept above as its text can't be
    // read under it. a reskin that drops or hides the bar leaves the screen's edge
    bool GetBottomBarTop(RE::GFxMovieView* view, float& top)
    {
        RE::GFxValue bar;
        const bool found = (view->GetVariable(&bar, "_root.Menu_mc.bottomBar") && bar.IsDisplayObject()) ||
                           (view->GetVariable(&bar, "_root.Menu.BottomBarInfo") && bar.IsDisplayObject());
        if (!found) {
            return false;
        }
        RE::GFxValue::DisplayInfo info;
        bar.GetDisplayInfo(&info);
        Rect rect;
        if (!info.GetVisible() || info.GetAlpha() <= 0.0 || !GetBounds(view, bar, rect)) {
            return false;
        }
        top = rect.top;
        return true;
    }

    // the card moved back onto the menu where it would go off it or under its bottom bar, as it is now: a bench's card is taller with
    // its ingredients, so placed low in the inventory it would be cut off there
    void KeepOnScreen(RE::GFxMovieView* view, RE::GFxValue& clip, float x, float y, float scale, float& toX, float& toY, float toScale)
    {
        RE::GFxValue visible;
        Rect now;
        Rect screen;
        float width;
        float height;
        if (scale <= 0.0f || !GetBounds(view, clip, now) || !view->GetVariable(&visible, "Stage.visibleRect") || !visible.IsObject() ||
            !GetNumber(visible, "x", screen.left) || !GetNumber(visible, "y", screen.top) || !GetNumber(visible, "width", width) ||
            !GetNumber(visible, "height", height)) {
            return;
        }
        screen.right = screen.left + width;
        screen.bottom = screen.top + height;
        if (float barTop; GetBottomBarTop(view, barTop) && barTop > screen.top && barTop < screen.bottom) {
            screen.bottom = barTop;
        }

        // where it would be drawn once moved and sized
        const float by = toScale / scale;
        const auto fit = [](float& at, float low, float high, float min, float max) {
            if (low < min || high - low > max - min) {
                at += min - low;
            } else if (high > max) {
                at -= high - max;
            }
        };
        fit(toX, toX + (now.left - x) * by, toX + (now.right - x) * by, screen.left, screen.right);
        fit(toY, toY + (now.top - y) * by, toY + (now.bottom - y) * by, screen.top, screen.bottom);
    }

    // moreHUD Inventory Edition's large card follows SkyUI's only as the card changes, so it's moved and sized with it
    void MoveMoreHUD(RE::GFxMovieView* view, float dx, float dy, float scaleBy)
    {
        RE::GFxValue clip;
        float x;
        float y;
        float scale;
        if (!view->GetVariable(&clip, "_root.AHZItemCardContainer") || !clip.IsDisplayObject() || !GetNumber(clip, "_x", x) ||
            !GetNumber(clip, "_y", y) || !GetNumber(clip, "_xscale", scale)) {
            return;
        }
        clip.SetMember("_x", x + dx);
        clip.SetMember("_y", y + dy);
        clip.SetMember("_xscale", scale * scaleBy);
        clip.SetMember("_yscale", scale * scaleBy);
    }

    void UpdateCard(RE::GFxMovieView* view)
    {
        RE::GFxValue clip;
        float x;
        float y;
        float scale;
        if (!GetCard(view, clip) || !GetNumber(clip, "_x", x) || !GetNumber(clip, "_y", y) || !GetNumber(clip, "_xscale", scale) ||
            scale <= 0.0f) {
            return;
        }

        if (card.view != view || !card.placed) {
            card = { view, true, x, y, scale, x, y, scale };

            // the item menus' spot noted, and a bench's card put on it, sized to match, from its own top left
            float left;
            float top;
            float right;
            if (GetBounds(view, clip, left, top, right)) {
                if (!IsCraftingMenu(view)) {
                    itemMenuCard = { true, (left + right) * 0.5f, top, right - left };
                    SaveSpots();
                } else if (LoadSpots(); itemMenuCard.valid && IsCardSet()) {
                    const float by = itemMenuCard.width / (right - left);
                    card.baseScale = scale * by;
                    card.baseX = itemMenuCard.centreX - itemMenuCard.width * 0.5f - (left - x) * by;
                    card.baseY = itemMenuCard.top - (top - y) * by;
                }
            }
        } else if (x != card.x || y != card.y || scale != card.scale) {
            // moved since by the menu or another mod, by however far it moved, as they move it by offsets (SkyUI's vertical one,
            // moreHUD's at a bench), so ours stay on top
            card.baseX     += x - card.x;
            card.baseY     += y - card.y;
            card.baseScale *= scale / card.scale;
            card.x          = x;
            card.y          = y;
            card.scale      = scale;
        }

        // positive is right and up
        const auto& values = Settings::values;
        float toX = card.baseX + values.itemCardX;
        float toY = card.baseY - values.itemCardY;
        const float toScale = card.baseScale * values.itemCardScale;
        if (IsCardSet()) {
            KeepOnScreen(view, clip, x, y, scale, toX, toY, toScale);
        }
        if (toX == x && toY == y && toScale == scale) {
            return;
        }

        clip.SetMember("_x", toX);
        clip.SetMember("_y", toY);
        clip.SetMember("_xscale", toScale);
        clip.SetMember("_yscale", toScale);

        // as Flash keeps them, rounded to its twips, so the next frame finds them unchanged
        GetNumber(clip, "_x", card.x);
        GetNumber(clip, "_y", card.y);
        GetNumber(clip, "_xscale", card.scale);
        MoveMoreHUD(view, card.x - x, card.y - y, card.scale / scale);
    }

    // SkyUI's area for turning the preview with the mouse, moved and sized round its middle with the preview
    void UpdateRotationArea(RE::GFxMovieView* view)
    {
        RE::GFxValue clip;
        float x;
        float y;
        float width;
        float height;
        const bool found = (view->GetVariable(&clip, "_root.Menu_mc.mouseRotationRect") && clip.IsDisplayObject()) ||
                           (view->GetVariable(&clip, "_root.Menu.MouseRotationRect") && clip.IsDisplayObject());
        if (!found || !GetNumber(clip, "_x", x) || !GetNumber(clip, "_y", y) || !GetNumber(clip, "_width", width) ||
            !GetNumber(clip, "_height", height)) {
            return;
        }

        auto& area = rotationArea;
        if (area.view != view || x != area.x || y != area.y || width != area.width || height != area.height) {
            area = { view, x, y, width, height, x, y, width, height };
            if (!IsCraftingMenu(view)) {
                itemMenuArea = { true, x, y, width, height };
                SaveSpots();
            } else if (LoadSpots(); itemMenuArea.valid && IsPreviewSet()) {
                area.baseX = itemMenuArea.x;
                area.baseY = itemMenuArea.y;
                area.baseWidth = itemMenuArea.width;
                area.baseHeight = itemMenuArea.height;
            }
        }

        const auto& values   = Settings::values;
        const float toWidth  = area.baseWidth * values.itemPreviewScale;
        const float toHeight = area.baseHeight * values.itemPreviewScale;
        const float toX      = area.baseX + values.itemPreviewX - (toWidth - area.baseWidth) * 0.5f;
        const float toY      = area.baseY - values.itemPreviewY - (toHeight - area.baseHeight) * 0.5f;
        if (toX == x && toY == y && toWidth == width && toHeight == height) {
            return;
        }

        clip.SetMember("_x", toX);
        clip.SetMember("_y", toY);
        clip.SetMember("_width", toWidth);
        clip.SetMember("_height", toHeight);

        GetNumber(clip, "_x", area.x);
        GetNumber(clip, "_y", area.y);
        GetNumber(clip, "_width", area.width);
        GetNumber(clip, "_height", area.height);
    }

    // the models already loaded keep where they were placed as they loaded, so they're moved by however far the preview's position and
    // scale just moved. later ones load where it now is
    void MoveLoadedModels(RE::Inventory3DManager* manager, const RE::NiPoint3& by, float scaleBy)
    {
        for (auto& model : manager->GetRuntimeData().loadedModels) {
            if (auto* node = model.spModel.get()) {
                node->local.translate += by;
                node->local.scale *= scaleBy;
                RE::NiUpdateData data{};
                node->Update(data);
            }
        }
    }

    void UpdatePreview()
    {
        // positive is right and up. a lower x is further right
        const auto& values   = Settings::values;
        const float offsetX  = -values.itemPreviewX / PIXELS_PER_UNIT;
        const float offsetZ  = values.itemPreviewY / PIXELS_PER_UNIT;
        const auto offsetFor = [&](Axis axis) { return axis == kX ? offsetX : axis == kZ ? offsetZ : values.itemPreviewScale; };

        auto* ini = RE::INISettingCollection::GetSingleton();
        for (auto& one : previewSettings) {
            if (auto* setting = ini ? ini->GetSetting(one.name) : nullptr) {
                Apply(setting->data.f, one.value, offsetFor(one.axis), one.axis == kScale);
            }
        }

        auto* manager = RE::Inventory3DManager::GetSingleton();
        if (!manager) {
            return;
        }

        // the preview manager reads the settings as a menu opens, so what it holds may already be one of them as set here, the offsets
        // already on. its base is then that setting's
        const auto knownBase = [&](Axis axis, float current, Value& value) {
            if (value.valid && current == value.last) {
                return;
            }
            for (const auto& one : previewSettings) {
                if (one.axis == axis && one.value.valid && current == one.value.last) {
                    value = { true, one.value.base, current };
                    return;
                }
            }
        };
        knownBase(kX,     manager->itemPos.x,     managerValues[0]);
        knownBase(kZ,     manager->itemPos.z,     managerValues[1]);
        knownBase(kScale, manager->itemScale,     managerValues[2]);
        knownBase(kX,     manager->itemPosCopy.x, managerValues[3]);
        knownBase(kZ,     manager->itemPosCopy.z, managerValues[4]);
        knownBase(kScale, manager->itemScaleCopy, managerValues[5]);

        // every menu's preview goes where the inventory's is, depth included, so the sliders place it the same everywhere: benches, the
        // container and magic menus place theirs a little differently
        const RE::NiPoint3 from = manager->itemPos;
        const float scale = manager->itemScale;
        bool changed = false;
        if (IsPreviewSet()) {
            const auto onInventory = [&](Value& value, float current, const PreviewSetting& setting) {
                if (!value.valid || current != value.last) {
                    value = { true, setting.value.base, current };
                } else {
                    value.base = setting.value.base;
                }
            };
            if (previewSettings[1].value.valid && previewSettings[5].value.valid && previewSettings[9].value.valid) {
                onInventory(managerValues[0], manager->itemPos.x,     previewSettings[1]);
                onInventory(managerValues[1], manager->itemPos.z,     previewSettings[5]);
                onInventory(managerValues[2], manager->itemScale,     previewSettings[9]);
                onInventory(managerValues[3], manager->itemPosCopy.x, previewSettings[1]);
                onInventory(managerValues[4], manager->itemPosCopy.z, previewSettings[5]);
                onInventory(managerValues[5], manager->itemScaleCopy, previewSettings[9]);
            }
            auto* depth = ini ? ini->GetSetting("fInventory3DItemPosYWide:Interface") : nullptr;
            if (depth && manager->itemPos.y != depth->data.f) {
                manager->itemPos.y = depth->data.f;
                manager->itemPosCopy.y = depth->data.f;
                changed = true;
            }
        }

        changed |= Apply(manager->itemPos.x, managerValues[0], offsetX, false);
        changed |= Apply(manager->itemPos.z, managerValues[1], offsetZ, false);
        changed |= Apply(manager->itemScale, managerValues[2], values.itemPreviewScale, true);
       
        Apply(manager->itemPosCopy.x, managerValues[3], offsetX, false);
        Apply(manager->itemPosCopy.z, managerValues[4], offsetZ, false);
        Apply(manager->itemScaleCopy, managerValues[5], values.itemPreviewScale, true);

        if (changed && scale > 0.0f) {
            MoveLoadedModels(manager, manager->itemPos - from, manager->itemScale / scale);
        }
    }
}

namespace ItemDisplay
{
    void Update(RE::GFxMovieView* view)
    {
        if (!Settings::values.enabled || !view) {
            return;
        }

        // a menu lays itself out over its first frames, and SkyUI's once its config is in (see IsSkyUIWaiting), so it's left that long,
        // then taken as found. a gap in the updates is a menu closed, as a new one can reuse the view
        constexpr int SETTLE_FRAMES = 10;
        constexpr auto CLOSED = std::chrono::milliseconds(250);
        static RE::GFxMovieView* settling = nullptr;
        static int frames = 0;
        static bool ready = false;
        static auto last = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (view != settling || now - last > CLOSED) {
            settling = view;
            frames = 0;
            ready = false;
            card = {};
            rotationArea = {};
        }
        last = now;

        constexpr int GIVE_UP_FRAMES = 600;
        if (!ready) {
            if (frames < SETTLE_FRAMES || (IsSkyUIWaiting(view) && frames < GIVE_UP_FRAMES)) {
                ++frames;
                return;
            }
            ready = true;
        }

        UpdateCard(view);
        UpdateRotationArea(view);
        UpdatePreview();
    }

}
