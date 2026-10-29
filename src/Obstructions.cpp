#include "Obstructions.h"
#include "Settings.h"

#include <unordered_set>

// the hiding and the reference walk follow Menu Studio's declutter, thank you to its author
namespace
{
    // how often, and how things come back
    constexpr float CHECK_INTERVAL =  0.1f;   // seconds between checks, as the camera moves
    constexpr float HOLD_MARGIN    = 20.0f;   // something hidden stays hidden until it's this much further out of the way than it took to hide it
    constexpr float LINGER         =  0.25f;  // and then for this many seconds, so it doesn't flicker as the camera moves
    constexpr float SETTLE         =  2.0f;   // seconds after the camera or the one shown last moved that hidden things can still come back

    // the sight lines, from the camera to points across the body
    constexpr float CORRIDOR       =  25.0f;   // how far round a sight line something counts as in the way
    constexpr float STOP_GAP       =  40.0f;   // sight lines stop this far in front of the body, so what stands beside it or touches it stays
    constexpr float BODY_WIDTH     =  50.0f;   // sight lines also go this far either side of the body, across the view, so they widen like a cone
    constexpr float FRONT_GAP      =  60.0f;   // how far nearer than the body something small must be to count as in front of it
    constexpr float REACH_PAD      = 128.0f;   // searched this far past the camera's distance from the body

    // sizes, by the largest extent in the world
    constexpr float FLOOR_ROOM     =  15.0f;   // anything whose top is no higher than this above the feet is floor, and stays
    constexpr float CLUTTER        =  40.0f;   // smaller than this, something is clutter: it goes with what it sits on, but takes nothing with it
    constexpr float LARGEST        = 250.0f;   // bigger than this, its box reaches well past it, so it's hidden only when a sight line hits its collision
    constexpr float GROUP_LARGEST  = 600.0f;   // pieces stop joining a group once it would grow bigger than this, so a run of walls can't go
    constexpr float TOUCH          =   4.0f;   // how near two boxes must come to count as touching
    constexpr float EFFECT_RADIUS  =  16.0f;   // an effect with no size of its own (a candle's flame) is taken as a sphere this big

    // the sight lines cast against collision, for what's bigger than LARGEST
    constexpr       RE::COL_LAYER RAY_LAYER = RE::COL_LAYER::kLOS;  // the building's walls, posts and beams, and the rest of what blocks sight
    constexpr int   RAY_HITS = 6;                                   // how many things one sight line is followed through
    constexpr float RAY_STEP = 1.0f;                                // a sight line carries on this far past each hit, so it doesn't hit the same face again

    constexpr float BIG = std::numeric_limits<float>::max();

    // ----- what's hidden -----

    struct Hidden
    {
        RE::ObjectRefHandle ref;
        RE::NiPointer<RE::NiAVObject> node;  // the node culled, as a reference's 3D can be replaced while hidden
        float clearFor = 0.0f;               // seconds it's been out of the way
    };
    std::vector<Hidden> hidden;

    // candle flames and the like are add-on nodes. a placed model only marks where its flame burns, and one particle system draws every flame
    // of that kind, so a flame in the way is hidden along with the rest of its kind
    struct HiddenFlame
    {
        RE::NiPointer<RE::NiAVObject> drawn;
        float clearFor = 0.0f;
    };
    std::vector<HiddenFlame> hiddenFlames;

    struct FlameCache
    {
        RE::NiPointer<RE::NiAVObject> root;
        std::vector<RE::NiPointer<RE::BSValueNode>> spots;
    };
    std::unordered_map<RE::FormID, FlameCache> flameSpots;  // by FormID, updated if the 3D changes

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

    // whether something hidden can come back, once it's been out of the way long enough. back in the way, the wait starts again
    bool Settled(float& clearFor, bool inTheWay, float elapsed)
    {
        clearFor = inTheWay ? 0.0f : clearFor + elapsed;
        return clearFor >= LINGER;
    }

    // ----- the camera and the body -----

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

    // the body: its bones, the points the sight lines go to, how near its front is to the camera, and where its feet are
    struct Body
    {
        std::vector<RE::NiPoint3> bones;    // the body itself: head, chest and pelvis, and a horse's back
        std::vector<RE::NiPoint3> targets;  // where the sight lines go: across the body and either side of it
        float nearDepth = 0.0f;
        float feetZ = 0.0f;
    };

