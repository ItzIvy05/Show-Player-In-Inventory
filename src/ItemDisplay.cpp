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

        // its middle from its origin, per 1% of its size, and its parent's offset. measured when placed and once laid out, as its
        // bounds change with the item shown
        bool  hasCentre    = false;
        float centreOffset = 0.0f;
        float parentShift  = 0.0f;
        bool  spotTaken    = false;  // its base noted as the item menus' spot, once the menu's laid out
        bool  onSpot       = false;  // a bench's, put on that spot
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
        bool  spotTaken  = false;  // its base noted as the item menus' spot, once the menu's laid out
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

    // the base taken afresh if the value isn't what was last set, then set to what `to` makes of it. false if it was already there
    template <class To>
    bool Apply(float& current, Value& value, To&& to)
    {
        if (!value.valid || current != value.last) {
            value = { true, current, current };
        }
        const float target = to(value.base);
        if (target == current) {
            return false;
        }
        current = target;
        value.last = target;
        return true;
    }

    // the sizes as used, never lower than the smallest: at nothing the card's clip is left unplaceable and the loaded models can't be
    // sized back up, and a saved or typed-in zero would do the same
    float CardScale()
    {
        return std::max(Settings::values.itemCardScale, Settings::MIN_ITEM_SCALE);
    }

    float PreviewScale()
    {
        return std::max(Settings::values.itemPreviewScale, Settings::MIN_ITEM_SCALE);
    }

    // the menu's size on screen, as last found
    float visibleWidth  = 0.0f;
    float visibleHeight = 0.0f;

    // the screen's shape, from the menu once found, as the display's size can misread while alt-tabbed
    float Aspect()
    {
        if (visibleWidth > 0.0f && visibleHeight > 0.0f) {
            return visibleWidth / visibleHeight;
        }
        const ImVec2 display = FUCK::GetDisplaySize();
        return display.y > 0.0f ? display.x / display.y : 0.0f;
    }

    float Lerp(float a, float b, float t)
    {
        return a + t * (b - a);
    }

    // Auto-Adjustment: placed by the screen's shape, between the rows below. across in the movie's pixels from the screen's
    // middle, the movie 720 high

    // a unit of the preview's position as a share of the screen's width, on any shape. a unit up is as many pixels
    constexpr float PREVIEW_SHARE_PER_UNIT = 0.00623f;

    // positions based on the default ini value, fInventory3DItemPosYWide
    constexpr float AUTO_DEPTH = -500.0f;

    struct AutoLayout
    {
        float aspect;
        float cardX;         // the card's middle, right of the screen's
        float previewX;      // the preview's
        float previewZ;      // the preview's height, in the game's units
        float previewScale;  // and size
    };

    // 21:9 is 43:18 (3440x1440)
    constexpr std::array<AutoLayout, 3> AUTO_LAYOUTS{ {
        // aspect        card    preview height size
        { 16.0f / 9.0f,  290.0f, 290.0f, 14.0f, 1.80f },
        { 43.0f / 18.0f, 570.0f, 565.0f, 12.0f, 1.50f },
        { 32.0f / 9.0f,  720.0f, 720.0f,  8.0f, 0.85f },
    } };

    AutoLayout GetAutoLayout()
    {
        const float aspect = Aspect();
        if (aspect <= AUTO_LAYOUTS.front().aspect) {
            return AUTO_LAYOUTS.front();
        }
        for (std::size_t i = 1; i < AUTO_LAYOUTS.size(); ++i) {
            const auto& low  = AUTO_LAYOUTS[i - 1];
            const auto& high = AUTO_LAYOUTS[i];
            if (aspect <= high.aspect) {
                const float t = (aspect - low.aspect) / (high.aspect - low.aspect);
                return { aspect, Lerp(low.cardX, high.cardX, t), Lerp(low.previewX, high.previewX, t), Lerp(low.previewZ, high.previewZ, t),
                    Lerp(low.previewScale, high.previewScale, t) };
            }
        }
        return AUTO_LAYOUTS.back();
    }

    // the preview's place across in the game's units, a lower x further right
    float AutoPreviewX(const AutoLayout& layout)
    {
        const float width = visibleWidth > 0.0f ? visibleWidth : 720.0f * Aspect();  // a 720 high movie until it's found
        return width > 0.0f ? -layout.previewX / (PREVIEW_SHARE_PER_UNIT * width) : 0.0f;
    }

    bool IsAutoAdjust()
    {
        return Settings::values.autoAdjust;
    }

    bool IsCardSet()
    {
        const auto& values = Settings::values;
        return IsAutoAdjust() || values.itemCardX != 0.0f || values.itemCardY != 0.0f || values.itemCardScale != 1.0f;
    }

    bool IsPreviewSet()
    {
        const auto& values = Settings::values;
        return IsAutoAdjust() || values.itemPreviewX != 0.0f || values.itemPreviewY != 0.0f || values.itemPreviewScale != 1.0f;
    }

    bool IsCraftingMenu(RE::GFxMovieView* view)
    {
        RE::GFxValue bench;
        return view->GetVariable(&bench, "_root.Menu.ItemInfoHolder") && bench.IsDisplayObject();
    }

    // where a clip is drawn, in another clip's space
    bool GetBoundsIn(RE::GFxValue& clip, RE::GFxValue& space, Rect& out)
    {
        RE::GFxValue bounds;
        return clip.Invoke("getBounds", &bounds, &space, 1) && bounds.IsObject() && GetNumber(bounds, "xMin", out.left) &&
               GetNumber(bounds, "yMin", out.top) && GetNumber(bounds, "xMax", out.right) && GetNumber(bounds, "yMax", out.bottom) &&
               out.right > out.left;
    }

    // and on the menu
    bool GetBounds(RE::GFxMovieView* view, RE::GFxValue& clip, Rect& out)
    {
        RE::GFxValue root;
        return view->GetVariable(&root, "_root") && GetBoundsIn(clip, root, out);
    }

    // how far across the menu a clip's parent puts it
    bool GetParentShift(RE::GFxMovieView* view, RE::GFxValue& clip, float& shift, Rect* inParent = nullptr)
    {
        RE::GFxValue parent;
        Rect onMenu;
        Rect local;
        if (!clip.GetMember("_parent", &parent) || !parent.IsDisplayObject() || !GetBounds(view, clip, onMenu) || !GetBoundsIn(clip, parent, local)) {
            return false;
        }
        shift = onMenu.left - local.left;
        if (inParent) {
            *inParent = local;
        }
        return true;
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

    // the part of the movie on screen, 720 high
    bool GetVisible(RE::GFxMovieView* view, Rect& out)
    {
        RE::GFxValue visible;
        float width;
        float height;
        if (!view->GetVariable(&visible, "Stage.visibleRect") || !visible.IsObject() || !GetNumber(visible, "x", out.left) ||
            !GetNumber(visible, "y", out.top) || !GetNumber(visible, "width", width) || !GetNumber(visible, "height", height) || width <= 0.0f ||
            height <= 0.0f) {
            return false;
        }
        out.right     = out.left + width;
        out.bottom    = out.top + height;
        visibleWidth  = width;
        visibleHeight = height;
        return true;
    }

    bool IsSkyUI(RE::GFxMovieView* view)
    {
        RE::GFxValue manager;
        return view->GetVariable(&manager, "_global.skyui.util.ConfigManager") && !manager.IsUndefined();
    }

    // SkyUI lays out the card and rotation area as its config loads, setting _config just before
    bool IsSkyUIWaiting(RE::GFxMovieView* view)
    {
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

    // whether this is the screen shape the spots were saved on. compared loosely, as the settings file rounds the number when it's
    // saved, so it never comes back exactly the same
    bool IsSpotAspect(float aspect)
    {
        return std::abs(Settings::values.spotAspect - aspect) < 0.001f;
    }

    // the item menus' spots kept in the settings, so a bench opened before any item menu still has them. only on the screen they were
    // taken on, as the menus lay out by its shape
    void SaveSpots()
    {
        auto& values = Settings::values;
        const float aspect = Aspect();
        const bool same = IsSpotAspect(aspect) && values.cardSpotX == itemMenuCard.centreX && values.cardSpotTop == itemMenuCard.top &&
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
        if (values.spotAspect <= 0.0f || !IsSpotAspect(Aspect())) {
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
    // its ingredients, so placed low in the inventory it would be cut off there. without across, only up and down
    void KeepOnScreen(RE::GFxMovieView* view, RE::GFxValue& clip, float x, float y, float scale, float& toX, float& toY, float toScale,
        bool across = true)
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
        if (across) {
            fit(toX, toX + (now.left - x) * by, toX + (now.right - x) * by, screen.left, screen.right);
        }
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

    // a bench's description, kept centred over the card
    void CentreMenuDescription(RE::GFxMovieView* view, float middle)
    {
        RE::GFxValue clip;
        Rect bounds;
        float x;
        if (!view->GetVariable(&clip, "_root.Menu.MenuDescriptionHolder") || !clip.IsDisplayObject() || !GetNumber(clip, "_x", x) ||
            !GetBounds(view, clip, bounds)) {
            return;
        }
        if (const float dx = middle - (bounds.left + bounds.right) * 0.5f; std::abs(dx) > 0.5f) {
            clip.SetMember("_x", x + dx);
        }
    }

    // settled: the menu's laid out, so the item menus' spot can be noted
    void UpdateCard(RE::GFxMovieView* view, bool settled)
    {
        RE::GFxValue clip;
        float x;
        float y;
        float scale;
        if (!GetCard(view, clip) || !GetNumber(clip, "_x", x) || !GetNumber(clip, "_y", y) || !GetNumber(clip, "_xscale", scale) ||
            scale <= 0.0f) {
            return;
        }

        // a bench's card put on the item menus' spot, sized to match. tried again once laid out
        const auto putOnSpot = [&] {
            float left;
            float top;
            float right;
            if (card.onSpot || !IsCraftingMenu(view) || !GetBounds(view, clip, left, top, right)) {
                return;
            }
            if (LoadSpots(); itemMenuCard.valid && IsCardSet()) {
                const float by = itemMenuCard.width / (right - left);
                card.baseScale = scale * by;
                card.baseX     = itemMenuCard.centreX - itemMenuCard.width * 0.5f - (left - x) * by;
                card.baseY     = itemMenuCard.top - (top - y) * by;
                card.onSpot    = true;
            }
        };

        if (card.view != view || !card.placed) {
            card = { view, true, x, y, scale, x, y, scale };
            if (Rect local; GetParentShift(view, clip, card.parentShift, &local)) {
                card.hasCentre    = true;
                card.centreOffset = ((local.left + local.right) * 0.5f - x) / scale;
            }
            putOnSpot();
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

        // the item menus' spot noted, and the card's middle measured again, now it's laid out
        if (settled && !card.spotTaken) {
            card.spotTaken = true;
            if (Rect local; GetParentShift(view, clip, card.parentShift, &local)) {
                card.hasCentre    = true;
                card.centreOffset = ((local.left + local.right) * 0.5f - x) / scale;
            }
            putOnSpot();
            // moved back to where the menu put it
            if (Rect now; !IsCraftingMenu(view) && GetBounds(view, clip, now)) {
                const float dx = card.baseX - x;
                itemMenuCard = { true, (now.left + now.right) * 0.5f + dx, now.top + card.baseY - y, now.right - now.left };
                SaveSpots();
            }
        }

        // positive is right and up. with Auto-Adjustment, the menu's height and size, at the layout's place across
        const auto& values = Settings::values;
        float toScale = card.baseScale * CardScale();
        float toX     = card.baseX + values.itemCardX;
        float toY     = card.baseY - values.itemCardY;
        if (IsAutoAdjust()) {
            toScale = card.baseScale;
            toX     = card.baseX;
            toY     = card.baseY;
            if (Rect visible; card.hasCentre && GetVisible(view, visible)) {
                const float middle = (visible.left + visible.right) * 0.5f + GetAutoLayout().cardX;
                toX = middle - card.parentShift - card.centreOffset * toScale;
            }
        }
        if (IsCardSet()) {
            KeepOnScreen(view, clip, x, y, scale, toX, toY, toScale, !IsAutoAdjust());
            if (card.hasCentre && IsCraftingMenu(view)) {
                CentreMenuDescription(view, card.parentShift + toX + card.centreOffset * toScale);
            }
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
    void UpdateRotationArea(RE::GFxMovieView* view, bool settled)
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
            if (IsCraftingMenu(view)) {
                if (LoadSpots(); itemMenuArea.valid && IsPreviewSet()) {
                    area.baseX = itemMenuArea.x;
                    area.baseY = itemMenuArea.y;
                    area.baseWidth = itemMenuArea.width;
                    area.baseHeight = itemMenuArea.height;
                }
            }
        }

        // the item menus' spot noted, now it's laid out
        if (settled && !area.spotTaken && !IsCraftingMenu(view)) {
            area.spotTaken = true;
            itemMenuArea   = { true, area.baseX, area.baseY, area.baseWidth, area.baseHeight };
            SaveSpots();
        }

        const auto& values = Settings::values;
        float toWidth  = area.baseWidth * PreviewScale();
        float toHeight = area.baseHeight * PreviewScale();
        float toX      = area.baseX + values.itemPreviewX - (toWidth - area.baseWidth) * 0.5f;
        float toY      = area.baseY - values.itemPreviewY - (toHeight - area.baseHeight) * 0.5f;

        // with Auto-Adjustment, sized and moved with the preview
        float parentShift;
        Rect visible;
        if (IsAutoAdjust() && GetVisible(view, visible) && GetParentShift(view, clip, parentShift)) {
            const auto  layout = GetAutoLayout();
            const auto& scale  = previewSettings[9].value;
            const auto& z      = previewSettings[5].value;
            const float by     = scale.valid && scale.base > 0.0f ? layout.previewScale / scale.base : 1.0f;
            const float up     = z.valid ? (layout.previewZ - z.base) * PREVIEW_SHARE_PER_UNIT * (visible.right - visible.left) : 0.0f;
            const float middle = (visible.left + visible.right) * 0.5f + layout.previewX;
            toWidth  = area.baseWidth * by;
            toHeight = area.baseHeight * by;
            toX      = middle - parentShift - toWidth * 0.5f;
            toY      = area.baseY + area.baseHeight * 0.5f - up - toHeight * 0.5f;
        }
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
        // positive is right and up, a lower x further right. with Auto-Adjustment, the layout's
        const auto& values  = Settings::values;
        const float offsetX = -values.itemPreviewX / PIXELS_PER_UNIT;
        const float offsetZ = values.itemPreviewY / PIXELS_PER_UNIT;
        const bool  forced  = IsAutoAdjust();
        const auto  layout  = GetAutoLayout();
        const auto  to = [&](Axis axis) {
            return [&, axis](float base) {
                if (forced) {
                    return axis == kX ? AutoPreviewX(layout) : axis == kZ ? layout.previewZ : layout.previewScale;
                }
                return axis == kX ? base + offsetX : axis == kZ ? base + offsetZ : base * PreviewScale();
            };
        };

        auto* ini = RE::INISettingCollection::GetSingleton();
        for (auto& one : previewSettings) {
            if (auto* setting = ini ? ini->GetSetting(one.name) : nullptr) {
                Apply(setting->data.f, one.value, to(one.axis));
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
            auto*       depth   = ini ? ini->GetSetting("fInventory3DItemPosYWide:Interface") : nullptr;
            const float toDepth = forced ? AUTO_DEPTH : depth ? depth->data.f : manager->itemPos.y;
            if (manager->itemPos.y != toDepth) {
                manager->itemPos.y     = toDepth;
                manager->itemPosCopy.y = toDepth;
                changed = true;
            }
        }

        changed |= Apply(manager->itemPos.x, managerValues[0], to(kX));
        changed |= Apply(manager->itemPos.z, managerValues[1], to(kZ));
        changed |= Apply(manager->itemScale, managerValues[2], to(kScale));

        Apply(manager->itemPosCopy.x, managerValues[3], to(kX));
        Apply(manager->itemPosCopy.z, managerValues[4], to(kZ));
        Apply(manager->itemScaleCopy, managerValues[5], to(kScale));

        if (changed && scale > 0.0f) {
            MoveLoadedModels(manager, manager->itemPos - from, manager->itemScale / scale);
        }
    }
}

namespace
{
    // an item menu's opened since the last update
    std::atomic_bool menuOpened = true;
}

namespace ItemDisplay
{
    void OnMenuOpen(std::string_view menuName)
    {
        if (menuName == RE::InventoryMenu::MENU_NAME || menuName == RE::MagicMenu::MENU_NAME ||
            menuName == RE::BarterMenu::MENU_NAME || menuName == RE::ContainerMenu::MENU_NAME ||
            menuName == RE::CraftingMenu::MENU_NAME || menuName == RE::GiftMenu::MENU_NAME) {
            menuOpened = true;
        }
    }

    void Update(RE::GFxMovieView* view)
    {
        if (!Settings::values.enabled || !view) {
            return;
        }

        // a vanilla menu lays itself out over its first frames, SkyUI's once its config is in. each menu opened starts afresh
        constexpr int SETTLE_FRAMES = 10;
        static RE::GFxMovieView* settling = nullptr;
        static int frames = 0;
        static bool ready = false;
        if (view != settling || menuOpened.exchange(false)) {
            settling = view;
            frames = 0;
            ready = false;
            card = {};
            rotationArea = {};
        }

        // the preview from the first frame, as it's the game's settings rather than the menu's layout
        UpdatePreview();

        // with Auto-Adjustment, the card and area too, as their place across doesn't depend on the menu's
        constexpr int GIVE_UP_FRAMES = 600;
        if (!ready) {
            const bool waiting = IsSkyUI(view) ? IsSkyUIWaiting(view) : frames < SETTLE_FRAMES;
            if (waiting && frames < GIVE_UP_FRAMES) {
                ++frames;
                if (IsAutoAdjust()) {
                    UpdateCard(view, false);
                    UpdateRotationArea(view, false);
                }
                return;
            }
            ready = true;
        }

        UpdateCard(view, true);
        UpdateRotationArea(view, true);
    }

}