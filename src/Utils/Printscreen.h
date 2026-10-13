#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
struct ImDrawData;

// Captura sob demanda a imagem final do Skyrim somada ao ImGui do Wheel.
// A solicitação vem do Input; a cópia e a escrita ocorrem no Present, quando
// a GPU já possui os dois elementos da composição.
namespace Printscreen
{
    void Request();
    [[nodiscard]] bool HasPendingRequest();

    // Para o caminho normal, onde o ImGui já foi desenhado sobre a cena.
    void CaptureFinalTexture(ID3D11Device* device,
        ID3D11DeviceContext* context, ID3D11Texture2D* texture);

    // Para proxies que expõem uma superfície de UI separada. O módulo copia a
    // cena para uma textura temporária e desenha o ImGui nela somente para a
    // captura, sem interferir no caminho de apresentação/frame generation.
    void CaptureSceneWithImGui(ID3D11Device* device,
        ID3D11DeviceContext* context, ID3D11Texture2D* scene,
        ImDrawData* drawData);
}
