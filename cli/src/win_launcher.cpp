#ifndef UNICODE
#define UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>

#define WM_TRAYICON (WM_APP + 1)
#define IDM_OPEN_UI 1001
#define IDM_STATUS 1002
#define IDM_STOP 1003
#define IDM_EXIT 1004

static HINSTANCE g_hInst;
static HWND g_hWnd;
static NOTIFYICONDATA g_nid;
static HANDLE g_hChildProcess = nullptr;
static bool g_serverRunning = false;

static std::wstring ExeDir() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    auto pos = s.rfind(L'\\');
    return pos != std::wstring::npos ? s.substr(0, pos) : s;
}

static void StartServer() {
    if (g_serverRunning) return;
    std::wstring cmd = L"\"" + ExeDir() + L"\\merak.exe\" serve";
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        g_hChildProcess = pi.hProcess;
        CloseHandle(pi.hThread);
        g_serverRunning = true;
    }
}

static void StopServer() {
    if (g_hChildProcess) {
        GenerateConsoleCtrlEvent(CTRL_C_EVENT, GetProcessId(g_hChildProcess));
        WaitForSingleObject(g_hChildProcess, 5000);
        TerminateProcess(g_hChildProcess, 0);
        CloseHandle(g_hChildProcess);
        g_hChildProcess = nullptr;
    }
    g_serverRunning = false;
}

static void OpenWebUI() {
    ShellExecuteW(nullptr, L"open", L"http://localhost:3888", nullptr, nullptr, SW_SHOW);
}

static void UpdateTrayMenu(HMENU menu) {
    ModifyMenuW(menu, IDM_STATUS, MF_BYCOMMAND | MF_STRING, IDM_STATUS,
                g_serverRunning ? L"Status: Running" : L"Status: Stopped");
    EnableMenuItem(menu, IDM_STOP, MF_BYCOMMAND | (g_serverRunning ? MF_ENABLED : MF_DISABLED));
    EnableMenuItem(menu, IDM_OPEN_UI, MF_BYCOMMAND | (g_serverRunning ? MF_ENABLED : MF_DISABLED));
}

static void ShowTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_OPEN_UI, L"Open Merak WebUI");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | MF_DISABLED, IDM_STATUS, L"Status: Starting...");
    AppendMenuW(menu, MF_STRING, IDM_STOP, L"Stop Server");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
    UpdateTrayMenu(menu);

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hWnd);
    WORD cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, g_hWnd, nullptr);
    DestroyMenu(menu);

    switch (cmd) {
    case IDM_OPEN_UI: OpenWebUI(); break;
    case IDM_STOP: StopServer(); break;
    case IDM_EXIT:
        if (g_serverRunning) {
            int answer = MessageBoxW(g_hWnd, L"Server is still running. Stop and exit?",
                                     L"Merak", MB_YESNO | MB_ICONQUESTION);
            if (answer == IDYES) { StopServer(); DestroyWindow(g_hWnd); }
        } else {
            DestroyWindow(g_hWnd);
        }
        break;
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_nid.cbSize = sizeof(NOTIFYICONDATA);
        g_nid.hWnd = hWnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(g_nid.szTip, L"Merak Agent");
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        // Poll for server readiness, then open browser
        std::thread([]() {
            for (int i = 0; i < 60; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock == INVALID_SOCKET) continue;
                struct sockaddr_in addr = {};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(3888);
                addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
                    closesocket(sock);
                    g_serverRunning = true;
                    ShellExecuteW(nullptr, L"open", L"http://localhost:3888",
                                  nullptr, nullptr, SW_SHOW);
                    break;
                }
                closesocket(sock);
            }
        }).detach();
        return 0;
    }
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowTrayMenu();
        if (lParam == WM_LBUTTONDBLCLK) OpenWebUI();
        return 0;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        StopServer();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    g_hInst = hInstance;
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    StartServer();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MerakLauncher";
    RegisterClassExW(&wc);

    g_hWnd = CreateWindowExW(0, L"MerakLauncher", L"Merak", 0,
                             0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    WSACleanup();
    return 0;
}
