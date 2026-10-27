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
bool SendIPCCommand(TE_IpcMsgType type, const void* payload = nullptr, uint32_t payload_len = 0);
void InjectPayload();
void UninjectPayload();

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAYICON 1
#define ID_TRAY_EXIT 1001
#define ID_TRAY_RELOAD 1002

NOTIFYICONDATAW nid = {};
static UINT g_uMsgTaskbarCreated = 0;
static HMODULE g_hDll = NULL;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, PWSTR pCmdLine, int /*nCmdShow*/) {
    if (pCmdLine && (wcsstr(pCmdLine, L"-exit") || wcsstr(pCmdLine, L"--exit") || wcsstr(pCmdLine, L"/exit"))) {
        HANDLE hExit = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"TaskbarEngine_Exit_Event");
        if (hExit) {
            SetEvent(hExit);
            CloseHandle(hExit);
        }
        return 0;
    }

    /* Attach to the interactive user desktop if started from background */
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hDesk) {
        hDesk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL);
    }
    if (hDesk) {
        SetThreadDesktop(hDesk);
        CloseDesktop(hDesk);
    }

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"TaskbarEngine_Host_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    HANDLE hExitEvent = CreateEventW(NULL, FALSE, FALSE, L"TaskbarEngine_Exit_Event");
    if (hExitEvent) {
        ResetEvent(hExitEvent);
    }

    g_uMsgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    ChangeWindowMessageFilter(g_uMsgTaskbarCreated, MSGFLT_ADD);
    ChangeWindowMessageFilter(WM_TRAYICON, MSGFLT_ADD);

    const wchar_t CLASS_NAME[] = L"TaskbarEngineHost";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"TaskbarEngine", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        if (hExitEvent) CloseHandle(hExitEvent);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    ChangeWindowMessageFilterEx(hwnd, g_uMsgTaskbarCreated, MSGFLT_ALLOW, NULL);
    ChangeWindowMessageFilterEx(hwnd, WM_TRAYICON, MSGFLT_ALLOW, NULL);

    SetupTrayIcon(hwnd);

    // Inject Payload
    InjectPayload();

    // Small delay before notifying config
    Sleep(200);

    // Initial config load
    LoadConfigAndNotify(hwnd);

    MSG msg = {};
    bool running = true;
    while (running) {
        DWORD wait_res = MsgWaitForMultipleObjectsEx(1, &hExitEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (wait_res == WAIT_OBJECT_0) {
            SendMessageW(hwnd, WM_CLOSE, 0, 0);
            running = false;
            break;
        } else if (wait_res == WAIT_FAILED) {
            break;
        }
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    RemoveTrayIcon(hwnd);
    UninjectPayload();

    if (hExitEvent) {
        CloseHandle(hExitEvent);
    }
    if (hMutex) {
        CloseHandle(hMutex);
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

    HANDLE hThread = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        NOTIFYICONDATAW local_nid = nid;
        Shell_NotifyIconW(NIM_ADD, &local_nid);
        return 0;
    }, NULL, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, 1500);
        CloseHandle(hThread);
    }
}

void RemoveTrayIcon(HWND /*hwnd*/) {
    HANDLE hThread = CreateThread(NULL, 0, [](LPVOID) -> DWORD {
        NOTIFYICONDATAW local_nid = nid;
        Shell_NotifyIconW(NIM_DELETE, &local_nid);
        return 0;
    }, NULL, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, 1500);
        CloseHandle(hThread);
    }
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
    SendIPCCommand(TE_IPC_MSG_RELOAD_CONFIG);
}

