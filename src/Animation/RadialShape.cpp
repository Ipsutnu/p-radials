#include "PCH.h"
#include "Animation/RadialShape.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kOverflowAngleStep = kPi / 15.0f;
    constexpr float kOverflowBaseOffset = 65.0f;
    constexpr float kOverflowRingStep = 42.0f;

    float RegularPolygonRadius(float radius, int sides, float angle,
        float vertexAngle = -kPi * 0.5f)
    {
        const float halfSector = kPi / static_cast<float>(sides);
        // O raio polar encontra a lateral do polígono. Nos limites do setor
        // ele alcança exatamente o vértice, mantendo as pontas rígidas.
        const float sector = halfSector * 2.0f;
        const float local = std::remainder(angle - (vertexAngle + halfSector), sector);
        return radius * std::cos(halfSector) / std::max(0.05f, std::cos(local));
    }

    float PointedStarRadius(float radius, int points, float innerRatio,
        float angle, float vertexAngle = -kPi * 0.5f)
    {
        const float segmentAngle = kPi / static_cast<float>(points);
        float phase = std::fmod(angle - vertexAngle + kPi * 2.0f, kPi * 2.0f);
        if (phase < 0.0f) phase += kPi * 2.0f;
        const float vertex = phase / segmentAngle;
        const int segment = static_cast<int>(std::floor(vertex));
        const float local = vertex - static_cast<float>(segment);
        const float from = (segment % 2 == 0) ? 1.0f : innerRatio;
        const float to = (segment % 2 == 0) ? innerRatio : 1.0f;
        return radius * (from + (to - from) * local);
    }

    float RayPolygonRadius(const ImVec2* vertices, int count, float radius,
        float angle)
    {
        const ImVec2 ray(std::cos(angle), std::sin(angle));
        float nearest = radius;
        bool found = false;
        for (int i = 0; i < count; ++i)
        {
            const ImVec2 unitA = vertices[i];
            const ImVec2 unitB = vertices[(i + 1) % count];
            const ImVec2 a(unitA.x * radius, unitA.y * radius);
            const ImVec2 edge((unitB.x - unitA.x) * radius,
                (unitB.y - unitA.y) * radius);
            const float denominator = edge.x * ray.y - edge.y * ray.x;
            if (std::abs(denominator) < 0.0001f)
                continue;
            const float segmentT = -(a.x * ray.y - a.y * ray.x) / denominator;
            if (segmentT < -0.0001f || segmentT > 1.0001f)
                continue;
            const ImVec2 intersection(a.x + edge.x * segmentT,
                a.y + edge.y * segmentT);
            const float rayT = intersection.x * ray.x + intersection.y * ray.y;
            if (rayT > 0.0001f && (!found || rayT < nearest))
            {
                nearest = rayT;
                found = true;
            }
        }
        return nearest;
    }

    float SharpStarRadius(float radius, int points, float innerRatio, float angle,
        float vertexAngle = -kPi * 0.5f)
    {
        std::array<ImVec2, 12> vertices{};
        const int count = points * 2;
        for (int i = 0; i < count; ++i)
        {
            const float vertexRadius = (i % 2 == 0) ? 1.0f : innerRatio;
            const float vertexAngleAt = vertexAngle +
                kPi * static_cast<float>(i) / static_cast<float>(points);
            vertices[static_cast<std::size_t>(i)] = ImVec2(
                std::cos(vertexAngleAt) * vertexRadius,
                std::sin(vertexAngleAt) * vertexRadius);
        }
        return RayPolygonRadius(vertices.data(), count, radius, angle);
    }

    float HeartRadius(float radius, float angle)
    {
        // Um coração não é bem representado por uma única função polar.
        // Este contorno Bézier fecha a cava, os dois lóbulos e a ponta em uma
        // silhueta contínua; o raio abaixo apenas encontra essa silhueta.
        static const std::vector<ImVec2> kHeartOutline = [] {
            struct Cubic { ImVec2 p0, p1, p2, p3; };
            constexpr float verticalScale = 0.88f;
            constexpr float verticalOffset = -0.10f;
            // Os controles próximos da cava unem os lóbulos sem a aparência
            // de dois círculos afastados.
            constexpr std::array<Cubic, 4> curves{
                Cubic{ { 0.00f, -0.19f }, { -0.13f, -0.53f },
                    { -0.49f, -0.74f }, { -0.74f, -0.48f } },
                Cubic{ { -0.74f, -0.48f }, { -1.00f, -0.16f },
                    { -0.45f, 0.66f }, { 0.00f, 0.98f } },
                Cubic{ { 0.00f, 0.98f }, { 0.45f, 0.66f },
                    { 1.00f, -0.16f }, { 0.74f, -0.48f } },
                Cubic{ { 0.74f, -0.48f }, { 0.49f, -0.74f },
                    { 0.13f, -0.53f }, { 0.00f, -0.19f } }
            };
            constexpr int samplesPerCurve = 16;
            std::vector<ImVec2> outline;
            outline.reserve(curves.size() * samplesPerCurve);
            for (const Cubic& curve : curves)
            {
                for (int i = 0; i < samplesPerCurve; ++i)
                {
                    const float t = static_cast<float>(i) / samplesPerCurve;
                    const float inverse = 1.0f - t;
                    const float w0 = inverse * inverse * inverse;
                    const float w1 = 3.0f * inverse * inverse * t;
                    const float w2 = 3.0f * inverse * t * t;
                    const float w3 = t * t * t;
                    const float x = curve.p0.x * w0 + curve.p1.x * w1 + curve.p2.x * w2 + curve.p3.x * w3;
                    const float y = curve.p0.y * w0 + curve.p1.y * w1 + curve.p2.y * w2 + curve.p3.y * w3;
                    outline.emplace_back(
                        x, y * verticalScale + verticalOffset);
                }
            }
            return outline;
        }();
        return RayPolygonRadius(kHeartOutline.data(),
            static_cast<int>(kHeartOutline.size()), radius, angle);
    }

    float ShapeRadius(RadialShape::Style style, float radius, float angle)
    {
        switch (style)
        {
        case RadialShape::Style::HarmonicFlower:
            return radius * (1.0f + 0.085f * std::cos(angle * 4.0f));
        case RadialShape::Style::DualOrbit:
            return radius * (1.0f + 0.105f * std::cos(angle * 2.0f));
        case RadialShape::Style::Turbine:
            return radius * (1.0f + 0.075f * std::sin(angle * 3.0f));
        case RadialShape::Style::SpiralGalaxy:
        {
            float phase = std::fmod(angle + kPi * 0.5f + kPi * 2.0f, kPi * 2.0f);
            if (phase < 0.0f) phase += kPi * 2.0f;
            return radius * (0.90f + 0.20f * phase / (kPi * 2.0f));
        }
        case RadialShape::Style::PulsarCrown:
            return radius * (1.0f + 0.13f * std::cos(angle * 7.0f));
        case RadialShape::Style::LiquidDiamond:
        {
            const float c = std::abs(std::cos(angle));
            const float s = std::abs(std::sin(angle));
            const float superellipse = std::pow(
                std::pow(c, 4.0f) + std::pow(s, 4.0f), -0.25f);
            return radius * (0.91f + 0.12f * superellipse);
        }
        case RadialShape::Style::CometTail:
            return radius * (1.0f + 0.12f * std::cos(angle) +
                0.045f * std::sin(angle * 3.0f));
        case RadialShape::Style::RoseEngine:
            return radius * (1.0f + 0.14f * std::sin(angle * 5.0f) +
                0.035f * std::cos(angle * 10.0f));
        case RadialShape::Style::QuantumRipple:
            return radius * (1.0f + 0.075f * std::sin(angle * 6.0f) +
                0.055f * std::cos(angle * 11.0f));
        case RadialShape::Style::Flower:
            // O antigo "Star": mantém o perfil mais macio, agora nomeado
            // corretamente como Flower.
            return PointedStarRadius(radius, 5, 0.56f, angle);
        case RadialShape::Style::FourPointStar:
            return PointedStarRadius(radius, 4, 0.43f, angle);
        case RadialShape::Style::Hexagon:
            return RegularPolygonRadius(radius, 6, angle);
        case RadialShape::Style::Triangle:
            return RegularPolygonRadius(radius, 3, angle);
        case RadialShape::Style::Diamond:
            return RegularPolygonRadius(radius, 4, angle);
        case RadialShape::Style::Star:
            return SharpStarRadius(radius, 5, 0.42f, angle);
        case RadialShape::Style::Heart:
            return HeartRadius(radius, angle);
        case RadialShape::Style::SixPointStar:
            return PointedStarRadius(radius, 6, 0.56f, angle);
        default:
            return radius;
        }
    }

    ImVec2 PositionAt(RadialShape::Style style, const ImVec2& center,
        float radius, float angle)
    {
        const float shapedRadius = ShapeRadius(style, radius, angle);
        return ImVec2(center.x + std::cos(angle) * shapedRadius,
            center.y + std::sin(angle) * shapedRadius);
    }
}

