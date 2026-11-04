#include "Icon.h"
#include "IconCustom.h"
#include "IconResolver.h"
#include "SWFBridge.h"
#include "RenderManager.h"
#include "Config.h"
#include "Logger.h"
#include "SvgRasterizer.h"

#include <RE/Skyrim.h>

#include <Windows.h>
#include <wincodec.h>

#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>


namespace ItemIcon
{
    namespace
    {
        // ============================================================
        // STORAGE
        // ============================================================

        std::unordered_map<Type, ID3D11ShaderResourceView*> g_icons;

        IWICImagingFactory* g_wicFactory = nullptr;

        bool g_initialized = false;


        // ============================================================
        // ICON TABLE
        // ============================================================

        struct IconEntry
        {
            Type type;
            const wchar_t* filename;
        };


        static constexpr IconEntry ICON_FILES[] =
        {
            // --------------------------------------------------------
            // FALLBACK
            // --------------------------------------------------------

            { Type::Unknown, L"default_misc.png" },


            // --------------------------------------------------------
            // WEAPONS
            // --------------------------------------------------------

            { Type::DefaultWeapon,       L"default_weapon.png" },

            { Type::WeaponSword,         L"weapon_sword.png" },
            { Type::WeaponGreatsword,    L"weapon_greatsword.png" },
            { Type::WeaponDaedra,        L"weapon_daedra.png" },
            { Type::WeaponDagger,        L"weapon_dagger.png" },
            { Type::WeaponWarAxe,        L"weapon_waraxe.png" },
            { Type::WeaponBattleaxe,     L"weapon_battleaxe.png" },
            { Type::WeaponMace,          L"weapon_mace.png" },
            { Type::WeaponHammer,        L"weapon_hammer.png" },
            { Type::WeaponStaff,         L"weapon_staff.png" },
            { Type::WeaponBow,           L"weapon_bow.png" },
            { Type::WeaponArrow,         L"weapon_arrow.png" },
            { Type::WeaponPickaxe,       L"weapon_pickaxe.png" },
            { Type::WeaponWoodAxe,       L"weapon_woodaxe.png" },
            { Type::WeaponCrossbow,      L"weapon_crossbow.png" },
            { Type::WeaponBolt,          L"weapon_bolt.png" },
            { Type::WeaponFishingRod,    L"weapon_fishingrod.png" },


            // --------------------------------------------------------
            // ARMOR
            // --------------------------------------------------------

            { Type::DefaultArmor,        L"default_armor.png" },

            { Type::LightArmorBody,      L"lightarmor_body.png" },
            { Type::LightArmorHead,      L"lightarmor_head.png" },
            { Type::LightArmorHands,     L"lightarmor_hands.png" },
            { Type::LightArmorForearms,  L"lightarmor_forearms.png" },
            { Type::LightArmorFeet,      L"lightarmor_feet.png" },
            { Type::LightArmorCalves,    L"lightarmor_calves.png" },
            { Type::LightArmorShield,    L"lightarmor_shield.png" },
            { Type::LightArmorMask,      L"lightarmor_mask.png" },

            { Type::ArmorBody,           L"armor_body.png" },
            { Type::ArmorHead,           L"armor_head.png" },
            { Type::ArmorHands,          L"armor_hands.png" },
            { Type::ArmorForearms,       L"armor_forearms.png" },
            { Type::ArmorFeet,           L"armor_feet.png" },
            { Type::ArmorCalves,         L"armor_calves.png" },
            { Type::ArmorShield,         L"armor_shield.png" },
            { Type::ArmorMask,           L"armor_mask.png" },
            { Type::ArmorBracer,         L"armor_bracer.png" },
            { Type::ArmorDaedra,         L"armor_daedra.png" },


            // --------------------------------------------------------
            // CLOTHING
            // --------------------------------------------------------

            { Type::ClothingBody,        L"clothing_body.png" },
            { Type::ClothingRobe,        L"clothing_robe.png" },
            { Type::ClothingHead,        L"clothing_head.png" },
            { Type::ClothingPants,       L"clothing_pants.png" },
            { Type::ClothingHands,       L"clothing_hands.png" },
            { Type::ClothingForearms,    L"clothing_forearms.png" },
            { Type::ClothingFeet,        L"clothing_feet.png" },
            { Type::ClothingCalves,      L"clothing_calves.png" },
            { Type::ClothingShoes,       L"clothing_shoes.png" },
            { Type::ClothingShield,      L"clothing_shield.png" },
            { Type::ClothingMask,        L"clothing_mask.png" },

            { Type::ClothingBackpack,    L"clothing_backpack.png" },
            { Type::ClothingCloak,       L"clothing_cloak.png" },


            // --------------------------------------------------------
            // JEWELRY
            // --------------------------------------------------------

            { Type::ArmorAmulet,         L"armor_amulet.png" },
            { Type::ArmorRing,           L"armor_ring.png" },
            { Type::ArmorCirclet,        L"armor_circlet.png" },


            // --------------------------------------------------------
            // BOOK / SCROLL
            // --------------------------------------------------------

            { Type::DefaultScroll,       L"default_scroll.png" },

            { Type::DefaultBook,         L"default_book.png" },
            { Type::DefaultBookRead,     L"default_book_read.png" },

            { Type::BookTome,            L"book_tome.png" },
            { Type::BookTomeRead,        L"book_tome_read.png" },
            { Type::BookJournal,         L"book_journal.png" },
            { Type::BookNote,            L"book_note.png" },
            { Type::BookMap,             L"book_map.png" },

            { Type::ScrollSpider,        L"scroll_spider.png" },


            // --------------------------------------------------------
            // FOOD
            // --------------------------------------------------------

            { Type::DefaultFood,         L"default_food.png" },
            { Type::FoodWine,            L"food_wine.png" },
            { Type::FoodBeer,            L"food_beer.png" },


            // --------------------------------------------------------
            // INGREDIENT
            // --------------------------------------------------------

            { Type::DefaultIngredient,   L"default_ingredient.png" },


            // --------------------------------------------------------
            // KEY
            // --------------------------------------------------------

            { Type::DefaultKey,          L"default_key.png" },
            { Type::KeyHouse,            L"key_house.png" },


            // --------------------------------------------------------
            // POTIONS
            // --------------------------------------------------------

            { Type::DefaultPotion,       L"default_potion.png" },

            { Type::PotionHealth,        L"potion_health.png" },
            { Type::PotionStamina,       L"potion_stam.png" },
            { Type::PotionMagicka,       L"potion_magic.png" },

            { Type::PotionPoison,        L"potion_poison.png" },

            { Type::PotionFrost,         L"potion_frost.png" },
            { Type::PotionFire,          L"potion_fire.png" },
            { Type::PotionShock,         L"potion_shock.png" },


            // --------------------------------------------------------
            // MISC
            // --------------------------------------------------------

            { Type::DefaultMisc,         L"default_misc.png" },

            { Type::MiscArtifact,        L"misc_artifact.png" },
            { Type::MiscClutter,         L"misc_clutter.png" },
            { Type::MiscLockpick,        L"misc_lockpick.png" },
            { Type::MiscSoulGem,         L"misc_soulgem.png" },

            { Type::SoulGemEmpty,        L"soulgem_empty.png" },
            { Type::SoulGemPartial,      L"soulgem_partial.png" },
            { Type::SoulGemFull,         L"soulgem_full.png" },

            { Type::SoulGemGrandEmpty,   L"soulgem_grandempty.png" },
            { Type::SoulGemGrandPartial, L"soulgem_grandpartial.png" },
            { Type::SoulGemGrandFull,    L"soulgem_grandfull.png" },

            { Type::SoulGemAzura,        L"soulgem_azura.png" },

            { Type::MiscGem,             L"misc_gem.png" },
            { Type::MiscOre,             L"misc_ore.png" },
            { Type::MiscIngot,           L"misc_ingot.png" },
            { Type::MiscHide,            L"misc_hide.png" },
            { Type::MiscStrips,          L"misc_strips.png" },
            { Type::MiscLeather,         L"misc_leather.png" },
            { Type::MiscWood,            L"misc_wood.png" },
            { Type::MiscRemains,         L"misc_remains.png" },
            { Type::MiscTrollSkull,      L"misc_trollskull.png" },
            { Type::MiscTorch,           L"misc_torch.png" },
            { Type::MiscGoldSack,        L"misc_goldsack.png" },
            { Type::MiscGold,            L"misc_gold.png" },
            { Type::MiscDragonClaw,      L"misc_dragonclaw.png" },

            { Type::MiscHousePart,       L"misc_housepart.png" },
            { Type::MiscCamping,         L"misc_camping.png" },
            { Type::MiscChitin,          L"misc_chitin.png" },
            { Type::MiscHorseTack,       L"misc_horsetack.png" },

            { Type::SoulGemTomatoEmpty,  L"soulgem_tomatoempty.png" },
            { Type::SoulGemTomatoPartial,L"soulgem_tomatopartial.png" },
            { Type::SoulGemTomatoFull,   L"soulgem_tomatofull.png" },

            { Type::SoulGemAyleidCrystalEmpty,
                L"soulgem_ayleidcrystalempty.png" },

            { Type::SoulGemAyleidCrystalPartial,
                L"soulgem_ayleidcrystalpartial.png" },

            { Type::SoulGemAyleidCrystalFull,
                L"soulgem_ayleidcrystalfull.png" },

            { Type::MiscDwarvenScrap,    L"misc_dwarvenscrap.png" },
            { Type::MiscElderScroll,     L"misc_elderscroll.png" },
            { Type::MiscJar,             L"misc_jar.png" },

            { Type::MiscTool,            L"misc_tool.png" },
            { Type::MiscToy,             L"misc_toy.png" },
            { Type::MiscInstrument,      L"misc_instrument.png" },
            { Type::MiscBearTrap,        L"misc_beartrap.png" },


            // --------------------------------------------------------
            // MAGIC
            // --------------------------------------------------------

            { Type::DefaultAlteration,   L"default_alteration.png" },
            { Type::DefaultIllusion,     L"default_illusion.png" },
            { Type::DefaultDestruction,  L"default_destruction.png" },
            { Type::DefaultConjuration,  L"default_conjuration.png" },
            { Type::DefaultRestoration,  L"default_restoration.png" },

            { Type::DefaultShout,        L"default_shout.png" },
            { Type::DefaultPower,        L"default_power.png" },
            { Type::DefaultEffect,       L"default_effect.png" },

            { Type::MagicFire,           L"magic_fire.png" },
            { Type::MagicFrost,          L"magic_frost.png" },
            { Type::MagicShock,          L"magic_shock.png" },

            { Type::MagicSun,            L"magic_sun.png" },
            { Type::MagicWind,           L"magic_wind.png" },
            { Type::MagicVampire,        L"magic_vampire.png" },
            { Type::MagicWater,          L"magic_water.png" },
            { Type::MagicEarth,          L"magic_earth.png" }
        };


