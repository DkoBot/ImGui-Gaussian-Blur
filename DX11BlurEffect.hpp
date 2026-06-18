#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include "Imgui/imgui_internal.h"

#pragma comment(lib, "d3dcompiler.lib")

class DX11BlurEffect
{
public:
    DX11BlurEffect() = default;
    ~DX11BlurEffect();

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void BeginBlur();
    void ApplyBlur(ImDrawList* drawList, const ImVec2& pos, const ImVec2& size, float radius, float rounding = 0.f, ImDrawFlags flags = 0);
    void EndBlur();

    // 新增：设置是否捕获 ImGui 内容（而不仅是游戏画面）
    void SetCaptureImGui(bool capture) { captureImGui = capture; }
    // 新增：强制刷新模糊缓存（窗口尺寸变化时调用）
    void InvalidateCache() { cacheValid = false; }

private:
    bool CreateShaders();
    bool CreateSamplerState();
    bool CreateBlurTextures(int width, int height);
    bool CreateFullscreenQuadResources();
    void DrawFullscreenQuad();
    // 新增：执行实际的 2-Pass 模糊，可复用
    void ExecuteBlurPass(float radius);
    // 新增：将当前 RT 内容拷贝到 blurTexture
    void CaptureCurrentRT();

    struct FullscreenQuadVertex;

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    ID3D11PixelShader* blurShaderX = nullptr;
    ID3D11PixelShader* blurShaderY = nullptr;
    ID3D11SamplerState* samplerState = nullptr;

    ID3D11Texture2D* blurTexture = nullptr;
    ID3D11ShaderResourceView* blurSRV = nullptr;
    ID3D11RenderTargetView* blurRTV = nullptr;

    ID3D11Texture2D* blurTextureX = nullptr;
    ID3D11ShaderResourceView* blurSRVX = nullptr;
    ID3D11RenderTargetView* blurRTVX = nullptr;

    ID3D11Texture2D* blurTextureY = nullptr;
    ID3D11ShaderResourceView* blurSRVY = nullptr;
    ID3D11RenderTargetView* blurRTVY = nullptr;

    ID3D11Buffer* fullscreenQuadVertexBuffer = nullptr;
    ID3D11VertexShader* fullscreenQuadVertexShader = nullptr;
    ID3D11InputLayout* fullscreenQuadInputLayout = nullptr;

    // 新增：常量缓冲（复用，避免每帧创建）
    ID3D11Buffer* constantBuffer = nullptr;

    ID3D11RenderTargetView* rtBackup = nullptr;
    int                     backbufferWidth = 0;
    int                     backbufferHeight = 0;
    bool                    isInitialized = false;

    // 新增：缓存机制
    bool                    cacheValid = false;      // 模糊结果是否有效
    float                   cachedRadius = 0.0f;       // 缓存的模糊半径
    bool                    captureImGui = false;      // 是否捕获 ImGui 内容
};

inline DX11BlurEffect blurEffect = DX11BlurEffect();

// ============================================================================
// Implementation
// ============================================================================

static constexpr const char* BLUR_X_SHADER = R"(
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    cbuffer Constants : register(b0) { 
        float pixelSize; 
        float radius;
        float padding[2];
    }
 
    float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
        float4 color = 0;
        float total_weight = 0;
        float r = clamp(radius, 0.0, 64.0);  // 限制最大半径保证性能
        if (r < 0.5) return tex.Sample(samp, uv);  // 半径太小，直接返回原图
        
        float sigma = r / 3.0;
        [loop]
        for(float x = -r; x <= r; x += 1.0) {
            float weight = exp(-(x * x) / (2.0 * sigma * sigma));
            color += tex.Sample(samp, uv + float2(x * pixelSize, 0)) * weight;
            total_weight += weight;
        }
        return color / total_weight;
    }
)";

static constexpr const char* BLUR_Y_SHADER = R"(
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    cbuffer Constants : register(b0) { 
        float pixelSize; 
        float radius;
        float padding[2];
    }
 
    float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
        float4 color = 0;
        float total_weight = 0;
        float r = clamp(radius, 0.0, 64.0);
        if (r < 0.5) return tex.Sample(samp, uv);
        
        float sigma = r / 3.0;
        [loop]
        for(float y = -r; y <= r; y += 1.0) {
            float weight = exp(-(y * y) / (2.0 * sigma * sigma));
            color += tex.Sample(samp, uv + float2(0, y * pixelSize)) * weight;
            total_weight += weight;
        }
        return color / total_weight;
    }
)";

static constexpr const char* FULLSCREEN_QUAD_VS = R"(
    struct VS_OUTPUT {
        float4 position : SV_POSITION;
        float2 uv : TEXCOORD0;
    };
    VS_OUTPUT main(float2 position : POSITION) {
        VS_OUTPUT output;
        output.position = float4(position, 0.0f, 1.0f);
        output.uv = float2((position.x + 1.0f) * 0.5f, 1.0f - (position.y + 1.0f) * 0.5f);
        return output;
    }
)";

struct DX11BlurEffect::FullscreenQuadVertex {
    float position[2];
};

