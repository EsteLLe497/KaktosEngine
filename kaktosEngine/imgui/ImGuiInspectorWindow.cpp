#include "../framework.h"
#include "ImGuiInspectorWindow.h"
#include "../NovelRuntime.h"

#include <d3d11.h>
#include "vendor/imgui.h"
#include "vendor/backends/imgui_impl_dx11.h"
#include "vendor/backends/imgui_impl_win32.h"

#pragma comment(lib, "d3d11.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace
{
    constexpr const wchar_t* kImGuiInspectorClass = L"KaktosImGuiInspectorWindow";
    constexpr UINT_PTR kRenderTimerId = 1;
}

bool ImGuiInspectorWindow::Show(HWND owner, NovelRuntime* runtime, ImGuiInspectorPanel panel)
{
    runtime_ = runtime;
    activePanel_ = panel;
    if (window_ && IsWindow(window_))
    {
        if (embedded_)
        {
            Shutdown();
        }
        else
        {
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        return true;
        }
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = ImGuiInspectorWindow::WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kImGuiInspectorClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered)
    {
        return false;
    }

    window_ = CreateWindowExW(
        0,
        kImGuiInspectorClass,
        L"ImGui Debug Inspector",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        520,
        720,
        owner,
        nullptr,
        instance,
        this);
    if (!window_)
    {
        return false;
    }

    if (!CreateDeviceD3D(window_))
    {
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 17.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;

    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_, deviceContext_);
    imguiInitialized_ = true;

    ShowWindow(window_, SW_SHOW);
    UpdateWindow(window_);
    SetTimer(window_, kRenderTimerId, 16, nullptr);
    return true;
}

