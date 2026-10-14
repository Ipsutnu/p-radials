#include "PCH.h"
#include "Animation/QuickDrawMagicEffects.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr ImU32 kCore = IM_COL32(255, 246, 216, 255);
    constexpr ImU32 kGold = IM_COL32(239, 196, 104, 255);
    constexpr ImU32 kRadial = IM_COL32(245, 245, 245, 255);
    // Cancelamento também usa branco: não deixa um pulso azulado residual.
    constexpr ImU32 kCold = kCore;
    constexpr float kTwoPi = 6.28318530718f;
    constexpr float kCrackSpawnInterval = 0.075f;
    constexpr float kCrackLifetimeMin = 0.11f;
    constexpr float kCrackLifetimeMax = 0.26f;
    constexpr float kCrackAngleSpread = 0.18f;
    constexpr float kCrackRareChance = 0.07f;
    constexpr float kCrackRareLength = 260.0f;

    struct Spark
    {
        ImVec2 position{};
        ImVec2 velocity{};
        float age = 0.0f;
        float lifetime = 0.0f;
        bool cold = false;
    };

    struct CrackSegment
    {
        ImVec2 from{};
        ImVec2 to{};
        float alpha = 1.0f;
        float thickness = 1.0f;
    };

    struct EdgeSpark
    {
        ImVec2 position{};
        ImVec2 velocity{};
        float size = 1.0f;
    };

    struct CrackFlash
    {
        std::vector<CrackSegment> segments;
        std::vector<EdgeSpark> sparks;
        ImVec2 center{};
        // A escala global parte do ponto de contato com o círculo do Quick Draw.
        ImVec2 anchor{};
        float age = 0.0f;
        float lifetime = 0.0f;
    };

    // Os quatro estilos abaixo não usam as rachaduras: cada um tem seu próprio
    // ciclo de vida para não parecer uma variação do mesmo raio estático.
    struct BouncingLaser
    {
        ImVec2 position{};
        ImVec2 velocity{};
        float age = 0.0f;
        float lifetime = 0.0f;
        float size = 1.0f;
        int bounces = 0;
        int maxBounces = 5;
    };

    struct FallingEdgeSpark
    {
        ImVec2 position{};
        ImVec2 velocity{};
        float age = 0.0f;
        float lifetime = 0.0f;
        float size = 1.0f;
    };

    struct GhostCircle
    {
        ImVec2 position{};
        ImVec2 linkTarget{};
        float age = 0.0f;
        float lifetime = 0.0f;
        float radius = 0.0f;
        bool linked = false;
    };

    struct GeometryTrace
    {
        std::vector<ImVec2> points;
        float age = 0.0f;
        float lifetime = 0.0f;
        float drawDuration = 0.0f;
    };

    std::vector<Spark> g_releaseSparks;
    std::vector<ImVec2> g_releasePath;
    ImVec2 g_releaseCenter{};
    float g_releaseRadius = 0.0f;
    float g_releaseT = 0.0f;
    bool g_releaseMatched = false;
    std::vector<CrackFlash> g_crackFlashes;
    float g_lastCrackSpawnTime = -100.0f;
    float g_lastCrackAngle = 0.0f;
    bool g_hasLastCrackAngle = false;
    int g_crackSeed = 0;
    std::vector<BouncingLaser> g_bouncingLasers;
    std::vector<FallingEdgeSpark> g_fallingEdgeSparks;
    std::vector<GhostCircle> g_ghostCircles;
    std::vector<GeometryTrace> g_geometryTraces;
    float g_lastSpecialSpawnTime = -100.0f;
    int g_specialSeed = 0;

    ImU32 WithAlpha(ImU32 color, float alpha)
    {
        const int source = static_cast<int>((color >> 24u) & 0xFFu);
        const int result = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * source);
        return (color & 0x00FFFFFFu) | (static_cast<ImU32>(result) << 24u);
    }

    float Smooth(float value)
    {
        value = std::clamp(value, 0.0f, 1.0f);
        return value * value * (3.0f - 2.0f * value);
    }

    float Random01(int value)
    {
        const float noise = std::sin(static_cast<float>(value) * 12.9898f) * 43758.5453f;
        return noise - std::floor(noise);
    }

    ImVec2 ToScreen(const ImVec2& point, const ImVec2& center, float radius)
    {
        return ImVec2(center.x + point.x * radius, center.y + point.y * radius);
    }

    ImVec2 Normalized(const ImVec2& value)
    {
        const float length = std::sqrt(value.x * value.x + value.y * value.y);
        return length > 0.001f ? ImVec2(value.x / length, value.y / length) : ImVec2(0.0f, -1.0f);
    }

    float AngleDelta(float a, float b)
    {
        return std::atan2(std::sin(a - b), std::cos(a - b));
    }

    ImVec2 Add(const ImVec2& a, const ImVec2& b)
    {
        return ImVec2(a.x + b.x, a.y + b.y);
    }

    ImVec2 Scale(const ImVec2& value, float amount)
    {
        return ImVec2(value.x * amount, value.y * amount);
    }

    ImVec2 DirectionFromAngle(float angle)
    {
        return ImVec2(std::cos(angle), std::sin(angle));
    }

    void AppendCrackBranch(CrackFlash& flash, ImVec2 start, float angle,
        float length, int seed, float alpha)
    {
        const int segments = 2 + static_cast<int>(Random01(seed + 4) * 3.0f);
        ImVec2 point = start;
        float direction = angle;
        for (int i = 0; i < segments; ++i)
        {
            direction += (Random01(seed + i * 17) - 0.5f) * 0.72f;
            const float step = length / static_cast<float>(segments) *
                (0.70f + Random01(seed + i * 29) * 0.55f);
            const ImVec2 next(point.x + std::cos(direction) * step,
                point.y + std::sin(direction) * step);
            flash.segments.push_back({ point, next,
                alpha * (1.0f - static_cast<float>(i) / (segments + 1)),
                i == 0 ? 1.1f : 0.75f });
            point = next;
        }
    }

    // kCrackSpawnInterval  // frequência máxima de novos flashes
    // kCrackLifetimeMin
    // kCrackLifetimeMax    // duração do flash
    // kCrackAngleSpread    // abertura lateral dos raios
    // kCrackRareChance     // frequência de raio longo
    // kCrackRareLength     // alcance máximo raro
    void SpawnCrackFlash(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_crackSeed * 101;
        
        CrackFlash flash{};
        flash.center = center;
        flash.anchor = ImVec2(center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius);
        
        flash.lifetime = kCrackLifetimeMin +
            Random01(seed + 1) * (kCrackLifetimeMax - kCrackLifetimeMin);
        
        const int rayCount = 1 + static_cast<int>(Random01(seed + 2) * 3.0f);
        
        flash.segments.reserve(static_cast<std::size_t>(rayCount) * 10);

        for (int ray = 0; ray < rayCount; ++ray)
        {
            const int raySeed = seed + ray * 113;
        
            const float rayAngle = angle +
                (Random01(raySeed) - 0.5f) * kCrackAngleSpread;
        
            const ImVec2 outward(std::cos(rayAngle), std::sin(rayAngle));
        
            const ImVec2 start(center.x + outward.x * radius,
                center.y + outward.y * radius);
        
            const bool rareLong = Random01(raySeed + 3) > 1.0f - kCrackRareChance;
        
            const float totalLength = rareLong
                ? kCrackRareLength * (0.55f + Random01(raySeed + 5) * 0.45f)
                : 22.0f + Random01(raySeed + 6) * 95.0f;
        
            const int segments = 3 + static_cast<int>(Random01(raySeed + 7) * 4.0f);
        
            ImVec2 point = start;
        
            float direction = rayAngle + (Random01(raySeed + 8) - 0.5f) * 0.35f;
        
            for (int i = 0; i < segments; ++i)
            {
                direction += (Random01(raySeed + i * 31) - 0.5f) * 0.72f;
        
                const float step = totalLength / static_cast<float>(segments) *
                    (0.65f + Random01(raySeed + i * 43) * 0.65f);
        
                const ImVec2 next(point.x + std::cos(direction) * step,
                    point.y + std::sin(direction) * step);
        
                const float fade = 1.0f - static_cast<float>(i) / (segments + 1);
        
                flash.segments.push_back({ point, next, fade, i == 0 ? 1.35f : 0.9f });

                // Para dar mais galhos perceptíveis if (i > 0 && Random01(raySeed + i * 59) > 0.57f)
                // Diminua 0.57f para 0.35f: cria ramificações com mais frequência.
                if (i > 0 && Random01(raySeed + i * 59) > 0.35f)
                {
                    // Para eles abrirem mais de lado, neste valor:
                    // 0.55f + Random01(...) * 0.42f
                    // aumente, por exemplo, para:
                    // 0.85f + Random01(...) * 0.70f
                    // Ele controla o ângulo lateral do galho em relação ao raio principal.
                    const float branchAngle = direction +
                        (Random01(raySeed + i * 67) > 0.5f ? 1.0f : -1.0f) *
                        (0.55f + Random01(raySeed + i * 71) * 0.60f);
                    
                    // step * (0.35f + Random01(...) * 0.60f)
                    // Aumente para algo como:
                    // step * (0.65f + Random01(...) * 0.95f)
                    // Isso deixa os galhos laterais maiores.
                    AppendCrackBranch(flash, point, branchAngle,
                        step * (0.35f + Random01(raySeed + i * 79) * 0.90f),
                        raySeed + i * 83, fade * 0.72f);
                }
        
                point = next;
            }

            const int sparkCount = 2 + static_cast<int>(Random01(raySeed + 89) * 4.0f);
            for (int i = 0; i < sparkCount; ++i)
            {
                const float sparkAngle = rayAngle +
                    (Random01(raySeed + i * 97) - 0.5f) * 0.34f;
                const float distance = 4.0f + Random01(raySeed + i * 103) * 24.0f;
                flash.sparks.push_back({
                    ImVec2(start.x + std::cos(sparkAngle) * distance,
                        start.y + std::sin(sparkAngle) * distance),
                    ImVec2(0.0f, 0.0f), 1.0f });
            }
        }
        g_crackFlashes.push_back(std::move(flash));
    }

    void AddFlashSpark(CrackFlash& flash, const ImVec2& point,
        const ImVec2& velocity = {}, float size = 1.0f)
    {
        flash.sparks.push_back({ point, velocity, size });
    }

    CrackFlash MakeEdgeFlash(int seed, const ImVec2& center)
    {
        CrackFlash flash{};
        flash.center = center;
        flash.lifetime = kCrackLifetimeMin +
            Random01(seed + 1) * (kCrackLifetimeMax - kCrackLifetimeMin);
        flash.segments.reserve(28);
        return flash;
    }

    void SpawnSineWaveFlash(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_crackSeed * 101;
        CrackFlash flash = MakeEdgeFlash(seed, center);
        const ImVec2 outward(std::cos(angle), std::sin(angle));
        const ImVec2 tangent(-outward.y, outward.x);
        const ImVec2 start(center.x + outward.x * radius,
            center.y + outward.y * radius);
        flash.anchor = start;
        const int points = 9 + static_cast<int>(Random01(seed + 2) * 5.0f);
        const float length = 58.0f + Random01(seed + 3) * 92.0f;
        const float amplitude = 6.0f + Random01(seed + 4) * 15.0f;
        const float phase = Random01(seed + 5) * 1.8f;
        ImVec2 previous = start;
        for (int i = 1; i <= points; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(points);
            const float forward = length * t;
            const float side = std::sin(t * 12.0f + phase) * amplitude * (1.0f - t * 0.35f);
            const ImVec2 next(start.x + outward.x * forward + tangent.x * side,
                start.y + outward.y * forward + tangent.y * side);
            flash.segments.push_back({ previous, next, 1.0f - t * 0.48f, 1.05f });
            previous = next;
        }
        AddFlashSpark(flash, start);
        for (int i = 0; i < 7; ++i)
        {
            const float sparkAngle = angle + (Random01(seed + i * 17) - 0.5f) * 1.4f;
            const float speed = 35.0f + Random01(seed + i * 23) * 90.0f;
            AddFlashSpark(flash, previous,
                ImVec2(std::cos(sparkAngle) * speed, std::sin(sparkAngle) * speed),
                0.7f + Random01(seed + i * 29) * 1.2f);
        }
        g_crackFlashes.push_back(std::move(flash));
    }

    void SpawnSquarePulseFlash(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_crackSeed * 101;
        CrackFlash flash = MakeEdgeFlash(seed, center);
        const ImVec2 outward(std::cos(angle), std::sin(angle));
        const ImVec2 tangent(-outward.y, outward.x);
        ImVec2 point(center.x + outward.x * radius, center.y + outward.y * radius);
        flash.anchor = point;
        const int steps = 5 + static_cast<int>(Random01(seed + 2) * 4.0f);
        for (int i = 0; i < steps; ++i)
        {
            const float forward = (11.0f + Random01(seed + i * 13) * 15.0f) * 2.0f;
            const float side = (Random01(seed + i * 17) > 0.5f ? 1.0f : -1.0f) *
                (6.0f + Random01(seed + i * 19) * 14.0f) * 2.0f;
            const ImVec2 corner(point.x + tangent.x * side, point.y + tangent.y * side);
            const ImVec2 next(corner.x + outward.x * forward, corner.y + outward.y * forward);
            const float fade = 1.0f - static_cast<float>(i) / static_cast<float>(steps + 1);
            flash.segments.push_back({ point, corner, fade, 1.1f });
            flash.segments.push_back({ corner, next, fade, 1.1f });
            if (i == 1 || i == steps - 1) AddFlashSpark(flash, corner);
            point = next;
        }
        g_crackFlashes.push_back(std::move(flash));
    }

    void SpawnArrowSurgeFlash(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_crackSeed * 101;
        CrackFlash flash = MakeEdgeFlash(seed, center);
        flash.anchor = ImVec2(center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius);
        const int arrowGroups = 2 + static_cast<int>(Random01(seed + 2) * 2.0f);
        for (int group = 0; group < arrowGroups; ++group)
        {
            const float groupAngle = group == 0 ? angle : Random01(seed + group * 19) * kTwoPi;
            const ImVec2 outward(std::cos(groupAngle), std::sin(groupAngle));
            const ImVec2 tangent(-outward.y, outward.x);
            const ImVec2 start(center.x + outward.x * radius,
                center.y + outward.y * radius);
            const int arrows = 2 + static_cast<int>(Random01(seed + group * 23) * 3.0f);
            for (int i = 0; i < arrows; ++i)
            {
                const float distance = 18.0f + static_cast<float>(i) *
                    (40.0f + Random01(seed + group * 29 + i * 23) * 35.0f);
                const float span = 22.0f + Random01(seed + group * 31 + i * 29) * 28.0f;
                const float depth = 30.0f + Random01(seed + group * 37 + i * 31) * 42.0f;
                const ImVec2 tip(start.x + outward.x * (distance + depth),
                    start.y + outward.y * (distance + depth));
                const ImVec2 left(start.x + outward.x * distance + tangent.x * span,
                    start.y + outward.y * distance + tangent.y * span);
                const ImVec2 right(start.x + outward.x * distance - tangent.x * span,
                    start.y + outward.y * distance - tangent.y * span);
                const float fade = 1.0f - static_cast<float>(i) * 0.18f;
                flash.segments.push_back({ left, tip, fade, 1.45f });
                flash.segments.push_back({ right, tip, fade, 1.45f });
                AddFlashSpark(flash, tip);
            }
        }
        g_crackFlashes.push_back(std::move(flash));
    }

    void SpawnPrismShardFlash(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_crackSeed * 101;
        CrackFlash flash = MakeEdgeFlash(seed, center);
        flash.anchor = ImVec2(center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius);
        const int cubes = 3 + static_cast<int>(Random01(seed + 2) * 3.0f);
        for (int i = 0; i < cubes; ++i)
        {
            const float cubeAngle = i == 0 ? angle : Random01(seed + i * 31) * kTwoPi;
            const ImVec2 outward(std::cos(cubeAngle), std::sin(cubeAngle));
            const float distance = radius + 35.0f + Random01(seed + i * 37) * 270.0f;
            const float size = 14.0f + Random01(seed + i * 41) * 30.0f;
            const ImVec2 pivot(center.x + outward.x * distance,
                center.y + outward.y * distance);
            const ImVec2 offset((Random01(seed + i * 43) - 0.5f) * size * 1.2f,
                (Random01(seed + i * 47) - 0.5f) * size * 1.2f);
            const ImVec2 a(pivot.x - size, pivot.y - size);
            const ImVec2 b(pivot.x + size, pivot.y - size);
            const ImVec2 c(pivot.x + size, pivot.y + size);
            const ImVec2 d(pivot.x - size, pivot.y + size);
            const ImVec2 a2(a.x + offset.x, a.y + offset.y);
            const ImVec2 b2(b.x + offset.x, b.y + offset.y);
            const ImVec2 c2(c.x + offset.x, c.y + offset.y);
            const ImVec2 d2(d.x + offset.x, d.y + offset.y);
            const float fade = 1.0f - static_cast<float>(i) * 0.15f;
            const auto addSquare = [&](const ImVec2& p0, const ImVec2& p1,
                const ImVec2& p2, const ImVec2& p3) {
                flash.segments.push_back({ p0, p1, fade, 1.0f });
                flash.segments.push_back({ p1, p2, fade, 1.0f });
                flash.segments.push_back({ p2, p3, fade, 1.0f });
                flash.segments.push_back({ p3, p0, fade, 1.0f });
            };
            addSquare(a, b, c, d);
            addSquare(a2, b2, c2, d2);
            flash.segments.push_back({ a, a2, fade * 0.72f, 0.8f });
            flash.segments.push_back({ b, b2, fade * 0.72f, 0.8f });
            flash.segments.push_back({ c, c2, fade * 0.72f, 0.8f });
            flash.segments.push_back({ d, d2, fade * 0.72f, 0.8f });
            AddFlashSpark(flash, c2);
        }
        g_crackFlashes.push_back(std::move(flash));
    }

    void SpawnWorldbreakerArcFlash(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_crackSeed * 101;
        CrackFlash flash = MakeEdgeFlash(seed, center);
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        const ImVec2 outward(std::cos(angle), std::sin(angle));
        const ImVec2 tangent(-outward.y, outward.x);
        const ImVec2 start(center.x + outward.x * radius,
            center.y + outward.y * radius);
        flash.anchor = start;
        const float edgeX = outward.x >= 0.0f
            ? (screen.x - start.x) / std::max(outward.x, 0.001f)
            : -start.x / std::min(outward.x, -0.001f);
        const float edgeY = outward.y >= 0.0f
            ? (screen.y - start.y) / std::max(outward.y, 0.001f)
            : -start.y / std::min(outward.y, -0.001f);
        const float reach = std::max(30.0f, std::min(std::abs(edgeX), std::abs(edgeY)));
        const int segments = 11;
        ImVec2 previous = start;
        for (int i = 1; i <= segments; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(segments);
            const float side = i == segments ? 0.0f :
                (Random01(seed + i * 17) - 0.5f) * (56.0f + Random01(seed + i * 23) * 90.0f);
            const ImVec2 next(start.x + outward.x * reach * t + tangent.x * side,
                start.y + outward.y * reach * t + tangent.y * side);
            const float fade = 1.0f - t * 0.38f;
            flash.segments.push_back({ previous, next, fade, i < 3 ? 2.25f : 1.25f });
            if (i > 2 && i < segments && Random01(seed + i * 29) > 0.40f)
                AppendCrackBranch(flash, next, angle +
                    (Random01(seed + i * 31) > 0.5f ? 1.0f : -1.0f) *
                    (0.70f + Random01(seed + i * 37) * 0.65f),
                    55.0f + Random01(seed + i * 41) * 130.0f, seed + i * 43, fade * 0.68f);
            previous = next;
        }
        AddFlashSpark(flash, previous, ImVec2(outward.x * 75.0f, outward.y * 75.0f), 1.8f);
        g_crackFlashes.push_back(std::move(flash));
    }

    void SpawnRicochetLasers(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_specialSeed * 131;
        const int count = 1 + static_cast<int>(Random01(seed + 1) * 2.0f);
        const ImVec2 direction = DirectionFromAngle(angle);
        const ImVec2 tangent(-direction.y, direction.x);
        for (int i = 0; i < count; ++i)
        {
            const float spread = (Random01(seed + i * 17) - 0.5f) * 0.22f;
            const ImVec2 laserDirection = DirectionFromAngle(angle + spread);
            const float tangentOffset = (Random01(seed + i * 23) - 0.5f) * radius * 0.20f;
            BouncingLaser laser{};
            laser.position = Add(Add(center, Scale(direction, radius + 3.0f)),
                Scale(tangent, tangentOffset));
            laser.velocity = Scale(laserDirection,
                640.0f + Random01(seed + i * 29) * 410.0f);
            laser.lifetime = 1.2f + Random01(seed + i * 31) * 1.15f;
            laser.size = 0.75f + Random01(seed + i * 37) * 0.85f;
            laser.maxBounces = 5 + static_cast<int>(Random01(seed + i * 41) * 11.0f);
            g_bouncingLasers.push_back(laser);
        }
        if (g_bouncingLasers.size() > 28)
            g_bouncingLasers.erase(g_bouncingLasers.begin(), g_bouncingLasers.begin() +
                static_cast<std::ptrdiff_t>(g_bouncingLasers.size() - 28));
    }

    void SpawnFallingSparks(const ImVec2& center, float radius)
    {
        const int seed = ++g_specialSeed * 137;
        const int count = 3 + static_cast<int>(Random01(seed + 1) * 5.0f);
        for (int i = 0; i < count; ++i)
        {
            const float angle = Random01(seed + i * 19) * kTwoPi;
            const ImVec2 radial = DirectionFromAngle(angle);
            FallingEdgeSpark spark{};
            spark.position = Add(center, Scale(radial,
                radius * (0.80f + Random01(seed + i * 23) * 0.26f)));
            spark.velocity = ImVec2((Random01(seed + i * 29) - 0.5f) * 78.0f,
                55.0f + Random01(seed + i * 31) * 115.0f);
            spark.lifetime = 0.60f + Random01(seed + i * 37) * 0.85f;
            spark.size = 0.55f + Random01(seed + i * 41) * 1.10f;
            g_fallingEdgeSparks.push_back(spark);
        }
        if (g_fallingEdgeSparks.size() > 150)
            g_fallingEdgeSparks.erase(g_fallingEdgeSparks.begin(), g_fallingEdgeSparks.begin() +
                static_cast<std::ptrdiff_t>(g_fallingEdgeSparks.size() - 150));
    }

    ImVec2 BiasedScreenPosition(int seed, const ImVec2& center, float radius, float angle)
    {
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        // A maior parte nasce no sentido do gesto; o restante preenche a tela
        // para o efeito nunca parecer preso a um único trilho radial.
        if (Random01(seed) < 0.72f)
        {
            const ImVec2 direction = DirectionFromAngle(angle +
                (Random01(seed + 3) - 0.5f) * 1.05f);
            const float reach = radius + 65.0f + Random01(seed + 5) *
                std::max(screen.x, screen.y) * 0.42f;
            return ImVec2(std::clamp(center.x + direction.x * reach, 18.0f, screen.x - 18.0f),
                std::clamp(center.y + direction.y * reach, 18.0f, screen.y - 18.0f));
        }
        return ImVec2(24.0f + Random01(seed + 7) * std::max(1.0f, screen.x - 48.0f),
            24.0f + Random01(seed + 11) * std::max(1.0f, screen.y - 48.0f));
    }

    void SpawnGhostCircle(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_specialSeed * 149;
        GhostCircle circle{};
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        // Em vez de pingos independentes, a maior parte dos anéis nasce de
        // outro anel ainda vivo: uma rede de circuitos em expansão.
        if (!g_ghostCircles.empty() && Random01(seed) < 0.78f)
        {
            const GhostCircle& parent = g_ghostCircles.back();
            const ImVec2 branch = DirectionFromAngle(Random01(seed + 1) * kTwoPi);
            const float distance = 48.0f + Random01(seed + 2) * 176.0f;
            circle.position = ImVec2(
                std::clamp(parent.position.x + branch.x * distance, 28.0f, screen.x - 28.0f),
                std::clamp(parent.position.y + branch.y * distance, 28.0f, screen.y - 28.0f));
            circle.linkTarget = parent.position;
            circle.linked = true;
        }
        else
        {
            circle.position = BiasedScreenPosition(seed, center, radius, angle);
        }
        circle.radius = 13.0f + Random01(seed + 3) * 50.0f;
        circle.lifetime = 0.54f + Random01(seed + 5) * 0.70f;
        g_ghostCircles.push_back(circle);
        if (g_ghostCircles.size() > 32)
            g_ghostCircles.erase(g_ghostCircles.begin(), g_ghostCircles.begin() +
                static_cast<std::ptrdiff_t>(g_ghostCircles.size() - 32));
    }

    void SpawnGeometryTrace(const ImVec2& center, float radius, float angle)
    {
        const int seed = ++g_specialSeed * 157;
        GeometryTrace trace{};
        const ImVec2 pivot = BiasedScreenPosition(seed, center, radius, angle);
        const int sides = 3 + static_cast<int>(Random01(seed + 1) * 6.0f);
        const float shapeRadius = 18.0f + Random01(seed + 2) * 52.0f;
        const float rotation = Random01(seed + 3) * kTwoPi;
        trace.points.reserve(static_cast<std::size_t>(sides + 1));
        for (int i = 0; i < sides; ++i)
        {
            const float a = rotation + kTwoPi * static_cast<float>(i) / static_cast<float>(sides);
            const float jitter = 0.76f + Random01(seed + i * 17) * 0.35f;
            trace.points.push_back(Add(pivot, Scale(DirectionFromAngle(a), shapeRadius * jitter)));
        }
        trace.points.push_back(trace.points.front());
        trace.drawDuration = 0.16f + Random01(seed + 5) * 0.30f;
        trace.lifetime = trace.drawDuration + 0.34f + Random01(seed + 7) * 0.42f;
        g_geometryTraces.push_back(std::move(trace));
        if (g_geometryTraces.size() > 18)
            g_geometryTraces.erase(g_geometryTraces.begin(), g_geometryTraces.begin() +
                static_cast<std::ptrdiff_t>(g_geometryTraces.size() - 18));
    }

    void SpawnSpecialEdgeEffect(int style, const ImVec2& center, float radius, float angle)
    {
        switch (style)
        {
        case 7: SpawnRicochetLasers(center, radius, angle); break;
        case 8: SpawnFallingSparks(center, radius); break;
        case 9: SpawnGhostCircle(center, radius, angle); break;
        case 10: SpawnGeometryTrace(center, radius, angle); break;
        default: break;
        }
    }

    float SpecialSpawnInterval(int style)
    {
        switch (style)
        {
        case 7: return 0.09f;
        case 8: return 0.035f;
        case 9: return 0.12f;
        case 10: return 0.18f;
        default: return kCrackSpawnInterval;
        }
    }

    void SpawnEdgeFlash(int style, const ImVec2& center, float radius, float angle)
    {
        switch (std::clamp(style, 0, 6))
        {
        case 1: SpawnCrackFlash(center, radius, angle); break;
        case 2: SpawnSineWaveFlash(center, radius, angle); break;
        case 3: SpawnSquarePulseFlash(center, radius, angle); break;
        case 4: SpawnArrowSurgeFlash(center, radius, angle); break;
        case 5: SpawnPrismShardFlash(center, radius, angle); break;
        case 6: SpawnWorldbreakerArcFlash(center, radius, angle); break;
        default: break;
        }
    }

    void DrawCrackFlashes(ImDrawList* draw, float alpha, float deltaTime,
        float globalSize)
    {
        const float dt = std::clamp(deltaTime, 0.0f, 0.05f);
        
        for (auto& flash : g_crackFlashes)
        {
            flash.age += dt;
        
            const float life = std::max(0.0f, 1.0f - flash.age / flash.lifetime);
        
            const float flashIn = Smooth(std::min(1.0f, flash.age / 0.022f));
        
            const float intensity = alpha * flashIn * life; // A opacidade das rachaduras
        
            const auto scaled = [&](const ImVec2& point) {
                return ImVec2(flash.anchor.x + (point.x - flash.anchor.x) * globalSize,
                    flash.anchor.y + (point.y - flash.anchor.y) * globalSize);
            };
            for (const auto& segment : flash.segments)
                draw->AddLine(scaled(segment.from), scaled(segment.to),
                    WithAlpha(kRadial, intensity * segment.alpha * 0.6f),
                    segment.thickness * globalSize);
        
            for (const auto& spark : flash.sparks)
            {
                const ImVec2 traveled(spark.position.x + spark.velocity.x * flash.age,
                    spark.position.y + spark.velocity.y * flash.age);
                const ImVec2 position = scaled(traveled);
                draw->AddCircleFilled(position, 3.2f * spark.size * globalSize,
                    WithAlpha(kRadial, intensity * 0.11f), 12);
                draw->AddCircleFilled(position, 0.95f * spark.size * globalSize,
                    WithAlpha(kCore, intensity), 12);
            }
        }
        
        std::erase_if(g_crackFlashes, [](const CrackFlash& flash) {
            return flash.age >= flash.lifetime;
        });
    }

    void DrawSpecialEdgeEffects(ImDrawList* draw, float alpha, float deltaTime,
        float globalSize)
    {
        const float dt = std::clamp(deltaTime, 0.0f, 0.05f);
        const ImVec2 screen = ImGui::GetIO().DisplaySize;

        for (auto& laser : g_bouncingLasers)
        {
            laser.age += dt;
            ImVec2 next = Add(laser.position, Scale(laser.velocity, dt));
            if (next.x < 0.0f || next.x > screen.x)
            {
                next.x = std::clamp(next.x, 0.0f, screen.x);
                laser.velocity.x = -laser.velocity.x;
                ++laser.bounces;
            }
            if (next.y < 0.0f || next.y > screen.y)
            {
                next.y = std::clamp(next.y, 0.0f, screen.y);
                laser.velocity.y = -laser.velocity.y;
                ++laser.bounces;
            }
            laser.position = next;
            const float life = std::max(0.0f, 1.0f - laser.age / laser.lifetime);
            const ImVec2 tail = Add(laser.position,
                Scale(Normalized(laser.velocity), -13.0f * globalSize));
            draw->AddLine(tail, laser.position, WithAlpha(kGold, alpha * life * 0.20f),
                5.0f * laser.size * globalSize);
            draw->AddLine(tail, laser.position, WithAlpha(kCore, alpha * life),
                1.15f * laser.size * globalSize);
            draw->AddCircleFilled(laser.position, 2.5f * laser.size * globalSize,
                WithAlpha(kCore, alpha * life * 0.90f), 10);
        }
        std::erase_if(g_bouncingLasers, [](const BouncingLaser& laser) {
            return laser.age >= laser.lifetime || laser.bounces >= laser.maxBounces;
        });

        for (auto& spark : g_fallingEdgeSparks)
        {
            spark.age += dt;
            spark.velocity.y += 185.0f * dt;
            spark.position = Add(spark.position, Scale(spark.velocity, dt));
            const float life = std::max(0.0f, 1.0f - spark.age / spark.lifetime);
            const ImVec2 tail = Add(spark.position, Scale(Normalized(spark.velocity),
                -7.0f * spark.size * globalSize));
            draw->AddLine(tail, spark.position, WithAlpha(kGold, alpha * life * 0.24f),
                3.0f * spark.size * globalSize);
            draw->AddCircleFilled(spark.position, 1.15f * spark.size * globalSize,
                WithAlpha(kCore, alpha * life), 10);
        }
        std::erase_if(g_fallingEdgeSparks, [&](const FallingEdgeSpark& spark) {
            return spark.age >= spark.lifetime || spark.position.y > screen.y + 18.0f;
        });

        for (auto& circle : g_ghostCircles)
        {
            circle.age += dt;
            const float progress = std::clamp(circle.age / circle.lifetime, 0.0f, 1.0f);
            const float drawProgress = Smooth(std::min(1.0f, progress * 4.6f));
            const float life = 1.0f - progress;
            const float radius = circle.radius * globalSize;
            if (circle.linked)
            {
                const float connection = Smooth(std::min(1.0f, progress * 4.6f));
                const ImVec2 end(circle.linkTarget.x + (circle.position.x - circle.linkTarget.x) * connection,
                    circle.linkTarget.y + (circle.position.y - circle.linkTarget.y) * connection);
                draw->AddLine(circle.linkTarget, end, WithAlpha(kRadial, alpha * life * 0.15f),
                    4.0f * globalSize);
                draw->AddLine(circle.linkTarget, end, WithAlpha(kCore, alpha * life * 0.78f),
                    0.85f * globalSize);
            }
            // O anel tem raio fixo: ele é desenhado a partir de um ponto,
            // completa a volta e só então desaparece, sem parecer uma gota.
            constexpr int segments = 40;
            const int drawnSegments = std::max(1, static_cast<int>(std::ceil(
                static_cast<float>(segments) * drawProgress)));
            for (int i = 0; i < drawnSegments; ++i)
            {
                const float a0 = kTwoPi * static_cast<float>(i) / static_cast<float>(segments) - 1.5708f;
                const float a1 = kTwoPi * static_cast<float>(i + 1) / static_cast<float>(segments) - 1.5708f;
                const ImVec2 from = Add(circle.position, Scale(DirectionFromAngle(a0), radius));
                const ImVec2 to = Add(circle.position, Scale(DirectionFromAngle(a1), radius));
                draw->AddLine(from, to, WithAlpha(kRadial, alpha * life * 0.16f),
                    4.6f * globalSize);
                draw->AddLine(from, to, WithAlpha(kCore, alpha * life * 0.90f),
                    0.95f * globalSize);
            }
            draw->AddCircleFilled(circle.position, 1.4f * globalSize,
                WithAlpha(kCore, alpha * life), 10);
        }
        std::erase_if(g_ghostCircles, [](const GhostCircle& circle) {
            return circle.age >= circle.lifetime;
        });

        for (auto& trace : g_geometryTraces)
        {
            trace.age += dt;
            if (trace.points.size() < 2)
                continue;
            const float progress = std::clamp(trace.age / trace.drawDuration, 0.0f, 1.0f);
            const float fade = std::max(0.0f, 1.0f - trace.age / trace.lifetime);
            const float scaledSegments = progress * static_cast<float>(trace.points.size() - 1);
            const int complete = std::min(static_cast<int>(scaledSegments),
                static_cast<int>(trace.points.size() - 1));
            for (int i = 0; i < complete; ++i)
            {
                draw->AddLine(trace.points[i], trace.points[i + 1],
                    WithAlpha(kGold, alpha * fade * 0.18f), 4.0f * globalSize);
                draw->AddLine(trace.points[i], trace.points[i + 1],
                    WithAlpha(kCore, alpha * fade * 0.80f), 0.9f * globalSize);
            }
            if (complete < static_cast<int>(trace.points.size() - 1))
            {
                const float partial = scaledSegments - static_cast<float>(complete);
                const ImVec2 from = trace.points[complete];
                const ImVec2 to = trace.points[complete + 1];
                const ImVec2 tip(from.x + (to.x - from.x) * partial,
                    from.y + (to.y - from.y) * partial);
                draw->AddLine(from, tip, WithAlpha(kCore, alpha * fade), 1.05f * globalSize);
            }
        }
        std::erase_if(g_geometryTraces, [](const GeometryTrace& trace) {
            return trace.age >= trace.lifetime;
        });
    }

    void EmitReleaseSparks(bool cold)
    {
        if (g_releasePath.empty())
            return;

        constexpr int count = 16;
        g_releaseSparks.reserve(g_releaseSparks.size() + count);
        for (int i = 0; i < count; ++i)
        {
            const float angle = static_cast<float>(i) * 0.392699f +
                static_cast<float>((i * 7) % 5) * 0.08f;
            const float speed = 42.0f + static_cast<float>((i * 17) % 31);
            const ImVec2 direction(std::cos(angle), std::sin(angle));
            g_releaseSparks.push_back({
                g_releaseCenter,
                ImVec2(direction.x * speed, direction.y * speed),
                0.0f, 0.22f + static_cast<float>(i % 4) * 0.045f, cold
            });
        }
    }
}

