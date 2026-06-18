# ImGui DX11 Gaussian Blur

[English](README_EN.md) | [简体中文](README.md)

基于 [UnknownCheats 论坛帖子](https://www.unknowncheats.me/forum/4320263-post1.html) 改进，修复了原作者遗留问题，整合为单头文件（`.hpp`）即插即用。

## 效果预览

![窗口模糊](https://github.com/user-attachments/assets/a9b783c0-9c62-4c54-873f-4bc19f03fcc3)
![全屏模糊](https://github.com/user-attachments/assets/8ad2a6ca-7cbf-4765-baff-3e00559460a1)

## 原理

创建 DX11 高斯模糊着色器（2-Pass 分离卷积），捕获当前 BackBuffer 内容，对指定区域执行水平+垂直模糊，最终通过 `ImDrawList::AddImageRounded` 渲染到 ImGui。

## 快速开始

### 1. 引入头文件

```cpp
#include "DX11BlurEffect.hpp"
```

### 2. 初始化

在 `ImGui_ImplDX11_Init` 之后调用：

```cpp
ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
blurEffect.Initialize(g_pd3dDevice, g_pd3dDeviceContext);
```

### 3. 渲染循环中使用

```cpp
// 捕获当前 BackBuffer
blurEffect.BeginBlur();

ImGui_ImplDX11_NewFrame();
ImGui_ImplWin32_NewFrame();
ImGui::NewFrame();

// 创建窗口（透明背景，让模糊可见）
ImGui::Begin("Blur Window", nullptr, ImGuiWindowFlags_NoBackground);

// 应用模糊到窗口区域
blurEffect.ApplyBlur(
    ImGui::GetWindowDrawList(),
    ImGui::GetWindowPos(),
    ImGui::GetWindowSize(),
    15.0f,   // radius: 模糊半径 (0~64)
    10.0f    // rounding: 圆角半径
);

// 绘制你的控件...
ImGui::Text("This window has Gaussian Blur background!");
ImGui::End();

// 恢复渲染状态
blurEffect.EndBlur();

ImGui::Render();
ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
```

## API 参考

| 方法 | 说明 |
|------|------|
| `bool Initialize(ID3D11Device*, ID3D11DeviceContext*)` | 初始化模糊效果（创建 Shader、纹理、采样器） |
| `void BeginBlur()` | 捕获当前 BackBuffer 到模糊源纹理 |
| `void ApplyBlur(ImDrawList*, ImVec2 pos, ImVec2 size, float radius, float rounding = 0.f, ImDrawFlags flags = 0)` | 执行 2-Pass 高斯模糊并绘制到指定 DrawList |
| `void EndBlur()` | 恢复原始渲染目标，清理 Shader 状态 |
| `void InvalidateCache()` | 强制使模糊缓存失效，下次 ApplyBlur 重新计算 |
| `void SetCaptureImGui(bool)` | 设置是否捕获 ImGui 内容（默认只捕获游戏画面） |

## 注意事项

1. **缓存机制**：`ApplyBlur` 采用缓存优化，相同 `radius` 会复用上帧结果。修改参数后需调用 `InvalidateCache()`
   ```cpp
   static float radius = 10.0f;
   if (ImGui::SliderFloat("Blur", &radius, 0.0f, 64.0f)) {
       blurEffect.InvalidateCache();  // 参数变化，刷新缓存
   }
   ```

2. **窗口背景透明**：使用模糊时需添加 `ImGuiWindowFlags_NoBackground`，否则默认黑色背景会覆盖模糊纹理

3. **全屏模糊 vs 窗口模糊**：
   ```cpp
   // 窗口模式：只模糊窗口区域
   blurEffect.ApplyBlur(ImGui::GetWindowDrawList(), winPos, winSize, radius);
   
   // 全屏模式：模糊整个视口（适合背景遮罩）
   blurEffect.ApplyBlur(ImGui::GetBackgroundDrawList(), viewportPos, viewportSize, radius);
   ```

4. **性能**：`radius` 越大采样次数越多，建议限制在 `0~64` 范围内。多窗口可复用同一次 `BeginBlur/EndBlur` 的缓存结果

## 修复的原作者问题

| 问题 | 修复方式 |
|------|---------|
| `radius` 参数未传入 Shader | 常量缓冲增加 `radius` 字段，Shader 动态控制采样范围 |
| 只模糊游戏画面，忽略 ImGui 层 | 支持 `SetCaptureImGui` 控制捕获范围 |
| 每帧重复执行 2-Pass 模糊 | 添加 `cacheValid` + `cachedRadius` 缓存机制 |
| 常量缓冲每帧创建释放 | `Initialize` 时创建一次，全程复用 |
| Shader 动态循环性能差 | 限制最大半径 64，添加 `[loop]` 属性 |

## 文件结构

```
DX11BlurEffect.hpp    # 单头文件实现（包含类声明+实现）
main.cpp              # 示例程序（可选）
```

## License

基于原帖开源实现改进，遵循原帖分享精神。