#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <commctrl.h>
#include <sdk/te_types.h>
#include <sdk/te_ipc.h>
#include <sdk/te_plugin.h>
#include <sdk/te_events.h>
#include "icon_capture.h"
#include "uia_discovery.h"
#include "frame_loop.h"
#include "icon_hover_internal.h"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

static DWORD GetCurrentGdiHandles(void) {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

/* ── SYS-018 & PERF-403: Bounded LRU Cache & GDI Handle Count Stability ── */

TEST_CASE("Phase 3 - GDI Handle Count Stability Over 1000 Events (SYS-018 & PERF-403)", "[phase3][sys_018][perf_403]") {
    REQUIRE(TE_IconCaptureInit() == TE_S_OK);
    TE_IconCaptureInvalidate();

    DWORD gdi_baseline = GetCurrentGdiHandles();
    uint32_t initial_count = TE_IconCaptureGetCacheCount();
    REQUIRE(initial_count == 0);

    /* Generate 1000 distinct icon capture requests */
    const int TEST_EVENTS = 1000;
    for (int i = 0; i < TEST_EVENTS; i++) {
        RECT r = { i % 200, 0, (i % 200) + 16, 16 };
        wchar_t app_key[64];
        swprintf_s(app_key, L"TestApp_%d", i);

        HBITMAP bmp = NULL;
        /* Fallback creates snapshot bitmap */
        HRESULT hr = TE_IconCaptureGetBitmapWithBounds(app_key, -1, &r, &bmp);
        REQUIRE(SUCCEEDED(hr));
        REQUIRE(bmp != NULL);

        /* Bounded LRU cache must never exceed 64 entries */
        uint32_t current_cache_count = TE_IconCaptureGetCacheCount();
        REQUIRE(current_cache_count <= 64);
    }

    /* Cache size must strictly be at max capacity (64) */
    REQUIRE(TE_IconCaptureGetCacheCount() == 64);

    /* GDI handle delta must be bounded to <= 64 (never 1000) */
    DWORD gdi_peak = GetCurrentGdiHandles();
    DWORD gdi_delta = (gdi_peak >= gdi_baseline) ? (gdi_peak - gdi_baseline) : 0;
    REQUIRE(gdi_delta <= 64);

    /* Full invalidation must immediately delete all 64 cached GDI handles */
    TE_IconCaptureInvalidate();
    REQUIRE(TE_IconCaptureGetCacheCount() == 0);

    DWORD gdi_after = GetCurrentGdiHandles();
    REQUIRE(gdi_after <= gdi_baseline + 2); // Leeway for OS internal handle recycling
}

/* ── SYS-018 & PERF-403: Fine-Grained Cache Invalidation ───────────────── */

TEST_CASE("Phase 3 - Fine-Grained Cache Invalidation Leaves Unaffected Bitmaps Intact", "[phase3][sys_018][fine_grained]") {
    REQUIRE(TE_IconCaptureInit() == TE_S_OK);
    TE_IconCaptureInvalidate();

    RECT r1 = { 0, 0, 16, 16 };
    RECT r2 = { 20, 0, 36, 16 };
    RECT r3 = { 40, 0, 56, 16 };

    HBITMAP bmp1 = NULL, bmp2 = NULL, bmp3 = NULL;
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapWithBounds(L"AppAlpha", -1, &r1, &bmp1)));
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapWithBounds(L"AppBeta", -1, &r2, &bmp2)));
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapWithBounds(L"AppGamma", -1, &r3, &bmp3)));

    REQUIRE(TE_IconCaptureGetCacheCount() == 3);

    /* Test 1: Invalidate "App" must NOT evict "AppAlpha" (no false substring match) */
    TE_IconCaptureInvalidateApp(L"App");
    REQUIRE(TE_IconCaptureGetCacheCount() == 3);

    /* Test 2: Invalidate ONLY AppBeta */
    TE_IconCaptureInvalidateApp(L"AppBeta");
    REQUIRE(TE_IconCaptureGetCacheCount() == 2);

    /* AppAlpha and AppGamma must still hit the cache without re-extracting */
    HBITMAP hit1 = NULL, hit3 = NULL;
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapWithBounds(L"AppAlpha", -1, NULL, &hit1)));
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapWithBounds(L"AppGamma", -1, NULL, &hit3)));
    REQUIRE(hit1 == bmp1);
    REQUIRE(hit3 == bmp3);

    /* Test 3: Fine-grained HWND invalidation */
    HWND wnd_a = CreateWindowW(L"STATIC", L"WndA", WS_POPUP, 0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    HWND wnd_b = CreateWindowW(L"STATIC", L"WndB", WS_POPUP, 0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(wnd_a != NULL);
    REQUIRE(wnd_b != NULL);

    RECT r4 = { 60, 0, 76, 16 };
    RECT r5 = { 80, 0, 96, 16 };
    HBITMAP bmp4 = NULL, bmp5 = NULL;

    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapEx(NULL, -1, &r4, wnd_a, 0, &bmp4)));
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapEx(NULL, -1, &r5, wnd_b, 0, &bmp5)));
    REQUIRE(TE_IconCaptureGetCacheCount() == 4); // 2 previous + 2 new

    /* Invalidate wnd_a: only wnd_a is evicted; wnd_b, AppAlpha, AppGamma remain */
    TE_IconCaptureInvalidateHwnd(wnd_a);
    REQUIRE(TE_IconCaptureGetCacheCount() == 3);

    /* Invalidate wnd_b */
    TE_IconCaptureInvalidateHwnd(wnd_b);
    REQUIRE(TE_IconCaptureGetCacheCount() == 2);

    DestroyWindow(wnd_a);
    DestroyWindow(wnd_b);

    /* Test 4: Fine-grained PID invalidation */
    RECT r6 = { 100, 0, 116, 16 };
    RECT r7 = { 120, 0, 136, 16 };
    HBITMAP bmp6 = NULL, bmp7 = NULL;
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapEx(NULL, -1, &r6, NULL, 77771, &bmp6)));
    REQUIRE(SUCCEEDED(TE_IconCaptureGetBitmapEx(NULL, -1, &r7, NULL, 77772, &bmp7)));
    REQUIRE(TE_IconCaptureGetCacheCount() == 4);

    TE_IconCaptureInvalidatePid(77771);
    REQUIRE(TE_IconCaptureGetCacheCount() == 3);

    TE_IconCaptureInvalidatePid(77772);
    REQUIRE(TE_IconCaptureGetCacheCount() == 2);

    TE_IconCaptureInvalidate();
    REQUIRE(TE_IconCaptureGetCacheCount() == 0);
}