bool DX11BlurEffect::CreateShaders() {
    HRESULT hr = S_OK;
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    hr = D3DCompile(BLUR_X_SHADER, strlen(BLUR_X_SHADER), nullptr, nullptr, nullptr,
        "main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) errorBlob->Release();
        return false;
    }

    hr = device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &blurShaderX);
    shaderBlob->Release();
    if (FAILED(hr)) return false;

    hr = D3DCompile(BLUR_Y_SHADER, strlen(BLUR_Y_SHADER), nullptr, nullptr, nullptr,
        "main", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &shaderBlob, &errorBlob);
    if (FAILED(hr)) {
        if (errorBlob) errorBlob->Release();
        return false;
    }

    hr = device->CreatePixelShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), nullptr, &blurShaderY);
    shaderBlob->Release();
    if (errorBlob) errorBlob->Release();
    if (FAILED(hr)) return false;

    return true;
}

bool DX11BlurEffect::CreateSamplerState() {
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device->CreateSamplerState(&samplerDesc, &samplerState);
    return !FAILED(hr);
}

bool DX11BlurEffect::CreateBlurTextures(int width, int height) {
    auto create_texture_resources = [&](ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv) -> bool {
        if (*texture) (*texture)->Release(); *texture = nullptr;
        if (*srv) (*srv)->Release(); *srv = nullptr;
        if (*rtv) (*rtv)->Release(); *rtv = nullptr;

        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, texture);
        if (FAILED(hr)) return false;
        hr = device->CreateShaderResourceView(*texture, nullptr, srv);
        if (FAILED(hr)) return false;
        hr = device->CreateRenderTargetView(*texture, nullptr, rtv);
        if (FAILED(hr)) return false;
        return true;
        };

    if (!create_texture_resources(&blurTextureX, &blurSRVX, &blurRTVX)) return false;
    if (!create_texture_resources(&blurTextureY, &blurSRVY, &blurRTVY)) return false;
    if (!create_texture_resources(&blurTexture, &blurSRV, &blurRTV)) return false;
    return true;
}

bool DX11BlurEffect::CreateFullscreenQuadResources() {
    FullscreenQuadVertex vertices[] = {
        {-1.0f,  1.0f},
        { 1.0f,  1.0f},
        {-1.0f, -1.0f},
        { 1.0f, -1.0f}
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };

    HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, &fullscreenQuadVertexBuffer);
    if (FAILED(hr)) return false;

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlobVS = nullptr;

    hr = D3DCompile(FULLSCREEN_QUAD_VS, strlen(FULLSCREEN_QUAD_VS), nullptr, nullptr, nullptr,
        "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errorBlobVS);
    if (FAILED(hr)) {
        if (errorBlobVS) errorBlobVS->Release();
        return false;
    }

    hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &fullscreenQuadVertexShader);
    if (FAILED(hr)) {
        vsBlob->Release();
        return false;
    }

    hr = device->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &fullscreenQuadInputLayout);
    vsBlob->Release();
    if (errorBlobVS) errorBlobVS->Release();
    return !FAILED(hr);
}

void DX11BlurEffect::DrawFullscreenQuad() {
    if (!fullscreenQuadVertexBuffer || !fullscreenQuadVertexShader) return;

    UINT stride = sizeof(FullscreenQuadVertex);
    UINT offset = 0;
    context->IASetVertexBuffers(0, 1, &fullscreenQuadVertexBuffer, &stride, &offset);
    context->IASetInputLayout(fullscreenQuadInputLayout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(fullscreenQuadVertexShader, nullptr, 0);
    context->Draw(4, 0);
}

bool DX11BlurEffect::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
    if (!dev || !ctx) return false;
    device = dev;
    context = ctx;
    if (!CreateShaders() || !CreateSamplerState() || !CreateBlurTextures(1, 1) || !CreateFullscreenQuadResources()) {
        return false;
    }

    // 创建复用的常量缓冲
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(float) * 4;  // float4
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    HRESULT hr = device->CreateBuffer(&cbDesc, nullptr, &constantBuffer);
    if (FAILED(hr)) return false;

    isInitialized = true;
    return true;
}