        // ============================================================
        // KEYWORD HELPER
        // ============================================================

        template <class T>
        bool HasKeyword(T* a_form, std::string_view a_keyword)
        {
            if (!a_form)
                return false;

            // HasKeywordString() não enxerga de forma confiável keywords
            // anexados em runtime (KID/OCF) e, em alguns formulários, também
            // deixa passar aliases vanilla como WeapTypePickaxe.  A
            // classificação de fallback precisa olhar a lista efetivamente
            // presente no formulário, tal como o resolvedor I4.
            for (const auto* keyword : a_form->GetKeywords())
            {
                if (!keyword || keyword->formEditorID.empty())
                    continue;

                const std::string_view editorID = keyword->formEditorID.c_str();
                if (editorID.size() != a_keyword.size())
                    continue;

                bool equal = true;
                for (std::size_t i = 0; i < editorID.size(); ++i)
                {
                    const auto lhs = static_cast<unsigned char>(editorID[i]);
                    const auto rhs = static_cast<unsigned char>(a_keyword[i]);
                    if (std::tolower(lhs) != std::tolower(rhs))
                    {
                        equal = false;
                        break;
                    }
                }
                if (equal)
                    return true;
            }

            return false;
        }

        bool ContainsInsensitive(std::string_view a_text, std::string_view a_fragment)
        {
            if (a_fragment.empty() || a_fragment.size() > a_text.size())
                return false;
            for (std::size_t start = 0; start + a_fragment.size() <= a_text.size(); ++start)
            {
                bool equal = true;
                for (std::size_t i = 0; i < a_fragment.size(); ++i)
                {
                    const auto lhs = static_cast<unsigned char>(a_text[start + i]);
                    const auto rhs = static_cast<unsigned char>(a_fragment[i]);
                    if (std::tolower(lhs) != std::tolower(rhs))
                    {
                        equal = false;
                        break;
                    }
                }
                if (equal)
                    return true;
            }
            return false;
        }