/* ── SYS-024: Network/Disk Blocking Prevention in AppID Queries ────────── */

TEST_CASE("Phase 3 - Network and Disk Blocking Prevention in AppID File Queries (SYS-024)", "[phase3][sys_024]") {
    REQUIRE(TE_IconCaptureInit() == TE_S_OK);

    /* 1. Arbitrary non-path UIA AppIDs must not trigger SHGetFileInfoW */
    HBITMAP bmp = NULL;
    HRESULT hr_explorer = TE_IconCaptureGetBitmap(L"Microsoft.Windows.Explorer", -1, &bmp);
    REQUIRE(hr_explorer != TE_S_OK);

    HRESULT hr_uwp = TE_IconCaptureGetBitmap(L"AppID:Microsoft.Windows.ShellExperienceHost_cw5n1h2txyewy!App", -1, &bmp);
    REQUIRE(hr_uwp != TE_S_OK);

    /* 2. UNC network paths must be rejected immediately to prevent network namespace blocking */
    HRESULT hr_unc = TE_IconCaptureGetBitmap(L"\\\\192.168.1.1\\share\\app.exe", -1, &bmp);
    REQUIRE(hr_unc != TE_S_OK);

    HRESULT hr_unc_fwd = TE_IconCaptureGetBitmap(L"//192.168.1.1/share/app.exe", -1, &bmp);
    REQUIRE(hr_unc_fwd != TE_S_OK);

    /* 3. Non-existent local files must not succeed or block */
    HRESULT hr_nonexist = TE_IconCaptureGetBitmap(L"C:\\DoesNotExist_123456789\\bogus.exe", -1, &bmp);
    REQUIRE(hr_nonexist != TE_S_OK);

    /* 4. Oversized path exceeding MAX_PATH */
    std::wstring long_path = L"C:\\";
    long_path.append(300, L'a');
    long_path.append(L".exe");
    HRESULT hr_long = TE_IconCaptureGetBitmap(long_path.c_str(), -1, &bmp);
    REQUIRE(hr_long != TE_S_OK);

    /* 5. Valid existing local file system path with drive prefix */
    wchar_t notepad_path[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\notepad.exe", notepad_path, MAX_PATH);

    DWORD attrs = GetFileAttributesW(notepad_path);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        // If notepad.exe exists, check that it queries or falls back cleanly
        HRESULT hr_local = TE_IconCaptureGetBitmap(notepad_path, -1, &bmp);
        REQUIRE((hr_local == TE_S_OK || hr_local == TE_E_FAIL));
    }

    TE_IconCaptureInvalidate();
}

