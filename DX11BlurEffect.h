#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include "Imgui/imgui_internal.h" // Assuming ImGui headers are in this relative path

class DX11BlurEffect
{
public:
    DX11BlurEffect() = default;
    ~DX11BlurEffect();

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void BeginBlur();
    void ApplyBlur(ImDrawList* drawList, const ImVec2& pos, const ImVec2& size, float radius, float rounding = 0.f, ImDrawFlags flags = 0);
    void EndBlur();

private:
    bool CreateShaders();
    bool CreateSamplerState();
    bool CreateBlurTextures(int width, int height); // Renamed to CreateBlurTextures (plural)
    bool CreateFullscreenQuadResources();
    void DrawFullscreenQuad();

    struct FullscreenQuadVertex; // Forward declaration

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    ID3D11PixelShader* blurShaderX = nullptr;
    ID3D11PixelShader* blurShaderY = nullptr;
    ID3D11SamplerState* samplerState = nullptr;

    ID3D11Texture2D* blurTexture = nullptr; // Original blurTexture (might not be directly used in updated ApplyBlur, but kept for BeginBlur resource copy)
    ID3D11ShaderResourceView* blurSRV = nullptr;   // Original blurSRV (might be input for horizontal pass)
    ID3D11RenderTargetView* blurRTV = nullptr;   // Original blurRTV (might be intermediate target)

    // Separate textures for two-pass blur:
    ID3D11Texture2D* blurTextureX = nullptr;
    ID3D11ShaderResourceView* blurSRVX = nullptr;
    ID3D11RenderTargetView* blurRTVX = nullptr;

    ID3D11Texture2D* blurTextureY = nullptr;
    ID3D11ShaderResourceView* blurSRVY = nullptr;
    ID3D11RenderTargetView* blurRTVY = nullptr;


    // Fullscreen Quad Resources:
    ID3D11Buffer* fullscreenQuadVertexBuffer = nullptr;
    ID3D11VertexShader* fullscreenQuadVertexShader = nullptr;
    ID3D11InputLayout* fullscreenQuadInputLayout = nullptr;


    ID3D11RenderTargetView* rtBackup = nullptr;
    int                     backbufferWidth = 0;
    int                     backbufferHeight = 0;
    bool                    isInitialized = false;
};
inline DX11BlurEffect blurEffect = DX11BlurEffect();