    std::optional<Body> GetBody(RE::Actor* subject, const View& view, bool upperBody)
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

        // the sight lines go from above the head down to the knees, or to the chest when framed from there, through the middle of the body
        // and either side of it across the view
        const RE::NiPoint3 up{ 0.0f, 0.0f, 1.0f };
        const RE::NiPoint3 knees = pelvis->world.translate - up * ((pelvis->world.translate.z - body.feetZ) * 0.5f);
        const RE::NiPoint3 side = view.right * BODY_WIDTH;
        std::vector<RE::NiPoint3> points{ head->world.translate + up * 20.0f, head->world.translate, chest->world.translate };
        if (!upperBody) {
            points.push_back(pelvis->world.translate);
            points.push_back(knees);
        }
        for (const auto& point : points) {
            body.targets.push_back(point);
            body.targets.push_back(point + side);
            body.targets.push_back(point - side);
        }

        // a rider's horse is shown too: its feet are the floor, and sight lines also go along its body, from the chest to the rump at the
        // height of its legs and its back, unless the framing leaves it out. headings run from +y towards +x. not a flying mount (a dragon),
        // far too big to treat as the body
        RE::NiPointer<RE::Actor> mount;
        const auto* mountRace = subject->IsOnMount() && subject->GetMount(mount) && mount ? mount->GetRace() : nullptr;
        if (mountRace && mountRace->data.flags.none(RE::RACE_DATA::Flag::kFlies)) {
            const RE::NiPoint3 base = mount->GetPosition();
            body.feetZ = base.z;
            if (!upperBody) {
                constexpr float HORSE_REACH = 90.0f;
                const float heading = mount->GetAngleZ();
                const RE::NiPoint3 along{ std::sin(heading), std::cos(heading), 0.0f };
                for (const float reach : { -HORSE_REACH, 0.0f, HORSE_REACH }) {
                    body.bones.push_back(base + along * reach + up * 110.0f);
                    for (const float height : { 60.0f, 130.0f }) {
                        body.targets.push_back(base + along * reach + up * height);
                    }
                }
            }
        }

