#include "Obstructions.h"
#include "Settings.h"

#include <unordered_map>

// the hiding and the reference walk follow Menu Studio's declutter, thank you to its author
namespace
{
    constexpr float CHECK_INTERVAL = 0.1f;  // seconds between checks, as the camera moves
    constexpr float CORRIDOR = 25.0f;       // how far around a sight line something counts as in the way
    constexpr float STOP_GAP = 40.0f;       // sight lines stop this far in front of the body, so what stands beside it or touches it stays
    constexpr float BODY_WIDTH = 50.0f;     // sight lines also go this far either side of the body, across the view, so they widen like a cone
    constexpr float FRONT_GAP = 60.0f;      // how far nearer than the body something must be to count as in front of it
    constexpr float FLOOR_ROOM = 15.0f;     // anything whose top is no higher than this above the feet is floor, and stays
    constexpr float LARGEST = 250.0f;       // bigger than this, something is hidden only when it's across a sight line, so a room keeps its walls
    constexpr float REACH_PAD = 128.0f;     // searched this far past the camera's distance from the body
    constexpr float LINGER = 1.0f;          // seconds something stays hidden after it's out of the way, so it doesn't flicker as the camera moves
    constexpr float SETTLE = 2.0f;          // seconds after the camera or the one shown last moved that hidden things can still come back
    constexpr float EFFECT_RADIUS = 16.0f;  // an effect with no size of its own (a candle's flame) is taken as a sphere this big

    struct Hidden
    {
        RE::ObjectRefHandle ref;
        RE::NiPointer<RE::NiAVObject> node;  // the node culled, as a reference's 3D can be replaced while hidden
        float clearFor = 0.0f;               // seconds it's been out of the way
    };

    // whether something hidden can come back, which it does once it's been out of the way long enough. if it gets back in the way, the wait starts again
    bool Settled(float& clearFor, bool inTheWay, float elapsed)
    {
        clearFor = inTheWay ? 0.0f : clearFor + elapsed;
        return clearFor >= LINGER;
    }

    std::vector<Hidden> hidden;
    float sinceCheck = CHECK_INTERVAL;

    // while neither the camera nor the one shown moves, only their animation changes what's in the way (an equip, a bend), which would
    // hide and show the same things over and over. so once things settle after a move, nothing comes back until the next one
    struct LastSeen
    {
        RE::NiPoint3 eye;
        RE::NiPoint3 forward;
        RE::NiPoint3 feet;
        bool known = false;
    };
    LastSeen lastSeen;
    float sinceMoved = 0.0f;

    // the camera as this frame sees from: where it is, its axes, and how wide it sees (tangents of half the view)
    struct View
    {
        RE::NiPoint3 eye;
        RE::NiPoint3 forward;
        RE::NiPoint3 right;
        RE::NiPoint3 up;
        float tanRight = 1.0f;
        float tanTop = 1.0f;

        float Depth(const RE::NiPoint3& p) const { return (p - eye).Dot(forward); }
    };

    RE::TESObjectREFR* ToPointer(RE::TESObjectREFR* ref) { return ref; }
    RE::TESObjectREFR* ToPointer(RE::TESObjectREFR& ref) { return &ref; }

    // every reference within a_radius of a_origin, in the interior or the loaded exterior cells. TES::ForEachReferenceInRange
    // isn't used, as on AE it reads the sky cell through an uncertain offset and crashes outdoors (found by Menu Studio)
    template <class Fn>
    void ForEachRefInRange(const RE::NiPoint3& origin, float radius, Fn fn)
    {
        auto* tes = RE::TES::GetSingleton();
        if (!tes) {
            return;
        }

        auto visit = [&](auto&& entry) {
            if (auto* ref = ToPointer(entry)) {
                fn(*ref);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        };

        if (tes->interiorCell) {
            tes->interiorCell->ForEachReferenceInRange(origin, radius, visit);
            return;
        }

        auto* grid = tes->gridCells;
        const std::uint32_t length = grid ? grid->length : 0;
        for (std::uint32_t x = 0; x < length; ++x) {
            for (std::uint32_t y = 0; y < length; ++y) {
                auto* cell = grid->GetCell(x, y);
                const auto* coords = cell && cell->IsAttached() ? cell->GetCoordinates() : nullptr;
                if (!coords) {
                    continue;
                }

                // only the cells the range reaches into, a cell is 4096 units square
                if (coords->worldX < origin.x + radius && coords->worldX + 4096.0f > origin.x - radius &&
                    coords->worldY < origin.y + radius && coords->worldY + 4096.0f > origin.y - radius) {
                    cell->ForEachReferenceInRange(origin, radius, visit);
                }
            }
        }
    }

    // whether the line from a to b passes through the box, both in the box's own space (a slab test)
    bool SegmentHitsBox(const RE::NiPoint3& a, const RE::NiPoint3& b, const RE::NiPoint3& min, const RE::NiPoint3& max)
    {
        float enter = 0.0f;
        float leave = 1.0f;
        const float from[3]{ a.x, a.y, a.z };
        const float delta[3]{ b.x - a.x, b.y - a.y, b.z - a.z };
        const float lo[3]{ min.x, min.y, min.z };
        const float hi[3]{ max.x, max.y, max.z };

        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(delta[axis]) < 1e-6f) {
                if (from[axis] < lo[axis] || from[axis] > hi[axis]) {
                    return false;
                }
                continue;
            }

            float t0 = (lo[axis] - from[axis]) / delta[axis];
            float t1 = (hi[axis] - from[axis]) / delta[axis];
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            enter = std::max(enter, t0);
            leave = std::min(leave, t1);
            if (enter > leave) {
                return false;
            }
        }

