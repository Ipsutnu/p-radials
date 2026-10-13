#include "Printscreen.h"

#include "Logger.h"

#include <atomic>
#include <filesystem>
#include <format>

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>

#pragma comment(lib, "windowscodecs.lib")

namespace Printscreen
{
    namespace
    {
        std::atomic_bool g_pending{ false };

        std::filesystem::path NextScreenshotPath()
        {
            // Skyrim executa a partir da própria pasta raiz. Não usamos a
            // pasta de imagens do Windows para manter o mesmo local e padrão
            // de nome que o screenshot nativo do jogo.
            const auto root = std::filesystem::current_path();
            for (std::uint32_t index = 1; ; ++index)
            {
                const auto candidate = root /
                    std::format("ScreenShot{:02}.png", index);
                std::error_code error;
                if (!std::filesystem::exists(candidate, error))
                    return candidate;
            }
        }

        bool IsWicSupportedFormat(DXGI_FORMAT format,
            WICPixelFormatGUID& pixelFormat)
        {
            switch (format)
            {
            case DXGI_FORMAT_B8G8R8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8X8_UNORM:
            case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                pixelFormat = GUID_WICPixelFormat32bppBGRA;
                return true;
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                pixelFormat = GUID_WICPixelFormat32bppRGBA;
                return true;
            default:
                return false;
            }
        }

