#include "framework.h"
#include "kaktosEngine.h"
#include "NovelRuntime.h"

#define MAX_LOADSTRING 100

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
NovelRuntime g_runtime;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_KAKTOSENGINE, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!g_runtime.Initialize())
    {
        MessageBoxW(nullptr, L"Failed to initialize GDI+.", szTitle, MB_OK | MB_ICONERROR);
        return FALSE;
    }

    g_runtime.LoadScenario();

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
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_KAKTOSENGINE));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_KAKTOSENGINE);
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));
    return RegisterClassExW(&wcex);
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

    HMENU hMenu = GetMenu(hWnd);
    if (hMenu)
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
        if (g_runtime.HandleMouseDown(point))
        {
            SetCapture(hWnd);
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        if ((wParam & MK_LBUTTON) && g_runtime.HandleMouseMove(point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        break;
    }
    case WM_LBUTTONUP:
    {
        const POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        const bool releasedDrag = g_runtime.HandleMouseUp(point);
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
        }
        if (releasedDrag)
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        if (g_runtime.HandleClick(point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL:
    {
        POINT point = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &point);
        if (g_runtime.HandleMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), point))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
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
    case WM_CHAR:
        if (g_runtime.HandleChar(static_cast<wchar_t>(wParam)))
        {
            InvalidateRect(hWnd, nullptr, TRUE);
            return 0;
        }
        break;
    case WM_COMMAND:
    {
        const int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_VIEW_COMPONENTS:
        case IDM_VIEW_INSPECTOR:
        case IDM_VIEW_FLOWGRAPH:
        case IDM_VIEW_PREVIEW:
        case IDM_VIEW_EVENTLIST:
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
        g_runtime.Draw(hdc, clientRect);
        EndPaint(hWnd, &ps);
        return 0;
    }
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