        // ============================================================
        // WIC PNG LOADER
        // ============================================================

        ID3D11ShaderResourceView* LoadPNG(
            ID3D11Device* a_device,
            const std::filesystem::path& a_path)
        {
            if (!a_device || !g_wicFactory)
                return nullptr;


            IWICBitmapDecoder* decoder = nullptr;

            HRESULT hr =
                g_wicFactory->CreateDecoderFromFilename(
                    a_path.c_str(),
                    nullptr,
                    GENERIC_READ,
                    WICDecodeMetadataCacheOnLoad,
                    &decoder);

            if (FAILED(hr) || !decoder)
                return nullptr;


            IWICBitmapFrameDecode* frame = nullptr;

            hr = decoder->GetFrame(0, &frame);

            if (FAILED(hr) || !frame)
            {
                decoder->Release();
                return nullptr;
            }


            UINT width = 0;
            UINT height = 0;

            frame->GetSize(&width, &height);

            if (!width || !height)
            {
                frame->Release();
                decoder->Release();
                return nullptr;
            }


            IWICFormatConverter* converter = nullptr;

            hr =
                g_wicFactory->CreateFormatConverter(
                    &converter);

            if (FAILED(hr) || !converter)
            {
                frame->Release();
                decoder->Release();
                return nullptr;
            }


            hr =
                converter->Initialize(
                    frame,
                    GUID_WICPixelFormat32bppRGBA,
                    WICBitmapDitherTypeNone,
                    nullptr,
                    0.0,
                    WICBitmapPaletteTypeCustom);

            if (FAILED(hr))
            {
                converter->Release();
                frame->Release();
                decoder->Release();
                return nullptr;
            }


            const UINT stride = width * 4;
            const UINT size = stride * height;

            std::vector<std::uint8_t> pixels(size);


            hr =
                converter->CopyPixels(
                    nullptr,
                    stride,
                    size,
                    pixels.data());

            if (FAILED(hr))
            {
                converter->Release();
                frame->Release();
                decoder->Release();
                return nullptr;
            }


            D3D11_TEXTURE2D_DESC textureDesc{};

            textureDesc.Width = width;
            textureDesc.Height = height;
            textureDesc.MipLevels = 1;
            textureDesc.ArraySize = 1;
            textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.Usage = D3D11_USAGE_DEFAULT;
            textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;


            D3D11_SUBRESOURCE_DATA data{};

            data.pSysMem = pixels.data();
            data.SysMemPitch = stride;


            ID3D11Texture2D* texture = nullptr;

            hr =
                a_device->CreateTexture2D(
                    &textureDesc,
                    &data,
                    &texture);

            if (FAILED(hr) || !texture)
            {
                converter->Release();
                frame->Release();
                decoder->Release();
                return nullptr;
            }


            ID3D11ShaderResourceView* srv = nullptr;

            hr =
                a_device->CreateShaderResourceView(
                    texture,
                    nullptr,
                    &srv);

            texture->Release();

            converter->Release();
            frame->Release();
            decoder->Release();


            if (FAILED(hr))
                return nullptr;

            return srv;
        }