bool SendIPCCommand(TE_IpcMsgType type, const void* payload, uint32_t payload_len) {
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    const wchar_t* pipe_names[] = { TE_PIPE_NAME, TE_PIPE_NAME_LEGACY, L"\\\\.\\pipe\\TaskbarEngineIPC" };

    for (int retry = 0; retry < 5; retry++) {
        for (int p = 0; p < 3; p++) {
            hPipe = CreateFileW(pipe_names[p], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
            if (hPipe != INVALID_HANDLE_VALUE) break;
            if (GetLastError() == ERROR_PIPE_BUSY) {
                WaitNamedPipeW(pipe_names[p], 200);
                hPipe = CreateFileW(pipe_names[p], GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
                if (hPipe != INVALID_HANDLE_VALUE) break;
            }
        }
        if (hPipe != INVALID_HANDLE_VALUE) break;
        Sleep(50);
    }
    if (hPipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    TE_IpcHeader header;
    if (FAILED(TE_IpcBuildHeader(&header, type, payload_len))) {
        CloseHandle(hPipe);
        return false;
    }

    OVERLAPPED ol = {};
    ol.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ol.hEvent) {
        CloseHandle(hPipe);
        return false;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(hPipe, &header, sizeof(header), &bytesWritten, &ol)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            DWORD wait_res = WaitForSingleObject(ol.hEvent, 3000);
            if (wait_res != WAIT_OBJECT_0 || !GetOverlappedResult(hPipe, &ol, &bytesWritten, FALSE)) {
                CancelIoEx(hPipe, &ol);
                DWORD dummy = 0;
                GetOverlappedResult(hPipe, &ol, &dummy, TRUE);
                CloseHandle(ol.hEvent);
                CloseHandle(hPipe);
                return false;
            }
        } else {
            CloseHandle(ol.hEvent);
            CloseHandle(hPipe);
            return false;
        }
    }

    if (payload_len > 0 && payload != nullptr) {
        ResetEvent(ol.hEvent);
        ol.Offset = 0; ol.OffsetHigh = 0;
        if (!WriteFile(hPipe, payload, payload_len, &bytesWritten, &ol)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                DWORD wait_res = WaitForSingleObject(ol.hEvent, 3000);
                if (wait_res != WAIT_OBJECT_0 || !GetOverlappedResult(hPipe, &ol, &bytesWritten, FALSE)) {
                    CancelIoEx(hPipe, &ol);
                    DWORD dummy = 0;
                    GetOverlappedResult(hPipe, &ol, &dummy, TRUE);
                    CloseHandle(ol.hEvent);
                    CloseHandle(hPipe);
                    return false;
                }
            } else {
                CloseHandle(ol.hEvent);
                CloseHandle(hPipe);
                return false;
            }
        }
    }

    FlushFileBuffers(hPipe);

    /* Read server acknowledgment to ensure synchronized transaction */
    ResetEvent(ol.hEvent);
    ol.Offset = 0; ol.OffsetHigh = 0;
    TE_IpcHeader resp;
    DWORD bytesRead = 0;
    if (!ReadFile(hPipe, &resp, sizeof(resp), &bytesRead, &ol)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            DWORD wait_res = WaitForSingleObject(ol.hEvent, 3000);
            if (wait_res != WAIT_OBJECT_0 || !GetOverlappedResult(hPipe, &ol, &bytesRead, FALSE)) {
                CancelIoEx(hPipe, &ol);
                DWORD dummy = 0;
                GetOverlappedResult(hPipe, &ol, &dummy, TRUE);
            }
        }
    }

    CloseHandle(ol.hEvent);
    CloseHandle(hPipe);
    return true;
}

HHOOK g_hHook = NULL;
void InjectPayload() {
    OutputDebugStringW(L"[TaskbarEngine] InjectPayload called.\n");
    
    UninjectPayload();

    HWND taskbar_hwnd = NULL;
    for (int retry = 0; retry < 10; retry++) {
        taskbar_hwnd = FindWindowW(L"Shell_TrayWnd", NULL);
        if (taskbar_hwnd) break;
        Sleep(200);
    }

    if (!taskbar_hwnd) {
        OutputDebugStringW(L"[TaskbarEngine] Failed to locate Shell_TrayWnd.\n");
        return;
    }

    DWORD explorer_thread_id = GetWindowThreadProcessId(taskbar_hwnd, NULL);
    if (explorer_thread_id == 0) {
        OutputDebugStringW(L"[TaskbarEngine] Invalid thread ID for Shell_TrayWnd.\n");
        return;
    }

    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(NULL, path, MAX_PATH)) return;
    std::wstring basePath = path;
    size_t pos = basePath.find_last_of(L"\\/");
    std::wstring dirPath = (pos != std::wstring::npos) ? basePath.substr(0, pos) : L"";

    std::wstring dllPath = dirPath + L"\\EngineDLL.dll";
    if (GetFileAttributesW(dllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring corePath = dirPath + L"\\Core\\EngineDLL.dll";
        if (GetFileAttributesW(corePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
            dllPath = corePath;
        } else {
            std::wstring relCorePath = dirPath + L"\\..\\Core\\EngineDLL.dll";
            if (GetFileAttributesW(relCorePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                dllPath = relCorePath;
            }
        }
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
                g_hDll = hDll;
                PostMessageW(taskbar_hwnd, WM_NULL, 0, 0);
                OutputDebugStringW(L"[TaskbarEngine] Hook successfully installed.\n");
            } else {
                FreeLibrary(hDll);
                OutputDebugStringW(L"[TaskbarEngine] SetWindowsHookExW failed.\n");
            }
        } else {
            FreeLibrary(hDll);
            OutputDebugStringW(L"[TaskbarEngine] Could not locate TE_GetMsgHookProc.\n");
        }
    } else {
        OutputDebugStringW(L"[TaskbarEngine] Could not load EngineDLL.dll.\n");
    }
}

void UninjectPayload() {
    if (g_hHook) {
        UnhookWindowsHookEx(g_hHook);
        g_hHook = NULL;
    }
    if (g_hDll) {
        FreeLibrary(g_hDll);
        g_hDll = NULL;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == g_uMsgTaskbarCreated && g_uMsgTaskbarCreated != 0) {
        SetupTrayIcon(hwnd);
        InjectPayload();
        return 0;
    }

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
            SendIPCCommand(TE_IPC_MSG_SHUTDOWN);
            Sleep(200);
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
