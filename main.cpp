#include "head.hpp"
#include "DX11BlurEffect.hpp"
#include <d3dcompiler.h>

// 全局变量
HWND g_hWnd = nullptr;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pMainRTV = nullptr;
ID3D11Texture2D* g_pBackgroundTexture = nullptr;
ID3D11ShaderResourceView* g_pBackgroundSRV = nullptr;

// 新增：第二张背景图资源
ID3D11Texture2D* g_pBackgroundTexture2 = nullptr;
ID3D11ShaderResourceView* g_pBackgroundSRV2 = nullptr;

// 新增：当前使用的背景图索引
static int g_CurrentBgIndex = 0;  // 0 = 第一张, 1 = 第二张

// 背景渲染用的 Shader 资源
ID3D11VertexShader* g_pBgVS = nullptr;
ID3D11PixelShader* g_pBgPS = nullptr;
ID3D11InputLayout* g_pBgLayout = nullptr;
ID3D11Buffer* g_pBgVB = nullptr;
ID3D11SamplerState* g_pBgSampler = nullptr;

int g_Width = 1280;
int g_Height = 800;

// 前向声明
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
bool LoadBackgroundImage(const wchar_t* filename, ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV);
bool CreateBackgroundShader();
void DrawBackgroundImage();
void CleanupBackgroundShader();
void RenderFrame();

int main(int argc, char** argv)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L,
                      GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                      L"DX11 ImGui Window", nullptr };
    ::RegisterClassEx(&wc);
    g_hWnd = ::CreateWindowEx(0, wc.lpszClassName, L"DX11 + ImGui Background Demo",
        WS_OVERLAPPEDWINDOW, 100, 100, g_Width, g_Height,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!CreateDeviceD3D(g_hWnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    ::ShowWindow(g_hWnd, SW_SHOWDEFAULT);
    ::UpdateWindow(g_hWnd);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.Fonts->Clear();
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hWnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    if (!blurEffect.Initialize(g_pd3dDevice, g_pd3dDeviceContext))
    {
        MessageBoxW(g_hWnd, L"Failed to initialize blur effect!", L"Error", MB_OK | MB_ICONERROR);
    }

    // 加载两张背景图片
    if (!LoadBackgroundImage(L"girl_backpack_butterflies_1031438_2560x1600.jpg", &g_pBackgroundTexture, &g_pBackgroundSRV))
    {
        MessageBoxW(g_hWnd, L"Failed to load background image 1!", L"Error", MB_OK | MB_ICONERROR);
    }
    if (!LoadBackgroundImage(L"girl_glance_smile_222054_2560x1600.jpg", &g_pBackgroundTexture2, &g_pBackgroundSRV2))
    {
        MessageBoxW(g_hWnd, L"Failed to load background image 2!", L"Error", MB_OK | MB_ICONERROR);
    }

    if (!CreateBackgroundShader())
    {
        MessageBoxW(g_hWnd, L"Failed to create background shader!", L"Error", MB_OK | MB_ICONERROR);
    }

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        RenderFrame();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupBackgroundShader();
    CleanupDeviceD3D();
    ::DestroyWindow(g_hWnd);
    ::UnregisterClass(wc.lpszClassName, wc.hInstance);

    return 0;
}

// 背景图片渲染用的 Shader
static constexpr const char* BG_VERTEX_SHADER = R"(
    struct VS_INPUT {
        float2 Pos : POSITION;
        float2 Tex : TEXCOORD0;
    };
    struct PS_INPUT {
        float4 Pos : SV_POSITION;
        float2 Tex : TEXCOORD0;
    };
    PS_INPUT main(VS_INPUT input) {
        PS_INPUT output;
        output.Pos = float4(input.Pos, 0.0, 1.0);
        output.Tex = input.Tex;
        return output;
    }
)";

static constexpr const char* BG_PIXEL_SHADER = R"(
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    struct PS_INPUT {
        float4 Pos : SV_POSITION;
        float2 Tex : TEXCOORD0;
    };
    float4 main(PS_INPUT input) : SV_TARGET {
        return tex.Sample(samp, input.Tex);
    }
)";

bool CreateBackgroundShader()
{
    HRESULT hr;
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    hr = D3DCompile(BG_VERTEX_SHADER, strlen(BG_VERTEX_SHADER), nullptr,
        nullptr, nullptr, "main", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &vsBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob) { errorBlob->Release(); }
        return false;
    }

    hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), nullptr, &g_pBgVS);
    if (FAILED(hr)) { vsBlob->Release(); return false; }
    hr = D3DCompile(BG_PIXEL_SHADER, strlen(BG_PIXEL_SHADER), nullptr,
        nullptr, nullptr, "main", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0, &psBlob, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob) { errorBlob->Release(); }
        vsBlob->Release();
        return false;
    }
    hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(),
        psBlob->GetBufferSize(), nullptr, &g_pBgPS);
    psBlob->Release();
    if (FAILED(hr)) { vsBlob->Release(); return false; }
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_pd3dDevice->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(), &g_pBgLayout);
    vsBlob->Release();
    if (FAILED(hr)) return false;
    struct BgVertex { float x, y, u, v; };
    BgVertex vertices[] = {
        { -1.0f,  1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f },
        {  1.0f, -1.0f, 1.0f, 1.0f },
        { -1.0f, -1.0f, 0.0f, 1.0f },
    };
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DEFAULT;
    vbDesc.ByteWidth = sizeof(vertices);
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { vertices, 0, 0 };
    hr = g_pd3dDevice->CreateBuffer(&vbDesc, &vbData, &g_pBgVB);
    if (FAILED(hr)) return false;
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    hr = g_pd3dDevice->CreateSamplerState(&sampDesc, &g_pBgSampler);
    if (FAILED(hr)) return false;
    return true;
}