        // ============================================================
        // MAGIC
        // ============================================================

        Type ResolveSpell(RE::SpellItem* a_spell)
        {
            if (!a_spell)
                return Type::DefaultPower;


            // --------------------------------------------------------
            // SkyUI BaseID overrides
            // --------------------------------------------------------

            const auto baseID =
                a_spell->GetFormID() & 0x00FFFFFF;


            switch (baseID)
            {
            case 14517:
            case 16210:
            case 14518:
                return Type::MagicSun;

            case 120651:
                return Type::MiscRemains;

            case 96045:
                return Type::MagicWind;

            case 467744:
            case 467729:
            case 467771:
                return Type::MagicFire;

            default:
                break;
            }


            // --------------------------------------------------------
            // SCHOOL
            // --------------------------------------------------------

            const auto school =
                a_spell->GetAssociatedSkill();


            switch (school)
            {
            case RE::ActorValue::kAlteration:
                return Type::DefaultAlteration;

            case RE::ActorValue::kConjuration:
                return Type::DefaultConjuration;

            case RE::ActorValue::kIllusion:
                return Type::DefaultIllusion;

            case RE::ActorValue::kRestoration:
                return Type::DefaultRestoration;

            case RE::ActorValue::kDestruction:
            {
                auto* effect =
                    a_spell->GetCostliestEffectItem();

                auto* baseEffect =
                    effect ?
                    effect->baseEffect :
                    a_spell->GetAVEffect();


                if (baseEffect)
                {
                    switch (baseEffect->data.resistVariable)
                    {
                    case RE::ActorValue::kResistFire:
                        return Type::MagicFire;

                    case RE::ActorValue::kResistFrost:
                        return Type::MagicFrost;

                    case RE::ActorValue::kResistShock:
                        return Type::MagicShock;

                    default:
                        break;
                    }
                }


                return Type::DefaultDestruction;
            }

            default:
                break;
            }


            return Type::DefaultPower;
        }


        // ============================================================
        // ARMOR
        // ============================================================

