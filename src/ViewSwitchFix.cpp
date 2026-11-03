#include "ViewSwitchFix.h"

namespace
{
    constexpr std::size_t PLAYER_UPDATE = 0xAD;
    constexpr std::size_t SET_WEAPON_ATTACHED = 0xB4;
    constexpr std::size_t FINISH_WEAPON_TRANSITION = 0x15;

    bool lastThirdPerson = false;
    bool perspectiveSeeded = false;

    void ReapplyAttachment(RE::PlayerCharacter* player, bool drawn)
    {
        bool weapon = false;
        for (const bool leftHand : { false, true }) {
            auto* object = player->GetEquippedObject(leftHand);
            if (object && object->IsWeapon()) {
                REL::RelocateVirtual<void(RE::PlayerCharacter*, RE::TESForm*, bool, bool)>(SET_WEAPON_ATTACHED, SET_WEAPON_ATTACHED, player, object, drawn, leftHand);
                weapon = true;
            }
        }

        // only with a weapon in hand, as finishing the transition again readies a spell again too, replaying its equip sound
        if (weapon) {
            REL::RelocateVirtual<void(RE::ActorState*, bool)>(FINISH_WEAPON_TRANSITION, FINISH_WEAPON_TRANSITION, player->AsActorState(), drawn);
        }

        if (!drawn) {
            return;
        }

        for (const bool firstPerson : { false, true }) {
            const auto& biped = player->GetBiped(firstPerson);
            auto* shield = biped ? biped->GetShieldObject() : nullptr;
            if (shield && shield->item && shield->partClone && shield->item->Is(RE::FormType::Armor, RE::FormType::Light)) {
                shield->partClone->SetAppCulled(false);
            }
        }
    }

    // watches the first/third person flag every unpaused frame
    struct PlayerUpdateHook
    {
        static void Update(RE::PlayerCharacter* a_this, float a_delta)
        {
            _Update(a_this, a_delta);

            const bool thirdPerson = a_this->GetPlayerRuntimeData().playerFlags.isInThirdPersonMode;
            const bool switched = perspectiveSeeded && thirdPerson != lastThirdPerson;
            perspectiveSeeded = true;
            lastThirdPerson = thirdPerson;

            if (!switched) {
                return;
            }

            const auto state = a_this->AsActorState()->GetWeaponState();
            if (state == RE::WEAPON_STATE::kSheathed) {
                return;
            }

            ReapplyAttachment(a_this, state != RE::WEAPON_STATE::kWantToSheathe && state != RE::WEAPON_STATE::kSheathing);
        }

        static inline REL::Relocation<decltype(Update)> _Update;
    };
}

void ViewSwitchFix::Install()
{
    REL::Relocation<std::uintptr_t> vtbl{ RE::PlayerCharacter::VTABLE[0] };
    PlayerUpdateHook::_Update = vtbl.write_vfunc(PLAYER_UPDATE, PlayerUpdateHook::Update);
    logger::info("[ViewSwitchFix] Installed player update hook.");
}

void ViewSwitchFix::Reset()
{
    perspectiveSeeded = false;
}