void DrawBackgroundImage()
{
    // 根据当前索引选择背景图
    ID3D11ShaderResourceView* currentSRV = (g_CurrentBgIndex == 0) ? g_pBackgroundSRV : g_pBackgroundSRV2;
    if (!currentSRV || !g_pBgVS) return;

    ID3D11ShaderResourceView* oldSRVs[1] = { nullptr };
    ID3D11SamplerState* oldSamps[1] = { nullptr };
    ID3D11VertexShader* oldVS = nullptr;
    ID3D11PixelShader* oldPS = nullptr;
    ID3D11InputLayout* oldLayout = nullptr;
    ID3D11Buffer* oldVB = nullptr;
    UINT oldStride = 0, oldOffset = 0;
    D3D11_PRIMITIVE_TOPOLOGY oldTopology;
    g_pd3dDeviceContext->VSGetShader(&oldVS, nullptr, nullptr);
    g_pd3dDeviceContext->PSGetShader(&oldPS, nullptr, nullptr);
    g_pd3dDeviceContext->IAGetInputLayout(&oldLayout);
    g_pd3dDeviceContext->IAGetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOffset);
    g_pd3dDeviceContext->IAGetPrimitiveTopology(&oldTopology);
    g_pd3dDeviceContext->PSGetShaderResources(0, 1, oldSRVs);
    g_pd3dDeviceContext->PSGetSamplers(0, 1, oldSamps);
    g_pd3dDeviceContext->VSSetShader(g_pBgVS, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(g_pBgPS, nullptr, 0);
    g_pd3dDeviceContext->IASetInputLayout(g_pBgLayout);
    UINT stride = sizeof(float) * 4;
    UINT offset = 0;
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &g_pBgVB, &stride, &offset);
    g_pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_pd3dDeviceContext->PSSetShaderResources(0, 1, &currentSRV);
    g_pd3dDeviceContext->PSSetSamplers(0, 1, &g_pBgSampler);
    g_pd3dDeviceContext->Draw(6, 0);
    g_pd3dDeviceContext->VSSetShader(oldVS, nullptr, 0);
    g_pd3dDeviceContext->PSSetShader(oldPS, nullptr, 0);
    g_pd3dDeviceContext->IASetInputLayout(oldLayout);
    g_pd3dDeviceContext->IASetVertexBuffers(0, 1, &oldVB, &oldStride, &oldOffset);
    g_pd3dDeviceContext->IASetPrimitiveTopology(oldTopology);
    g_pd3dDeviceContext->PSSetShaderResources(0, 1, oldSRVs);
    g_pd3dDeviceContext->PSSetSamplers(0, 1, oldSamps);
    if (oldVS) oldVS->Release();
    if (oldPS) oldPS->Release();
    if (oldLayout) oldLayout->Release();
    if (oldVB) oldVB->Release();
    if (oldSRVs[0]) oldSRVs[0]->Release();
    if (oldSamps[0]) oldSamps[0]->Release();
}

void CleanupBackgroundShader()
{
    if (g_pBgVS) { g_pBgVS->Release(); g_pBgVS = nullptr; }
    if (g_pBgPS) { g_pBgPS->Release(); g_pBgPS = nullptr; }
    if (g_pBgLayout) { g_pBgLayout->Release(); g_pBgLayout = nullptr; }
    if (g_pBgVB) { g_pBgVB->Release(); g_pBgVB = nullptr; }
    if (g_pBgSampler) { g_pBgSampler->Release(); g_pBgSampler = nullptr; }
}

