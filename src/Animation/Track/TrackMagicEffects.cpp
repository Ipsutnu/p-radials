#include "PCH.h"
#include "Animation/Track/TrackMagicEffects.h"
#include "Animation/RadialShape.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr ImU32 kWarmCore = IM_COL32(255, 241, 195, 255);
    constexpr ImU32 kGold = IM_COL32(239, 196, 104, 255);
    constexpr ImU32 kCold = IM_COL32(116, 183, 232, 255);

    struct Ember
    {
        ImVec2 position{};
        ImVec2 velocity{};
        float age = 0.0f;
        float lifetime = 0.0f;
        bool cold = false;
    };

    std::vector<Ember> g_embers;
    std::vector<ImVec2> g_releasePath;
    float g_releasePulse = 0.0f;
    bool g_releaseValid = true;
    bool g_gameplayWasActive = false;
    float g_gameplayPulse = 0.0f;
    std::array<float, 4> g_gameplaySparkAngles{};
    std::array<float, 4> g_gameplaySparkVelocities{};
    bool g_gameplaySparkMotionInitialized = false;
    unsigned int g_gameplaySparkOpenSeed = 0;
    int g_gameplaySparkUpdateFrame = -1;

    float SparkPulse(unsigned int value)
    {
        // Gerador determinístico pequeno: evita depender de estado do jogo
        // para dar um novo impulso sutil a cada abertura do radial.
        value = value * 1664525u + 1013904223u;
        return static_cast<float>((value >> 8u) & 0xFFFFu) / 65535.0f;
    }

    void NudgeSparkVelocities(float impulse)
    {
        constexpr float kMaxVelocity = 5.0f;
        for (float& velocity : g_gameplaySparkVelocities)
            velocity = std::clamp(velocity + impulse,
                -kMaxVelocity, kMaxVelocity);
    }

    ImU32 WithAlpha(ImU32 color, float alpha)
    {
        const int base = static_cast<int>((color >> 24u) & 0xFFu);
        const int result = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) *
            static_cast<float>(base));
        return (color & 0x00FFFFFFu) | (static_cast<ImU32>(result) << 24u);
    }

    ImVec2 Perpendicular(const ImVec2& value)
    {
        const float length = std::sqrt(value.x * value.x + value.y * value.y);
        if (length <= 0.001f)
            return ImVec2(0.0f, -1.0f);
        return ImVec2(-value.y / length, value.x / length);
    }

    void EmitReleaseEmbers(const std::vector<ImVec2>& points, bool cold)
    {
        if (points.size() < 2)
            return;

        constexpr int count = 18;
        g_embers.reserve(g_embers.size() + count);
        for (int i = 0; i < count; ++i)
        {
            const float t = static_cast<float>(i) /
                static_cast<float>(count - 1);
            const std::size_t index = std::min(
                static_cast<std::size_t>(t * static_cast<float>(points.size() - 1)),
                points.size() - 2);
            const ImVec2 from = points[index];
            const ImVec2 to = points[index + 1];
            const ImVec2 normal = Perpendicular(
                ImVec2(to.x - from.x, to.y - from.y));
            const float sign = (i % 2 == 0) ? 1.0f : -1.0f;
            const float speed = 28.0f + static_cast<float>((i * 19) % 37);
            g_embers.push_back(Ember{
                ImVec2(from.x + (to.x - from.x) * t,
                    from.y + (to.y - from.y) * t),
                ImVec2(normal.x * sign * speed,
                    normal.y * sign * speed - 12.0f),
                0.0f, 0.32f + static_cast<float>(i % 4) * 0.06f, cold });
        }
    }
}

namespace TrackMagicEffects
{
    void Reset()
    {
        g_embers.clear();
        g_releasePath.clear();
        g_releasePulse = 0.0f;
        g_gameplayWasActive = false;
        g_gameplayPulse = 0.0f;
        g_gameplaySparkAngles = {};
        g_gameplaySparkVelocities = {};
        g_gameplaySparkMotionInitialized = false;
        g_gameplaySparkOpenSeed = 0;
        g_gameplaySparkUpdateFrame = -1;
    }

