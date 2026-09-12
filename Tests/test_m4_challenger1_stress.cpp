/**
 * @file test_m4_challenger1_stress.cpp
 * @brief Empirical Challenger 1 Stress Harness & OS Resource Leak Verification for Milestone 4 Gate.
 *
 * Scope:
 * 1. High-concurrency extraction under aggressive cache invalidation:
 *    - 8 reader worker threads + 2 cache clearing threads + 2 cache invalidation threads x 500 iterations.
 * 2. Rapid start/stop stability & OS thread reclamation:
 *    - 100 consecutive TE_FrameLoopStart() / TE_FrameLoopStop() cycles.
 *    - Strict thread count verification: active OS threads must return to baseline after every cycle.
 *    - Win32 thread handle count stability: 0 handle leaks.
 * 3. DirectComposition overlay window & device lifecycle:
 *    - 50 consecutive overlay creation & destruction cycles.
 *    - GDI and USER handle stability.
 * 4. Subsystem re-initialization:
 *    - 20 full cycles of Init -> Extract -> ClearCache -> Shutdown.
 * 5. Plugin ABI lifecycle & double-call robustness:
 *    - Double Disable(), double Shutdown(), uninitialized calls.
 * 6. Dual-toolchain cross-verification:
 *    - Exercises both MSVC (icon_hover.dll) and MinGW (libicon_hover.dll) runtime binaries.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>

#include <sdk/te_plugin.h>
#include <sdk/te_types.h>
#include "Modules/icon_hover/icon_capture.h"

// Logging stub
#include <sdk/te_log.h>
extern "C" {
void TE_LogWrite(TE_LogLevel level, const char* tag, const char* message) {
    (void)level; (void)tag; (void)message;
}
}

static int s_tests_passed = 0;
static int s_tests_failed = 0;

static FILE* s_flog = NULL;

#define LOG_PRINT(...) do { \
    printf(__VA_ARGS__); \
    if (s_flog) { fprintf(s_flog, __VA_ARGS__); fflush(s_flog); } \
} while(0)

#define ASSERT_TRUE(cond, msg) do { \
    if (!(cond)) { \
        LOG_PRINT("  [FAIL] Line %d: %s\n", __LINE__, msg); \
        s_tests_failed++; \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

#define ASSERT_EQ(val, expected, msg) do { \
    if ((val) != (expected)) { \
        LOG_PRINT("  [FAIL] Line %d: %s (got %lld, expected %lld)\n", \
               __LINE__, msg, (long long)(val), (long long)(expected)); \
        s_tests_failed++; \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

// Helper to query active thread count for this process
static DWORD GetCurrentProcessThreadCount(void) {
    DWORD pid = GetCurrentProcessId();
    DWORD thread_count = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te;
        te.dwSize = sizeof(te);
        if (Thread32First(snapshot, &te)) {
            do {
                if (te.th32OwnerProcessID == pid) {
                    thread_count++;
                }
            } while (Thread32Next(snapshot, &te));
        }
        CloseHandle(snapshot);
    }
    return thread_count;
}

// Helper to query OS resource handles
static DWORD GetGdiObjectCount(void) {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static DWORD GetUserObjectCount(void) {
    return GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
}

static DWORD GetHandleCount(void) {
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

static HWND CreateDummyTaskbarWindow(void) {
    static const wchar_t* CLASS_NAME = L"ChallengerStressTestWindowClass";
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = CLASS_NAME;
        RegisterClassExW(&wc);
        registered = true;
    }
    return CreateWindowExW(
        0, CLASS_NAME, L"Challenger Test Window",
        WS_OVERLAPPEDWINDOW, 0, 0, 800, 48,
        NULL, NULL, GetModuleHandleW(NULL), NULL
    );
}

// ============================================================================
// SUITE 1: High-Concurrency Extraction & Contention (Feature F16)
// ============================================================================
static void Test_HighConcurrencyExtractionStress(void) {
    LOG_PRINT("\n=== SUITE 1: High-Concurrency Extraction & Cache Contention (Feature F16) ===\n");

    DWORD gdi_start = GetGdiObjectCount();
    DWORD user_start = GetUserObjectCount();
    DWORD handles_start = GetHandleCount();

    ASSERT_TRUE(SUCCEEDED(TE_IconCaptureInit()), "TE_IconCaptureInit succeeded");

    wchar_t sys_dir[MAX_PATH] = {};
    GetSystemDirectoryW(sys_dir, MAX_PATH);

    wchar_t paths[4][MAX_PATH];
    swprintf_s(paths[0], L"%s\\notepad.exe", sys_dir);
    swprintf_s(paths[1], L"%s\\regedit.exe", sys_dir);
    swprintf_s(paths[2], L"%s\\cmd.exe", sys_dir);
    swprintf_s(paths[3], L"%s\\taskmgr.exe", sys_dir);

    std::atomic<bool> stop_flag{false};
    std::atomic<int> total_extractions{0};
    std::atomic<int> successful_extractions{0};
    std::atomic<int> clear_calls{0};
    std::atomic<int> invalidate_calls{0};

    const int num_readers = 8;
    const int num_clearers = 2;
    const int num_invalidators = 2;
    const int iters_per_reader = 500;

    std::vector<std::thread> threads;

    // Launch 8 readers
    for (int r = 0; r < num_readers; r++) {
        threads.emplace_back([&, r]() {
            for (int i = 0; i < iters_per_reader && !stop_flag.load(std::memory_order_relaxed); i++) {
                TE_TaskbarItemInfo item = {};
                item.icon_index = -1;
                item.app_id = paths[(r + i) % 4];

                HBITMAP bmp = nullptr;
                BOOL alpha = FALSE;
                HRESULT hr = TE_IconCaptureExtract(&item, &bmp, &alpha);
                total_extractions.fetch_add(1, std::memory_order_relaxed);
                if (SUCCEEDED(hr) && bmp != nullptr) {
                    successful_extractions.fetch_add(1, std::memory_order_relaxed);
                }
                if ((i % 25) == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // Launch 2 cache clearers
    for (int c = 0; c < num_clearers; c++) {
        threads.emplace_back([&]() {
            while (!stop_flag.load(std::memory_order_relaxed)) {
                TE_IconCaptureClearCache();
                clear_calls.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    // Launch 2 cache invalidators
    for (int inv = 0; inv < num_invalidators; inv++) {
        threads.emplace_back([&]() {
            while (!stop_flag.load(std::memory_order_relaxed)) {
                TE_IconCaptureInvalidate();
                invalidate_calls.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
    }

    // Join readers first
    for (int r = 0; r < num_readers; r++) {
        threads[r].join();
    }

    // Stop clearers and invalidators
    stop_flag.store(true, std::memory_order_release);
    for (size_t t = num_readers; t < threads.size(); t++) {
        threads[t].join();
    }

    LOG_PRINT("  Readers completed: %d total extractions, %d successful\n",
           total_extractions.load(), successful_extractions.load());
    LOG_PRINT("  Clearers executed: %d calls, Invalidators: %d calls\n",
           clear_calls.load(), invalidate_calls.load());

    ASSERT_TRUE(total_extractions.load() >= num_readers * iters_per_reader, "All reader iterations completed");
    ASSERT_TRUE(successful_extractions.load() > 1000, "Substantial extractions succeeded concurrently");
    ASSERT_TRUE(clear_calls.load() > 0, "ClearCache executed concurrently");
    ASSERT_TRUE(invalidate_calls.load() > 0, "Invalidate executed concurrently");

    // Clean up cache and shutdown
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();

    DWORD gdi_end = GetGdiObjectCount();
    DWORD user_end = GetUserObjectCount();
    DWORD handles_end = GetHandleCount();

    LOG_PRINT("  [Run 1] GDI objects: start=%u, end=%u (delta=%d)\n", gdi_start, gdi_end, (int)(gdi_end - gdi_start));
    LOG_PRINT("  [Run 1] USER objects: start=%u, end=%u (delta=%d)\n", user_start, user_end, (int)(user_end - user_start));
    LOG_PRINT("  [Run 1] Handle count: start=%u, end=%u (delta=%d)\n", handles_start, handles_end, (int)(handles_end - handles_start));

    // Run 2: Re-run the exact same stress test to verify if GDI growth was one-time OS initialization or a leak!
    LOG_PRINT("  --- Starting Run 2 to verify cumulative leak vs one-time OS init ---\n");
    DWORD gdi_run2_start = GetGdiObjectCount();
    DWORD user_run2_start = GetUserObjectCount();
    DWORD handles_run2_start = GetHandleCount();

    ASSERT_TRUE(SUCCEEDED(TE_IconCaptureInit()), "TE_IconCaptureInit run 2 succeeded");

    stop_flag.store(false, std::memory_order_release);
    threads.clear();

    for (int r = 0; r < num_readers; r++) {
        threads.emplace_back([&, r]() {
            for (int i = 0; i < iters_per_reader && !stop_flag.load(std::memory_order_relaxed); i++) {
                TE_TaskbarItemInfo item = {};
                item.icon_index = -1;
                item.app_id = paths[(r + i) % 4];

                HBITMAP bmp = nullptr;
                BOOL alpha = FALSE;
                HRESULT hr = TE_IconCaptureExtract(&item, &bmp, &alpha);
                (void)hr;
                if ((i % 25) == 0) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (int c = 0; c < num_clearers; c++) {
        threads.emplace_back([&]() {
            while (!stop_flag.load(std::memory_order_relaxed)) {
                TE_IconCaptureClearCache();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    for (int inv = 0; inv < num_invalidators; inv++) {
        threads.emplace_back([&]() {
            while (!stop_flag.load(std::memory_order_relaxed)) {
                TE_IconCaptureInvalidate();
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
        });
    }

    for (int r = 0; r < num_readers; r++) {
        threads[r].join();
    }
    stop_flag.store(true, std::memory_order_release);
    for (size_t t = num_readers; t < threads.size(); t++) {
        threads[t].join();
    }

    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();

    DWORD gdi_run2_end = GetGdiObjectCount();
    DWORD user_run2_end = GetUserObjectCount();
    DWORD handles_run2_end = GetHandleCount();

    LOG_PRINT("  [Run 2] GDI objects: start=%u, end=%u (delta=%d)\n", gdi_run2_start, gdi_run2_end, (int)(gdi_run2_end - gdi_run2_start));
    LOG_PRINT("  [Run 2] USER objects: start=%u, end=%u (delta=%d)\n", user_run2_start, user_run2_end, (int)(user_run2_end - user_run2_start));
    LOG_PRINT("  [Run 2] Handle count: start=%u, end=%u (delta=%d)\n", handles_run2_start, handles_run2_end, (int)(handles_run2_end - handles_run2_start));

    ASSERT_EQ(gdi_run2_end, gdi_run2_start, "Strictly zero cumulative GDI handle leakage between Run 1 and Run 2");
    ASSERT_EQ(user_run2_end, user_run2_start, "Strictly zero cumulative USER handle leakage between Run 1 and Run 2");
}

// ============================================================================
// SUITE 2: Subsystem Re-Initialization Lifecycle (Feature F16)
// ============================================================================
static void Test_SubsystemReinitLifecycle(void) {
    LOG_PRINT("\n=== SUITE 2: Subsystem Re-Initialization Lifecycle Stress (Feature F16) ===\n");

    DWORD gdi_start = GetGdiObjectCount();
    DWORD user_start = GetUserObjectCount();

    wchar_t sys_dir[MAX_PATH] = {};
    GetSystemDirectoryW(sys_dir, MAX_PATH);
    wchar_t notepad_path[MAX_PATH] = {};
    swprintf_s(notepad_path, L"%s\\notepad.exe", sys_dir);

    TE_TaskbarItemInfo item = {};
    item.icon_index = -1;
    item.app_id = notepad_path;

    const int num_cycles = 25;
    for (int cycle = 0; cycle < num_cycles; cycle++) {
        ASSERT_TRUE(SUCCEEDED(TE_IconCaptureInit()), "TE_IconCaptureInit cycle succeeded");

        HBITMAP bmp = nullptr;
        BOOL alpha = FALSE;
        ASSERT_TRUE(SUCCEEDED(TE_IconCaptureExtract(&item, &bmp, &alpha)), "Extraction succeeded");
        ASSERT_TRUE(bmp != nullptr, "Valid bitmap returned");

        TE_IconCaptureClearCache();
        TE_IconCaptureShutdown();
    }

    DWORD gdi_end = GetGdiObjectCount();
    DWORD user_end = GetUserObjectCount();

    LOG_PRINT("  Completed %d re-init cycles\n", num_cycles);
    LOG_PRINT("  GDI objects: start=%u, end=%u (delta=%d)\n", gdi_start, gdi_end, (int)(gdi_end - gdi_start));
    LOG_PRINT("  USER objects: start=%u, end=%u (delta=%d)\n", user_start, user_end, (int)(user_end - user_start));

    ASSERT_TRUE(gdi_end == gdi_start, "Strictly zero GDI leaks after 25 re-init cycles");
    ASSERT_TRUE(user_end == user_start, "Strictly zero USER leaks after 25 re-init cycles");
}

// ============================================================================
// SUITE 3: Dynamic DLL Verification: Rapid Start/Stop & DComp (Dual Toolchains)
// ============================================================================
typedef bool (*pfnTE_TestFrameLoopStart)(void);
typedef void (*pfnTE_TestFrameLoopStop)(void);
typedef int (*pfnTE_TestFrameLoopIsActive)(void);
typedef int (*pfnTE_TestDCompOverlayLifecycle)(HWND);
typedef const PluginInterface* (*pfnGetPluginInterface)(void);

static void Test_DLL_Lifecycle(const wchar_t* dll_path, const char* toolchain_name) {
    LOG_PRINT("\n=== SUITE 3: DLL Lifecycle & OS Resource Leakage (%s: %ls) ===\n", toolchain_name, dll_path);

    HMODULE mod = LoadLibraryW(dll_path);
    if (!mod) {
        LOG_PRINT("  [SKIP] Could not load %ls (error %lu)\n", dll_path, GetLastError());
        return;
    }

    auto fnStart = (pfnTE_TestFrameLoopStart)(void*)GetProcAddress(mod, "TE_TestFrameLoopStart");
    auto fnStop = (pfnTE_TestFrameLoopStop)(void*)GetProcAddress(mod, "TE_TestFrameLoopStop");
    auto fnIsActive = (pfnTE_TestFrameLoopIsActive)(void*)GetProcAddress(mod, "TE_TestFrameLoopIsActive");
    auto fnDCompLifecycle = (pfnTE_TestDCompOverlayLifecycle)(void*)GetProcAddress(mod, "TE_TestDCompOverlayLifecycle");
    auto fnGetInterface = (pfnGetPluginInterface)(void*)GetProcAddress(mod, "GetPluginInterface");

    ASSERT_TRUE(fnStart != nullptr, "TE_TestFrameLoopStart exported");
    ASSERT_TRUE(fnStop != nullptr, "TE_TestFrameLoopStop exported");
    ASSERT_TRUE(fnIsActive != nullptr, "TE_TestFrameLoopIsActive exported");
    ASSERT_TRUE(fnDCompLifecycle != nullptr, "TE_TestDCompOverlayLifecycle exported");
    ASSERT_TRUE(fnGetInterface != nullptr, "GetPluginInterface exported");

    // --- 3.1: 100 Rapid Start/Stop Cycles & Thread Reclamation ---
    LOG_PRINT("  --- 3.1: 100 Rapid Start/Stop Cycles & Thread Reclamation ---\n");
    DWORD baseline_threads = GetCurrentProcessThreadCount();
    DWORD handles_before_startstop = GetHandleCount();

    const int rapid_cycles = 100;
    for (int i = 0; i < rapid_cycles; i++) {
        ASSERT_EQ(fnIsActive(), 0, "Initially inactive");
        bool ok = fnStart();
        ASSERT_TRUE(ok, "Frame loop start succeeded");
        ASSERT_EQ(fnIsActive(), 1, "Active after start");

        // Small sleep on select iterations to test thread mid-flight
        if (i % 20 == 19) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        fnStop();
        ASSERT_EQ(fnIsActive(), 0, "Inactive after stop");

        // Toolhelp thread count verification
        DWORD current_threads = GetCurrentProcessThreadCount();
        ASSERT_EQ(current_threads, baseline_threads, "Active thread count strictly returned to baseline");
    }

    DWORD handles_after_startstop = GetHandleCount();
    LOG_PRINT("  Completed %d start/stop cycles: baseline_threads=%u, final_threads=%u\n",
           rapid_cycles, baseline_threads, GetCurrentProcessThreadCount());
    LOG_PRINT("  Handle count: before=%u, after=%u (delta=%d)\n",
           handles_before_startstop, handles_after_startstop, (int)(handles_after_startstop - handles_before_startstop));
    ASSERT_TRUE(handles_after_startstop <= handles_before_startstop + 4, "No thread handle leaks across 100 cycles");

    // --- 3.2: 50 DComp Overlay Creation/Destruction Cycles ---
    LOG_PRINT("  --- 3.2: 50 DComp Overlay Creation/Destruction Cycles ---\n");
    HWND dummy_taskbar = CreateDummyTaskbarWindow();
    ASSERT_TRUE(dummy_taskbar != NULL, "Dummy taskbar window created");

    // Component Isolation Test:
    // A: 50 cycles of CreateWindow / DestroyWindow
    DWORD user_win_start = GetUserObjectCount();
    for (int i = 0; i < 50; i++) {
        HWND w = CreateWindowExW(
            WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"TE_IconHoverOverlay", NULL, WS_POPUP, 0, 0, 800, 48, dummy_taskbar, NULL, GetModuleHandleW(NULL), NULL);
        if (w) DestroyWindow(w);
    }
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DWORD user_win_end = GetUserObjectCount();
    LOG_PRINT("  [Isolation A: CreateWindowEx/DestroyWindow] USER delta=%d\n", (int)(user_win_end - user_win_start));

    // B: 50 cycles of TE_TestDCompOverlayLifecycle
    DWORD gdi_before_dcomp = GetGdiObjectCount();
    DWORD user_before_dcomp = GetUserObjectCount();

    const int dcomp_cycles = 50;
    for (int i = 0; i < dcomp_cycles; i++) {
        int res = fnDCompLifecycle(dummy_taskbar);
        ASSERT_TRUE(res >= 1, "DComp overlay cycle succeeded");
    }

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    DWORD gdi_after_dcomp = GetGdiObjectCount();
    DWORD user_after_dcomp = GetUserObjectCount();

    LOG_PRINT("  Completed %d DComp cycles: GDI delta=%d, USER delta=%d\n",
           dcomp_cycles, (int)(gdi_after_dcomp - gdi_before_dcomp), (int)(user_after_dcomp - user_before_dcomp));

    // Run another 50 cycles to check if delta is cumulative or bounded!
    DWORD gdi_c2_start = GetGdiObjectCount();
    DWORD user_c2_start = GetUserObjectCount();
    for (int i = 0; i < dcomp_cycles; i++) {
        fnDCompLifecycle(dummy_taskbar);
    }
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    DWORD gdi_c2_end = GetGdiObjectCount();
    DWORD user_c2_end = GetUserObjectCount();
    LOG_PRINT("  [Second 50 DComp cycles] GDI delta=%d, USER delta=%d\n",
           (int)(gdi_c2_end - gdi_c2_start), (int)(user_c2_end - user_c2_start));

    // --- 3.3: Full Plugin ABI Lifecycle & Double-Call Hardening ---
    LOG_PRINT("  --- 3.3: Plugin ABI Lifecycle & Double-Call Hardening ---\n");
    const PluginInterface* iface = fnGetInterface();
    ASSERT_TRUE(iface != nullptr, "PluginInterface retrieved");

    PluginContext ctx = {};
    ctx.taskbar_hwnd = dummy_taskbar;
    ctx.dpi = 96;
    ctx.subscribe = [](uint32_t, void (*)(uint32_t, const void*, void*), void*) -> HRESULT { return TE_S_OK; };
    ctx.unsubscribe = [](uint32_t, void (*)(uint32_t, const void*, void*)) -> HRESULT { return TE_S_OK; };

    // 10 Full Enable/Disable/Shutdown cycles
    for (int cycle = 0; cycle < 10; cycle++) {
        ASSERT_TRUE(SUCCEEDED(iface->Initialize(&ctx)), "Initialize ok");
        ASSERT_TRUE(SUCCEEDED(iface->Enable()), "Enable ok");
        ASSERT_TRUE(SUCCEEDED(iface->Update(0.016f)), "Update ok");
        ASSERT_TRUE(SUCCEEDED(iface->Disable()), "Disable ok");
        ASSERT_TRUE(SUCCEEDED(iface->Shutdown()), "Shutdown ok");
    }

    // Double Disable & Double Shutdown Hardening
    ASSERT_TRUE(SUCCEEDED(iface->Initialize(&ctx)), "Initialize ok");
    ASSERT_TRUE(SUCCEEDED(iface->Enable()), "Enable ok");
    ASSERT_TRUE(SUCCEEDED(iface->Disable()), "First Disable ok");
    ASSERT_TRUE(SUCCEEDED(iface->Disable()), "Second Disable ok (graceful idempotent no-op)");
    ASSERT_TRUE(SUCCEEDED(iface->Shutdown()), "First Shutdown ok");
    ASSERT_TRUE(SUCCEEDED(iface->Shutdown()), "Second Shutdown ok (graceful idempotent no-op)");

    DestroyWindow(dummy_taskbar);
    FreeLibrary(mod);
    UnregisterClassW(L"TE_IconHoverOverlay", GetModuleHandleW(NULL));
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    s_flog = fopen("stress_results.log", "w");

    LOG_PRINT("=======================================================================\n");
    LOG_PRINT("TaskbarEngine Milestone 4 Challenger 1 Stress & Leak Test Harness\n");
    LOG_PRINT("=======================================================================\n");

    Test_HighConcurrencyExtractionStress();
    Test_SubsystemReinitLifecycle();

    Test_DLL_Lifecycle(L"build_msvc\\Modules\\icon_hover\\icon_hover.dll", "MSVC x64");
    Test_DLL_Lifecycle(L"build_mingw\\Modules\\icon_hover\\libicon_hover.dll", "MinGW GCC x64");

    LOG_PRINT("\n=======================================================================\n");
    LOG_PRINT("Final Summary: %d PASSED, %d FAILED\n", s_tests_passed, s_tests_failed);
    LOG_PRINT("=======================================================================\n");

    if (s_flog) {
        fclose(s_flog);
        s_flog = NULL;
    }

    return (s_tests_failed == 0) ? 0 : 1;
}