        return true;
    }

    bool Inside(const RE::NiPoint3& p, const RE::NiPoint3& min, const RE::NiPoint3& max)
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }

    // how far a point is from the line between a and b
    float DistanceToSegment(const RE::NiPoint3& p, const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        const RE::NiPoint3 ab = b - a;
        const float lengthSq = ab.Dot(ab);
        const float t = lengthSq > 0.0f ? std::clamp((p - a).Dot(ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
        return p.GetDistance(a + ab * t);
    }


    // whether any part of a box is on screen, from its corners (numbered so each bit picks a side on one axis). anything reaching behind the camera
    // only counts from where it passes the camera, so a beam running overhead from behind isn't on screen....hopefully
    bool OnScreen(const View& view, const std::array<RE::NiPoint3, 8>& corners)
    {
        constexpr float NEAR_PLANE = 1.0f;
        constexpr float EDGE = 1.1f;  // a little past the screen's edge, so what's just off it doesn't creep in as the camera moves

        std::array<float, 8> depths{};
        for (int i = 0; i < 8; ++i) {
            depths[i] = view.Depth(corners[i]);
        }

        bool ahead = false;
        constexpr float BIG = std::numeric_limits<float>::max();
        float left = BIG;
        float right = -BIG;
        float bottom = BIG;
        float top = -BIG;
        auto project = [&](const RE::NiPoint3& point, float depth) {
            const RE::NiPoint3 offset = point - view.eye;
            const float x = offset.Dot(view.right) / (depth * view.tanRight);
            const float y = offset.Dot(view.up) / (depth * view.tanTop);
            left = std::min(left, x);
            right = std::max(right, x);
            bottom = std::min(bottom, y);
            top = std::max(top, y);
            ahead = true;
        };

        for (int i = 0; i < 8; ++i) {
            if (depths[i] > NEAR_PLANE) {
                project(corners[i], depths[i]);
            }

            // each edge once, from its corner with that axis' bit clear, cut where it crosses in front of the camera
            for (int axis = 1; axis < 8; axis <<= 1) {
                const int j = i | axis;
                if ((i & axis) || (depths[i] > NEAR_PLANE) == (depths[j] > NEAR_PLANE)) {
                    continue;
                }
                const float t = (NEAR_PLANE - depths[i]) / (depths[j] - depths[i]);
                project(corners[i] + (corners[j] - corners[i]) * t, NEAR_PLANE);
            }
        }

        return ahead && left < EDGE && right > -EDGE && bottom < EDGE && top > -EDGE;
    }

    // the body: the points the sight lines go to, how near its front is to the camera, and where its feet are
    struct Body
    {
        std::vector<RE::NiPoint3> bones;    // the body itself: head, chest and pelvis
        std::vector<RE::NiPoint3> targets;  // where the sight lines go: across the body and either side of it
        float nearDepth = 0.0f;
        float feetZ = 0.0f;
    };

    // a sight line from the camera toward a point, stopped in front of the body, so it never reaches what stands level with it
    RE::NiPoint3 StopBefore(const View& view, const Body& body, const RE::NiPoint3& target)
    {
        const float depth = view.Depth(target);
        const float stop = body.nearDepth - STOP_GAP;
        if (depth <= 0.0f || stop <= 0.0f) {
            return view.eye;
        }
        return view.eye + (target - view.eye) * std::min(1.0f, stop / depth);
    }

    // the reference's shape in the world: its bounds as a box in its own space, or failing that the sphere round its 3D
    struct Shape
    {
        RE::NiPoint3 min;
        RE::NiPoint3 max;
        bool isBox = false;
        RE::NiPoint3 center;                    // without a box, a sphere
        float radius = 0.0f;
        std::array<RE::NiPoint3, 8> corners{};  // the box's corners in the world, or the sphere's extremes on the world axes
        float size = 0.0f;                      // the largest extent in the world
    };

    Shape SphereAt(const RE::NiPoint3& center, float radius)
    {
        Shape shape;
        shape.center = center;
        shape.radius = radius;
        for (int i = 0; i < 8; ++i) {
            shape.corners[i] = center + RE::NiPoint3{ (i & 1) ? radius : -radius, (i & 2) ? radius : -radius, (i & 4) ? radius : -radius };
        }
        shape.size = radius * 2.0f;
        return shape;
    }

    Shape ShapeOf(RE::TESObjectREFR& ref, RE::NiAVObject& root)
    {
        Shape shape;
        shape.min = ref.GetBoundMin();
        shape.max = ref.GetBoundMax();
        shape.isBox = shape.max.x - shape.min.x >= 1.0f || shape.max.y - shape.min.y >= 1.0f || shape.max.z - shape.min.z >= 1.0f;

        if (shape.isBox) {
            for (int i = 0; i < 8; ++i) {
                const RE::NiPoint3 corner{ (i & 1) ? shape.max.x : shape.min.x, (i & 2) ? shape.max.y : shape.min.y, (i & 4) ? shape.max.z : shape.min.z };
                shape.corners[i] = root.world * corner;
            }
            const RE::NiPoint3 extent = (shape.max - shape.min) * root.world.scale;
            shape.size = std::max({ extent.x, extent.y, extent.z });
            return shape;
        }

        // people have no bounds, so they're the sphere round their 3D. an effect placed on its own can have neither while paused,
        // so it's a small sphere where it stands
        const auto& sphere = root.worldBound;
        return sphere.radius > 0.0f ? SphereAt(sphere.center, sphere.radius) : SphereAt(root.world.translate, EFFECT_RADIUS);
    }

    // whether a sight line crosses the reference. one the body stands inside (a room's mesh, a big rug) is left, as hiding it would take
    // the room away
    bool CrossesSightLine(const Shape& shape, RE::NiAVObject& root, const View& view, const Body& body)
    {
        if (!shape.isBox) {
            for (const auto& bone : body.bones) {
                if (shape.center.GetDistance(bone) < shape.radius) {
                    return false;
                }
            }
            return std::ranges::any_of(body.targets, [&](const RE::NiPoint3& target) {
                return DistanceToSegment(shape.center, view.eye, StopBefore(view, body, target)) < shape.radius + CORRIDOR;
            });
        }

        // the bounds are in the reference's own space, which its 3D's world transform maps to the world
        const RE::NiTransform toLocal = root.world.Invert();
        for (const auto& bone : body.bones) {
            if (Inside(toLocal * bone, shape.min, shape.max)) {
                return false;
            }
        }

        const float scale = root.world.scale > 0.0f ? root.world.scale : 1.0f;
        const RE::NiPoint3 widened = RE::NiPoint3{ CORRIDOR, CORRIDOR, CORRIDOR } / scale;
        const RE::NiPoint3 eyeLocal = toLocal * view.eye;
        return std::ranges::any_of(body.targets, [&](const RE::NiPoint3& target) {
            return SegmentHitsBox(eyeLocal, toLocal * StopBefore(view, body, target), shape.min - widened, shape.max + widened);
        });
    }

    // whether the reference should be hidden. it has to be on screen and above the floor, and either cross a sight line or be small and wholly
    // in front of the body, which clears the edges of the view too
    bool IsInTheWay(const Shape& shape, RE::NiAVObject& root, const View& view, const Body& body)
    {
        float topZ = -std::numeric_limits<float>::max();
        float farDepth = -std::numeric_limits<float>::max();
        for (const auto& corner : shape.corners) {
            topZ = std::max(topZ, corner.z);
            farDepth = std::max(farDepth, view.Depth(corner));
        }

        if (topZ < body.feetZ + FLOOR_ROOM || !OnScreen(view, shape.corners)) {
            return false;
        }

        if (shape.size <= LARGEST && farDepth < body.nearDepth - FRONT_GAP) {
            return true;
        }

        return CrossesSightLine(shape, root, view, body);
    }

    // candle flames and the like are add-on nodes. a placed model only marks where its flame burns, and one particle system draws every flame
    // of that kind, so a flame in the way is hidden along with the rest of its kind
    struct HiddenFlames
    {
        RE::NiPointer<RE::NiAVObject> drawn;
        float clearFor = 0.0f;
    };
    std::vector<HiddenFlames> hiddenFlames;
    std::unordered_map<const RE::NiAVObject*, std::vector<RE::NiPointer<RE::BSValueNode>>> flameSpots;  // by 3D, found once a showing

    void FindFlameSpots(RE::NiAVObject* object, std::vector<RE::NiPointer<RE::BSValueNode>>& spots)
    {
        if (!object) {
            return;
        }
        if (auto* spot = netimmerse_cast<RE::BSValueNode*>(object); spot && spot->associatedObject) {
            spots.emplace_back(spot);
        }
        if (auto* node = object->AsNode()) {
            for (const auto& child : node->GetChildren()) {
                FindFlameSpots(child.get(), spots);
            }
        }
    }

    const std::vector<RE::NiPointer<RE::BSValueNode>>& FlameSpotsOf(RE::NiAVObject& root)
    {
        auto [entry, added] = flameSpots.try_emplace(&root);
        if (added) {
            FindFlameSpots(&root, entry->second);
        }
        return entry->second;
    }

    bool HasLight(RE::NiAVObject* object)
    {
        if (!object) {
            return false;
        }
        if (netimmerse_cast<RE::NiLight*>(object)) {
            return true;
        }

        auto* node = object->AsNode();
        if (!node) {
            return false;
        }
        return std::ranges::any_of(node->GetChildren(), [](const auto& child) { return HasLight(child.get()); });
    }

    bool IsHiddenHere(const RE::TESObjectREFR& ref)
    {
        return std::ranges::any_of(hidden, [&](const Hidden& entry) { return entry.ref.get().get() == &ref; });
    }

    void Cull(RE::TESObjectREFR& ref, RE::NiAVObject* node)
    {
        if (node && !node->GetAppCulled()) {
            node->SetAppCulled(true);
            hidden.push_back({ ref.GetHandle(), RE::NiPointer<RE::NiAVObject>(node) });
        }
    }

    // hides the reference's 3D. a light keeps the branch its light is on, as culling that would put the light out too, so only its model goes
    void Hide(RE::TESObjectREFR& ref, RE::NiAVObject& root)
    {
        auto* node = root.AsNode();
        if (!HasLight(&root) || !node) {
            Cull(ref, &root);
            return;
        }

        for (const auto& child : node->GetChildren()) {
            if (child && !HasLight(child.get())) {
                Cull(ref, child.get());
            }
        }
    }

    std::optional<View> GetView()
    {
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* root = camera ? camera->cameraRoot.get() : nullptr;
        auto* worldCamera = RE::Main::WorldRootCamera();
        if (!root || !worldCamera) {
            return std::nullopt;
        }

        const auto& frustum = worldCamera->GetRuntimeData2().viewFrustum;
        if (frustum.fRight <= 0.0f || frustum.fTop <= 0.0f) {
            return std::nullopt;
        }

        // the camera looks down its own y axis, with x to its right and z up
        const auto& rotate = root->world.rotate;
        View view;
        view.eye = root->world.translate;
        view.right = { rotate.entry[0][0], rotate.entry[1][0], rotate.entry[2][0] };
        view.forward = { rotate.entry[0][1], rotate.entry[1][1], rotate.entry[2][1] };
        view.up = { rotate.entry[0][2], rotate.entry[1][2], rotate.entry[2][2] };
        view.tanRight = frustum.fRight;
        view.tanTop = frustum.fTop;
        return view;
    }

    std::optional<Body> GetBody(RE::Actor* subject, const View& view)
    {
        auto* model = subject ? subject->Get3D(false) : nullptr;
        auto* head = model ? model->GetObjectByName("NPC Head [Head]") : nullptr;
        auto* chest = model ? model->GetObjectByName("NPC Spine2 [Spn2]") : nullptr;
        auto* pelvis = model ? model->GetObjectByName("NPC Pelvis [Pelv]") : nullptr;
        if (!head || !chest || !pelvis) {
            return std::nullopt;
        }

        Body body;
        body.feetZ = subject->GetPosition().z;
        for (const auto* bone : { head, chest, pelvis }) {
            body.bones.push_back(bone->world.translate);
        }

        // the sight lines go from above the head down to the knees, through the middle of the body and either side of it across the view
        const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
        const RE::NiPoint3 knees = pelvis->world.translate - up * ((pelvis->world.translate.z - body.feetZ) * 0.5f);
        const RE::NiPoint3 side = view.right * BODY_WIDTH;
        for (const auto& point : { head->world.translate + up * 20.0f, head->world.translate, chest->world.translate, pelvis->world.translate, knees }) {
            body.targets.push_back(point);
            body.targets.push_back(point + side);
            body.targets.push_back(point - side);
        }

        body.nearDepth = std::numeric_limits<float>::max();
        for (const auto& bone : body.bones) {
            body.nearDepth = std::min(body.nearDepth, view.Depth(bone));
        }
        return body;
    }

    void Check(RE::Actor* subject, float elapsed)
    {
        const auto view = GetView();
        const auto body = view ? GetBody(subject, *view) : std::nullopt;
        if (!body) {
            return;
        }

        const RE::NiPoint3 feet = subject->GetPosition();
        const bool moved = !lastSeen.known || view->eye.GetDistance(lastSeen.eye) > 1.0f || view->forward.Dot(lastSeen.forward) < 0.9999f ||
                           feet.GetDistance(lastSeen.feet) > 1.0f;
        lastSeen = { view->eye, view->forward, feet, true };
        sinceMoved = moved ? 0.0f : sinceMoved + elapsed;
        const bool settling = sinceMoved < SETTLE;

        RE::NiPointer<RE::Actor> mount;
        if (subject->IsOnMount()) {
            subject->GetMount(mount);
        }

        std::vector<RE::TESObjectREFR*> blocking;
        std::vector<RE::NiAVObject*> flames;
        const float reach = view->eye.GetDistance(subject->GetPosition()) + REACH_PAD;
        ForEachRefInRange(subject->GetPosition(), reach, [&](RE::TESObjectREFR& ref) {
            // the one shown and their horse stay. the player is in the way like anyone else when a follower is shown
            if (&ref == subject || &ref == mount.get() || ref.IsDisabled() || ref.IsDeleted()) {
                return;
            }

            auto* root = &ref == RE::PlayerCharacter::GetSingleton() ? ref.Get3D(false) : ref.Get3D();
            if (!root) {
                return;
            }
            if (IsInTheWay(ShapeOf(ref, *root), *root, *view, *body)) {
                blocking.push_back(&ref);
            }
            for (const auto& spot : FlameSpotsOf(*root)) {
                RE::NiAVObject* drawn = spot->associatedObject.get();
                if (std::ranges::find(flames, drawn) == flames.end() && IsInTheWay(SphereAt(spot->world.translate, EFFECT_RADIUS), *root, *view, *body)) {
                    flames.push_back(drawn);
                }
            }
        });

        // what's in the way gets hidden, and what's been clear long enough comes back, while things are still settling after a move
        for (auto* ref : blocking) {
            auto* root = ref == RE::PlayerCharacter::GetSingleton() ? ref->Get3D(false) : ref->Get3D();
            if (root && !IsHiddenHere(*ref)) {
                Hide(*ref, *root);  // only what isn't culled already, as another mod's hide is theirs to bring back
            }
        }

        for (auto* drawn : flames) {
            if (!drawn->GetAppCulled()) {
                drawn->SetAppCulled(true);
                hiddenFlames.push_back({ RE::NiPointer<RE::NiAVObject>(drawn) });
            }
        }
        std::erase_if(hiddenFlames, [&](HiddenFlames& entry) {
            if (!Settled(entry.clearFor, std::ranges::find(flames, entry.drawn.get()) != flames.end(), elapsed) || !settling) {
                return false;
            }
            entry.drawn->SetAppCulled(false);
            return true;
        });

        std::erase_if(hidden, [&](Hidden& entry) {
            const auto ref = entry.ref.get();
            if (ref && (!Settled(entry.clearFor, std::ranges::find(blocking, ref.get()) != blocking.end(), elapsed) || !settling)) {
                return false;
            }

            if (entry.node) {
                entry.node->SetAppCulled(false);
            }
            return true;
        });
    }
}

namespace Obstructions
{
    void Update(float interval, RE::Actor* subject)
    {
        if (!Settings::values.hideObstructions || !subject) {
            RestoreAll();
            return;
        }

        sinceCheck += interval;
        if (sinceCheck < CHECK_INTERVAL) {
            return;
        }

        Check(subject, sinceCheck);
        sinceCheck = 0.0f;
    }

    void RestoreAll()
    {
        for (const auto& entry : hidden) {
            if (entry.node) {
                entry.node->SetAppCulled(false);
            }
        }

        hidden.clear();

        for (const auto& entry : hiddenFlames) {
            entry.drawn->SetAppCulled(false);
        }
        hiddenFlames.clear();
        flameSpots.clear();
        lastSeen.known = false;

        sinceCheck = CHECK_INTERVAL;  // so the next showing checks at once
    }
}
