#include "PCH.h"
#include "Menu/Armor.h"

#include <algorithm>
#include <vector>

namespace Armor
{
    namespace
    {
        std::vector<Loadout> g_loadouts;
        Loadout g_editing{};
        bool g_editingOpen = false;

        constexpr std::size_t Index(Slot slot)
        {
            return static_cast<std::size_t>(slot);
        }

        bool IsEmpty(const Loadout& loadout)
        {
            return std::none_of(loadout.forms.begin(), loadout.forms.end(),
                [](const ItemIdentity& item) { return item.formID != 0; });
        }

        bool HasBipedSlot(const RE::TESObjectARMO* armor,
            RE::BIPED_MODEL::BipedObjectSlot slot)
        {
            return armor && armor->HasPartOf(slot);
        }

        RE::TESForm* Lookup(RE::FormID id)
        {
            return id ? RE::TESForm::LookupByID(id) : nullptr;
        }

        RE::ExtraDataList* FindInventoryInstance(RE::Actor* actor,
            const ItemIdentity& item)
        {
            if (!actor || !item.formID || !item.hasUniqueID)
                return nullptr;
            const auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(item.formID);
            auto* changes = actor->GetInventoryChanges();
            if (!object || !changes || !changes->entryList)
                return nullptr;
            for (auto* entry : *changes->entryList)
            {
                if (!entry || entry->object != object || !entry->extraLists)
                    continue;
                for (auto* extra : *entry->extraLists)
                {
                    const auto* unique = extra ? extra->GetByType<RE::ExtraUniqueID>() : nullptr;
                    if (unique && unique->uniqueID == item.uniqueID)
                        return extra;
                }
            }
            return nullptr;
        }

        ItemIdentity CaptureEquippedIdentity(RE::Actor* actor, RE::TESForm* form)
        {
            ItemIdentity result{};
            if (!form)
                return result;
            result.formID = form->GetFormID();
            auto* object = form->As<RE::TESBoundObject>();
            auto* changes = actor ? actor->GetInventoryChanges() : nullptr;
            if (!object || !changes || !changes->entryList)
                return result;
            for (auto* entry : *changes->entryList)
            {
                if (!entry || entry->object != object || !entry->extraLists)
                    continue;
                for (auto* extra : *entry->extraLists)
                {
                    if (!extra || (!extra->HasType<RE::ExtraWorn>() &&
                        !extra->HasType<RE::ExtraWornLeft>()))
                        continue;
                    if (const auto* unique = extra->GetByType<RE::ExtraUniqueID>())
                    {
                        result.uniqueID = unique->uniqueID;
                        result.hasUniqueID = true;
                    }
                    return result;
                }
            }
            return result;
        }

        bool IsTwoHandedWeapon(const RE::TESForm* form)
        {
            const auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
            if (!weapon)
                return false;
            const auto type = weapon->GetWeaponType();
            return type == RE::WEAPON_TYPE::kTwoHandSword ||
                type == RE::WEAPON_TYPE::kTwoHandAxe ||
                type == RE::WEAPON_TYPE::kBow ||
                type == RE::WEAPON_TYPE::kCrossbow;
        }

        bool CanPlaceInHand(Slot slot, RE::TESForm* form)
        {
            const auto left = Lookup(g_editing.forms[Index(Slot::LeftHand)].formID);
            const auto right = Lookup(g_editing.forms[Index(Slot::RightHand)].formID);
            if (slot == Slot::LeftHand)
            {
                // Skyrim equips lights only in the left hand, and a two-hand
                // weapon on the right reserves both hands.
                return !IsTwoHandedWeapon(form) && !IsTwoHandedWeapon(right);
            }
            if (slot == Slot::RightHand)
            {
                if (IsTwoHandedWeapon(form))
                    return left == nullptr;
                return !IsTwoHandedWeapon(left);
            }
            return false;
        }

        void EquipForm(const ItemIdentity& item, Slot slot)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* manager = RE::ActorEquipManager::GetSingleton();
            auto* form = Lookup(item.formID);
            if (!player || !manager || !form)
                return;

            if (auto* shout = form->As<RE::TESShout>())
            {
                manager->EquipShout(player, shout);
                return;
            }