namespace RadialShape
{
    ImVec2 PositionAtAngle(Style style, const ImVec2& center,
        float radius, float angle)
    {
        return PositionAt(style, center, radius, angle);
    }

    ImVec2 PositionAtAngles(Style style, const ImVec2& center,
        float radius, float shapeAngle, float directionAngle)
    {
        const float shapedRadius = ShapeRadius(style, radius, shapeAngle);
        return ImVec2(center.x + std::cos(directionAngle) * shapedRadius,
            center.y + std::sin(directionAngle) * shapedRadius);
    }

    const char* Name(Style style)
    {
        switch (style)
        {
        case Style::HarmonicFlower: return "Harmonic Flower";
        case Style::DualOrbit: return "Dual Orbit";
        case Style::Turbine: return "Turbine";
        case Style::SpiralGalaxy: return "Spiral Galaxy";
        case Style::PulsarCrown: return "Pulsar Crown";
        case Style::LiquidDiamond: return "Liquid Diamond";
        case Style::CometTail: return "Comet Tail";
        case Style::RoseEngine: return "Rose Engine";
        case Style::QuantumRipple: return "Quantum Ripple";
        case Style::Flower: return "Flower";
        case Style::FourPointStar: return "Four-Point Star";
        case Style::Hexagon: return "Hexagon";
        case Style::Triangle: return "Triangle";
        case Style::Diamond: return "Diamond";
        case Style::Star: return "Star";
        case Style::Heart: return "Heart";
        case Style::SixPointStar: return "Six-Point Star";
        default: return "Classic Orbit";
        }
    }