bool ImGuiInspectorWindow::ShowEmbedded(HWND owner, NovelRuntime* runtime, const RECT& rect, ImGuiInspectorPanel panel)
{
    runtime_ = runtime;
    activePanel_ = panel;
    if (rect.right <= rect.left || rect.bottom <= rect.top)
    {
        Shutdown();
        return false;
    }

    if (window_ && IsWindow(window_) && !embedded_)
    {
        Shutdown();
    }
    if (window_ && IsWindow(window_))
    {
        MoveEmbedded(rect);
        ShowWindow(window_, SW_SHOW);
        return true;
    }

    HINSTANCE instance = GetModuleHandleW(nullptr);
    static bool registered = false;
    if (!registered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(WNDCLASSEXW);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = ImGuiInspectorWindow::WindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kImGuiInspectorClass;
        registered = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
    if (!registered)
    {
        return false;
    }

    embedded_ = true;
    window_ = CreateWindowExW(
        0,
        kImGuiInspectorClass,
        L"Kaktos Embedded Inspector",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        owner,
        nullptr,
        instance,
        this);
    if (!window_)
    {
        embedded_ = false;
        return false;
    }

    if (!CreateDeviceD3D(window_))
    {
        DestroyWindow(window_);
        window_ = nullptr;
        embedded_ = false;
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\meiryo.ttc", 17.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;

    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_, deviceContext_);
    imguiInitialized_ = true;

    SetTimer(window_, kRenderTimerId, 16, nullptr);
    return true;
}

void ImGuiInspectorWindow::MoveEmbedded(const RECT& rect)
{
    if (!window_ || !IsWindow(window_) || !embedded_)
    {
        return;
    }
    SetWindowPos(
        window_,
        HWND_TOP,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ImGuiInspectorWindow::SetPanel(ImGuiInspectorPanel panel)
{
    activePanel_ = panel;
    if (window_ && IsWindow(window_))
    {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ImGuiInspectorWindow::Shutdown()
{
    if (window_ && IsWindow(window_))
    {
        DestroyWindow(window_);
    }
    window_ = nullptr;
    embedded_ = false;
}

bool ImGuiInspectorWindow::IsOpen() const
{
    return window_ && IsWindow(window_);
}

bool ImGuiInspectorWindow::IsPanelOpen(ImGuiInspectorPanel panel) const
{
    return IsOpen() && activePanel_ == panel;
}

bool ImGuiInspectorWindow::CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC swapDesc = {};
    swapDesc.BufferCount = 2;
    swapDesc.BufferDesc.Width = 0;
    swapDesc.BufferDesc.Height = 0;
    swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.OutputWindow = hWnd;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.Windowed = TRUE;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL createdFeatureLevel = {};
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        static_cast<UINT>(_countof(featureLevels)),
        D3D11_SDK_VERSION,
        &swapDesc,
        &swapChain_,
        &device_,
        &createdFeatureLevel,
        &deviceContext_);
    if (FAILED(result))
    {
        return false;
    }
    CreateRenderTarget();
    return true;
}

void ImGuiInspectorWindow::CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (swapChain_)
    {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (deviceContext_)
    {
        deviceContext_->Release();
        deviceContext_ = nullptr;
    }
    if (device_)
    {
        device_->Release();
        device_ = nullptr;
    }
}

void ImGuiInspectorWindow::CreateRenderTarget()
{
    ID3D11Texture2D* backBuffer = nullptr;
    if (swapChain_ && SUCCEEDED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
    {
        device_->CreateRenderTargetView(backBuffer, nullptr, &renderTargetView_);
        backBuffer->Release();
    }
}

void ImGuiInspectorWindow::CleanupRenderTarget()
{
    if (renderTargetView_)
    {
        renderTargetView_->Release();
        renderTargetView_ = nullptr;
    }
}

void ImGuiInspectorWindow::RenderFrame()
{
    if (!imguiInitialized_ || !runtime_ || !deviceContext_ || !renderTargetView_)
    {
        return;
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin(
        "InspectorDock",
        nullptr,
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoTitleBar);
    if (ImGui::Button(activePanel_ == ImGuiInspectorPanel::Inspector ? "インスペクタ *" : "インスペクタ"))
    {
        activePanel_ = ImGuiInspectorPanel::Inspector;
    }
    ImGui::SameLine();
    if (ImGui::Button(activePanel_ == ImGuiInspectorPanel::Variables ? "変数 *" : "変数"))
    {
        activePanel_ = ImGuiInspectorPanel::Variables;
    }
    ImGui::Separator();
    if (activePanel_ == ImGuiInspectorPanel::Variables)
    {
        runtime_->DrawDebugVariablesImGui();
    }
    else
    {
        runtime_->DrawDebugInspectorImGui();
    }
    ImGui::End();

    ImGui::Render();
    const float clearColor[4] = { 0.055f, 0.075f, 0.105f, 1.0f };
    deviceContext_->OMSetRenderTargets(1, &renderTargetView_, nullptr);
    deviceContext_->ClearRenderTargetView(renderTargetView_, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swapChain_->Present(1, 0);
}

void ImGuiInspectorWindow::HandleResize(UINT width, UINT height)
{
    if (!swapChain_ || width == 0 || height == 0)
    {
        return;
    }
    CleanupRenderTarget();
    swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
}

LRESULT CALLBACK ImGuiInspectorWindow::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    ImGuiInspectorWindow* inspector = reinterpret_cast<ImGuiInspectorWindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        inspector = reinterpret_cast<ImGuiInspectorWindow*>(create ? create->lpCreateParams : nullptr);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(inspector));
    }

    if (inspector && inspector->imguiInitialized_ && ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
    {
        return 1;
    }

    switch (message)
    {
    case WM_SIZE:
        if (inspector && wParam != SIZE_MINIMIZED)
        {
            inspector->HandleResize(static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)));
        }
        return 0;
    case WM_TIMER:
        InvalidateRect(hWnd, nullptr, FALSE);
        return 0;
    case WM_PAINT:
        if (inspector)
        {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            inspector->RenderFrame();
            EndPaint(hWnd, &ps);
            return 0;
        }
        break;
    case WM_DESTROY:
        if (inspector)
        {
            KillTimer(hWnd, kRenderTimerId);
            if (inspector->imguiInitialized_)
            {
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
                ImGui::DestroyContext();
                inspector->imguiInitialized_ = false;
            }
            inspector->CleanupDeviceD3D();
            inspector->window_ = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}
