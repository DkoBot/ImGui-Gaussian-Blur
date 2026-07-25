# ImGui DX11 Gaussian Blur

[English](README_EN.md) | [简体中文](README.md)

Improved based on a post from the [UnknownCheats forum](https://www.unknowncheats.me/forum/4320263-post1.html). Fixes the issues left by the original author and is packaged into a single-header (`.hpp`) drop-in module.

## Preview

![Window Blur](https://github.com/user-attachments/assets/a9b783c0-9c62-4c54-873f-4bc19f03fcc3)
![Fullscreen Blur](https://github.com/user-attachments/assets/8ad2a6ca-7cbf-4765-baff-3e00559460a1)

## How It Works

1. `CaptureAndBlur` calls `IDXGISwapChain::GetBuffer(0)` to obtain the current BackBuffer, creates an SRV, and executes a 2-Pass Gaussian blur via DX11 pixel shaders (separable convolution: horizontal + vertical). The result is written to the internal `blurSRVY` texture.
2. `ApplyBlur` passes the `blurSRVY` handle as `ImTextureID` and draws it to the target region through `ImDrawList::AddImageRounded`.

**Important**: Every frame must call `CaptureAndBlur` once to populate the blur source before calling `ApplyBlur`. The two methods share the same intermediate texture, so multiple `ApplyBlur` calls do not re-run the blur passes.

## Quick Start

### 1. Include the Header

```cpp
#include "DX11BlurEffect.hpp"

DX11BlurEffectNS::BlurEffect blurEffect;
```

### 2. Initialization

Call after `ImGui_ImplDX11_Init`:

```cpp
ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
blurEffect.Initialize(g_pd3dDevice, g_pd3dDeviceContext);
```

### 3. Usage in the Render Loop

```cpp
// 1) Render what you want to blur (background image, game view, etc.) into the BackBuffer
DrawBackgroundImage();

// 2) Capture the BackBuffer and run the 2-Pass Gaussian blur
blurEffect.CaptureAndBlur(g_pSwapChain, 15.0f /* radius */);

ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

// 3) Create a window (transparent background so the blur is visible)
ImGui::Begin("Blur Window", nullptr, ImGuiWindowFlags_NoBackground);

// 4) Apply blur to the target region
blurEffect.ApplyBlur(
    ImGui::GetWindowDrawList(),
    ImGui::GetWindowPos(),
    ImGui::GetWindowSize(),
    15.0f,                              // radius: blur radius (0~64 recommended)
    10.0f,                              // rounding: corner radius
    ImDrawFlags_RoundCornersAll,        // flags: rounded-corner flags
    IM_COL32(255, 255, 255, 220)        // tint: color + opacity
);

// Draw your controls...
ImGui::Text("This window has a Gaussian Blur background!");
ImGui::End();

ImGui::Render();
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
g_pSwapChain->Present(1, 0);
```

## API Reference

| Method | Description |
|--------|-------------|
| `bool Initialize(ID3D11Device*, ID3D11DeviceContext*)` | Initializes the blur effect (creates shaders, textures, sampler, constant buffer) |
| `bool IsInitialized() const` | Checks whether initialization succeeded |
| `bool CaptureAndBlur(IDXGISwapChain*, float radius)` | Captures the BackBuffer and runs the 2-Pass blur into `blurSRVY`. Skips the blur when `radius < 0.5` and returns `true` |
| `void ApplyBlur(ImDrawList*, ImVec2 pos, ImVec2 size, float radius, float rounding, ImDrawFlags flags, ImU32 tint)` | Draws `blurSRVY` to the specified region of the target DrawList |

## Notes

### 1. BackBuffer must be Shader-Readable

`CaptureAndBlur` creates an `ID3D11ShaderResourceView` on the backBuffer. If the swapchain's `BufferUsage` does not include `DXGI_USAGE_SHADER_INPUT`, `CreateShaderResourceView` will fail and `blurSRVY` will stay empty — the result is an uninitialized texture on screen.

```cpp
DXGI_SWAP_CHAIN_DESC sd = {};
// ...
sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
```

### 2. Transparent Window Background

When using blur, add `ImGuiWindowFlags_NoBackground`, otherwise the default black background will cover the blur texture.

### 3. Fullscreen Blur vs. Window Blur

```cpp
// Window mode: blur only the window region
blurEffect.ApplyBlur(ImGui::GetWindowDrawList(),
    ImGui::GetWindowPos(), ImGui::GetWindowSize(),
    radius, rounding, ImDrawFlags_RoundCornersAll, IM_COL32(255, 255, 255, 220));

// Fullscreen mode: blur the entire viewport (suitable for background masks)
blurEffect.ApplyBlur(ImGui::GetBackgroundDrawList(),
    ImGui::GetMainViewport()->Pos, ImGui::GetMainViewport()->Size,
    radius, rounding, ImDrawFlags_RoundCornersAll, IM_COL32(255, 255, 255, 220));
```

### 4. Tint Controls Opacity

`ApplyBlur`'s `tint` argument is passed directly to `AddImageRounded` — the alpha channel controls the blur layer's opacity. `IM_COL32(255,255,255,255)` is fully opaque, `IM_COL32(255,255,255,180)` is semi-transparent and reveals the original image underneath.

### 5. Performance

The larger the `radius`, the more samples are taken. The shader clamps the radius to `0~64`. Call `CaptureAndBlur` once per frame and reuse the result across multiple `ApplyBlur` calls.

## Issues Fixed from the Original Author

| Issue | Fix |
|-------|-----|
| `radius` parameter was not passed to the shader | Added a `radius` field to the constant buffer; the shader dynamically controls the sampling range |
| Constant buffer was created/destroyed every frame | Created once in `Initialize` and reused throughout the lifetime |
| Poor performance from dynamic loops in the shader | Limited the maximum radius to 64 and added the `[loop]` attribute |
| Cache mechanism was complex and prone to invalidation | Simplified workflow: `CaptureAndBlur` once per frame, `ApplyBlur` reuses the result |
| BackBuffer not shader-readable caused the blur to fail | Sample code explicitly requires `BufferUsage` to include `DXGI_USAGE_SHADER_INPUT` |

## File Structure

```
DX11BlurEffect.hpp    # Single-header implementation (class declaration + implementation)
main.cpp              # Example program (optional)
girl_backpack_butterflies_1031438_2560x1600.jpg  # Sample background image
girl_glance_smile_222054_2560x1600.jpg           # Sample background image
```

## License

Improved based on the open-source implementation from the original post. Inherits the sharing spirit of the original post.