void RenderFrame()
{
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRTV, nullptr);
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g_pd3dDeviceContext->ClearRenderTargetView(g_pMainRTV, clearColor);
    DrawBackgroundImage();
    blurEffect.BeginBlur();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2((g_Width - 500) / 2.0f, (g_Height - 400) / 2.0f), ImGuiCond_FirstUseEver);

    // 恢复默认窗口背景（不透明黑色）
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.06f, 0.94f)); // ImGui 默认深色

    ImGui::Begin("Hello ImGui", nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground);  // 先禁用背景，手动控制

    static bool is = false;
    static float sliderValue = 50.0f;
    static float roundingValue = 10.0f;

    if (is) {
        blurEffect.ApplyBlur(ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), ImGui::GetWindowSize(), sliderValue, roundingValue);
    }
    else {
        blurEffect.ApplyBlur(ImGui::GetBackgroundDrawList(), ImGui::GetMainViewport()->Pos, ImGui::GetMainViewport()->Size, sliderValue, roundingValue);
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec4 defaultBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        ImU32 bgColor = ImColor(defaultBg);
        drawList->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),bgColor, roundingValue);
    }

    ImGui::Text("Current Radius: %.1f", sliderValue);
    ImGui::Separator();
    ImGui::Text("This is an ImGui window with Gaussian Blur");
    ImGui::Separator();
    ImGui::Text("Window Size: %d x %d", g_Width, g_Height);

    if (ImGui::Checkbox(u8"Switch", &is)) {
        blurEffect.InvalidateCache();
    }
    if (ImGui::SliderFloat("Blur Radius", &sliderValue, 0.0f, 100.0f)) {
        blurEffect.InvalidateCache();
    }
    if (ImGui::SliderFloat("Blur Rounding", &roundingValue, 0.0f, 365.0f)) {
        blurEffect.InvalidateCache();
    }
    if (ImGui::Button("Reset Radius")) {
        sliderValue = 10.0f;
        blurEffect.InvalidateCache();
    }
    ImGui::SameLine();
    if (ImGui::Button("Max Radius")) {
        sliderValue = 64.0f;
        blurEffect.InvalidateCache();
    }

    // 背景图片切换按钮
    ImGui::Separator();
    ImGui::Text("Background Image:");
    if (ImGui::Button("Image 1: Backpack")) {
        g_CurrentBgIndex = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Image 2: Smile")) {
        g_CurrentBgIndex = 1;
    }
    ImGui::Text("Current: %s", g_CurrentBgIndex == 0 ? "Backpack" : "Smile");

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

    ImGui::End();
    ImGui::PopStyleColor();  // 恢复窗口背景色

    blurEffect.EndBlur();
    ImGui::Render();
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_pMainRTV, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
}
bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = g_Width;
    sd.BufferDesc.Height = g_Height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[1] = { D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevelArray,
        1,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );

    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pMainRTV);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_pMainRTV) { g_pMainRTV->Release(); g_pMainRTV = nullptr; }
}

void CleanupDeviceD3D()
{
    if (g_pBackgroundSRV) { g_pBackgroundSRV->Release(); g_pBackgroundSRV = nullptr; }
    if (g_pBackgroundTexture) { g_pBackgroundTexture->Release(); g_pBackgroundTexture = nullptr; }
    if (g_pBackgroundSRV2) { g_pBackgroundSRV2->Release(); g_pBackgroundSRV2 = nullptr; }
    if (g_pBackgroundTexture2) { g_pBackgroundTexture2->Release(); g_pBackgroundTexture2 = nullptr; }

    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

// 修改：支持加载到指定资源
bool LoadBackgroundImage(const wchar_t* filename, ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV)
{
    IWICImagingFactory* pWICFactory = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICFormatConverter* pConverter = nullptr;
    HRESULT hr;

    hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        return false;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pWICFactory));
    if (FAILED(hr))
    {
        CoUninitialize();
        return false;
    }

    hr = pWICFactory->CreateDecoderFromFilename(
        filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &pDecoder);
    if (FAILED(hr))
    {
        pWICFactory->Release();
        CoUninitialize();
        return false;
    }

    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr))
    {
        pDecoder->Release();
        pWICFactory->Release();
        CoUninitialize();
        return false;
    }

    hr = pWICFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr))
    {
        pFrame->Release();
        pDecoder->Release();
        pWICFactory->Release();
        CoUninitialize();
        return false;
    }

    hr = pConverter->Initialize(
        pFrame,
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom
    );
    if (FAILED(hr))
    {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pWICFactory->Release();
        CoUninitialize();
        return false;
    }

    UINT imgWidth = 0, imgHeight = 0;
    pConverter->GetSize(&imgWidth, &imgHeight);

    UINT stride = imgWidth * 4;
    UINT bufferSize = stride * imgHeight;
    std::vector<BYTE> pixels(bufferSize);
    hr = pConverter->CopyPixels(nullptr, stride, bufferSize, pixels.data());
    if (FAILED(hr))
    {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pWICFactory->Release();
        CoUninitialize();
        return false;
    }

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = imgWidth;
    texDesc.Height = imgHeight;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels.data();
    initData.SysMemPitch = stride;

    hr = g_pd3dDevice->CreateTexture2D(&texDesc, &initData, outTexture);
    if (FAILED(hr))
    {
        pConverter->Release();
        pFrame->Release();
        pDecoder->Release();
        pWICFactory->Release();
        CoUninitialize();
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    hr = g_pd3dDevice->CreateShaderResourceView(*outTexture, &srvDesc, outSRV);

    pConverter->Release();
    pFrame->Release();
    pDecoder->Release();
    pWICFactory->Release();
    CoUninitialize();

    return SUCCEEDED(hr);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            g_Width = LOWORD(lParam);
            g_Height = HIWORD(lParam);
            CreateRenderTarget();
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;

    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProc(hWnd, msg, wParam, lParam);
}