            if (auto* spell = form->As<RE::SpellItem>())
            {
                // Os slots nativos de magia usam FormIDs distintos das mãos
                // lógicas do Actor. Mantemos o mesmo mapeamento do Quick Draw.
                constexpr RE::FormID kLeftHandSlot = 0x13F43;
                constexpr RE::FormID kRightHandSlot = 0x13F42;
                const auto* handSlot = RE::TESForm::LookupByID<RE::BGSEquipSlot>(
                    slot == Slot::LeftHand ? kLeftHandSlot : kRightHandSlot);
                if (handSlot)
                    manager->EquipSpell(player, spell, handSlot);
                return;
            }

            if (auto* object = form->As<RE::TESBoundObject>())
            {
                const bool left = slot == Slot::LeftHand || slot == Slot::Shield;
                manager->EquipObject(player, object, FindInventoryInstance(player, item), 1, nullptr,
                    false, left, true, true);
            }
        }

        void ImportArmorSlot(RE::PlayerCharacter* player, Slot target,
            RE::BIPED_MODEL::BipedObjectSlot wornSlot)
        {
            if (!player)
                return;
            if (auto* armor = player->GetWornArmor(wornSlot))
                g_editing.forms[Index(target)] = CaptureEquippedIdentity(player, armor);
        }
    }

    bool HasLoadout(int hotkey)
    {
        return FindLoadout(hotkey) != nullptr;
    }

    const Loadout* FindLoadout(int hotkey)
    {
        const auto it = std::find_if(g_loadouts.begin(), g_loadouts.end(),
            [hotkey](const Loadout& value) { return value.hotkey == hotkey; });
        return it != g_loadouts.end() ? &*it : nullptr;
    }

    bool ExecuteLoadout(int hotkey)
    {
        const auto* loadout = FindLoadout(hotkey);
        if (!loadout)
            return false;

        const Loadout copy = *loadout;
        const auto equip = [copy]() {
            for (std::size_t index = 0; index < copy.forms.size(); ++index)
            {
                const auto slot = static_cast<Slot>(index);
                // Este campo é uma ação posterior, não uma peça de equipamento.
                if (slot != Slot::SecondaryAutoUse)
                    EquipForm(copy.forms[index], slot);
            }
        };
        if (auto* tasks = SKSE::GetTaskInterface())
            tasks->AddTask(equip);
        else
            equip();
        return true;
    }

    void ClearLoadout(int hotkey)
    {
        std::erase_if(g_loadouts, [hotkey](const Loadout& value) {
            return value.hotkey == hotkey;
        });
    }

    bool MoveLoadout(int sourceHotkey, int destinationHotkey)
    {
        if (sourceHotkey == destinationHotkey)
            return HasLoadout(sourceHotkey);
        auto it = std::find_if(g_loadouts.begin(), g_loadouts.end(),
            [sourceHotkey](const Loadout& value) { return value.hotkey == sourceHotkey; });
        if (it == g_loadouts.end())
            return false;
        ClearLoadout(destinationHotkey);
        it->hotkey = destinationHotkey;
        return true;
    }

    void BeginEdit(int hotkey)
    {
        g_editing = {};
        g_editing.hotkey = hotkey;
        if (const auto* existing = FindLoadout(hotkey))
            g_editing = *existing;
        g_editingOpen = true;
    }

    bool IsEditing()
    {
        return g_editingOpen;
    }

    int EditingHotkey()
    {
        return g_editing.hotkey;
    }

    const Loadout& EditingLoadout()
    {
        return g_editing;
    }

    char* EditingName()
    {
        return g_editing.name.data();
    }

    void CancelEdit()
    {
        g_editingOpen = false;
        g_editing = {};
    }

    void CommitEdit()
    {
        if (!g_editingOpen)
            return;
        ClearLoadout(g_editing.hotkey);
        if (!IsEmpty(g_editing))
            g_loadouts.push_back(g_editing);
        CancelEdit();
    }

    void ClearEditing()
    {
        if (g_editingOpen)
        {
            g_editing.forms.fill({});
            g_editing.name.fill('\0');
        }
    }

    RE::FormID GetEditingSlot(Slot slot)
    {
        return g_editing.forms[Index(slot)].formID;
    }

    ItemIdentity GetEditingItem(Slot slot)
    {
        return g_editing.forms[Index(slot)];
    }

    ItemIdentity GetSecondaryAutoUse(int hotkey)
    {
        if (const auto* loadout = FindLoadout(hotkey))
            return loadout->forms[Index(Slot::SecondaryAutoUse)];
        return {};
    }

    void ClearEditingSlot(Slot slot)
    {
        if (g_editingOpen)
            g_editing.forms[Index(slot)] = {};
    }

    Slot PreferredSlotFor(RE::TESForm* form)
    {
        if (!form)
            return Slot::Other;
        if (form->As<RE::TESAmmo>())
            return Slot::Arrows;
        if (auto* armor = form->As<RE::TESObjectARMO>())
        {
            using Biped = RE::BIPED_MODEL::BipedObjectSlot;
            if (HasBipedSlot(armor, Biped::kShield)) return Slot::Shield;
            if (HasBipedSlot(armor, Biped::kHead) || HasBipedSlot(armor, Biped::kCirclet)) return Slot::Helmet;
            if (HasBipedSlot(armor, Biped::kBody)) return Slot::Armor;
            if (HasBipedSlot(armor, Biped::kFeet)) return Slot::Boots;
            if (HasBipedSlot(armor, Biped::kAmulet)) return Slot::Necklace;
            if (HasBipedSlot(armor, Biped::kRing))
            {
                for (int i = static_cast<int>(Slot::Ring0); i <= static_cast<int>(Slot::Ring9); ++i)
                    if (!g_editing.forms[static_cast<std::size_t>(i)].formID)
                        return static_cast<Slot>(i);
                return Slot::Ring0;
            }
        }
        if (form->As<RE::TESObjectWEAP>())
            return Slot::RightHand;
        if (form->As<RE::TESObjectLIGH>())
            return Slot::LeftHand;
        if (form->As<RE::SpellItem>())
            return Slot::LeftHand;
        return Slot::Other;
    }

    bool Accepts(Slot slot, RE::TESForm* form)
    {
        if (!form)
            return false;
        if (slot == Slot::LeftHand || slot == Slot::RightHand)
        {
            const bool light = form->As<RE::TESObjectLIGH>() != nullptr;
            const bool usable = form->As<RE::TESObjectWEAP>() || form->As<RE::SpellItem>() ||
                (slot == Slot::LeftHand && light);
            return usable && CanPlaceInHand(slot, form);
        }
        if (slot == Slot::Arrows)
            return form->As<RE::TESAmmo>() != nullptr;
        if (slot == Slot::SecondaryAutoUse)
        {
            // Mesmo conjunto de categorias que o Auto-Use secundário do
            // Quick Draw: magias, poderes/gritos e consumíveis. Luzes ficam
            // fora porque exigem mão esquerda e não são uma ação direta.
            return form->As<RE::SpellItem>() || form->As<RE::TESShout>() ||
                form->As<RE::AlchemyItem>();
        }
        if (slot == Slot::Other)
        {
            // Este espaço é o seletor de voz: gritos e poderes/vozes que o
            // jogo equipa no canal acionado pela tecla de shout.
            if (form->As<RE::TESShout>())
                return true;
            if (const auto* spell = form->As<RE::SpellItem>())
            {
                const auto type = spell->GetSpellType();
                return type == RE::MagicSystem::SpellType::kPower ||
                    type == RE::MagicSystem::SpellType::kLesserPower ||
                    type == RE::MagicSystem::SpellType::kVoicePower;
            }
            return false;
        }
        auto* armor = form->As<RE::TESObjectARMO>();
        if (!armor)
            return false;
        using Biped = RE::BIPED_MODEL::BipedObjectSlot;
        if (slot == Slot::Helmet) return HasBipedSlot(armor, Biped::kHead) || HasBipedSlot(armor, Biped::kCirclet);
        if (slot == Slot::Armor) return HasBipedSlot(armor, Biped::kBody);
        if (slot == Slot::Boots) return HasBipedSlot(armor, Biped::kFeet);
        if (slot == Slot::Necklace) return HasBipedSlot(armor, Biped::kAmulet);
        if (slot == Slot::Shield) return HasBipedSlot(armor, Biped::kShield);
        return slot >= Slot::Ring0 && slot <= Slot::Ring9 && HasBipedSlot(armor, Biped::kRing);
    }

    bool SetEditingSlot(Slot slot, RE::TESForm* form,
        std::uint16_t uniqueID, bool hasUniqueID)
    {
        if (!g_editingOpen || !Accepts(slot, form))
            return false;
        g_editing.forms[Index(slot)] = { form->GetFormID(), uniqueID, hasUniqueID };
        return true;
    }

    void ImportEquipped()
    {
        if (!g_editingOpen)
            return;
        // IMPORT substitui apenas as peças equipadas; o nome que o usuário
        // escreveu para o conjunto continua pertencendo a este loadout.
        g_editing.forms.fill({});
        auto* player = RE::PlayerCharacter::GetSingleton();
        using Biped = RE::BIPED_MODEL::BipedObjectSlot;
        ImportArmorSlot(player, Slot::Helmet, Biped::kHead);
        ImportArmorSlot(player, Slot::Armor, Biped::kBody);
        ImportArmorSlot(player, Slot::Boots, Biped::kFeet);
        ImportArmorSlot(player, Slot::Necklace, Biped::kAmulet);
        ImportArmorSlot(player, Slot::Shield, Biped::kShield);
        ImportArmorSlot(player, Slot::Ring0, Biped::kRing);
        if (player)
        {
            if (auto* left = player->GetEquippedObject(true))
                g_editing.forms[Index(Slot::LeftHand)] = CaptureEquippedIdentity(player, left);
            if (auto* right = player->GetEquippedObject(false))
                g_editing.forms[Index(Slot::RightHand)] = CaptureEquippedIdentity(player, right);
            // Importação não passa pelo seletor de slots. Normalize o caso
            // de arma de duas mãos para nunca persistir um par impossível.
            if (IsTwoHandedWeapon(Lookup(g_editing.forms[Index(Slot::RightHand)].formID)))
                g_editing.forms[Index(Slot::LeftHand)] = {};
        }
    }

    void Save(SKSE::SerializationInterface* serialization)
    {
        if (!serialization)
            return;
        const std::uint32_t count = static_cast<std::uint32_t>(
            std::min<std::size_t>(g_loadouts.size(), 256));
        if (!serialization->WriteRecordData(&count, sizeof(count)))
            return;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto& loadout = g_loadouts[i];
            if (!serialization->WriteRecordData(&loadout.hotkey, sizeof(loadout.hotkey)) ||
                !serialization->WriteRecordData(loadout.forms.data(),
                    static_cast<std::uint32_t>(sizeof(ItemIdentity) * loadout.forms.size())) ||
                !serialization->WriteRecordData(loadout.name.data(),
                    static_cast<std::uint32_t>(loadout.name.size())))
                return;
        }
    }

    bool Load(SKSE::SerializationInterface* serialization, std::uint32_t version,
        std::uint32_t)
    {
        Clear();
        if (!serialization || (version != 1 && version != 2 && version != 3 && version != 4))
            return false;
        std::uint32_t count = 0;
        if (serialization->ReadRecordData(&count, sizeof(count)) != sizeof(count) || count > 256)
            return false;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            Loadout loadout{};
            if (serialization->ReadRecordData(&loadout.hotkey, sizeof(loadout.hotkey)) != sizeof(loadout.hotkey))
                return false;
            if (version == 1)
            {
                constexpr std::size_t kLegacySlotCount =
                    static_cast<std::size_t>(Slot::SecondaryAutoUse);
                std::array<RE::FormID, kLegacySlotCount> oldIDs{};
                if (serialization->ReadRecordData(oldIDs.data(),
                        static_cast<std::uint32_t>(sizeof(RE::FormID) * oldIDs.size())) !=
                    sizeof(RE::FormID) * oldIDs.size())
                    return false;
                for (std::size_t slot = 0; slot < oldIDs.size(); ++slot)
                    loadout.forms[slot].formID = oldIDs[slot];
            }
            else
            {
                constexpr std::size_t kLegacySlotCount =
                    static_cast<std::size_t>(Slot::SecondaryAutoUse);
                const std::size_t storedCount = version == 2 ?
                    kLegacySlotCount : loadout.forms.size();
                if (serialization->ReadRecordData(loadout.forms.data(),
                        static_cast<std::uint32_t>(sizeof(ItemIdentity) * storedCount)) !=
                    sizeof(ItemIdentity) * storedCount)
                    return false;
            }
            if (version >= 4 &&
                serialization->ReadRecordData(loadout.name.data(),
                    static_cast<std::uint32_t>(loadout.name.size())) != loadout.name.size())
                return false;
            loadout.name.back() = '\0';
            for (auto& item : loadout.forms)
            {
                std::uint32_t resolved = 0;
                if (item.formID && serialization->ResolveFormID(item.formID, resolved))
                    item.formID = resolved;
                else if (item.formID)
                    item = {};
            }
            if (loadout.hotkey && !IsEmpty(loadout))
                g_loadouts.push_back(loadout);
        }
        return true;
    }

    void Clear()
    {
        g_loadouts.clear();
        CancelEdit();
    }
}