    const char* PersistentName(Style style)
    {
        // "Star" era o nome do formato Flower antes desta expansão. O novo
        // Star recebe uma chave persistente distinta para não trocar a escolha
        // de quem já possuía esse valor no INI.
        if (style == Style::Star)
            return "SharpStar";
        return Name(style);
    }

    Style FromName(std::string_view name)
    {
        if (name == "Star")
            return Style::Flower;
        if (name == "SharpStar")
            return Style::Star;
        for (int i = 0; i < Count(); ++i)
        {
            const auto style = static_cast<Style>(i);
            if (name == PersistentName(style)) return style;
        }
        return Style::ClassicOrbit;
    }

    ImVec2 MainPosition(Style style, const ImVec2& center, float radius,
        int index, int count, bool leftSide)
    {
        const int safeCount = std::max(count, 1);
        float angle = -kPi * 0.5f +
            kPi * 2.0f * static_cast<float>(index) / static_cast<float>(safeCount);
        if (leftSide) angle = kPi - angle;
        return PositionAt(style, center, radius, angle);
    }

    ImVec2 OverflowPosition(Style style, const ImVec2& center, float radius,
        int overflowIndex, bool leftSide)
    {
        overflowIndex = std::max(overflowIndex, 0);
        float angleStep = kOverflowAngleStep;
        float radialAdvance = 0.0f;
        switch (style)
        {
        case Style::HarmonicFlower:
            angleStep = kPi / 16.0f;
            radialAdvance = 1.25f;
            break;
        case Style::DualOrbit:
            angleStep = kPi / 12.0f;
            radialAdvance = (overflowIndex % 2 == 0) ? -8.0f : 8.0f;
            break;
        case Style::Turbine:
            angleStep = kPi / 13.0f;
            radialAdvance = 0.9f * static_cast<float>(overflowIndex % 13);
            break;
        case Style::SpiralGalaxy:
            angleStep = kPi / 10.0f;
            radialAdvance = 3.25f * static_cast<float>(overflowIndex % 20);
            break;
        case Style::PulsarCrown:
            angleStep = kPi / 14.0f;
            radialAdvance = 1.8f * static_cast<float>(overflowIndex % 7);
            break;
        case Style::LiquidDiamond:
            angleStep = kPi / 16.0f;
            radialAdvance = (overflowIndex % 4) * 3.0f;
            break;
        case Style::CometTail:
            angleStep = kPi / 11.0f;
            radialAdvance = 2.4f * static_cast<float>(overflowIndex % 11);
            break;
        case Style::RoseEngine:
            angleStep = kPi / 15.0f;
            radialAdvance = (overflowIndex % 5) * 4.0f;
            break;
        case Style::QuantumRipple:
            angleStep = kPi / 18.0f;
            radialAdvance = 6.0f * std::sin(static_cast<float>(overflowIndex) * 1.7f);
            break;
        case Style::Flower:
            angleStep = kPi / 15.0f;
            radialAdvance = (overflowIndex % 2 == 0) ? 2.0f : 10.0f;
            break;
        case Style::FourPointStar:
        case Style::Star:
        case Style::SixPointStar:
            angleStep = kPi / 16.0f;
            radialAdvance = (overflowIndex % 2 == 0) ? 4.0f : 11.0f;
            break;
        case Style::Hexagon:
        case Style::Triangle:
        case Style::Diamond:
            angleStep = kPi / 14.0f;
            radialAdvance = (overflowIndex % 3) * 4.0f;
            break;
        case Style::Heart:
            angleStep = kPi / 13.0f;
            radialAdvance = 3.0f * static_cast<float>(overflowIndex % 7);
            break;
        default:
            break;
        }
        const int itemsPerRing = std::max(1,
            static_cast<int>(std::floor(kPi * 2.0f / angleStep)));
        const int ring = overflowIndex / itemsPerRing;
        const int indexInRing = overflowIndex % itemsPerRing;
        const float overflowRadius = radius + kOverflowBaseOffset + radialAdvance +
            static_cast<float>(ring) * kOverflowRingStep;
        const float direction = leftSide ? -1.0f : 1.0f;
        const float angle = -kPi * 0.5f +
            direction * static_cast<float>(indexInRing) * angleStep;
        return PositionAt(style, center, overflowRadius, angle);
    }

    std::vector<ImVec2> Outline(Style style, const ImVec2& center,
        float radius, bool leftSide, int samples)
    {
        samples = std::max(samples, 12);
        int sharpSegments = 0;
        switch (style)
        {
        case Style::FourPointStar: sharpSegments = 8; break;
        case Style::Hexagon: sharpSegments = 6; break;
        case Style::Triangle: sharpSegments = 3; break;
        case Style::Diamond: sharpSegments = 4; break;
        case Style::Flower: sharpSegments = 10; break;
        case Style::Star: sharpSegments = 10; break;
        case Style::SixPointStar: sharpSegments = 12; break;
        default: break;
        }
        if (sharpSegments > 0)
            samples = ((samples + sharpSegments - 1) / sharpSegments) * sharpSegments;
        std::vector<ImVec2> points;
        points.reserve(static_cast<std::size_t>(samples));
        for (int i = 0; i < samples; ++i)
        {
            float angle = -kPi * 0.5f +
                kPi * 2.0f * static_cast<float>(i) / static_cast<float>(samples);
            if (leftSide) angle = kPi - angle;
            points.push_back(PositionAt(style, center, radius, angle));
        }
        return points;
    }
}
