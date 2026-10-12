#pragma once

#include <imgui.h>

#include <vector>

// Camada exclusivamente visual do Quick Draw. Não conhece itens, input,
// reconhecimento de gestos ou estados de equipamento.
namespace QuickDrawMagicEffects
{
    inline constexpr int kEdgeEffectStyleCount = 6;

    void Reset();

    void DrawLive(ImDrawList* draw, const std::vector<ImVec2>& normalizedPoints,
        const ImVec2& center, float radius, const ImVec2& cursor,
        bool drawing, bool gameplay, bool showCracks, int edgeEffectStyle,
        float edgeEffectSize, float edgeEffectOpacity, float alpha, float time);

    void DrawStoredStrokeGlow(ImDrawList* draw,
        const std::vector<ImVec2>& normalizedPoints,
        const ImVec2& center, float radius, float alpha);

    void TriggerRelease(const std::vector<ImVec2>& normalizedPoints,
        const ImVec2& center, float radius, bool matched);
    void DrawRelease(ImDrawList* draw, float alpha, float deltaTime);
}
