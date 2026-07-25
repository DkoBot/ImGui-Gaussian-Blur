#pragma once

#include <d3d11.h>
#include <DirectXMath.h>
#include <d3dcompiler.h>
#include "ImGui\imgui_internal.h"

#pragma comment(lib, "d3dcompiler.lib")

namespace DX11BlurEffectNS {
    // ============================================================================
    // Shader Sources
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
        float r = clamp(radius, 0.0, 64.0);
        if (r < 0.5) return float4(tex.Sample(samp, uv).b, tex.Sample(samp, uv).g, tex.Sample(samp, uv).r, tex.Sample(samp, uv).a);

        float sigma = r / 3.0;
        [loop]
        for(float x = -r; x <= r; x += 1.0) {
            float weight = exp(-(x * x) / (2.0 * sigma * sigma));
            float4 s = tex.Sample(samp, uv + float2(x * pixelSize, 0));
            color += float4(s.b, s.g, s.r, s.a) * weight;
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
        if (r < 0.5) return float4(tex.Sample(samp, uv).b, tex.Sample(samp, uv).g, tex.Sample(samp, uv).r, tex.Sample(samp, uv).a);

        float sigma = r / 3.0;
        [loop]
        for(float y = -r; y <= r; y += 1.0) {
            float weight = exp(-(y * y) / (2.0 * sigma * sigma));
            float4 s = tex.Sample(samp, uv + float2(0, y * pixelSize));
            color += float4(s.b, s.g, s.r, s.a) * weight;
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

    struct FullscreenQuadVertex {
        float position[2];
    };

    // ============================================================================
    // BlurEffect Class
    // ============================================================================

    struct BlurEffect {
        BlurEffect() = default;
        ~BlurEffect() { ReleaseResources(); }

        bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx) {
            if (!dev || !ctx) return false;
            device = dev;
            context = ctx;
            if (!CreateShaders() || !CreateSamplerState() || !CreateBlurTextures(1, 1) || !CreateFullscreenQuadResources()) {
                return false;
            }

            D3D11_BUFFER_DESC cbDesc = {};
            cbDesc.ByteWidth = sizeof(float) * 4;
            cbDesc.Usage = D3D11_USAGE_DYNAMIC;
            cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &constantBuffer))) {
                return false;
            }

