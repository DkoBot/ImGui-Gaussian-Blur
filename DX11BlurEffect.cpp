#include "DX11BlurEffect.h"
#include <DirectXMath.h>
#include "Imgui/imgui_internal.h"
#include <memory>
#include <array>
#include "log.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

// HLSL compiled shader bytecode
// In each shader, 128 = the blur amount. You can easily set this up to be configurable on a per-draw basis.
// 修改 BLUR_X_SHADER
static constexpr const char* BLUR_X_SHADER = R"(
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    cbuffer Constants : register(b0) { 
        float pixelSize; 
        float radius;      // 新增：模糊半径
        float padding[2];  // 对齐到 16 字节
    }
 
    float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
        float4 color = 0;
        float total_weight = 0;
        
        // 使用传入的 radius 而不是固定 128
        float r = max(radius, 1.0);  // 至少为 1，避免除零
        float sigma = r / 3.0;       // 标准差约为半径的 1/3
        
        for(float x = -r; x <= r; x += 1.0) {
            // 高斯权重公式
            float weight = exp(-(x * x) / (2.0 * sigma * sigma));
            color += tex.Sample(samp, uv + float2(x * pixelSize, 0)) * weight;
            total_weight += weight;
        }
 
        return color / total_weight;
    }
)";

// 修改 BLUR_Y_SHADER
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
        
        float r = max(radius, 1.0);
        float sigma = r / 3.0;
        
        for(float y = -r; y <= r; y += 1.0) {
            float weight = exp(-(y * y) / (2.0 * sigma * sigma));
            color += tex.Sample(samp, uv + float2(0, y * pixelSize)) * weight;
            total_weight += weight;
        }
 
        return color / total_weight;
    }
)";


bool DX11BlurEffect::CreateShaders() { // (Same as before)
    HRESULT hr = S_OK;
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    // Compile X shader
    hr = D3DCompile(
        BLUR_X_SHADER,
        strlen(BLUR_X_SHADER),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_DEBUG |
        D3DCOMPILE_SKIP_OPTIMIZATION |
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            LOG(LevelError) << "Failed to compile X shader: "
                << (char*)errorBlob->GetBufferPointer();
            errorBlob->Release();
        }
        return false;
    }

    // Create X pixel shader
    hr = device->CreatePixelShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &blurShaderX
    );

    if (shaderBlob) shaderBlob->Release();

    if (FAILED(hr)) {
        LOG(LevelError) << "Failed to create blur X shader: HR: " << hr;
        return false;
    }

    // Compile Y shader
    hr = D3DCompile(
        BLUR_Y_SHADER,
        strlen(BLUR_Y_SHADER),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        D3DCOMPILE_DEBUG |
        D3DCOMPILE_SKIP_OPTIMIZATION |
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &shaderBlob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            LOG(LevelError) << "Failed to compile Y shader: "
                << (char*)errorBlob->GetBufferPointer();
            errorBlob->Release();
        }
        return false;
    }

    // Create Y pixel shader
    hr = device->CreatePixelShader(
        shaderBlob->GetBufferPointer(),
        shaderBlob->GetBufferSize(),
        nullptr,
        &blurShaderY
    );

    if (shaderBlob) shaderBlob->Release();
    if (errorBlob) errorBlob->Release();

    if (FAILED(hr)) {
        LOG(LevelError) << "Failed to create blur Y shader: HR: " << hr;
        return false;
    }

    return true;
}

bool DX11BlurEffect::CreateSamplerState() { // (Same as before)
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD = 0;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    HRESULT hr = device->CreateSamplerState(&samplerDesc, &samplerState);
    if (FAILED(hr)) {
        LOG(LevelError) << "Failed to create sampler state: HR: " << hr;
        return false;
    }

    return true;
}


bool DX11BlurEffect::CreateBlurTextures(int width, int height) {
    auto create_texture_resources = [&](ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv, std::string debug_name) -> bool {
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
        if (FAILED(hr)) { LOG(LevelError) << ("Failed to create blur texture (" + debug_name + "): HR: ").c_str() << hr; return false; }
        hr = device->CreateShaderResourceView(*texture, nullptr, srv);
        if (FAILED(hr)) { LOG(LevelError) << ("Failed to create blur SRV (" + debug_name + "): HR: ").c_str() << hr; return false; }
        hr = device->CreateRenderTargetView(*texture, nullptr, rtv);
        if (FAILED(hr)) { LOG(LevelError) << ("Failed to create blur RTV (" + debug_name + "): HR: ").c_str() << hr; return false; }
        return true;
        };

    if (!create_texture_resources(&blurTextureX, &blurSRVX, &blurRTVX, "X")) return false;
    if (!create_texture_resources(&blurTextureY, &blurSRVY, &blurRTVY, "Y")) return false;

    // Create the original blurTexture as well (though might not be directly used in updated ApplyBlur)
    if (!create_texture_resources(&blurTexture, &blurSRV, &blurRTV, "Original")) return false;


    return true;
}


