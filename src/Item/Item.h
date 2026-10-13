// ItemPreview.h
#pragma once

#include <RE/Skyrim.h>
#include <imgui.h>

namespace ItemPreview
{
    struct BlurMask
    {
        ImVec2 center{};
        ImVec2 radii{};
    };

    // Multiplicador único da área protegida pelo blur. Aumente este valor
    // caso itens maiores ainda recebam blur nas extremidades.
    inline constexpr float kBlurMaskGlobalScale = 4.0f;

    bool Show(
        RE::TESForm* form,
        std::uint16_t uniqueID = 0,
        bool hasUniqueID = false
    );
    void Update();
    void Hide();
    bool IsVisible();
    // Retorna a elipse dinâmica do preview no espaço virtual do HUD.
    bool GetBlurMask(BlurMask& a_mask);

    // Resíduo visual curto disparado somente em troca de spell para item
    // comum. É uma camada 2D independente do preview UI3D e pode ser
    // removida sem alterar o carregamento/render do modelo.
    void DrawSpellExitShadow(ImDrawList* a_draw, float a_alpha = 1.0f);

    // Posição no HUD (em pixels de tela) onde o item 3D deve projetar.
    // Chame ANTES de Update() no mesmo frame, ou ele mantém a última
    // posição definida.


    void SetHudPosition(ImVec2 a_screenPos);
    void SetSizeScale(float a_scale);
    // Opacidade transitória do preview 3D. Usada pelo fechamento suave do
    // WheelSettings, sem alterar a escala ou a posição do item.
    void SetOpacity(float a_opacity);
    void InvalidateSizeScale();

    void SetRotationDirection(float a_direction);

    // True quando o modelo já tem geometria carregada (radius > 0) e já
    // recebeu ao menos uma atualização de posição/rotação nesse estado.
    bool IsReady();

    //var para calculo de boundbox do item
    static bool g_scaleNormalized = false;

    // Tamanho de referência que queremos para todos os modelos.
    // Depois você ajusta este número visualmente.
    static constexpr float kTargetBoundRadius = 50.0f;

    RE::TESForm* GetCurrentForm();
}