            isInitialized = true;
            return true;
        }
        bool IsInitialized() const { return isInitialized; }

        void ApplyBlur(
            ImDrawList* drawList,
            const ImVec2& pos,
            const ImVec2& size,
            float radius,
            float rounding,
            ImDrawFlags flags,
            ImU32 tint)
        {
            if (!isInitialized || !blurSRVY) return;

            ImVec2 screenSize = ImGui::GetIO().DisplaySize;
            if (screenSize.x <= 0 || screenSize.y <= 0) return;

            ImVec2 uv0(pos.x / screenSize.x, pos.y / screenSize.y);
            ImVec2 uv1((pos.x + size.x) / screenSize.x, (pos.y + size.y) / screenSize.y);

            drawList->AddImageRounded(
                reinterpret_cast<ImTextureID>(blurSRVY),
                pos,
                ImVec2(pos.x + size.x, pos.y + size.y),
                uv0,
                uv1,
                tint,
                rounding,
                flags
            );
        }

        bool CaptureAndBlur(IDXGISwapChain* pSwapChain, float radius) {
            if (!isInitialized || !pSwapChain || radius < 0.5f) return radius < 0.5f;

            ID3D11Texture2D* backBuffer = nullptr;
            if (FAILED(pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer) {
                return false;
            }

            D3D11_TEXTURE2D_DESC desc;
            backBuffer->GetDesc(&desc);

            if (backbufferWidth != (int)desc.Width || backbufferHeight != (int)desc.Height) {
                ReleaseBlurTextures();
                if (!CreateBlurTextures((int)desc.Width, (int)desc.Height, desc.Format)) {
                    backBuffer->Release();
                    return false;
                }
                backbufferWidth = (int)desc.Width;
                backbufferHeight = (int)desc.Height;
            }

            ID3D11ShaderResourceView* backBufferSRV = nullptr;
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = desc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = 1;
            HRESULT hr = device->CreateShaderResourceView(backBuffer, &srvDesc, &backBufferSRV);
            backBuffer->Release();
            if (FAILED(hr) || !backBufferSRV) {
                return false;
            }

            ExecuteBlurPass(radius, backBufferSRV);
            backBufferSRV->Release();
            return true;
        }

    private:
        // ============================================================================
        // Implementation
        // ============================================================================

        struct BlurConstants {
            float pixelSize;
            float radius;
            float padding[2];
        };

        bool CreateShaders() {
            ID3DBlob* shaderBlob = nullptr;
            ID3DBlob* errorBlob = nullptr;

            HRESULT hr = D3DCompile(BLUR_X_SHADER, strlen(BLUR_X_SHADER), nullptr, nullptr, nullptr,
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
            return !FAILED(hr);
        }

        bool CreateSamplerState() {
            D3D11_SAMPLER_DESC samplerDesc = {};
            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDesc.MinLOD = 0;
            samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
            return !FAILED(device->CreateSamplerState(&samplerDesc, &samplerState));
        }

        bool CreateBlurTextures(int width, int height, DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM) {
            auto create_texture_resources = [&](ID3D11Texture2D** texture, ID3D11ShaderResourceView** srv, ID3D11RenderTargetView** rtv, DXGI_FORMAT useFmt) -> bool {
                if (*texture) (*texture)->Release(); *texture = nullptr;
                if (*srv) (*srv)->Release(); *srv = nullptr;
                if (*rtv) (*rtv)->Release(); *rtv = nullptr;

                D3D11_TEXTURE2D_DESC textureDesc = {};
                textureDesc.Width = width;
                textureDesc.Height = height;
                textureDesc.MipLevels = 1;
                textureDesc.ArraySize = 1;
                textureDesc.Format = useFmt;
                textureDesc.SampleDesc.Count = 1;
                textureDesc.Usage = D3D11_USAGE_DEFAULT;
                textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

                if (FAILED(device->CreateTexture2D(&textureDesc, nullptr, texture))) return false;
                if (FAILED(device->CreateShaderResourceView(*texture, nullptr, srv))) return false;
                if (FAILED(device->CreateRenderTargetView(*texture, nullptr, rtv))) return false;
                return true;
            };

            if (!create_texture_resources(&blurTextureX, &blurSRVX, &blurRTVX, fmt)) return false;
            if (!create_texture_resources(&blurTextureY, &blurSRVY, &blurRTVY, DXGI_FORMAT_R8G8B8A8_UNORM)) return false;
            return true;
        }

        bool CreateFullscreenQuadResources() {
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

            if (FAILED(device->CreateBuffer(&vbDesc, &vbData, &fullscreenQuadVertexBuffer))) return false;

            D3D11_INPUT_ELEMENT_DESC layout[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
            };

            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* errorBlobVS = nullptr;
            HRESULT hr = D3DCompile(FULLSCREEN_QUAD_VS, strlen(FULLSCREEN_QUAD_VS), nullptr, nullptr, nullptr,
                "main", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errorBlobVS);
            if (FAILED(hr)) {
                if (errorBlobVS) errorBlobVS->Release();
                return false;
            }
            if (errorBlobVS) errorBlobVS->Release();

            hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &fullscreenQuadVertexShader);
            if (FAILED(hr)) {
                vsBlob->Release();
                return false;
            }

            hr = device->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &fullscreenQuadInputLayout);
            vsBlob->Release();
            return !FAILED(hr);
        }

        void DrawFullscreenQuad() {
            if (!fullscreenQuadVertexBuffer || !fullscreenQuadVertexShader) return;

            UINT stride = sizeof(FullscreenQuadVertex);
            UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &fullscreenQuadVertexBuffer, &stride, &offset);
            context->IASetInputLayout(fullscreenQuadInputLayout);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
            context->VSSetShader(fullscreenQuadVertexShader, nullptr, 0);
            context->Draw(4, 0);
        }

        void ExecuteBlurPass(float radius, ID3D11ShaderResourceView* inputSRV) {
            if (!constantBuffer || !inputSRV) return;

            ID3D11RenderTargetView* prevRTV = nullptr;
            ID3D11DepthStencilView* prevDSV = nullptr;
            context->OMGetRenderTargets(1, &prevRTV, &prevDSV);

            ID3D11BlendState* prevBlendState = nullptr;
            FLOAT prevBlendFactor[4] = {1,1,1,1};
            UINT prevSampleMask = 0xFFFFFFFF;
            context->OMGetBlendState(&prevBlendState, prevBlendFactor, &prevSampleMask);

            ID3D11DepthStencilState* prevDepthStencilState = nullptr;
            UINT prevStencilRef = 0;
            context->OMGetDepthStencilState(&prevDepthStencilState, &prevStencilRef);

            ID3D11RasterizerState* prevRasterizerState = nullptr;
            context->RSGetState(&prevRasterizerState);

            UINT prevNumScissor = 0;
            D3D11_RECT prevScissorRects[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            context->RSGetScissorRects(&prevNumScissor, nullptr);
            if (prevNumScissor > 0) {
                prevNumScissor = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                context->RSGetScissorRects(&prevNumScissor, prevScissorRects);
            }

            UINT numVP = 0;
            D3D11_VIEWPORT prevVP[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
            context->RSGetViewports(&numVP, nullptr);
            if (numVP > 0) {
                numVP = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                context->RSGetViewports(&numVP, prevVP);
            }

            ID3D11ShaderResourceView* prevSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            context->PSGetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, prevSRV);

            ID3D11SamplerState* prevSampler = nullptr;
            context->PSGetSamplers(0, 1, &prevSampler);

            ID3D11Buffer* prevPSCB[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            context->PSGetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, prevPSCB);

            ID3D11PixelShader* prevPS = nullptr;
            context->PSGetShader(&prevPS, nullptr, nullptr);

            ID3D11VertexShader* prevVS = nullptr;
            context->VSGetShader(&prevVS, nullptr, nullptr);

            ID3D11InputLayout* prevLayout = nullptr;
            context->IAGetInputLayout(&prevLayout);

            ID3D11Buffer* prevVB = nullptr;
            UINT prevVBStride = 0, prevVBOffset = 0;
            context->IAGetVertexBuffers(0, 1, &prevVB, &prevVBStride, &prevVBOffset);

            D3D11_PRIMITIVE_TOPOLOGY prevTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            context->IAGetPrimitiveTopology(&prevTopo);

            D3D11_VIEWPORT viewport = {
                0.0f,
                0.0f,
                static_cast<float>(backbufferWidth),
                static_cast<float>(backbufferHeight),
                0.0f,
                1.0f
            };
            context->RSSetViewports(1, &viewport);

            ID3D11ShaderResourceView* nullSRV[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
            context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);

            ID3D11RenderTargetView* rtvX[1] = { blurRTVX };
            context->OMSetRenderTargets(1, rtvX, nullptr);
            context->PSSetShader(blurShaderX, nullptr, 0);
            context->PSSetConstantBuffers(0, 1, &constantBuffer);
            context->PSSetSamplers(0, 1, &samplerState);

            ID3D11ShaderResourceView* srvArrayX[1] = { inputSRV };
            context->PSSetShaderResources(0, 1, srvArrayX);

            {
                BlurConstants constants = {1.0f / backbufferWidth, radius, 0.0f, 0.0f};
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (!FAILED(context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    memcpy(mapped.pData, &constants, sizeof(constants));
                    context->Unmap(constantBuffer, 0);
                }
            }

            DrawFullscreenQuad();

            context->PSSetShaderResources(0, 1, nullSRV);

            ID3D11RenderTargetView* rtvY[1] = { blurRTVY };
            context->OMSetRenderTargets(1, rtvY, nullptr);
            context->PSSetShader(blurShaderY, nullptr, 0);
            context->PSSetConstantBuffers(0, 1, &constantBuffer);
            context->PSSetSamplers(0, 1, &samplerState);

            ID3D11ShaderResourceView* srvArrayY[1] = { blurSRVX };
            context->PSSetShaderResources(0, 1, srvArrayY);

            {
                BlurConstants constants = {1.0f / backbufferHeight, radius, 0.0f, 0.0f};
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (!FAILED(context->Map(constantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    memcpy(mapped.pData, &constants, sizeof(constants));
                    context->Unmap(constantBuffer, 0);
                }
            }

            DrawFullscreenQuad();

            ID3D11RenderTargetView* nullRTV[1] = { nullptr };
            context->OMSetRenderTargets(1, nullRTV, nullptr);
            ID3D11SamplerState* nullSamplerState = nullptr;
            context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRV);
            context->PSSetSamplers(0, 1, &nullSamplerState);
            ID3D11Buffer* nullCB[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
            context->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, nullCB);
            context->PSSetShader(nullptr, nullptr, 0);
            context->VSSetShader(nullptr, nullptr, 0);

            context->RSSetState(nullptr);
            context->OMSetDepthStencilState(nullptr, 0);

            context->OMSetBlendState(prevBlendState ? prevBlendState : nullptr, prevBlendFactor, prevSampleMask);
            if (prevDepthStencilState) context->OMSetDepthStencilState(prevDepthStencilState, prevStencilRef);
            if (prevRasterizerState) context->RSSetState(prevRasterizerState);

            context->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, prevSRV);
            context->PSSetSamplers(0, 1, &prevSampler);
            context->PSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, prevPSCB);
            context->PSSetShader(prevPS, nullptr, 0);
            context->VSSetShader(prevVS, nullptr, 0);
            context->IASetInputLayout(prevLayout);
            context->IASetPrimitiveTopology(prevTopo);
            if (prevVB) context->IASetVertexBuffers(0, 1, &prevVB, &prevVBStride, &prevVBOffset);
            if (numVP > 0) context->RSSetViewports(numVP, prevVP);
            if (prevNumScissor > 0) context->RSSetScissorRects(prevNumScissor, prevScissorRects);
            context->OMSetRenderTargets(1, &prevRTV, prevDSV);

            if (prevRTV) prevRTV->Release();
            if (prevDSV) prevDSV->Release();
            if (prevBlendState) prevBlendState->Release();
            if (prevDepthStencilState) prevDepthStencilState->Release();
            if (prevRasterizerState) prevRasterizerState->Release();
            for (int i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++) {
                if (prevSRV[i]) prevSRV[i]->Release();
            }
            for (int i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++) {
                if (prevPSCB[i]) prevPSCB[i]->Release();
            }
            if (prevSampler) prevSampler->Release();
            if (prevPS) prevPS->Release();
            if (prevVS) prevVS->Release();
            if (prevLayout) prevLayout->Release();
            if (prevVB) prevVB->Release();
        }

        void ReleaseBlurTextures() {
            if (blurTextureX) { blurTextureX->Release(); blurTextureX = nullptr; }
            if (blurSRVX) { blurSRVX->Release(); blurSRVX = nullptr; }
            if (blurRTVX) { blurRTVX->Release(); blurRTVX = nullptr; }
            if (blurTextureY) { blurTextureY->Release(); blurTextureY = nullptr; }
            if (blurSRVY) { blurSRVY->Release(); blurSRVY = nullptr; }
            if (blurRTVY) { blurRTVY->Release(); blurRTVY = nullptr; }
        }

        void ReleaseResources() {
            if (blurShaderX) { blurShaderX->Release(); blurShaderX = nullptr; }
            if (blurShaderY) { blurShaderY->Release(); blurShaderY = nullptr; }
            if (samplerState) { samplerState->Release(); samplerState = nullptr; }
            ReleaseBlurTextures();
            if (fullscreenQuadVertexBuffer) { fullscreenQuadVertexBuffer->Release(); fullscreenQuadVertexBuffer = nullptr; }
            if (fullscreenQuadVertexShader) { fullscreenQuadVertexShader->Release(); fullscreenQuadVertexShader = nullptr; }
            if (fullscreenQuadInputLayout) { fullscreenQuadInputLayout->Release(); fullscreenQuadInputLayout = nullptr; }
            if (constantBuffer) { constantBuffer->Release(); constantBuffer = nullptr; }
        }

        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;

        ID3D11PixelShader* blurShaderX = nullptr;
        ID3D11PixelShader* blurShaderY = nullptr;
        ID3D11SamplerState* samplerState = nullptr;

        ID3D11Texture2D* blurTextureX = nullptr;
        ID3D11ShaderResourceView* blurSRVX = nullptr;
        ID3D11RenderTargetView* blurRTVX = nullptr;

        ID3D11Texture2D* blurTextureY = nullptr;
        ID3D11ShaderResourceView* blurSRVY = nullptr;
        ID3D11RenderTargetView* blurRTVY = nullptr;

        ID3D11Buffer* fullscreenQuadVertexBuffer = nullptr;
        ID3D11VertexShader* fullscreenQuadVertexShader = nullptr;
        ID3D11InputLayout* fullscreenQuadInputLayout = nullptr;

        ID3D11Buffer* constantBuffer = nullptr;

        int                     backbufferWidth = 0;
        int                     backbufferHeight = 0;
        bool                    isInitialized = false;
    };
} // namespace DX11BlurEffectNS