void DX11BlurEffect::CaptureCurrentRT() {
    ID3D11RenderTargetView* currentRTV = nullptr;
    context->OMGetRenderTargets(1, &currentRTV, nullptr);
    if (!currentRTV) return;

    ID3D11Texture2D* currentRT = nullptr;
    currentRTV->GetResource(reinterpret_cast<ID3D11Resource**>(&currentRT));
    if (!currentRT) {
        currentRTV->Release();
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    currentRT->GetDesc(&desc);

    if (backbufferWidth != desc.Width || backbufferHeight != desc.Height) {
        if (!CreateBlurTextures(desc.Width, desc.Height)) {
            currentRT->Release();
            currentRTV->Release();
            return;
        }
        backbufferWidth = desc.Width;
        backbufferHeight = desc.Height;
        cacheValid = false;  // 尺寸变化，缓存失效
    }

    context->CopyResource(blurTexture, currentRT);

    currentRT->Release();
    currentRTV->Release();
}

void DX11BlurEffect::BeginBlur() {
    if (!isInitialized) return;
    context->OMGetRenderTargets(1, &rtBackup, nullptr);
    CaptureCurrentRT();
    cacheValid = false;  // 新帧，缓存失效
}

void DX11BlurEffect::ExecuteBlurPass(float radius) {
    if (!constantBuffer) return;

    struct BlurConstants {
        float pixelSize;
        float radius;
        float padding[2];
    };

    D3D11_VIEWPORT viewport = {
        0.0f, 
        0.0f, 
        static_cast<float>(backbufferWidth), 
        static_cast<float>(backbufferHeight), 
        0.0f, 
        1.0f
    };
    context->RSSetViewports(1, &viewport);

    // Pass 1: Horizontal blur
    ID3D11RenderTargetView* rtvX[] = { blurRTVX };
    context->OMSetRenderTargets(1, rtvX, nullptr);
    context->PSSetShader(blurShaderX, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetSamplers(0, 1, &samplerState);
    context->PSSetShaderResources(0, 1, &blurSRV);

    {
        BlurConstants constants = {1.0f / backbufferWidth, radius, 0.0f, 0.0f};
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (!FAILED(hr)) {
            memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(constantBuffer, 0);
        }
    }

    DrawFullscreenQuad();

    // Unbind - 修复：声明 nullSRV 和 nullRTV
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    context->PSSetShaderResources(0, 1, nullSRV);
    ID3D11RenderTargetView* nullRTV[] = { nullptr };
    context->OMSetRenderTargets(1, nullRTV, nullptr);

    // Pass 2: Vertical blur
    ID3D11RenderTargetView* rtvY[] = { blurRTVY };
    context->OMSetRenderTargets(1, rtvY, nullptr);
    context->PSSetShader(blurShaderY, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetSamplers(0, 1, &samplerState);
    context->PSSetShaderResources(0, 1, &blurSRVX);

    {
        BlurConstants constants = {1.0f / backbufferHeight, radius, 0.0f, 0.0f};
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (!FAILED(hr)) {
            memcpy(mapped.pData, &constants, sizeof(constants));
            context->Unmap(constantBuffer, 0);
        }
    }

    DrawFullscreenQuad();

    // Unbind
    context->PSSetShaderResources(0, 1, nullSRV);
    context->OMSetRenderTargets(1, nullRTV, nullptr);

    cacheValid = true;
    cachedRadius = radius;
}

void DX11BlurEffect::ApplyBlur(ImDrawList* drawList, const ImVec2& pos, const ImVec2& size,
    float radius, float rounding, ImDrawFlags flags) {
    if (!isInitialized) return;

    // 优化：如果缓存有效且半径相同，直接复用结果
    if (!cacheValid || cachedRadius != radius) {
        ExecuteBlurPass(radius);
    }

    // 恢复原始渲染目标
    context->OMSetRenderTargets(1, &rtBackup, nullptr);

    // 修复：声明 viewport 变量
    D3D11_VIEWPORT viewport = {
        0.0f,
        0.0f,
        ImGui::GetIO().DisplaySize.x,
        ImGui::GetIO().DisplaySize.y,
        0.0f,
        1.0f
    };
    context->RSSetViewports(1, &viewport);

    // 计算 UV 坐标
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    ImVec2 uv0(pos.x / screenSize.x, pos.y / screenSize.y);
    ImVec2 uv1((pos.x + size.x) / screenSize.x, (pos.y + size.y) / screenSize.y);

    // 绘制模糊后的纹理
    drawList->AddImageRounded(
        reinterpret_cast<ImTextureID>(blurSRVY),
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        uv0, uv1,
        ImColor(ImVec4(1.f, 1.f, 1.f, 1.f)),
        rounding,
        flags
    );
}
void DX11BlurEffect::EndBlur() {
    if (!isInitialized) return;

    if (rtBackup) {
        context->OMSetRenderTargets(1, &rtBackup, nullptr);
        rtBackup->Release();
        rtBackup = nullptr;
    }

    context->PSSetShader(nullptr, nullptr, 0);
    ID3D11SamplerState* nullSampler = nullptr;
    context->PSSetSamplers(0, 1, &nullSampler);
}

DX11BlurEffect::~DX11BlurEffect() {
    if (blurShaderX) blurShaderX->Release();
    if (blurShaderY) blurShaderY->Release();
    if (samplerState) samplerState->Release();
    if (blurTexture) blurTexture->Release();
    if (blurSRV) blurSRV->Release();
    if (blurRTV) blurRTV->Release();
    if (blurTextureX) blurTextureX->Release();
    if (blurSRVX) blurSRVX->Release();
    if (blurRTVX) blurRTVX->Release();
    if (blurTextureY) blurTextureY->Release();
    if (blurSRVY) blurSRVY->Release();
    if (blurRTVY) blurRTVY->Release();
    if (fullscreenQuadVertexBuffer) fullscreenQuadVertexBuffer->Release();
    if (fullscreenQuadVertexShader) fullscreenQuadVertexShader->Release();
    if (fullscreenQuadInputLayout) fullscreenQuadInputLayout->Release();
    if (constantBuffer) constantBuffer->Release();
}