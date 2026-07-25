# ImGui DX11 Gaussian Blur

[English](README_EN.md) | [简体中文](README.md)

基于 [UnknownCheats 论坛帖子](https://www.unknowncheats.me/forum/4320263-post1.html) 改进，修复了原作者遗留问题，整合为单头文件（`.hpp`）即插即用。

## 效果预览

![窗口模糊](https://github.com/user-attachments/assets/a9b783c0-9c62-4c54-873f-4bc19f03fcc3)
![全屏模糊](https://github.com/user-attachments/assets/8ad2a6ca-7cbf-4765-baff-3e00559460a1)

## 原理

1. `CaptureAndBlur` 调用 `IDXGISwapChain::GetBuffer(0)` 拿到当前 BackBuffer，创建 SRV，然后通过 DX11 像素着色器（2-Pass 分离卷积：水平 + 垂直）执行高斯模糊，结果输出到内部 `blurSRVY` 纹理。
2. `ApplyBlur` 拿到 `blurSRVY` 句柄作为 `ImTextureID`，通过 `ImDrawList::AddImageRounded` 绘制到指定的窗口区域或全屏背景层。

**重要约束**：每帧必须先调用一次 `CaptureAndBlur` 填充模糊源，再调用 `ApplyBlur` 渲染模糊结果。两者共用同一张中间纹理，多次 `ApplyBlur` 不会重复执行模糊 Pass。

## 快速开始

### 1. 引入头文件

```cpp
#include "DX11BlurEffect.hpp"

DX11BlurEffectNS::BlurEffect blurEffect;
```

### 2. 初始化

在 `ImGui_ImplDX11_Init` 之后调用：

```cpp
ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
blurEffect.Initialize(g_pd3dDevice, g_pd3dDeviceContext);
```

### 3. 渲染循环中使用

```cpp
// 1) 渲染你想模糊的画面（背景图、游戏画面等）到 BackBuffer
DrawBackgroundImage();

// 2) 捕获 BackBuffer 并执行 2-Pass 高斯模糊
blurEffect.CaptureAndBlur(g_pSwapChain, 15.0f /* radius */);

ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

// 3) 创建窗口（透明背景，让模糊可见）
ImGui::Begin("Blur Window", nullptr, ImGuiWindowFlags_NoBackground);

// 4) 应用模糊到指定区域
blurEffect.ApplyBlur(
    ImGui::GetWindowDrawList(),
    ImGui::GetWindowPos(),
    ImGui::GetWindowSize(),
    15.0f,                              // radius: 模糊半径（建议 0~64）
    10.0f,                              // rounding: 圆角半径
    ImDrawFlags_RoundCornersAll,        // flags: 圆角标志
    IM_COL32(255, 255, 255, 220)        // tint: 颜色 + 不透明度
);

// 绘制你的控件...
ImGui::Text("This window has Gaussian Blur background!");
ImGui::End();

ImGui::Render();
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
g_pSwapChain->Present(1, 0);
```

## API 参考

| 方法 | 说明 |
|------|------|
| `bool Initialize(ID3D11Device*, ID3D11DeviceContext*)` | 初始化模糊效果（创建 Shader、纹理、采样器、常量缓冲） |
| `bool IsInitialized() const` | 检查是否已成功初始化 |
| `bool CaptureAndBlur(IDXGISwapChain*, float radius)` | 捕获 BackBuffer 并执行 2-Pass 模糊到 `blurSRVY`。`radius < 0.5` 时跳过模糊但返回 `true` |
| `void ApplyBlur(ImDrawList*, ImVec2 pos, ImVec2 size, float radius, float rounding, ImDrawFlags flags, ImU32 tint)` | 把 `blurSRVY` 绘制到指定 DrawList 的指定区域 |

## 注意事项

### 1. BackBuffer 必须可被 Shader 读取

`CaptureAndBlur` 内部对 `backBuffer` 创建 `ID3D11ShaderResourceView`。如果 swapchain 的 `BufferUsage` 不包含 `DXGI_USAGE_SHADER_INPUT`，`CreateShaderResourceView` 会失败，`blurSRVY` 永远为空，看到的会是一片未初始化纹理。

```cpp
DXGI_SWAP_CHAIN_DESC sd = {};
// ...
sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
```

### 2. 窗口背景透明

使用模糊时需添加 `ImGuiWindowFlags_NoBackground`，否则默认黑色背景会覆盖模糊纹理。

### 3. 全屏模糊 vs 窗口模糊

```cpp
// 窗口模式：只模糊窗口区域
blurEffect.ApplyBlur(ImGui::GetWindowDrawList(),
    ImGui::GetWindowPos(), ImGui::GetWindowSize(),
    radius, rounding, ImDrawFlags_RoundCornersAll, IM_COL32(255, 255, 255, 220));

// 全屏模式：模糊整个视口（适合背景遮罩）
blurEffect.ApplyBlur(ImGui::GetBackgroundDrawList(),
    ImGui::GetMainViewport()->Pos, ImGui::GetMainViewport()->Size,
    radius, rounding, ImDrawFlags_RoundCornersAll, IM_COL32(255, 255, 255, 220));
```

### 4. tint 控制不透明度

`ApplyBlur` 的 `tint` 参数直接传给 `AddImageRounded` —— 通过 alpha 通道控制模糊层的透明度。`IM_COL32(255,255,255,255)` 完全不透明，`IM_COL32(255,255,255,180)` 半透明可看到背后原图。

### 5. 性能

`radius` 越大采样次数越多，Shader 内 clamp 到 `0~64`。每帧建议只调用一次 `CaptureAndBlur`，多次 `ApplyBlur` 复用结果。

## 修复的原作者问题

| 问题 | 修复方式 |
|------|---------|
| `radius` 参数未传入 Shader | 常量缓冲增加 `radius` 字段，Shader 动态控制采样范围 |
| 常量缓冲每帧创建释放 | `Initialize` 时创建一次，全程复用 |
| Shader 动态循环性能差 | 限制最大半径 64，添加 `[loop]` 属性 |
| 缓存机制复杂且容易失效 | 简化工作流：每帧 `CaptureAndBlur` 一次，`ApplyBlur` 多次复用 |
| BackBuffer 不可读导致模糊失败 | 示例代码明确要求 `BufferUsage` 含 `DXGI_USAGE_SHADER_INPUT` |

## 文件结构

```
DX11BlurEffect.hpp    # 单头文件实现（包含类声明+实现）
main.cpp              # 示例程序（可选）
girl_backpack_butterflies_1031438_2560x1600.jpg  # 示例背景图
girl_glance_smile_222054_2560x1600.jpg           # 示例背景图
```

## License

基于原帖开源实现改进，遵循原帖分享精神。