        Type ResolveArmor(RE::TESObjectARMO* a_armor)
        {
            if (!a_armor)
                return Type::DefaultArmor;

            using Slot = RE::BIPED_MODEL::BipedObjectSlot;
            const auto hasSlot = [a_armor](Slot slot) {
                return a_armor->HasPartOf(slot);
            };

            // O slot biped é a fonte mais confiável. Mods frequentemente
            // omitem os keywords de joias, embora mantenham esses slots para
            // o equipamento funcionar corretamente no jogo.
            if (hasSlot(Slot::kRing))
                return Type::ArmorRing;
            if (hasSlot(Slot::kAmulet))
                return Type::ArmorAmulet;

            if (HasKeyword(a_armor, "ArmorCloak"))
                return Type::ClothingCloak;
            if (HasKeyword(a_armor, "ArmorBackpack"))
                return Type::ClothingBackpack;

            if (HasKeyword(a_armor, "ArmorRing"))
                return Type::ArmorRing;
            if (HasKeyword(a_armor, "ArmorNecklace") ||
                HasKeyword(a_armor, "ArmorAmulet"))
                return Type::ArmorAmulet;

            // Alguns mods usam apenas os keywords de capacete/capuz e o
            // slot Circlet técnico. Sem estes aliases, um elmo leve cai no
            // ícone de circlet e um capuz no fallback de armadura pesada.
            const bool head = hasSlot(Slot::kHead) || a_armor->IsHelmet() ||
                HasKeyword(a_armor, "ArmorHelmet") ||
                HasKeyword(a_armor, "ClothingHead") ||
                HasKeyword(a_armor, "ClothingHood");
            const bool body = hasSlot(Slot::kBody);
            const bool hands = hasSlot(Slot::kHands);
            const bool forearms = hasSlot(Slot::kForearms);
            const bool feet = hasSlot(Slot::kFeet);
            const bool calves = hasSlot(Slot::kCalves);
            const bool shield = hasSlot(Slot::kShield);
            const bool mask = HasKeyword(a_armor, "ArmorMask") ||
                HasKeyword(a_armor, "ClothingMask") ||
                HasKeyword(a_armor, "ArmorDragonPriestMask") ||
                HasKeyword(a_armor, "VendorItemDragonPriestMask");
            // Vários elmos ocupam também o slot técnico de circlet para
            // bloquear a combinação visual. Isso não os torna circlets.
            const bool circletOnly = (hasSlot(Slot::kCirclet) ||
                a_armor->IsCirclet() || HasKeyword(a_armor, "ArmorCirclet")) &&
                !head && !mask;
            const auto armorType = a_armor->GetArmorType();
            const bool heavy = a_armor->IsHeavyArmor() ||
                armorType == RE::BGSBipedObjectForm::ArmorType::kHeavyArmor ||
                HasKeyword(a_armor, "ArmorHeavy");
            const bool light = a_armor->IsLightArmor() ||
                armorType == RE::BGSBipedObjectForm::ArmorType::kLightArmor ||
                HasKeyword(a_armor, "ArmorLight");
            const bool clothing = a_armor->IsClothing() ||
                armorType == RE::BGSBipedObjectForm::ArmorType::kClothing ||
                HasKeyword(a_armor, "ArmorClothing") ||
                HasKeyword(a_armor, "VendorItemClothing");

            // Clothing vem antes de light/heavy. Alguns capuzes usam o slot
            // técnico de cabeça (ou até flags herdadas de armadura), mas o
            // ArmorType/keyword de roupa é a intenção visual mais específica.
            // Sem essa precedência eles acabavam em armor_head.
            if (clothing)
            {
                if (mask) return Type::ClothingMask;
                if (shield || HasKeyword(a_armor, "ClothingShield")) return Type::ClothingShield;
                if (HasKeyword(a_armor, "ClothingRobe")) return Type::ClothingRobe;
                if (HasKeyword(a_armor, "ClothingPants")) return Type::ClothingPants;
                if (HasKeyword(a_armor, "ClothingShoes")) return Type::ClothingShoes;
                if (head) return Type::ClothingHead;
                // Vestes normalmente ocupam corpo e uma extensão de braço ou
                // perna; roupas de corpo simples conservam clothing_body.
                if (body && (forearms || calves)) return Type::ClothingRobe;
                if (body) return Type::ClothingBody;
                if (forearms) return Type::ClothingForearms;
                if (hands) return Type::ClothingHands;
                if (calves) return Type::ClothingCalves;
                if (feet) return Type::ClothingFeet;
            }

            // A ordem abaixo evita que uma máscara/bracer seja reduzida ao
            // slot mais genérico de cabeça/mãos.
            if (heavy)
            {
                // Máscaras de Dragon Priest costumam trazer material Daedric,
                // mas visualmente continuam sendo máscaras, não armaduras
                // Daedric genéricas.
                if (mask) return Type::ArmorMask;
                if (HasKeyword(a_armor, "ArmorMaterialDaedric") ||
                    HasKeyword(a_armor, "ArmorDaedric")) return Type::ArmorDaedra;
                if (HasKeyword(a_armor, "ArmorBracer")) return Type::ArmorBracer;
                if (shield) return Type::ArmorShield;
                if (head) return Type::ArmorHead;
                if (forearms) return Type::ArmorForearms;
                if (hands) return Type::ArmorHands;
                if (calves) return Type::ArmorCalves;
                if (feet) return Type::ArmorFeet;
                if (body) return Type::ArmorBody;
            }

            if (light)
            {
                if (mask) return Type::LightArmorMask;
                if (shield) return Type::LightArmorShield;
                if (head) return Type::LightArmorHead;
                if (forearms) return Type::LightArmorForearms;
                if (hands) return Type::LightArmorHands;
                if (calves) return Type::LightArmorCalves;
                if (feet) return Type::LightArmorFeet;
                if (body) return Type::LightArmorBody;
            }

            // Circlets ficam depois de todas as peças de cabeça completas.
            if (circletOnly) return Type::ArmorCirclet;

            // Fallback para armaduras de mods que não classificam material,
            // mas expõem corretamente os slots físicos. DefaultArmor só fica
            // para casos realmente sem tipo/slot representável.
            if (mask) return Type::ArmorMask;
            if (shield) return Type::ArmorShield;
            if (head) return Type::ArmorHead;
            if (forearms) return Type::ArmorForearms;
            if (hands) return Type::ArmorHands;
            if (calves) return Type::ArmorCalves;
            if (feet) return Type::ArmorFeet;
            if (body) return Type::ArmorBody;
            if (hasSlot(Slot::kCirclet) || a_armor->IsCirclet())
                return Type::ArmorCirclet;
            return Type::DefaultArmor;
        }


