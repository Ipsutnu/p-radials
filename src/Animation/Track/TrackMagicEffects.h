#pragma once

#include <imgui.h>

#include <vector>

namespace RadialShape
{
    enum class Style : int;
}

// Camada decorativa do editor/radial custom. Nenhuma função deste módulo
// modifica Track::Layout, input, validação ou a posição dos itens.
namespace TrackMagicEffects
{
    void Reset();

    void DrawEditorLiveStroke(ImDrawList* draw,
        const std::vector<ImVec2>& points, const ImVec2& tip, float time);
    void DrawEditorRegisteredEdge(ImDrawList* draw,
        const std::vector<ImVec2>& points, bool activeDrawing, float time);
    void TriggerEditorRelease(const std::vector<ImVec2>& points, bool valid);
    void DrawEditorRelease(ImDrawList* draw, float deltaTime);

    void DrawGameplayAura(ImDrawList* draw, const ImVec2& center,
        float radius, bool leftSide, RadialShape::Style shape,
        float alpha, float time, bool active, bool showSparks);
}