bool DX11BlurEffect::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx) { // (Updated to call CreateBlurTextures and CreateFullscreenQuadResources)
    if (!dev || !ctx) {
        LOG(LevelError) << "Invalid DirectX device or context provided";
        return false;
    }

    device = dev;
    context = ctx;

    if (!CreateShaders() || !CreateSamplerState() || !CreateBlurTextures(1, 1) || !CreateFullscreenQuadResources()) {
        LOG(LevelError) << "Failed to initialize blur effect";
        return false;
    }

    isInitialized = true;
    return true;
}

void DX11BlurEffect::BeginBlur() { // (Updated BeginBlur to use CreateBlurTextures for resize)
    if (!isInitialized) {
        LOG(LevelError) << "Blur effect not initialized";
        return;
    }

    // Get current render target dimensions
    ID3D11RenderTargetView* currentRTV = nullptr;
    context->OMGetRenderTargets(1, &currentRTV, nullptr);
    if (!currentRTV) {
        LOG(LevelError) << "No active render target";
        return;
    }

    ID3D11Texture2D* currentRT = nullptr;
    currentRTV->GetResource(reinterpret_cast<ID3D11Resource**>(&currentRT));
    if (!currentRT) {
        currentRTV->Release();
        LOG(LevelError) << "Failed to get render target texture";
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    currentRT->GetDesc(&desc);

    if (backbufferWidth != desc.Width || backbufferHeight != desc.Height) {
        if (!CreateBlurTextures(desc.Width, desc.Height)) { // Use CreateBlurTextures (plural) for resizing
            currentRT->Release();
            currentRTV->Release();
            return;
        }
        backbufferWidth = desc.Width;
        backbufferHeight = desc.Height;
    }

    // Store current render target
    context->OMGetRenderTargets(1, &rtBackup, nullptr);

    // Copy current render target to blur texture (original blurTexture)
    context->CopyResource(blurTexture, currentRT);

    currentRT->Release();
    currentRTV->Release();
}

void DX11BlurEffect::ApplyBlur(ImDrawList* drawList, const ImVec2& pos, const ImVec2& size,
    float radius, float rounding, ImDrawFlags flags)
{
    if (!isInitialized) {
        LOG(LevelError) << "Blur effect not initialized";
        return;
    }

    // 修改常量缓冲结构，加入 radius
    struct BlurConstants {
        float pixelSize[4];  // x = pixelSize, y = radius, z = unused, w = unused
    };

    ID3D11Buffer* constantBuffer = nullptr;
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(BlurConstants);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        HRESULT hr = device->CreateBuffer(&desc, nullptr, &constantBuffer);
        if (FAILED(hr)) {
            LOG(LevelError) << "Failed to create constant buffer: HR: " << hr;
            return;
        }
    }

    // ========== Pass 1: 水平模糊 ==========
    ID3D11RenderTargetView* rtvX[] = { blurRTVX };
    context->OMSetRenderTargets(1, rtvX, nullptr);
    context->PSSetShader(blurShaderX, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetSamplers(0, 1, &samplerState);
    context->PSSetShaderResources(0, 1, &blurSRV);

    {
        // 关键修改：传入 radius
        BlurConstants constants = {
            {1.0f / backbufferWidth, radius, 0.0f, 0.0f}
        };
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { /* Handle error */ }
        memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer, 0);
    }

    D3D11_VIEWPORT viewport = {
        0.0f, 0.0f,
        static_cast<float>(backbufferWidth),
        static_cast<float>(backbufferHeight),
        0.0f, 1.0f
    };
    context->RSSetViewports(1, &viewport);
    DrawFullscreenQuad();

    // Unbind
    ID3D11ShaderResourceView* nullSRV[] = { nullptr };
    context->PSSetShaderResources(0, 1, nullSRV);
    ID3D11RenderTargetView* nullRTV[] = { nullptr };
    context->OMSetRenderTargets(1, nullRTV, nullptr);

    // ========== Pass 2: 垂直模糊 ==========
    ID3D11RenderTargetView* rtvY[] = { blurRTVY };
    context->OMSetRenderTargets(1, rtvY, nullptr);
    context->PSSetShader(blurShaderY, nullptr, 0);
    context->PSSetConstantBuffers(0, 1, &constantBuffer);
    context->PSSetSamplers(0, 1, &samplerState);
    context->PSSetShaderResources(0, 1, &blurSRVX);

    {
        // 关键修改：传入 radius
        BlurConstants constants = {
            {1.0f / backbufferHeight, radius, 0.0f, 0.0f}
        };
        D3D11_MAPPED_SUBRESOURCE mapped;
        HRESULT hr = context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr)) { /* Handle error */ }
        memcpy(mapped.pData, &constants, sizeof(constants));
        context->Unmap(constantBuffer, 0);
    }
    context->RSSetViewports(1, &viewport);
    DrawFullscreenQuad();


    // **3. Render the *blurred texture (blurTextureY)* to ImGui**
    context->OMSetRenderTargets(1, &rtBackup, nullptr); // Restore original render target for ImGui
    viewport = { 0.0f, 0.0f, ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y, 0.0f, 1.0f }; // Restore ImGui viewport
    context->RSSetViewports(1, &viewport);

    // Assume screen size of the blurred texture
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;

    // Compute UVs based on the window's position and size
    ImVec2 uv0(pos.x / screenSize.x, pos.y / screenSize.y);
    ImVec2 uv1((pos.x + size.x) / screenSize.x, (pos.y + size.y) / screenSize.y);

    drawList->AddImageRounded(
        reinterpret_cast<ImTextureID>(blurSRVY), // Use blurSRVY (Y texture SRV) for final blurred image!
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        uv0,
        uv1,
        ImColor(ImVec4(1.f, 1.f, 1.f, 1.f)),
        rounding,
        flags
    );


    if (constantBuffer) constantBuffer->Release(); // Consider reusing constant buffer
}