        // ============================================================
        // WEAPON
        // ============================================================

        Type ResolveWeapon(RE::TESObjectWEAP* a_weapon)
        {
            if (!a_weapon)
                return Type::DefaultWeapon;


            // SkyUI refinements.

            if (HasKeyword(a_weapon, "WeapTypeWarhammer"))
                return Type::WeaponHammer;

            // A picareta vanilla e várias de mods usam WeapTypePickaxe;
            // outras expõem apenas VendorItemPickaxe. Ambas precisam vencer
            // o tipo interno OneHandAxe.
            // A picareta vanilla não tem WeapTypePickaxe/VendorItemPickaxe:
            // ela é internamente OneHandAxe. Seu FormID base é a única
            // identificação estável nesse caminho; GetFormEditorID() não é
            // garantido para TESObjectWEAP. Mods normalmente expõem um dos
            // keywords abaixo.
            constexpr RE::FormID kVanillaPickaxe = 0x000E3C16;
            const std::string_view editorID = a_weapon->GetFormEditorID()
                ? a_weapon->GetFormEditorID() : "";
            if (a_weapon->GetFormID() == kVanillaPickaxe ||
                HasKeyword(a_weapon, "VendorItemPickaxe") ||
                HasKeyword(a_weapon, "WeapTypePickaxe") ||
                ContainsInsensitive(editorID, "pickaxe"))
                return Type::WeaponPickaxe;

            if (HasKeyword(a_weapon, "VendorItemWoodAxe"))
                return Type::WeaponWoodAxe;

            if (HasKeyword(a_weapon, "ccBGSSSE001_FishingPoleKW"))
                return Type::WeaponFishingRod;


            switch (a_weapon->GetWeaponType())
            {
            case RE::WEAPON_TYPE::kOneHandSword:
                return Type::WeaponSword;

            case RE::WEAPON_TYPE::kOneHandDagger:
                return Type::WeaponDagger;

            case RE::WEAPON_TYPE::kOneHandAxe:
                return Type::WeaponWarAxe;

            case RE::WEAPON_TYPE::kOneHandMace:
                return Type::WeaponMace;

            case RE::WEAPON_TYPE::kTwoHandSword:
                return Type::WeaponGreatsword;

            case RE::WEAPON_TYPE::kTwoHandAxe:
                return Type::WeaponBattleaxe;

            case RE::WEAPON_TYPE::kBow:
                return Type::WeaponBow;

            case RE::WEAPON_TYPE::kCrossbow:
                return Type::WeaponCrossbow;

            case RE::WEAPON_TYPE::kStaff:
                return Type::WeaponStaff;

            default:
                return Type::DefaultWeapon;
            }
        }


        // ============================================================
        // POTION / FOOD
        // ============================================================

