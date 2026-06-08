#pragma once

#include "../framework.h"
#include <d3d11.h>
#include <dxgi.h>

class NovelRuntime;

enum class ImGuiInspectorPanel
{
    Inspector,
    Variables,
};

class ImGuiInspectorWindow
{
public:
    bool Show(HWND owner, NovelRuntime* runtime, ImGuiInspectorPanel panel = ImGuiInspectorPanel::Inspector);
    bool ShowEmbedded(HWND owner, NovelRuntime* runtime, const RECT& rect, ImGuiInspectorPanel panel = ImGuiInspectorPanel::Inspector);
    void MoveEmbedded(const RECT& rect);
    void SetPanel(ImGuiInspectorPanel panel);
    void Shutdown();
    bool IsOpen() const;
    bool IsPanelOpen(ImGuiInspectorPanel panel) const;

private:
    bool CreateDeviceD3D(HWND hWnd);
    void CleanupDeviceD3D();
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void RenderFrame();
    void HandleResize(UINT width, UINT height);
    static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    HWND window_ = nullptr;
    NovelRuntime* runtime_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* deviceContext_ = nullptr;
    IDXGISwapChain* swapChain_ = nullptr;
    ID3D11RenderTargetView* renderTargetView_ = nullptr;
    ImGuiInspectorPanel activePanel_ = ImGuiInspectorPanel::Inspector;
    bool imguiInitialized_ = false;
    bool embedded_ = false;
};
