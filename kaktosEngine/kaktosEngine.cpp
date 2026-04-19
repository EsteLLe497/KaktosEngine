#include "framework.h"
#include "kaktosEngine.h"
#include "NovelRuntime.h"
#include <shellapi.h>
#include <vector>

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
const wchar_t* kPreviewWindowClass = L"KaktosPreviewWindow";
NovelRuntime g_runtime;
bool g_playerMode = false;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK PreviewWndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_KAKTOSENGINE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!g_runtime.Initialize())
    {
        MessageBoxW(nullptr, L"Failed to initialize GDI+.", szTitle, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    WCHAR modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::wstring moduleName = modulePath;
    const size_t slash = moduleName.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        moduleName = moduleName.substr(slash + 1);
    }
    g_playerMode = moduleName.find(L"Player") != std::wstring::npos || moduleName.find(L"player") != std::wstring::npos;
    g_runtime.SetPlayerMode(g_playerMode);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring scenarioArg;
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring arg = argv[i];
            if (arg == L"--player")
            {
                g_playerMode = true;
                g_runtime.SetPlayerMode(true);
                continue;
            }
            if (!arg.empty() && arg[0] != L'-' && scenarioArg.empty())
            {
                scenarioArg = arg;
            }
        }
        LocalFree(argv);
    }

    g_runtime.LoadScenario(scenarioArg);

    if (!InitInstance(hInstance, nCmdShow))
    {
        g_runtime.Shutdown();
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_KAKTOSENGINE));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    g_runtime.Shutdown();
    return static_cast<int>(msg.wParam);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KAKTOSENGINE));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = nullptr;
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_KAKTOSENGINE);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    const ATOM mainAtom = RegisterClassExW(&wcex);

    WNDCLASSEXW previewClass = {};
    previewClass.cbSize = sizeof(WNDCLASSEX);
    previewClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    previewClass.lpfnWndProc = PreviewWndProc;
    previewClass.hInstance = hInstance;
    previewClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    previewClass.hbrBackground = nullptr;
    previewClass.lpszClassName = kPreviewWindowClass;
    RegisterClassExW(&previewClass);

    return mainAtom;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    const wchar_t* windowTitle = g_runtime.GetWindowTitle().empty() ? szTitle : g_runtime.GetWindowTitle().c_str();

    HWND hWnd = CreateWindowW(
        szWindowClass,
        windowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        0,
        1280,
        720,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    g_runtime.SetHostWindow(hWnd);
    DragAcceptFiles(hWnd, TRUE);

    HMENU hMenu = GetMenu(hWnd);
    if (g_playerMode && hMenu)
    {
        SetMenu(hWnd, nullptr);
    }
    else if (hMenu)
    {
        const UINT viewCommands[] = { IDM_VIEW_COMPONENTS, IDM_VIEW_INSPECTOR, IDM_VIEW_FLOWGRAPH, IDM_VIEW_PREVIEW, IDM_VIEW_EVENTLIST };
        for (UINT command : viewCommands)
        {
            CheckMenuItem(hMenu, command, MF_BYCOMMAND | (g_runtime.IsViewMenuChecked(command) ? MF_CHECKED : MF_UNCHECKED));
        }
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_playerMode)
        {
            return 0;
        }
        if (g_runtime.HandleMouseDown(point))
        {
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    }
    case WM_LBUTTONDBLCLK:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_playerMode)
        {
            return 0;
        }
        if (g_runtime.HandleDoubleClick(point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_playerMode)
        {
            return 0;
        }
        if ((wParam & MK_LBUTTON) && g_runtime.HandleMouseMove(point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_playerMode)
        {
            if (g_runtime.HandlePreviewClick(point))
            {
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            return 0;
        }
        const bool releasedDrag = g_runtime.HandleMouseUp(point);
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
        }
        if (releasedDrag)
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        if (g_runtime.HandleClick(point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
        }
        return 0;
    }
    case WM_RBUTTONUP:
    {
        if (g_playerMode)
        {
            return 0;
        }
        POINT clientPoint = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        POINT screenPoint = clientPoint;
        ClientToScreen(hWnd, &screenPoint);
        if (g_runtime.HandleRightClick(clientPoint, screenPoint))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL:
    {
        if (g_playerMode)
        {
            return 0;
        }
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &point);
        if (g_runtime.HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    }
    case WM_DROPFILES:
    {
        if (g_playerMode)
        {
            return 0;
        }
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        POINT point = {};
        DragQueryPoint(drop, &point);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> paths;
        paths.reserve(count);
        for (UINT i = 0; i < count; ++i)
        {
            WCHAR buffer[MAX_PATH] = {};
            DragQueryFileW(drop, i, buffer, MAX_PATH);
            paths.emplace_back(buffer);
        }
        DragFinish(drop);
        if (g_runtime.HandleFileDrop(point, paths))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (g_playerMode)
        {
            if (g_runtime.HandleKeyDown(wParam))
            {
                InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            break;
        }
        if (g_runtime.HandleKeyDown(wParam))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    case WM_CHAR:
        if (g_playerMode)
        {
            return 0;
        }
        if (g_runtime.HandleChar(static_cast<wchar_t>(wParam)))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        break;
    case WM_COMMAND:
    {
        if (g_playerMode)
        {
            return 0;
        }
        if (g_runtime.HandleControlCommand(wParam, lParam))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            g_runtime.RefreshPreviewWindow();
            return 0;
        }
        const int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_EDIT_RELOAD:
        case IDM_EDIT_UNDO:
        case IDM_EDIT_REDO:
        case IDM_EDIT_CUT:
        case IDM_EDIT_COPY:
        case IDM_EDIT_PASTE:
        case IDM_EDIT_SELECT_ALL:
        case IDM_EVENT_ADD_TEXT:
        case IDM_EVENT_DUPLICATE:
        case IDM_EVENT_DELETE:
        case IDM_EVENT_TOGGLE_ENABLE:
        case IDM_EVENT_MOVE_UP:
        case IDM_EVENT_MOVE_DOWN:
        case IDM_SCENE_RENAME:
        case IDM_SCENE_DUPLICATE:
        case IDM_SCENE_DELETE:
            if (g_runtime.ExecuteEditorCommand(static_cast<UINT>(wmId)))
            {
                InvalidateRect(hWnd, nullptr, TRUE);
                g_runtime.RefreshPreviewWindow();
            }
            break;
        case IDM_VIEW_COMPONENTS:
        case IDM_VIEW_INSPECTOR:
        case IDM_VIEW_FLOWGRAPH:
        case IDM_VIEW_PREVIEW:
        case IDM_VIEW_EVENTLIST:
        case IDM_VIEW_VARIABLES:
        case IDM_VIEW_RESET_LAYOUT:
        {
            if (g_runtime.HandleViewMenuCommand(static_cast<UINT>(wmId)))
            {
                HMENU hMenu = GetMenu(hWnd);
                if (hMenu)
                {
                    const UINT viewCommands[] = { IDM_VIEW_COMPONENTS, IDM_VIEW_INSPECTOR, IDM_VIEW_FLOWGRAPH, IDM_VIEW_PREVIEW, IDM_VIEW_EVENTLIST };
                    for (UINT command : viewCommands)
                    {
                        CheckMenuItem(hMenu, command, MF_BYCOMMAND | (g_runtime.IsViewMenuChecked(command) ? MF_CHECKED : MF_UNCHECKED));
                    }
                }
                InvalidateRect(hWnd, nullptr, TRUE);
                g_runtime.RefreshPreviewWindow();
            }
            break;
        }
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect;
        GetClientRect(hWnd, &clientRect);
        const int width = clientRect.right - clientRect.left;
        const int height = clientRect.bottom - clientRect.top;
        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP backBuffer = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, backBuffer);
        if (g_playerMode)
        {
            g_runtime.DrawPreviewWindow(memoryDc, clientRect);
        }
        else
        {
            g_runtime.Draw(memoryDc, clientRect);
        }
        BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(backBuffer);
        DeleteDC(memoryDc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (g_runtime.HandleTimer())
        {
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    default:
        break;
    }

    return (INT_PTR)FALSE;
}

LRESULT CALLBACK PreviewWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONDOWN:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_runtime.HandlePreviewMouseDown(point))
        {
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (g_runtime.HandlePreviewMouseMove(point))
        {
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
        }
        if (g_runtime.HandlePreviewMouseUp(point))
        {
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        if (g_runtime.HandlePreviewClick(point))
        {
            InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }
        break;
    }
    case WM_KEYDOWN:
        if (g_runtime.HandleKeyDown(wParam))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT clientRect = {};
        GetClientRect(hWnd, &clientRect);
        const int width = clientRect.right - clientRect.left;
        const int height = clientRect.bottom - clientRect.top;
        HDC memoryDc = CreateCompatibleDC(hdc);
        HBITMAP backBuffer = CreateCompatibleBitmap(hdc, width, height);
        HGDIOBJ oldBitmap = SelectObject(memoryDc, backBuffer);
        g_runtime.DrawPreviewWindow(memoryDc, clientRect);
        BitBlt(hdc, 0, 0, width, height, memoryDc, 0, 0, SRCCOPY);
        SelectObject(memoryDc, oldBitmap);
        DeleteObject(backBuffer);
        DeleteDC(memoryDc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        g_runtime.NotifyPreviewWindowDestroyed();
        return 0;
    default:
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}
