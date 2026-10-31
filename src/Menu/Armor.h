#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Armor
{
    inline constexpr std::size_t kLoadoutNameCapacity = 48;

    // Identidade da instância do inventário. FormID sozinho não distingue
    // cópias encantadas/renomeadas do mesmo objeto.
    struct ItemIdentity
    {
        RE::FormID formID = 0;
        std::uint16_t uniqueID = 0;
        bool hasUniqueID = false;
    };

    // Conjunto de equipamentos associado a uma tecla livre do Keyboard Hub.
    // Os anéis têm dez posições de reserva: mods podem preencher mais de uma
    // delas sem que seja preciso alterar o formato salvo no futuro.
    enum class Slot : std::uint8_t
    {
        Helmet,
        Armor,
        Boots,
        Necklace,
        Arrows,
        LeftHand,
        RightHand,
        Other,
        Shield,
        Ring0,
        Ring1,
        Ring2,
        Ring3,
        Ring4,
        Ring5,
        Ring6,
        Ring7,
        Ring8,
        Ring9,
        // Ação secundária do loadout: não equipa em um slot físico; é usada
        // automaticamente após o conjunto principal ser equipado.
        SecondaryAutoUse,
        Count
    };

    inline constexpr std::size_t kSlotCount = static_cast<std::size_t>(Slot::Count);

    struct Loadout
    {
        int hotkey = 0;
        std::array<ItemIdentity, kSlotCount> forms{};
        // Nome curto exibido como tooltip da tecla no Keyboard Hub.
        // Buffer fixo mantém a serialização compacta e previsível.
        std::array<char, kLoadoutNameCapacity> name{};
    };

    bool HasLoadout(int hotkey);
    // IDs negativos representam conjuntos guardados somente na gaveta: não
    // pertencem a nenhuma tecla até serem arrastados para o Keyboard Hub.
    int AllocateUnboundHotkey();
    const Loadout* FindLoadout(int hotkey);
    const std::vector<Loadout>& GetLoadouts();
    bool ExecuteLoadout(int hotkey);
    void ClearLoadout(int hotkey);
    bool MoveLoadout(int sourceHotkey, int destinationHotkey);

    void BeginEdit(int hotkey);
    bool IsEditing();
    int EditingHotkey();
    const Loadout& EditingLoadout();
    char* EditingName();
    void CancelEdit();
    void CommitEdit();
    void ClearEditing();
    void ImportEquipped();

    RE::FormID GetEditingSlot(Slot slot);
    ItemIdentity GetEditingItem(Slot slot);
    ItemIdentity GetSecondaryAutoUse(int hotkey);
    void ClearEditingSlot(Slot slot);
    bool SetEditingSlot(Slot slot, RE::TESForm* form,
        std::uint16_t uniqueID = 0, bool hasUniqueID = false);
    Slot PreferredSlotFor(RE::TESForm* form);
    // Magias que pertencem à lista jogável do personagem. Abilities,
    // doenças e efeitos internos podem existir em addedSpells, mas não
    // devem aparecer nos seletores do Hub.
    bool IsPlayableSpell(const RE::SpellItem* spell);
    bool Accepts(Slot slot, RE::TESForm* form);

    void Save(SKSE::SerializationInterface* serialization);
    bool Load(SKSE::SerializationInterface* serialization, std::uint32_t version,
        std::uint32_t length);
    void Clear();
}