        Type ResolveAlchemy(RE::AlchemyItem* a_item)
        {
            if (!a_item)
                return Type::DefaultPotion;


            if (a_item->IsPoison())
                return Type::PotionPoison;


            if (a_item->IsFood())
                return Type::DefaultFood;


            auto* effect =
                a_item->GetCostliestEffectItem();

            auto* baseEffect =
                effect ?
                effect->baseEffect :
                a_item->GetAVEffect();


            if (!baseEffect)
                return Type::DefaultPotion;


            // --------------------------------------------------------
            // Restoration values
            // --------------------------------------------------------

            switch (baseEffect->data.primaryAV)
            {
            case RE::ActorValue::kHealth:
                return Type::PotionHealth;

            case RE::ActorValue::kStamina:
                return Type::PotionStamina;

            case RE::ActorValue::kMagicka:
                return Type::PotionMagicka;

            default:
                break;
            }


            // --------------------------------------------------------
            // Resist potion
            // --------------------------------------------------------

            switch (baseEffect->data.primaryAV)
            {
            case RE::ActorValue::kResistFire:
                return Type::PotionFire;

            case RE::ActorValue::kResistFrost:
                return Type::PotionFrost;

            case RE::ActorValue::kResistShock:
                return Type::PotionShock;

            default:
                break;
            }


            return Type::DefaultPotion;
        }


        // ============================================================
        // MISC
        // ============================================================

        Type ResolveMisc(RE::TESObjectMISC* a_misc)
        {
            if (!a_misc)
                return Type::DefaultMisc;

            // The vanilla lockpick (Skyrim.esm:0000000A) also carries the
            // generic tool classification. Resolve it before VendorItemTool
            // so it receives its dedicated icon.
            if (a_misc->GetFormID() == 0x0000000A)
                return Type::MiscLockpick;


            // Mesma ideia do InventoryDataSetter:
            // keywords primeiro.

            if (HasKeyword(a_misc, "VendorItemDaedricArtifact"))
                return Type::MiscArtifact;

            if (HasKeyword(a_misc, "VendorItemGem"))
                return Type::MiscGem;

            if (HasKeyword(a_misc, "VendorItemAnimalHide"))
                return Type::MiscHide;

            if (HasKeyword(a_misc, "VendorItemTool"))
                return Type::MiscTool;

            if (HasKeyword(a_misc, "VendorItemAnimalPart"))
                return Type::MiscRemains;

            if (HasKeyword(a_misc, "VendorItemOreIngot"))
                return Type::MiscIngot;

            if (HasKeyword(a_misc, "VendorItemClutter"))
                return Type::MiscClutter;


            return Type::DefaultMisc;
        }
    }


    // ================================================================
    // INITIALIZE
    // ================================================================