        body.nearDepth = BIG;
        for (const auto& bone : body.bones) {
            body.nearDepth = std::min(body.nearDepth, view.Depth(bone));
        }
        return body;
    }

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

    // ----- the references round the body -----

    RE::TESObjectREFR* ToPointer(RE::TESObjectREFR* ref) { return ref; }
    RE::TESObjectREFR* ToPointer(RE::TESObjectREFR& ref) { return &ref; }

    // every reference within radius of the origin reference, in the interior or the loaded exterior cells
    template <class Fn>
    void ForEachRefInRange(RE::TESObjectREFR& origin, float radius, Fn fn)
    {
        auto* tes = RE::TES::GetSingleton();
        if (!tes) {
            return;
        }

        tes->ForEachReferenceInRange(&origin, radius, [&](auto&& entry) {
            if (auto* ref = ToPointer(entry)) {
                fn(*ref);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
    }

    // ----- geometry -----

    bool Inside(const RE::NiPoint3& p, const RE::NiPoint3& min, const RE::NiPoint3& max)
    {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
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

    // how far a point is from the line between a and b
    float DistanceToSegment(const RE::NiPoint3& p, const RE::NiPoint3& a, const RE::NiPoint3& b)
    {
        const RE::NiPoint3 ab = b - a;
        const float lengthSq = ab.Dot(ab);
        const float t = lengthSq > 0.0f ? std::clamp((p - a).Dot(ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
        return p.GetDistance(a + ab * t);
    }

    // whether any part of a box is on screen, from its corners (numbered so each bit picks a side on one axis). anything reaching behind
    // the camera only counts from where it passes the camera, so a beam running overhead from behind isn't on screen
    bool OnScreen(const View& view, const std::array<RE::NiPoint3, 8>& corners)
    {
        constexpr float NEAR_PLANE = 1.0f;
        constexpr float EDGE = 1.1f;  // a little past the screen's edge, so what's just off it doesn't creep in as the camera moves

        std::array<float, 8> depths{};
        for (int i = 0; i < 8; ++i) {
            depths[i] = view.Depth(corners[i]);
        }

        bool ahead = false;
        float left = BIG;
        float right = -BIG;
        float bottom = BIG;
        float top = -BIG;
        const auto project = [&](const RE::NiPoint3& point, float depth) {
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

    // ----- a reference as a check sees it -----

    // its shape, and what the tests ask of it, worked out once a check: its bounds as a box in its own space, or failing that a sphere
    // (people have no bounds, and an effect placed on its own can have no size while paused)
    struct Measured
    {
        bool isBox = false;
        RE::NiPoint3 min;  // the box
        RE::NiPoint3 max;
        RE::NiTransform toLocal;
        float scale = 1.0f;
        RE::NiPoint3 center;  // the sphere
        float radius = 0.0f;

        std::array<RE::NiPoint3, 8> corners{};  // the box's corners in the world, or the sphere's extremes on the world axes
        float size = 0.0f;                      // the largest extent in the world
        float nearDepth = BIG;
        float farDepth = -BIG;
        bool aboveFloor = false;
        bool onScreen = false;
        bool holdsBody = false;  // the body's inside it (a room's mesh, the seat they're on), which is never hidden, as that'd take it away
    };

    Measured Measure(const Measured& shape, const View& view, const Body& body)
    {
        Measured measured = shape;
        float topZ = -BIG;
        for (const auto& corner : measured.corners) {
            topZ = std::max(topZ, corner.z);
            measured.nearDepth = std::min(measured.nearDepth, view.Depth(corner));
            measured.farDepth = std::max(measured.farDepth, view.Depth(corner));
        }

        measured.aboveFloor = topZ >= body.feetZ + FLOOR_ROOM;
        measured.onScreen = measured.aboveFloor && OnScreen(view, measured.corners);
        measured.holdsBody = std::ranges::any_of(body.bones, [&](const RE::NiPoint3& bone) {
            return measured.isBox ? Inside(measured.toLocal * bone, measured.min, measured.max) : measured.center.GetDistance(bone) < measured.radius;
        });
        return measured;
    }

    Measured SphereAt(const RE::NiPoint3& center, float radius)
    {
        Measured shape;
        shape.center = center;
        shape.radius = radius;
        for (int i = 0; i < 8; ++i) {
            shape.corners[i] = center + RE::NiPoint3{ (i & 1) ? radius : -radius, (i & 2) ? radius : -radius, (i & 4) ? radius : -radius };
        }
        shape.size = radius * 2.0f;
        return shape;
    }

    Measured ShapeOf(RE::TESObjectREFR& ref, RE::NiAVObject& root)
    {
        Measured shape;
        shape.min = ref.GetBoundMin();
        shape.max = ref.GetBoundMax();
        shape.isBox = shape.max.x - shape.min.x >= 1.0f || shape.max.y - shape.min.y >= 1.0f || shape.max.z - shape.min.z >= 1.0f;
        if (!shape.isBox) {
            const auto& sphere = root.worldBound;
            return sphere.radius > 0.0f ? SphereAt(sphere.center, sphere.radius) : SphereAt(root.world.translate, EFFECT_RADIUS);
        }

        // the bounds are in the reference's own space, which its 3D's world transform maps to the world
        for (int i = 0; i < 8; ++i) {
            const RE::NiPoint3 corner{ (i & 1) ? shape.max.x : shape.min.x, (i & 2) ? shape.max.y : shape.min.y, (i & 4) ? shape.max.z : shape.min.z };
            shape.corners[i] = root.world * corner;
        }
        shape.toLocal = root.world.Invert();
        shape.scale = root.world.scale > 0.0f ? root.world.scale : 1.0f;
        const RE::NiPoint3 extent = (shape.max - shape.min) * shape.scale;
        shape.size = std::max({ extent.x, extent.y, extent.z });
        return shape;
    }

    // big enough that its box can't be trusted, so a sight line has to hit its collision instead
    bool IsBig(const RE::TESObjectREFR& ref, const Measured& measured)
    {
        return measured.isBox && measured.size > LARGEST && ref.GetFormType() != RE::FormType::ActorCharacter;
    }

    // whether something that isn't big should be hidden. it has to be on screen and above the floor, and either be small and wholly in front
    // of the body, which clears the edges of the view too, or cross a sight line. the margin widens both for something already hidden, so
    // it only comes back once it's well out of the way, and a camera going back and forth over the edge doesn't flicker it
    bool IsInTheWay(const Measured& measured, const View& view, const Body& body, float margin)
    {
        if (!measured.onScreen) {
            return false;
        }
        if (measured.size <= LARGEST && measured.farDepth < body.nearDepth - FRONT_GAP + margin) {
            return true;
        }
        if (measured.holdsBody) {
            return false;
        }

        const float corridor = CORRIDOR + margin;
        if (!measured.isBox) {
            return std::ranges::any_of(body.targets, [&](const RE::NiPoint3& target) {
                return DistanceToSegment(measured.center, view.eye, StopBefore(view, body, target)) < measured.radius + corridor;
            });
        }

        const RE::NiPoint3 widened = RE::NiPoint3{ corridor, corridor, corridor } / measured.scale;
        const RE::NiPoint3 eyeLocal = measured.toLocal * view.eye;
        return std::ranges::any_of(body.targets, [&](const RE::NiPoint3& target) {
            return SegmentHitsBox(eyeLocal, measured.toLocal * StopBefore(view, body, target), measured.min - widened, measured.max + widened);
        });
    }

    // ----- the sight lines cast against collision -----

    // everything with collision on the line from a to b, nearest first, as each hit's reference (nullptr for the ground)
    template <class Fn>
    void CastRay(RE::bhkWorld& world, RE::CFilter filter, const RE::NiPoint3& from, const RE::NiPoint3& to, Fn onHit)
    {
        auto* havok = world.GetWorld1();
        if (!havok) {
            return;
        }

        const float scale = RE::bhkWorld::GetWorldScale();
        RE::NiPoint3 start = from;
        for (int i = 0; i < RAY_HITS; ++i) {
            const RE::NiPoint3 left = to - start;
            const float length = left.Length();
            if (length <= RAY_STEP) {
                return;
            }

            RE::hkpWorldRayCastInput input;
            input.from = RE::hkVector4(start * scale);
            input.to = RE::hkVector4(to * scale);
            input.filterInfo = filter;

            RE::hkpWorldRayCastOutput output;
            {
                RE::BSReadLockGuard lock(world.worldLock);
                havok->CastRay(input, output);
            }
            if (!output.HasHit()) {
                return;
            }

            onHit(output.rootCollidable ? RE::TESHavokUtilities::FindCollidableRef(*output.rootCollidable) : nullptr);
            start = start + left * output.hitFraction + left * (RAY_STEP / length);
        }
    }

    // what the sight lines hit, as far as they go before the body. wide is the same pushed out from the body by HOLD_MARGIN, for what's
    // already hidden
    struct SightHits
    {
        std::unordered_set<const RE::TESObjectREFR*> hits;
        std::unordered_set<const RE::TESObjectREFR*> wide;
        std::vector<RE::TESObjectREFR*> all;  // both, once each, to check what stands further off than the search reaches
    };

    SightHits CastSightLines(RE::Actor* subject, const View& view, const Body& body)
    {
        SightHits sight;
        auto* cell = subject->GetParentCell();
        auto* world = cell ? cell->GetbhkWorld() : nullptr;
        if (!world) {
            return sight;
        }

        RE::NiPoint3 center;
        for (const auto& target : body.targets) {
            center = center + target;
        }
        center = center / static_cast<float>(body.targets.size());

        // a system group of their own, so the rays aren't filtered out as part of anything they pass
        RE::CFilter filter{};
        filter.SetSystemGroup(RE::bhkCollisionFilter::GetSingleton()->GetNewSystemGroup());
        filter.SetCollisionLayer(RAY_LAYER);

        std::unordered_set<const RE::TESObjectREFR*> either;
        const auto add = [&](std::unordered_set<const RE::TESObjectREFR*>& found, RE::TESObjectREFR* ref) {
            if (ref && found.insert(ref).second && either.insert(ref).second) {
                sight.all.push_back(ref);
            }
        };

        for (const auto& target : body.targets) {
            CastRay(*world, filter, view.eye, StopBefore(view, body, target), [&](RE::TESObjectREFR* ref) { add(sight.hits, ref); });

            // outwards from the body's middle, across the view
            RE::NiPoint3 out = target - center;
            out = out - view.forward * out.Dot(view.forward);
            const float length = out.Length();
            out = length > 1.0f ? out / length : view.up;
            CastRay(*world, filter, view.eye, StopBefore(view, body, target + out * HOLD_MARGIN), [&](RE::TESObjectREFR* ref) { add(sight.wide, ref); });
        }
        return sight;
    }

    // ----- what goes with what's in the way -----

    // a counter or a bar is often kit pieces set end to end, each open where it meets the next, so hiding one would leave its neighbours'
    // open ends showing. a piece is something with a box that can go along with what it touches: its box in its own space, widened by TOUCH
    struct Piece
    {
        RE::TESObjectREFR* ref = nullptr;
        RE::NiTransform toLocal;
        RE::NiPoint3 min;
        RE::NiPoint3 max;
        std::array<RE::NiPoint3, 8> corners{};
        float size = 0.0f;
        bool inTheWay = false;
        bool reachesFront = false;  // some of it is nearer the camera than the body
    };

    // something in the way, or something that could go with it: not a person, the floor, what's off screen or too big, or what the body's in
    std::optional<Piece> MakePiece(RE::TESObjectREFR& ref, const Measured& measured, bool inTheWay, const Body& body)
    {
        if (!measured.isBox || ref.GetFormType() == RE::FormType::ActorCharacter) {
            return std::nullopt;
        }
        if (!inTheWay && (measured.size > LARGEST || !measured.onScreen || measured.holdsBody)) {
            return std::nullopt;
        }

        const RE::NiPoint3 widened = RE::NiPoint3{ TOUCH, TOUCH, TOUCH } / measured.scale;
        return Piece{ &ref, measured.toLocal, measured.min - widened, measured.max + widened, measured.corners, measured.size, inTheWay,
            measured.nearDepth < body.nearDepth };
    }

    // whether two pieces touch: a corner of either is in the other's box. pieces set end to end share the corners of the end
    bool Touches(const Piece& a, const Piece& b)
    {
        const auto reaches = [](const Piece& box, const Piece& other) {
            return std::ranges::any_of(other.corners, [&](const RE::NiPoint3& corner) { return Inside(box.toLocal * corner, box.min, box.max); });
        };
        return reaches(a, b) || reaches(b, a);
    }

    // the world box round a group's pieces, to keep the group furniture-sized
    struct Bounds
    {
        RE::NiPoint3 low{ BIG, BIG, BIG };
        RE::NiPoint3 high{ -BIG, -BIG, -BIG };

        void Add(const std::array<RE::NiPoint3, 8>& corners)
        {
            for (const auto& corner : corners) {
                low = { std::min(low.x, corner.x), std::min(low.y, corner.y), std::min(low.z, corner.z) };
                high = { std::max(high.x, corner.x), std::max(high.y, corner.y), std::max(high.z, corner.z) };
            }
        }

        float Size() const { return std::max({ high.x - low.x, high.y - low.y, high.z - low.z }); }
    };

    // what touches something in the way goes with it: the pieces touching it, as long as they reach in front of the body and the group stays
    // furniture-sized, and the clutter sitting on any of it. what's bigger than LARGEST takes only clutter with it. they're in the way while
    // the first is, so they come back together. what joins is added to blocking
    void JoinTouching(const std::vector<Piece>& pieces, std::vector<RE::TESObjectREFR*>& blocking)
    {
        std::vector<bool> taken(pieces.size(), false);
        std::vector<std::size_t> group;
        for (std::size_t first = 0; first < pieces.size(); ++first) {
            if (!pieces[first].inTheWay || taken[first]) {
                continue;
            }

            taken[first] = true;
            group.assign(1, first);
            Bounds bounds;  // not counting clutter
            if (pieces[first].size >= CLUTTER && pieces[first].size <= LARGEST) {
                bounds.Add(pieces[first].corners);
            }

            for (std::size_t i = 0; i < group.size(); ++i) {
                const Piece& from = pieces[group[i]];
                if (from.size < CLUTTER) {
                    continue;
                }

                for (std::size_t j = 0; j < pieces.size(); ++j) {
                    const Piece& next = pieces[j];
                    if (taken[j] || next.size > LARGEST || !Touches(from, next)) {
                        continue;
                    }

                    if (next.size >= CLUTTER) {
                        if (from.size > LARGEST || !next.reachesFront) {
                            continue;
                        }
                        Bounds grown = bounds;
                        grown.Add(next.corners);
                        if (grown.Size() > GROUP_LARGEST) {
                            continue;
                        }
                        bounds = grown;
                    }

                    taken[j] = true;
                    group.push_back(j);
                    if (!next.inTheWay) {
                        blocking.push_back(next.ref);
                    }
                }
            }
        }
    }

    // ----- hiding -----

    RE::NiAVObject* RootOf(RE::TESObjectREFR& ref)
    {
        // the player's third-person 3D, as they're in the way like anyone else when a follower is shown
        return &ref == RE::PlayerCharacter::GetSingleton() ? ref.Get3D(false) : ref.Get3D();
    }

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

    const std::vector<RE::NiPointer<RE::BSValueNode>>& FlameSpotsOf(RE::TESObjectREFR& ref, RE::NiAVObject& root)
    {
        auto& entry = flameSpots[ref.GetFormID()];
        if (entry.root.get() != &root) {
            entry.root = RE::NiPointer<RE::NiAVObject>(&root);  // releases the old root
            entry.spots.clear();
            FindFlameSpots(&root, entry.spots);
        }
        return entry.spots;
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
        return node && std::ranges::any_of(node->GetChildren(), [](const auto& child) { return HasLight(child.get()); });
    }

    // only what isn't culled already, as another mod's hide is theirs to bring back
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
        if (!node || !HasLight(&root)) {
            Cull(ref, &root);
            return;
        }

        for (const auto& child : node->GetChildren()) {
            if (child && !HasLight(child.get())) {
                Cull(ref, child.get());
            }
        }
    }

    void Check(RE::Actor* subject, const std::vector<RE::Actor*>& shown, bool upperBody, float elapsed)
    {
        const auto view = GetView();
        const auto body = view ? GetBody(subject, *view, upperBody) : std::nullopt;
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
        if (subject->IsOnMount() && !subject->GetMount(mount)) {
            mount.reset();
        }

        std::unordered_set<const RE::TESObjectREFR*> wasHidden;
        for (const auto& entry : hidden) {
            if (const auto ref = entry.ref.get()) {
                wasHidden.insert(ref.get());
            }
        }
        std::unordered_set<const RE::NiAVObject*> wasHiddenFlame;
        for (const auto& entry : hiddenFlames) {
            wasHiddenFlame.insert(entry.drawn.get());
        }

        const SightHits sight = CastSightLines(subject, *view, *body);

        std::vector<RE::TESObjectREFR*> blocking;
        std::vector<Piece> pieces;
        std::vector<RE::NiAVObject*> flames;
        std::unordered_set<const RE::TESObjectREFR*> visited;
        const auto visit = [&](RE::TESObjectREFR& ref) {
            // the one shown, their horse, and anyone they hold stay
            if (!visited.insert(&ref).second || &ref == subject || &ref == mount.get() || std::ranges::find(shown, &ref) != shown.end() ||
                ref.IsDisabled() || ref.IsDeleted()) {
                return;
            }

            auto* root = RootOf(ref);
            if (!root) {
                return;
            }

            const Measured measured = Measure(ShapeOf(ref, *root), *view, *body);
            const bool hiddenAlready = wasHidden.contains(&ref);
            bool inTheWay = false;
            if (IsBig(ref, measured)) {
                const bool hit = sight.hits.contains(&ref) || (hiddenAlready && sight.wide.contains(&ref));
                inTheWay = hit && measured.onScreen && !measured.holdsBody;
            } else {
                inTheWay = IsInTheWay(measured, *view, *body, hiddenAlready ? HOLD_MARGIN : 0.0f);
            }

            if (inTheWay) {
                blocking.push_back(&ref);
            }
            if (auto piece = MakePiece(ref, measured, inTheWay, *body)) {
                pieces.push_back(std::move(*piece));
            }

            for (const auto& spot : FlameSpotsOf(ref, *root)) {
                RE::NiAVObject* drawn = spot->associatedObject.get();
                const float margin = wasHiddenFlame.contains(drawn) ? HOLD_MARGIN : 0.0f;
                if (std::ranges::find(flames, drawn) == flames.end() &&
                    IsInTheWay(Measure(SphereAt(spot->world.translate, EFFECT_RADIUS), *view, *body), *view, *body, margin)) {
                    flames.push_back(drawn);
                }
            }
        };

        // what's near enough, and what a sight line hit, as a big mesh can stand further off
        ForEachRefInRange(*subject, view->eye.GetDistance(feet) + REACH_PAD, visit);
        for (auto* ref : sight.all) {
            visit(*ref);
        }

        // what touches something in the way goes with it, and so does a flame on it, rather than being left floating
        const std::size_t ownBlocking = blocking.size();
        JoinTouching(pieces, blocking);
        for (std::size_t i = ownBlocking; i < blocking.size(); ++i) {
            auto* root = RootOf(*blocking[i]);
            if (!root) {
                continue;
            }
            for (const auto& spot : FlameSpotsOf(*blocking[i], *root)) {
                if (RE::NiAVObject* drawn = spot->associatedObject.get(); std::ranges::find(flames, drawn) == flames.end()) {
                    flames.push_back(drawn);
                }
            }
        }

        // what's in the way gets hidden, and what's been clear long enough comes back, while things are still settling after a move
        for (auto* ref : blocking) {
            if (auto* root = RootOf(*ref); root && !wasHidden.contains(ref)) {
                Hide(*ref, *root);
            }
        }
        for (auto* drawn : flames) {
            if (!drawn->GetAppCulled()) {
                drawn->SetAppCulled(true);
                hiddenFlames.push_back({ RE::NiPointer<RE::NiAVObject>(drawn) });
            }
        }

        const std::unordered_set<const RE::TESObjectREFR*> blockingSet(blocking.begin(), blocking.end());
        std::erase_if(hidden, [&](Hidden& entry) {
            const auto ref = entry.ref.get();
            if (ref && (!Settled(entry.clearFor, blockingSet.contains(ref.get()), elapsed) || !settling)) {
                return false;
            }
            if (entry.node) {
                entry.node->SetAppCulled(false);
            }
            return true;
        });

        std::erase_if(hiddenFlames, [&](HiddenFlame& entry) {
            if (!Settled(entry.clearFor, std::ranges::find(flames, entry.drawn.get()) != flames.end(), elapsed) || !settling) {
                return false;
            }
            entry.drawn->SetAppCulled(false);
            return true;
        });
    }
}

namespace Obstructions
{
    void Update(float interval, RE::Actor* subject, const std::vector<RE::Actor*>& shown, bool upperBody)
    {
        if (!Settings::values.hideObstructions || !subject) {
            RestoreAll();
            return;
        }

        sinceCheck += interval;
        if (sinceCheck < CHECK_INTERVAL) {
            return;
        }

        Check(subject, shown, upperBody, sinceCheck);
        sinceCheck = 0.0f;
    }

    bool IsLineClear(RE::Actor* around, const RE::NiPoint3& from, const RE::NiPoint3& to, std::initializer_list<const RE::TESObjectREFR*> ignore)
    {
        auto* cell = around ? around->GetParentCell() : nullptr;
        auto* world = cell ? cell->GetbhkWorld() : nullptr;
        if (!world) {
            return true;
        }

        RE::CFilter filter{};
        filter.SetSystemGroup(RE::bhkCollisionFilter::GetSingleton()->GetNewSystemGroup());
        filter.SetCollisionLayer(RAY_LAYER);

        bool clear = true;
        CastRay(*world, filter, from, to, [&](RE::TESObjectREFR* ref) {
            if (std::ranges::find(ignore, ref) == ignore.end()) {
                clear = false;
            }
        });
        return clear;
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