        bool WritePng(ID3D11Device* device, ID3D11DeviceContext* context,
            ID3D11Texture2D* source, const std::filesystem::path& path)
        {
            if (!device || !context || !source)
                return false;

            D3D11_TEXTURE2D_DESC sourceDesc{};
            source->GetDesc(&sourceDesc);
            if (sourceDesc.Width == 0 || sourceDesc.Height == 0)
                return false;

            WICPixelFormatGUID sourcePixelFormat{};
            if (!IsWicSupportedFormat(sourceDesc.Format, sourcePixelFormat))
            {
                Logger::GetSingleton().Print(
                    "Printscreen: unsupported texture format {}.",
                    static_cast<unsigned int>(sourceDesc.Format));
                return false;
            }

            Microsoft::WRL::ComPtr<ID3D11Texture2D> resolved;
            ID3D11Texture2D* readableSource = source;
            D3D11_TEXTURE2D_DESC readableDesc = sourceDesc;

            if (sourceDesc.SampleDesc.Count > 1)
            {
                readableDesc.SampleDesc.Count = 1;
                readableDesc.SampleDesc.Quality = 0;
                readableDesc.BindFlags = 0;
                readableDesc.CPUAccessFlags = 0;
                readableDesc.MiscFlags = 0;
                readableDesc.Usage = D3D11_USAGE_DEFAULT;
                if (FAILED(device->CreateTexture2D(&readableDesc, nullptr,
                        resolved.GetAddressOf())))
                    return false;
                context->ResolveSubresource(resolved.Get(), 0, source, 0,
                    sourceDesc.Format);
                readableSource = resolved.Get();
            }

            D3D11_TEXTURE2D_DESC stagingDesc = readableDesc;
            stagingDesc.Usage = D3D11_USAGE_STAGING;
            stagingDesc.BindFlags = 0;
            stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stagingDesc.MiscFlags = 0;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
            if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr,
                    staging.GetAddressOf())))
                return false;

            context->CopyResource(staging.Get(), readableSource);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
                return false;

            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            const bool shouldUninitialize = SUCCEEDED(comResult);
            Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
            HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));

            Microsoft::WRL::ComPtr<IWICStream> stream;
            Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
            Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
            Microsoft::WRL::ComPtr<IPropertyBag2> properties;
            Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
            Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

            if (SUCCEEDED(result))
                result = factory->CreateStream(stream.GetAddressOf());
            if (SUCCEEDED(result))
                result = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
            if (SUCCEEDED(result))
                result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                    encoder.GetAddressOf());
            if (SUCCEEDED(result))
                result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (SUCCEEDED(result))
                result = encoder->CreateNewFrame(frame.GetAddressOf(),
                    properties.GetAddressOf());
            if (SUCCEEDED(result))
                result = frame->Initialize(properties.Get());
            if (SUCCEEDED(result))
                result = frame->SetSize(sourceDesc.Width, sourceDesc.Height);

            WICPixelFormatGUID outputPixelFormat = GUID_WICPixelFormat32bppBGRA;
            if (SUCCEEDED(result))
                result = frame->SetPixelFormat(&outputPixelFormat);
            if (SUCCEEDED(result))
                result = factory->CreateBitmapFromMemory(sourceDesc.Width,
                    sourceDesc.Height, sourcePixelFormat, mapped.RowPitch,
                    mapped.RowPitch * sourceDesc.Height,
                    static_cast<BYTE*>(mapped.pData), bitmap.GetAddressOf());
            if (SUCCEEDED(result))
                result = factory->CreateFormatConverter(converter.GetAddressOf());
            if (SUCCEEDED(result))
                result = converter->Initialize(bitmap.Get(), outputPixelFormat,
                    WICBitmapDitherTypeNone, nullptr, 0.0,
                    WICBitmapPaletteTypeCustom);
            if (SUCCEEDED(result))
                result = frame->WriteSource(converter.Get(), nullptr);
            if (SUCCEEDED(result))
                result = frame->Commit();
            if (SUCCEEDED(result))
                result = encoder->Commit();

            context->Unmap(staging.Get(), 0);
            if (shouldUninitialize)
                CoUninitialize();
            return SUCCEEDED(result);
        }

        void FinishCapture(ID3D11Device* device, ID3D11DeviceContext* context,
            ID3D11Texture2D* texture)
        {
            const auto path = NextScreenshotPath();
            if (WritePng(device, context, texture, path))
            {
                Logger::GetSingleton().Print("Printscreen: saved '{}'.",
                    path.string());
            }
            else
            {
                Logger::GetSingleton().Print("Printscreen: failed to save '{}'.",
                    path.string());
            }
        }
    }

    void Request()
    {
        g_pending.store(true, std::memory_order_release);
    }

    bool HasPendingRequest()
    {
        return g_pending.load(std::memory_order_acquire);
    }

    void CaptureFinalTexture(ID3D11Device* device, ID3D11DeviceContext* context,
        ID3D11Texture2D* texture)
    {
        if (!g_pending.exchange(false, std::memory_order_acq_rel))
            return;
        FinishCapture(device, context, texture);
    }

    void CaptureSceneWithImGui(ID3D11Device* device,
        ID3D11DeviceContext* context, ID3D11Texture2D* scene,
        ImDrawData* drawData)
    {
        if (!g_pending.exchange(false, std::memory_order_acq_rel) || !device ||
            !context || !scene || !drawData)
            return;

        D3D11_TEXTURE2D_DESC sceneDesc{};
        scene->GetDesc(&sceneDesc);
        D3D11_TEXTURE2D_DESC compositeDesc = sceneDesc;
        compositeDesc.Usage = D3D11_USAGE_DEFAULT;
        compositeDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        compositeDesc.CPUAccessFlags = 0;
        compositeDesc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> composite;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> compositeTarget;
        if (FAILED(device->CreateTexture2D(&compositeDesc, nullptr,
                composite.GetAddressOf())) ||
            FAILED(device->CreateRenderTargetView(composite.Get(), nullptr,
                compositeTarget.GetAddressOf())))
        {
            Logger::GetSingleton().Print("Printscreen: unable to create composite target.");
            return;
        }

        context->CopyResource(composite.Get(), scene);

        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
        context->OMGetRenderTargets(1, previousTarget.GetAddressOf(),
            previousDepth.GetAddressOf());
        auto* target = compositeTarget.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        ImGui_ImplDX11_RenderDrawData(drawData);
        auto* oldTarget = previousTarget.Get();
        context->OMSetRenderTargets(1, &oldTarget, previousDepth.Get());

        FinishCapture(device, context, composite.Get());
    }
}