    void NudgeGameplayAuraRotation(int direction)
    {
        if (direction == 0)
            return;

        // Altera a velocidade física persistente, não um offset temporário.
        // Assim o scroll contrário freia, para e por fim inverte o giro.
        constexpr float kScrollImpulse = 0.42f;
        NudgeSparkVelocities(direction < 0 ? kScrollImpulse : -kScrollImpulse);
    }

    void DrawEditorLiveStroke(ImDrawList* draw,
        const std::vector<ImVec2>& points, const ImVec2& tip, float time)
    {
        if (!draw || points.empty())
            return;

        if (points.size() > 1)
        {
            draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                WithAlpha(kGold, 0.10f), 0, 13.0f);
            draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                WithAlpha(kGold, 0.34f), 0, 6.0f);
            draw->AddPolyline(points.data(), static_cast<int>(points.size()),
                kWarmCore, 0, 1.65f);
        }

        // Faíscas orbitais determinísticas: não adicionam estado de jogo.
        constexpr int sparkCount = 3;

        for (int i = 0; i < sparkCount; ++i)
        {
            const float phase = time * (5.0f + i * 0.43f) + i * 1.256f;
            const float orbit = 7.0f + static_cast<float>(i % 3) * 2.7f;
            const ImVec2 spark(tip.x + std::cos(phase) * orbit,
                tip.y + std::sin(phase * 1.31f) * orbit * 0.58f);
            draw->AddCircleFilled(spark, 3.8f, WithAlpha(kGold, 0.10f), 12);
            draw->AddCircleFilled(spark, 1.25f + (i % 2) * 0.35f,
                i % 2 == 0 ? kWarmCore : kCold, 12);
        }
        draw->AddCircleFilled(tip, 8.0f, WithAlpha(kGold, 0.18f), 20);
        draw->AddCircleFilled(tip, 2.2f, kWarmCore, 16);
    }

    void DrawEditorRegisteredEdge(ImDrawList* draw,
        const std::vector<ImVec2>& points, bool activeDrawing, float time)
    {
        if (!draw || !activeDrawing || points.size() < 2)
            return;

        // Vibração sutil no halo, sem deslocar o traço real/editável.
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const ImVec2 tangent(points[i].x - points[i - 1].x,
                points[i].y - points[i - 1].y);
            const ImVec2 normal = Perpendicular(tangent);
            const float a = std::sin(time * 14.0f + static_cast<float>(i) * 1.73f) * 0.85f;
            const float b = std::sin(time * 14.0f + static_cast<float>(i + 1) * 1.73f) * 0.85f;
            const ImVec2 from(points[i - 1].x + normal.x * a,
                points[i - 1].y + normal.y * a);
            const ImVec2 to(points[i].x + normal.x * b,
                points[i].y + normal.y * b);
            draw->AddLine(from, to, WithAlpha(kGold, 0.22f), 3.8f);
        }
    }

    void TriggerEditorRelease(const std::vector<ImVec2>& points, bool valid)
    {
        g_releasePath = points;
        g_releasePulse = 1.0f;
        g_releaseValid = valid;
        EmitReleaseEmbers(points, !valid);
    }

    void DrawEditorRelease(ImDrawList* draw, float deltaTime)
    {
        if (!draw)
            return;

        const float dt = std::clamp(deltaTime, 0.0f, 0.05f);
        if (g_releasePulse > 0.0f)
        {
            const float pulse = g_releasePulse;
            const ImU32 color = g_releaseValid ? kGold : kWarmCore; //kCold: azul celeste claro, 
            if (g_releasePath.size() > 1)
            {
                draw->AddPolyline(g_releasePath.data(),
                    static_cast<int>(g_releasePath.size()),
                    WithAlpha(color, pulse * 0.16f), 0,
                    14.0f + (1.0f - pulse) * 12.0f);
                draw->AddPolyline(g_releasePath.data(),
                    static_cast<int>(g_releasePath.size()),
                    WithAlpha(kWarmCore, pulse), 0,
                    2.5f + pulse * 2.0f);
            }
            g_releasePulse = std::max(0.0f, g_releasePulse - dt / 0.28f);
        }

        for (auto& ember : g_embers)
        {
            ember.age += dt;
            ember.position.x += ember.velocity.x * dt;
            ember.position.y += ember.velocity.y * dt;
            ember.velocity.y += 34.0f * dt;
            const float life = 1.0f - ember.age / ember.lifetime;
            if (life <= 0.0f)
                continue;
            const ImU32 color = ember.cold ? kCold : kGold;
            draw->AddCircleFilled(ember.position, 4.0f * life,
                WithAlpha(color, life * 0.20f), 12);
            draw->AddCircleFilled(ember.position, 1.4f * life + 0.35f,
                WithAlpha(ember.cold ? kWarmCore : color, life), 12);
        }
        std::erase_if(g_embers, [](const Ember& ember) {
            return ember.age >= ember.lifetime;
        });
    }

    void DrawGameplayAura(ImDrawList* draw, const ImVec2& center,
        float radius, bool leftSide, RadialShape::Style shape,
        float alpha, float time, bool active, bool showSparks)
    {
        if (!draw || !active)
        {
            g_gameplayWasActive = false;
            return;
        }
        if (!g_gameplayWasActive)
        {
            g_gameplayPulse = 1.0f;
            g_gameplayWasActive = true;
            if (!g_gameplaySparkMotionInitialized)
            {
                // Os quatro pontos partem em velocidades distintas, como
                // antes, mas estas passam a ser livres para o scroll alterar.
                for (int i = 0; i < 4; ++i)
                {
                    g_gameplaySparkAngles[i] = static_cast<float>(i) * 1.5708f;
                    g_gameplaySparkVelocities[i] = 0.90f + static_cast<float>(i) * 0.08f;
                }
                g_gameplaySparkMotionInitialized = true;
            }

            // Pequeno pulso aleatório por abertura, sem redefinir o sentido
            // que o jogador já estabeleceu usando o scroll.
            const float pulse = 0.12f + SparkPulse(++g_gameplaySparkOpenSeed) * 0.16f;
            const float signedPulse = (SparkPulse(g_gameplaySparkOpenSeed + 41u) < 0.5f)
                ? -pulse : pulse;
            NudgeSparkVelocities(signedPulse);
        }

        const float visibility = std::clamp(alpha, 0.0f, 1.0f);
        const int frame = ImGui::GetFrameCount();
        if (g_gameplaySparkUpdateFrame != frame)
        {
            const float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
            for (int i = 0; i < 4; ++i)
                g_gameplaySparkAngles[i] += g_gameplaySparkVelocities[i] * dt;
            g_gameplaySparkUpdateFrame = frame;
        }
        const auto outline = RadialShape::Outline(shape, center,
            radius + 6.0f, leftSide, 96);
        if (outline.size() > 1)
        {
            const float pulse = g_gameplayPulse;
            draw->AddPolyline(outline.data(), static_cast<int>(outline.size()),
                WithAlpha(kWarmCore, visibility * (0.04f + pulse * 0.14f)),
                ImDrawFlags_Closed, 5.0f + pulse * 4.0f);
        }

        // Poucas faíscas correm pelo perímetro; é só uma camada decorativa.
        if (showSparks)
        {
            for (int i = 0; i < 5; ++i)
            {
                const float angle = g_gameplaySparkAngles[i];
                const ImVec2 point = RadialShape::PositionAtAngle(shape, center,
                    radius + 6.0f, angle);
                const float twinkle = 0.55f + 0.45f * std::sin(time * 7.0f + i);
                draw->AddCircleFilled(point, 5.0f, WithAlpha(kGold,
                    visibility * 0.10f * twinkle), 12);
                draw->AddCircleFilled(point, 1.15f + twinkle * 0.65f,
                    WithAlpha(kWarmCore, visibility * twinkle), 12);
            }
        }
        g_gameplayPulse = std::max(0.0f,
            g_gameplayPulse - std::min(ImGui::GetIO().DeltaTime, 0.05f) / 0.26f);
    }
}