/* ── SYS-007 & PERF-404: COM Apartment Mode Collision Prevention ──────── */

TEST_CASE("Phase 3 - COM Apartment Mode Invariant (SYS-007 & PERF-404)", "[phase3][sys_007][perf_404]") {
    std::atomic<bool> mta_verified{false};
    std::atomic<HRESULT> discovery_hr{S_OK};

    /* Spawn dedicated worker thread initialized strictly as MTA */
    std::thread worker([&]() {
        HRESULT hr_init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        REQUIRE((hr_init == S_OK || hr_init == S_FALSE));

        TE_IconElementCache cache = {0};
        // Call UIA discovery on worker thread
        discovery_hr = TE_UiaDiscoverIcons(GetDesktopWindow(), &cache);

        // Discovery routine must never fail with RPC_E_CHANGED_MODE
        REQUIRE(discovery_hr.load() != RPC_E_CHANGED_MODE);

        // Verify COM apartment is still MTA after routine completes
        HRESULT hr_check = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        REQUIRE(hr_check == S_FALSE); // Already MTA on this thread
        CoUninitialize();

        if (SUCCEEDED(hr_init)) {
            CoUninitialize();
        }
        mta_verified = true;
    });

    worker.join();
    REQUIRE(mta_verified.load() == true);
}

/* ── SYS-006 & PERF-401: Zero UI Thread Blocking During UIA Discovery ──── */

TEST_CASE("Phase 3 - Zero UI Thread Blocking During UIA Discovery (SYS-006 & PERF-401)", "[phase3][sys_006][perf_401]") {
    const PluginInterface* plugin = TE_IconHoverGetPluginInterface();
    REQUIRE(plugin != nullptr);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"TE_Phase3TestTaskbar";
    RegisterClassW(&wc);
    HWND test_tb = CreateWindowW(L"TE_Phase3TestTaskbar", L"", WS_POPUP, 0, 0, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
    REQUIRE(test_tb != NULL);

    PluginContext ctx = {};
    ctx.struct_size = sizeof(PluginContext);
    ctx.taskbar_hwnd = test_tb;
    ctx.subscribe = [](uint32_t, void (*)(uint32_t, const void*, void*), void*) -> HRESULT { return S_OK; };
    ctx.unsubscribe = [](uint32_t, void (*)(uint32_t, const void*, void*)) -> HRESULT { return S_OK; };

    plugin->Initialize(&ctx);
    plugin->Enable();

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);

    /* Trigger asynchronous discovery: must return immediately (< 10 ms) */
    QueryPerformanceCounter(&start);
    TE_IconHoverTriggerAsyncDiscovery();
    QueryPerformanceCounter(&end);

    double elapsed_ms = (double)(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
    REQUIRE(elapsed_ms < 10.0);

    /* Multi-threaded burst trigger: 10 threads calling trigger concurrently must not deadlock or leak */
    std::vector<std::thread> triggers;
    for (int t = 0; t < 10; t++) {
        triggers.emplace_back([]() {
            TE_IconHoverTriggerAsyncDiscovery();
        });
    }
    for (auto& th : triggers) {
        th.join();
    }

    plugin->Disable();
    plugin->Shutdown();

    DestroyWindow(test_tb);
    UnregisterClassW(L"TE_Phase3TestTaskbar", GetModuleHandleW(NULL));
}

/* ── PERF-402: Animation Stutter via Synchronous Shell Hook Decoupling ─── */

TEST_CASE("Phase 3 - Animation Decoupling & Shell Hook Burst Test (PERF-402)", "[phase3][perf_402]") {
    /* 1. Start active frame loop */
    REQUIRE(SUCCEEDED(TE_FrameLoopStart()));
    REQUIRE(TE_FrameLoopIsActive() == 1);

    /* 2. Rapid burst of 50 shell hook events */
    for (int i = 0; i < 50; i++) {
        HWND dummy_app = (HWND)(INT_PTR)(0x10000 + i * 4);
        TE_IconCaptureInvalidateHwnd(dummy_app);
    }

    /* Frame loop must remain actively running throughout the burst without stopping */
    REQUIRE(TE_FrameLoopIsActive() == 1);

    /* Simulate debounced discovery trigger */
    TE_IconHoverTriggerAsyncDiscovery();

    /* Frame loop must NOT be stopped by discovery */
    REQUIRE(TE_FrameLoopIsActive() == 1);

    /* Clean up */
    TE_FrameLoopStop();
    REQUIRE(TE_FrameLoopIsActive() == 0);
}
