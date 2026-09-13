#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <fstream>
#include <sstream>
#include "TaskbarIPC.h"

extern "C" {
#include "../ThirdParty/cJSON/cJSON.h"
}

// Forward declarations
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void LoadConfigAndNotify(HWND hwnd);
void SetupTrayIcon(HWND hwnd);
void RemoveTrayIcon(HWND hwnd);
bool SendIPCMessage(const TE_IPC_Message& msg, TE_IPC_Message* reply = nullptr);
void InjectPayload();
void UninjectPayload();

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAYICON 1
#define ID_TRAY_EXIT 1001
#define ID_TRAY_RELOAD 1002

NOTIFYICONDATAW nid = {};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
    const wchar_t CLASS_NAME[] = L"TaskbarEngineHostClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"TaskbarEngine Host", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    SetupTrayIcon(hwnd);

    // Initial config load
    LoadConfigAndNotify(hwnd);

    // Inject Payload
    InjectPayload();

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

void SetupTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAYICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Placeholder for a sleek custom icon
    wcscpy_s(nid.szTip, 128, L"TaskbarEngine (Running)");

    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND /*hwnd*/) {
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, ID_TRAY_RELOAD, L"Reload Config");
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuW(hMenu, (UINT)-1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, L"Exit");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void LoadConfigAndNotify(HWND /*hwnd*/) {
    // Read JSON
    std::ifstream file("Config/taskbar_config.json");
    if (!file.is_open()) {
        file.open("../Config/taskbar_config.json");
    }
    if (!file.is_open()) {
        file.open("../../Config/taskbar_config.json");
    }
    if (!file.is_open()) return;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string jsonStr = buffer.str();

    cJSON* root = cJSON_Parse(jsonStr.c_str());
    if (!root) return;

    TE_IPC_Config config = {1.5f, 2.0f, 1024.0f, 64.0f, true};

    cJSON* max_scale = cJSON_GetObjectItemCaseSensitive(root, "max_scale");
    if (cJSON_IsNumber(max_scale)) config.max_scale = (float)max_scale->valuedouble;

    cJSON* spread_width = cJSON_GetObjectItemCaseSensitive(root, "spread_width");
    if (cJSON_IsNumber(spread_width)) config.spread_width = (float)spread_width->valuedouble;

    cJSON* spring_stiffness = cJSON_GetObjectItemCaseSensitive(root, "spring_stiffness");
    if (cJSON_IsNumber(spring_stiffness)) config.spring_stiffness = (float)spring_stiffness->valuedouble;

    cJSON* spring_damping = cJSON_GetObjectItemCaseSensitive(root, "spring_damping");
    if (cJSON_IsNumber(spring_damping)) config.spring_damping = (float)spring_damping->valuedouble;

    cJSON* enable_secondary_monitors = cJSON_GetObjectItemCaseSensitive(root, "enable_secondary_monitors");
    if (cJSON_IsBool(enable_secondary_monitors)) config.enable_secondary_monitors = cJSON_IsTrue(enable_secondary_monitors);

    cJSON_Delete(root);

    TE_IPC_Message msg = {};
    msg.type = TE_IPC_MSG_UPDATE_CONFIG;
    msg.data.config = config;

    SendIPCMessage(msg);
}

bool SendIPCMessage(const TE_IPC_Message& msg, TE_IPC_Message* reply) {
    HANDLE hPipe = CreateFileW(
        TE_IPC_PIPE_NAME,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesWritten = 0;
    WriteFile(hPipe, &msg, sizeof(msg), &bytesWritten, NULL);

    if (reply) {
        DWORD bytesRead = 0;
        ReadFile(hPipe, reply, sizeof(TE_IPC_Message), &bytesRead, NULL);
    }

    CloseHandle(hPipe);
    return true;
}

HHOOK g_hHook = NULL;
void InjectPayload() {
    OutputDebugStringW(L"[TaskbarEngine] InjectPayload called.\n");
    
    HWND taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
    if (!taskbar_hwnd) return;
    DWORD explorer_thread_id = GetWindowThreadProcessId(taskbar_hwnd, NULL);

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);
    std::wstring dllPath = path;
    size_t pos = dllPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dllPath = dllPath.substr(0, pos) + L"\\EngineDLL.dll";
    }
    
    HMODULE hDll = LoadLibraryW(dllPath.c_str());
    if (hDll) {
        HOOKPROC hook_proc = (HOOKPROC)GetProcAddress(hDll, "TE_GetMsgHookProc");
        if (!hook_proc) hook_proc = (HOOKPROC)GetProcAddress(hDll, "_TE_GetMsgHookProc@12");
        if (!hook_proc) hook_proc = (HOOKPROC)GetProcAddress(hDll, "TE_CBTHookProc");
        if (!hook_proc) hook_proc = (HOOKPROC)GetProcAddress(hDll, "_TE_CBTHookProc@12");
        if (hook_proc) {
            g_hHook = SetWindowsHookExW(WH_GETMESSAGE, hook_proc, hDll, explorer_thread_id);
            if (g_hHook) {
                PostMessageW(taskbar_hwnd, WM_NULL, 0, 0);
                OutputDebugStringW(L"[TaskbarEngine] Hook successfully installed.\n");
            }
        }
    }
}

void UninjectPayload() {
    if (g_hHook) {
        UnhookWindowsHookEx(g_hHook);
        g_hHook = NULL;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TRAYICON:
            if (LOWORD(lParam) == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                ShowContextMenu(hwnd, pt);
            }
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_TRAY_EXIT) {
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            } else if (LOWORD(wParam) == ID_TRAY_RELOAD) {
                LoadConfigAndNotify(hwnd);
            }
            return 0;

        case WM_CLOSE:
            RemoveTrayIcon(hwnd);
            UninjectPayload();
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