    bool Initialize()
    {
        if (g_initialized)
            return true;


        auto* device =
            RenderManager::GetDevice();

        if (!device)
            return false;


        HRESULT hr =
            CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&g_wicFactory));


        if (FAILED(hr) || !g_wicFactory)
            return false;


        const std::filesystem::path basePath =
            L"Data\\SKSE\\Plugins\\p-radials\\Icons";


        std::size_t svgLoaded = 0;
        std::size_t svgFailed = 0;
        std::size_t pngLoaded = 0;
        for (const auto& entry : ICON_FILES)
        {
            const auto pngPath =
                basePath / entry.filename;
            auto svgPath = pngPath;
            svgPath.replace_extension(L".svg");

            // Os SVGs-base têm prioridade para preservar nitidez em qualquer
            // escala. O PNG com o mesmo nome continua sendo o fallback.
            const bool hasSvg = std::filesystem::is_regular_file(svgPath);
            auto* srv = hasSvg ? SvgRasterizer::Load(device, svgPath) : nullptr;
            if (srv)
                ++svgLoaded;
            else if (hasSvg)
                ++svgFailed;
            if (!srv)
            {
                srv = LoadPNG(device, pngPath);
                if (srv)
                    ++pngLoaded;
            }


            if (srv)
            {
                g_icons[entry.type] = srv;
            }
        }

        Logger::GetSingleton().Print(
            "Base Icons: SVG loaded={} failed={} | PNG fallback loaded={}",
            svgLoaded, svgFailed, pngLoaded);


        g_initialized = true;

        // A base interna continua disponível mesmo se o leitor I4 não
        // encontrar nenhum arquivo ou uma fonte externa não puder ser lida.
        IconCustom::Initialize(device);
        SWFBridge::Initialize(device);

        return true;
    }


    // ================================================================
    // SHUTDOWN
    // ================================================================

    void Shutdown()
    {
        IconCustom::Shutdown();
        SWFBridge::Shutdown();

        for (auto& [type, srv] : g_icons)
        {
            if (srv)
            {
                srv->Release();
                srv = nullptr;
            }
        }


        g_icons.clear();


        if (g_wicFactory)
        {
            g_wicFactory->Release();
            g_wicFactory = nullptr;
        }

        g_initialized = false;
    }


    // ================================================================
    // RESOLVE
    // ================================================================

    Type ResolveType(RE::TESForm* a_form)
    {
        if (!a_form)
            return Type::Unknown;


        switch (a_form->GetFormType())
        {
        // ------------------------------------------------------------
        // WEAPON
        // ------------------------------------------------------------

        case RE::FormType::Weapon:
            return ResolveWeapon(
                a_form->As<RE::TESObjectWEAP>());


        // ------------------------------------------------------------
        // AMMO
        // ------------------------------------------------------------

        case RE::FormType::Ammo:
        {
            auto* ammo =
                a_form->As<RE::TESAmmo>();

            if (!ammo)
                return Type::WeaponArrow;


            // SkyUI AMMOFLAG_NONBOLT = 4.
            //
            // Sem depender do layout interno da sua TESAmmo,
            // arrow é o fallback seguro por enquanto.

            return Type::WeaponArrow;
        }


        // ------------------------------------------------------------
        // ARMOR
        // ------------------------------------------------------------

        case RE::FormType::Armor:
            return ResolveArmor(
                a_form->As<RE::TESObjectARMO>());


        // ------------------------------------------------------------
        // BOOK
        // ------------------------------------------------------------

        case RE::FormType::Book:
        {
            auto* book =
                a_form->As<RE::TESObjectBOOK>();

            if (!book)
                return Type::DefaultBook;


            if (book->data.flags.all(
                    RE::OBJ_BOOK::Flag::kTeachesSpell))
            {
                return Type::BookTome;
            }


            return Type::DefaultBook;
        }


        // ------------------------------------------------------------
        // SCROLL
        // ------------------------------------------------------------

        case RE::FormType::Scroll:
            return Type::DefaultScroll;


        // ------------------------------------------------------------
        // INGREDIENT
        // ------------------------------------------------------------

        case RE::FormType::Ingredient:
            return Type::DefaultIngredient;


        // ------------------------------------------------------------
        // POTION / FOOD
        // ------------------------------------------------------------

        case RE::FormType::AlchemyItem:
            return ResolveAlchemy(
                a_form->As<RE::AlchemyItem>());


        // ------------------------------------------------------------
        // KEY
        // ------------------------------------------------------------

        case RE::FormType::KeyMaster:
            return Type::DefaultKey;


        // ------------------------------------------------------------
        // SOUL GEM
        // ------------------------------------------------------------

        case RE::FormType::SoulGem:
            return Type::MiscSoulGem;


        // ------------------------------------------------------------
        // LIGHT / TORCH
        // ------------------------------------------------------------

        case RE::FormType::Light:
            return Type::MiscTorch;


        // ------------------------------------------------------------
        // MISC
        // ------------------------------------------------------------

        case RE::FormType::Misc:
            return ResolveMisc(
                a_form->As<RE::TESObjectMISC>());


        // ------------------------------------------------------------
        // SPELL
        // ------------------------------------------------------------

        case RE::FormType::Spell:
            return ResolveSpell(
                a_form->As<RE::SpellItem>());


        // ------------------------------------------------------------
        // SHOUT
        // ------------------------------------------------------------

        case RE::FormType::Shout:
            return Type::DefaultShout;


        // ------------------------------------------------------------
        // MAGIC EFFECT
        // ------------------------------------------------------------

        case RE::FormType::MagicEffect:
            return Type::DefaultEffect;


        // ------------------------------------------------------------
        // ENCHANTMENT
        // ------------------------------------------------------------

        case RE::FormType::Enchantment:
            return Type::DefaultEffect;


        default:
            break;
        }


        return Type::Unknown;
    }


    // ================================================================
    // GET
    // ================================================================

    ID3D11ShaderResourceView* Get(Type a_type)
    {
        auto it =
            g_icons.find(a_type);


        if (it != g_icons.end())
            return it->second;


        // Categoria não carregada:
        // tenta default_misc.

        it =
            g_icons.find(Type::Unknown);


        if (it != g_icons.end())
            return it->second;


        return nullptr;
    }


    ID3D11ShaderResourceView* Get(RE::TESForm* a_form)
    {
        if (Config::g_customIcons)
        {
            if (auto* icon = IconCustom::Get(a_form))
                return icon;
        }
        // Ícones locais do p-radials continuam tendo prioridade. O bridge
        // I4 entra apenas antes do símbolo genérico calculado por tipo.
        if (const auto resolved = IconResolver::Resolve({ a_form }); resolved &&
            resolved->iconSource && resolved->iconLabel)
        {
            if (auto* icon = SWFBridge::GetTexture(*resolved->iconSource,
                    *resolved->iconLabel))
                return icon;
        }
        return Get(
            ResolveType(a_form));
    }
}
