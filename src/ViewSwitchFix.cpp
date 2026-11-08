#include "ViewSwitchFix.h"

namespace
{
    constexpr std::size_t PLAYER_UPDATE = 0xAD;
    constexpr std::size_t SET_WEAPON_ATTACHED = 0xB4;
    constexpr std::size_t FINISH_WEAPON_TRANSITION = 0x15;

    bool lastThirdPerson = false;
    bool perspectiveSeeded = false;

    std::int16_t GetEquippedItemTypeId(RE::TESForm* a_form)
    {
        using func_t = decltype(&GetEquippedItemTypeId);
        static REL::Relocation<func_t> func{ RELOCATION_ID(14125, 14220) };  // TESForm::GetEquippedItemTypeId
        return func(a_form);
    }

    // the hand variables finishing the draw sets, without readying the spells again
    void SetHandGraphVariables(RE::PlayerCharacter* player)
    {
        static const RE::BSFixedString magicEquipped{ "iMagicEquipped" };
        player->SetGraphVariableInt(magicEquipped, player->GetActorRuntimeData().selectedSpells[RE::Actor::SlotTypes::kLeftHand] || player->GetActorRuntimeData().selectedSpells[RE::Actor::SlotTypes::kRightHand]);
        player->SetGraphVariableInt(RE::FixedStrings::GetSingleton()->iLeftHandEquipped, GetEquippedItemTypeId(player->GetEquippedObject(true)));
        player->SetGraphVariableInt(RE::FixedStrings::GetSingleton()->iRightHandEquipped, GetEquippedItemTypeId(player->GetEquippedObject(false)));
    }

    void ReapplyAttachment(RE::PlayerCharacter* player, RE::WEAPON_STATE state)
    {
        const bool drawn = state != RE::WEAPON_STATE::kWantToSheathe && state != RE::WEAPON_STATE::kSheathing;

        bool weapon = false;
        for (const bool leftHand : { false, true }) {
            auto* object = player->GetEquippedObject(leftHand);
            if (object && object->IsWeapon()) {
                REL::RelocateVirtual<void(RE::PlayerCharacter*, RE::TESForm*, bool, bool)>(SET_WEAPON_ATTACHED, SET_WEAPON_ATTACHED, player, object, drawn, leftHand);
                weapon = true;
            }
        }

        // finishing the transition readies a spell again too, replaying its equip sound, so a finished draw only gets its hand variables back
        if (weapon && state == RE::WEAPON_STATE::kDrawn) {
            SetHandGraphVariables(player);
        } else if (weapon) {
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

            ReapplyAttachment(a_this, state);
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
