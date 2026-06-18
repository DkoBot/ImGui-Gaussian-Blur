# ImGui DX11 Gaussian Blur

[English](README_EN.md) | [简体中文](README.md)

Improved based on a post from the [UnknownCheats forum](https://www.unknowncheats.me/forum/4320263-post1.html). Fixes the issues left by the original author and is packaged into a single-header (`.hpp`) drop-in module.

## Preview

![Window Blur](https://github.com/user-attachments/assets/a9b783c0-9c62-4c54-873f-4bc19f03fcc3)
![Fullscreen Blur](https://github.com/user-attachments/assets/8ad2a6ca-7cbf-4765-baff-3e00559460a1)

## How It Works

Creates a DX11 Gaussian blur shader (2-Pass separable convolution), captures the current BackBuffer contents, applies horizontal + vertical blur to the specified region, and finally renders the result to ImGui via `ImDrawList::AddImageRounded`.

## Quick Start

### 1. Include the Header

```cpp
#include "DX11BlurEffect.hpp"
```

### 2. Initialization

Call after `ImGui_ImplDX11_Init`:

```cpp
ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
blurEffect.Initialize(g_pd3dDevice, g_pd3dDeviceContext);
```

### 3. Usage in the Render Loop

```cpp
// Capture the current BackBuffer
blurEffect.BeginBlur();

ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

// Create a window (transparent background so the blur is visible)
ImGui::Begin("Blur Window", nullptr, ImGuiWindowFlags_NoBackground);

// Apply blur to the window region
blurEffect.ApplyBlur(
    ImGui::GetWindowDrawList(),
    ImGui::GetWindowPos(),
    ImGui::GetWindowSize(),
    15.0f,   // radius: blur radius (0~64)
    10.0f    // rounding: corner radius
);

// Draw your controls...
ImGui::Text("This window has a Gaussian Blur background!");
ImGui::End();

// Restore render state
blurEffect.EndBlur();

ImGui::Render();
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
```

## API Reference

| Method | Description |
|--------|-------------|
| `bool Initialize(ID3D11Device*, ID3D11DeviceContext*)` | Initializes the blur effect (creates shaders, textures, samplers) |
| `void BeginBlur()` | Captures the current BackBuffer into the blur source texture |
| `void ApplyBlur(ImDrawList*, ImVec2 pos, ImVec2 size, float radius, float rounding = 0.f, ImDrawFlags flags = 0)` | Performs 2-Pass Gaussian blur and draws to the specified DrawList |
| `void EndBlur()` | Restores the original render target and clears shader state |
| `void InvalidateCache()` | Forces the blur cache to be invalidated; the next `ApplyBlur` will recompute |
| `void SetCaptureImGui(bool)` | Sets whether to capture ImGui contents (default: only captures the game view) |

## Notes

1. **Caching mechanism**: `ApplyBlur` uses a cache optimization — the same `radius` reuses the previous frame's result. Call `InvalidateCache()` after changing parameters.
   ```cpp
   static float radius = 10.0f;
   if (ImGui::SliderFloat("Blur", &radius, 0.0f, 64.0f)) {
       blurEffect.InvalidateCache();  // Parameter changed, refresh the cache
   }
   ```

2. **Transparent window background**: When using the blur effect, you must add `ImGuiWindowFlags_NoBackground`; otherwise the default black background will cover the blur texture.

3. **Fullscreen blur vs. window blur**:
   ```cpp
   // Window mode: blur only the window region
   blurEffect.ApplyBlur(ImGui::GetWindowDrawList(), winPos, winSize, radius);
   
   // Fullscreen mode: blur the entire viewport (suitable for background masks)
   blurEffect.ApplyBlur(ImGui::GetBackgroundDrawList(), viewportPos, viewportSize, radius);
   ```

4. **Performance**: The larger the `radius`, the more samples are taken. It is recommended to keep it within the range `0~64`. Multiple windows can share the same `BeginBlur/EndBlur` cached result.

## Issues Fixed from the Original Author

| Issue | Fix |
|-------|-----|
| `radius` parameter was not passed to the shader | Added a `radius` field to the constant buffer; the shader dynamically controls the sampling range |
| Only blurred the game view, ignored the ImGui layer | Added `SetCaptureImGui` to control the capture range |
| Repeated 2-Pass blur every frame | Added `cacheValid` + `cachedRadius` caching mechanism |
| Constant buffer was created/destroyed every frame | Created once in `Initialize` and reused throughout the lifetime |
| Poor performance from dynamic loops in the shader | Limited the maximum radius to 64 and added the `[loop]` attribute |

## File Structure

```
DX11BlurEffect.hpp    # Single-header implementation (contains class declaration + implementation)
main.cpp              # Example program (optional)
```

## License

Improved based on the open-source implementation from the original post. Inherits the sharing spirit of the original post.