namespace QuickDrawMagicEffects
{
    void Reset()
    {
        g_releaseSparks.clear();
        g_releasePath.clear();
        g_releaseT = 0.0f;
        g_crackFlashes.clear();
        g_lastCrackSpawnTime = -100.0f;
        g_hasLastCrackAngle = false;
        g_bouncingLasers.clear();
        g_fallingEdgeSparks.clear();
        g_ghostCircles.clear();
        g_geometryTraces.clear();
        g_lastSpecialSpawnTime = -100.0f;
    }

    void ClearEdgeFlashes()
    {
        g_crackFlashes.clear();
        g_lastCrackSpawnTime = -100.0f;
        g_hasLastCrackAngle = false;
        g_bouncingLasers.clear();
        g_fallingEdgeSparks.clear();
        g_ghostCircles.clear();
        g_geometryTraces.clear();
        g_lastSpecialSpawnTime = -100.0f;
    }

    void DrawLive(ImDrawList* draw, const std::vector<ImVec2>& normalizedPoints,
        const ImVec2& center, float radius, const ImVec2& cursor,
        bool drawing, bool gameplay, bool showCracks, int edgeEffectStyle,
        float edgeEffectSize, float edgeEffectOpacity, float alpha, float time)
    {
        if (!draw)
            return;

        const float visibility = std::clamp(alpha, 0.0f, 1.0f);
        if (!showCracks)
        {
            ClearEdgeFlashes();
        }
        else if (drawing)
        {
            const ImVec2 offset(cursor.x - center.x, cursor.y - center.y);
            const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
            const ImVec2 direction = Normalized(offset);
            const float angle = std::atan2(direction.y, direction.x);

            const bool changedDirection = !g_hasLastCrackAngle ||
                std::abs(AngleDelta(angle, g_lastCrackAngle)) > 0.035f;
            if (edgeEffectStyle <= 6 &&
                (changedDirection || time - g_lastCrackSpawnTime >= kCrackSpawnInterval))
            {
                SpawnEdgeFlash(edgeEffectStyle, center, radius, angle);
                g_lastCrackSpawnTime = time;
                g_lastCrackAngle = angle;
                g_hasLastCrackAngle = true;
            }
            else if (edgeEffectStyle >= 7 && edgeEffectStyle < kEdgeEffectStyleCount)
            {
                const float interval = SpecialSpawnInterval(edgeEffectStyle);
                if (changedDirection || time - g_lastSpecialSpawnTime >= interval)
                {
                    SpawnSpecialEdgeEffect(edgeEffectStyle, center, radius, angle);
                    g_lastSpecialSpawnTime = time;
                    g_lastCrackAngle = angle;
                    g_hasLastCrackAngle = true;
                }
            }

            const float edgeProximity = Smooth(
                (distance / std::max(radius, 1.0f) - 0.55f) / 0.45f);
            if (edgeProximity > 0.0f)
                draw->AddCircle(center, radius + 1.5f, WithAlpha(kRadial,
                    visibility * edgeProximity * 0.10f), 96, 2.0f);
        }

        // As geometrias já nascem completas e não se rearranjam: só fazem
        // um flash curto e desaparecem, inclusive após o cursor parar.
        DrawCrackFlashes(draw,
            visibility * std::clamp(edgeEffectOpacity, 0.0f, 100.0f) / 100.0f,
            ImGui::GetIO().DeltaTime,
            std::clamp(edgeEffectSize, 25.0f, 250.0f) / 100.0f);

        DrawSpecialEdgeEffects(draw,
            visibility * std::clamp(edgeEffectOpacity, 0.0f, 100.0f) / 100.0f,
            ImGui::GetIO().DeltaTime,
            std::clamp(edgeEffectSize, 25.0f, 250.0f) / 100.0f);

        // Traço: halo largo, linha dourada e núcleo fino. O traço branco
        // original continua por cima para preservar a leitura já existente.
        if (normalizedPoints.size() > 1)
        {
            // const ImU32 trailColor = gameplay ? kCore : kGold;
            const ImU32 trailColor = kCore;
            std::vector<ImVec2> path;
            
            path.reserve(normalizedPoints.size());
            
            for (const auto& point : normalizedPoints)
                path.push_back(ToScreen(point, center, radius));
            
            draw->AddPolyline(path.data(), static_cast<int>(path.size()),
                WithAlpha(trailColor, visibility * 0.12f), 0, 15.0f);
            draw->AddPolyline(path.data(), static_cast<int>(path.size()),
                WithAlpha(trailColor, visibility * 0.76f), 0, 6.5f);
            draw->AddPolyline(path.data(), static_cast<int>(path.size()),
                WithAlpha(kCore, visibility), 0, 1.45f);
        }

        // As fagulhas e a ponta desaparecem ao soltar. O traço dourado acima
        // permanece junto da linha branca até o Quick Draw terminar.
        if (drawing)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float phase = time * (8.0f + i * 0.7f) + i * 2.1f;
                const float orbit = 5.0f + static_cast<float>(i) * 3.2f;
                
                const ImVec2 spark(cursor.x + std::cos(phase) * orbit,
                    cursor.y + std::sin(phase * 1.3f) * orbit * 0.62f);
                
                //3 bolinhas no cursor do Quick Draw
                //draw->AddCircleFilled(spark, 3.3f, WithAlpha(kGold, visibility * 0.13f), 12);
                //draw->AddCircleFilled(spark, 1.1f, WithAlpha(kCore, visibility), 12);
            }
            draw->AddCircleFilled(cursor, 7.0f, WithAlpha(kGold, visibility * 0.14f), 16);
            draw->AddCircleFilled(cursor, 1.8f, WithAlpha(kCore, visibility), 12);
        }
    }

    void DrawStoredStrokeGlow(ImDrawList* draw,
        const std::vector<ImVec2>& normalizedPoints,
        const ImVec2& center, float radius, float alpha)
    {
        if (!draw || normalizedPoints.size() < 2)
            return;

        std::vector<ImVec2> path;
        path.reserve(normalizedPoints.size());
        for (const auto& point : normalizedPoints)
            path.push_back(ToScreen(point, center, radius));

        const float visibility = std::clamp(alpha, 0.0f, 1.0f);
        draw->AddPolyline(path.data(), static_cast<int>(path.size()),
            WithAlpha(kGold, visibility * 0.13f), 0, 8.0f);
        draw->AddPolyline(path.data(), static_cast<int>(path.size()),
            WithAlpha(kGold, visibility * 0.66f), 0, 3.2f);

        // Pontas afiladas para o brilho parecer um traço de energia, não
        // apenas uma segunda linha grossa atrás do gesto salvo.
        const ImVec2 startDirection = Normalized(ImVec2(
            path[1].x - path[0].x, path[1].y - path[0].y));
        const ImVec2 endDirection = Normalized(ImVec2(
            path.back().x - path[path.size() - 2].x,
            path.back().y - path[path.size() - 2].y));
        const ImVec2 startNormal(-startDirection.y, startDirection.x);
        const ImVec2 endNormal(-endDirection.y, endDirection.x);
        const ImU32 tipColor = WithAlpha(kGold, visibility * 0.66f);
        draw->AddTriangleFilled(
            ImVec2(path.front().x - startDirection.x * 7.0f,
                path.front().y - startDirection.y * 7.0f),
            ImVec2(path.front().x + startNormal.x * 2.4f,
                path.front().y + startNormal.y * 2.4f),
            ImVec2(path.front().x - startNormal.x * 2.4f,
                path.front().y - startNormal.y * 2.4f), tipColor);
        draw->AddTriangleFilled(
            ImVec2(path.back().x + endDirection.x * 7.0f,
                path.back().y + endDirection.y * 7.0f),
            ImVec2(path.back().x + endNormal.x * 2.4f,
                path.back().y + endNormal.y * 2.4f),
            ImVec2(path.back().x - endNormal.x * 2.4f,
                path.back().y - endNormal.y * 2.4f), tipColor);
    }

    void TriggerRelease(const std::vector<ImVec2>& normalizedPoints,
        const ImVec2& center, float radius, bool matched)
    {
        g_releasePath.clear();
        g_releasePath.reserve(normalizedPoints.size());
        for (const auto& point : normalizedPoints)
            g_releasePath.push_back(ToScreen(point, center, radius));
        g_releaseCenter = center;
        g_releaseRadius = radius;
        g_releaseT = 1.0f;
        g_releaseMatched = matched;
        EmitReleaseSparks(!matched);
    }

    void DrawRelease(ImDrawList* draw, float alpha, float deltaTime,
        bool showConfirmationRings)
    {
        if (!draw)
            return;

        const float dt = std::clamp(deltaTime, 0.0f, 0.05f);
        
        const float visibility = std::clamp(alpha, 0.0f, 0.6f);
        
        if (g_releaseT > 0.0f)
        {
            const float t = g_releaseT;
        
            const ImU32 color = g_releaseMatched ? kCold : kCold;      // Cor externa : kGold : ; Cor clara interna: kCold
        
            const float expansion = (1.0f - t) * 228.0f; // Expansão: const float expansion = (1.0f - t) * 28.0f;
        
            if (showConfirmationRings)
            {
                draw->AddCircle(g_releaseCenter, g_releaseRadius + expansion,
                    WithAlpha(color, visibility * t * 0.62f), 96, 1.7f + t * 2.0f);
                draw->AddCircle(g_releaseCenter, g_releaseRadius + expansion * 0.42f,
                    WithAlpha(kCore, visibility * t * 0.18f), 96, 7.0f + t * 5.0f);
            }
        
            if (g_releasePath.size() > 1)
            {
        
                draw->AddPolyline(g_releasePath.data(), static_cast<int>(g_releasePath.size()),
                    WithAlpha(color, visibility * t * 0.24f), 0, 14.0f);
        
                draw->AddPolyline(g_releasePath.data(), static_cast<int>(g_releasePath.size()),
                    WithAlpha(g_releaseMatched ? kCore : kCold, visibility * t), 0, 2.5f);
            }
        
            g_releaseT = std::max(0.0f, g_releaseT - dt / 0.8f); // Duração: g_releaseT - dt / 0.26f
        }

        for (auto& spark : g_releaseSparks)
        {
            spark.age += dt;
            spark.position.x += spark.velocity.x * dt;
            spark.position.y += spark.velocity.y * dt;
            const float life = std::max(0.0f, 1.0f - spark.age / spark.lifetime);
            const ImU32 color = spark.cold ? kCold : kGold;
            draw->AddCircleFilled(spark.position, 3.5f * life,
                WithAlpha(color, visibility * life * 0.20f), 12);
            draw->AddCircleFilled(spark.position, 1.2f * life + 0.3f,
                WithAlpha(spark.cold ? kCore : color, visibility * life), 12);
        }
        std::erase_if(g_releaseSparks, [](const Spark& spark) {
            return spark.age >= spark.lifetime;
        });
    }
}
