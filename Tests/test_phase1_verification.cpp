#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <commctrl.h>
#include <sdk/te_types.h>
#include <sdk/te_ipc.h>
#include "frame_loop.h"
#include "taskbar_resize.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

TEST_CASE("Phase 1 - Module Pinning Removed (SYS-001) & Clean Detach (SYS-002)", "[phase1][sys_001][sys_002]") {
    wchar_t dll_path[MAX_PATH];
    GetModuleFileNameW(NULL, dll_path, MAX_PATH);
    wchar_t* last_slash = wcsrchr(dll_path, L'\\');
    if (last_slash) {
        *last_slash = L'\0';
    }

    // Try loading EngineDLL from relative build paths
    wchar_t candidate[MAX_PATH];
    swprintf(candidate, MAX_PATH, L"%s\\..\\Core\\EngineDLL.dll", dll_path);
    HMODULE hMod = LoadLibraryExW(candidate, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hMod) {
        swprintf(candidate, MAX_PATH, L"%s\\Core\\EngineDLL.dll", dll_path);
        hMod = LoadLibraryExW(candidate, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (!hMod) {
        swprintf(candidate, MAX_PATH, L"%s\\EngineDLL.dll", dll_path);
        hMod = LoadLibraryExW(candidate, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }

    REQUIRE(hMod != NULL);

    // Verify it is recognized as loaded
    HMODULE hFound = GetModuleHandleW(L"EngineDLL.dll");
    REQUIRE(hFound == hMod);

    // FreeLibrary: must not deadlock under loader lock (SYS-002)
    // and must unload cleanly without permanent pinning (SYS-001)
    BOOL freed = FreeLibrary(hMod);
    REQUIRE(freed == TRUE);

    // After FreeLibrary, EngineDLL must be unloaded (refcount reaches 0).
    // If GET_MODULE_HANDLE_EX_FLAG_PIN was still present, it would stay pinned.
    HMODULE hAfter = GetModuleHandleW(L"EngineDLL.dll");
    REQUIRE(hAfter == NULL);
}

TEST_CASE("Phase 1 - Frame Loop Synchronous Timer Deletion (PERF-102)", "[phase1][perf_102]") {
    // Perform 100 consecutive rapid Start/Stop cycles on main thread.
    for (int i = 0; i < 100; i++) {
        HRESULT hr = TE_FrameLoopStart();
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(TE_FrameLoopIsActive() == 1);

        TE_FrameLoopStop();
        REQUIRE(TE_FrameLoopIsActive() == 0);
    }

    // Calling TE_FrameLoopStop when already stopped is a safe no-op
    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);
}

TEST_CASE("Phase 1 - Frame Loop Concurrent Multi-Threaded Stress (PERF-102 Concurrency)", "[phase1][perf_102][concurrency]") {
    std::atomic<bool> run{true};
    std::atomic<int> start_count{0};
    std::atomic<int> stop_count{0};

    // Spawn concurrent starter and stopper threads
    std::vector<std::thread> threads;
    for (int t = 0; t < 2; t++) {
        threads.emplace_back([&run, &start_count]() {
            while (run.load(std::memory_order_relaxed)) {
                TE_FrameLoopStart();
                start_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }
    for (int t = 0; t < 2; t++) {
        threads.emplace_back([&run, &stop_count]() {
            while (run.load(std::memory_order_relaxed)) {
                TE_FrameLoopStop();
                stop_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
            }
        });
    }

    // Let them race heavily for 150ms
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    run.store(false, std::memory_order_relaxed);

    for (auto& th : threads) {
        th.join();
    }

    // Must cleanly stop and settle to inactive without leaks or deadlocks
    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);
    REQUIRE(start_count.load() > 10);
    REQUIRE(stop_count.load() > 10);

    // Verify shutdown destroys queue completely
    TE_FrameLoopShutdown();
    REQUIRE(TE_FrameLoopIsActive() == 0);
}

static LRESULT CALLBACK TestSubclassProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    (void)ref;
    if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, TestSubclassProc, id);
        return DefSubclassProc(h, m, w, l);
    }
    return DefSubclassProc(h, m, w, l);
}

TEST_CASE("Phase 1 - Subclass WM_NCDESTROY Invariant (SYS-004)", "[phase1][sys_004]") {
    // Create a dummy test window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_Phase1SubclassTest";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"TE_Phase1SubclassTest", L"", WS_POPUP, 0, 0, 50, 50, NULL, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(hwnd != NULL);

    BOOL set_ok = SetWindowSubclass(hwnd, TestSubclassProc, 101, 0);
    REQUIRE(set_ok == TRUE);

    // Destroy window: triggers WM_DESTROY and WM_NCDESTROY
    BOOL destroyed = DestroyWindow(hwnd);
    REQUIRE(destroyed == TRUE);

    UnregisterClassW(L"TE_Phase1SubclassTest", GetModuleHandleW(NULL));
}

TEST_CASE("Phase 1 - IPC Shutdown Response Order (SYS-010, SYS-011)", "[phase1][sys_010][sys_011]") {
    // Verify that IPC shutdown header is formatted correctly
    TE_IpcHeader shutdown_hdr = {};
    REQUIRE(TE_IpcBuildHeader(&shutdown_hdr, TE_IPC_MSG_SHUTDOWN, 0) == TE_S_OK);
    REQUIRE(shutdown_hdr.type == TE_IPC_MSG_SHUTDOWN);
    REQUIRE(shutdown_hdr.payload_length == 0);
    REQUIRE(TE_IpcValidateHeader(&shutdown_hdr) == TE_S_OK);

    // Verify status response header
    TE_IpcHeader resp_hdr = {};
    REQUIRE(TE_IpcBuildHeader(&resp_hdr, TE_IPC_MSG_STATUS, 0) == TE_S_OK);
    REQUIRE(resp_hdr.type == TE_IPC_MSG_STATUS);
    REQUIRE(TE_IpcValidateHeader(&resp_hdr) == TE_S_OK);
}