void DX11BlurEffect::EndBlur() { // (Same as before)
    if (!isInitialized) {
        LOG(LevelError) << "Blur effect not initialized";
        return;
    }

    if (rtBackup) {
        context->OMSetRenderTargets(1, &rtBackup, nullptr);
        rtBackup->Release();
        rtBackup = nullptr;
    }

    // Reset shader state
    context->PSSetShader(nullptr, nullptr, 0);
    ID3D11SamplerState* nullSampler = nullptr;
    context->PSSetSamplers(0, 1, &nullSampler);
}


// Definition of FullscreenQuadVertex structure (inside cpp file, no need to be in header if only used here)
struct DX11BlurEffect::FullscreenQuadVertex
{
    float position[2]; // In clip space, so only 2D needed
};


bool DX11BlurEffect::CreateFullscreenQuadResources() { // (Same as provided before)
    // 1. Vertex Buffer
    FullscreenQuadVertex vertices[] = {
        {-1.0f,  1.0f}, // Top-left
        { 1.0f,  1.0f}, // Top-right
        {-1.0f, -1.0f}, // Bottom-left
        { 1.0f, -1.0f}  // Bottom-right
    };

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = 0;
    vbDesc.MiscFlags = 0;
    D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };

    HRESULT hr = device->CreateBuffer(&vbDesc, &vbData, &fullscreenQuadVertexBuffer);
    if (FAILED(hr)) {
        LOG(LevelError) << "Failed to create fullscreen quad vertex buffer: HR: " << hr;
        return false;
    }

    // 2. Input Layout
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    UINT numElements = ARRAYSIZE(layout);

    // Vertex Shader for Fullscreen Quad
    static constexpr const char* FULLSCREEN_QUAD_VS = R"(
        struct VS_OUTPUT
        {
            float4 position : SV_POSITION;
            float2 uv : TEXCOORD0;
        };
 
        VS_OUTPUT main(float2 position : POSITION)
        {
            VS_OUTPUT output;
            output.position = float4(position, 0.0f, 1.0f);
            output.uv = float2((position.x + 1.0f) * 0.5f, 1.0f - (position.y + 1.0f) * 0.5f);
            return output;
        }
    )";


    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlobVS = nullptr;

    hr = D3DCompile(
        FULLSCREEN_QUAD_VS,
        strlen(FULLSCREEN_QUAD_VS),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "vs_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        &vsBlob,
        &errorBlobVS
    );

    if (FAILED(hr)) {
        if (errorBlobVS) {
            LOG(LevelError) << "Failed to compile fullscreen quad VS: " << (char*)errorBlobVS->GetBufferPointer();
            errorBlobVS->Release();
        }
        return false;
    }


    hr = device->CreateVertexShader(
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        nullptr,
        &fullscreenQuadVertexShader
    );
    if (FAILED(hr)) {
        LOG(LevelError) << "Failed to create fullscreen quad vertex shader: HR: " << hr;
        return false;
    }


    hr = device->CreateInputLayout(
        layout,
        numElements,
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &fullscreenQuadInputLayout
    );

    if (vsBlob) vsBlob->Release();
    if (errorBlobVS) errorBlobVS->Release();


    if (FAILED(hr)) {
        LOG(LevelError) << "Failed to create fullscreen quad input layout: HR: " << hr;
        return false;
    }
    return true;
}


void DX11BlurEffect::DrawFullscreenQuad() { // (Same as provided before)
    if (!fullscreenQuadVertexBuffer || !fullscreenQuadVertexShader) {
        LOG(LevelError) << "Fullscreen quad resources not initialized. Call CreateFullscreenQuadResources first.";
        return;
    }

    UINT stride = sizeof(FullscreenQuadVertex);
    UINT offset = 0;

    context->IASetVertexBuffers(0, 1, &fullscreenQuadVertexBuffer, &stride, &offset);
    context->IASetInputLayout(fullscreenQuadInputLayout);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->VSSetShader(fullscreenQuadVertexShader, nullptr, 0);


    context->Draw(4, 0);
}


DX11BlurEffect::~DX11BlurEffect() { // (Updated destructor to release new resources)
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
}