#include "icon_capture.h"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <psapi.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "psapi.lib")

static DWORD GetGdiCount() {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static SIZE_T GetWorkingSetBytes() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Test 1: IID Validation & Tier 1 Jumbo Shell Extraction Bug
// ----------------------------------------------------------------------------
void TestIIDAndTier1Extraction() {
    printf("\n=== TEST 1: IID Validation & Tier 1 Jumbo Shell Extraction ===\n");
    HRESULT hr_init = TE_IconCaptureInit();
    printf("TE_IconCaptureInit hr=0x%08X\n", hr_init);

    static const IID worker_IID_IImageList = { 0x46EB5926, 0x582E, 0x4017, { 0x9F, 0xDF, 0xE8, 0x99, 0xDE, 0x54, 0x1E, 0xC5 } };
    printf("Windows SDK IID_IImageList : {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
           IID_IImageList.Data1, IID_IImageList.Data2, IID_IImageList.Data3,
           IID_IImageList.Data4[0], IID_IImageList.Data4[1], IID_IImageList.Data4[2], IID_IImageList.Data4[3],
           IID_IImageList.Data4[4], IID_IImageList.Data4[5], IID_IImageList.Data4[6], IID_IImageList.Data4[7]);
    printf("Worker s_IID_IImageList    : {%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\n",
           worker_IID_IImageList.Data1, worker_IID_IImageList.Data2, worker_IID_IImageList.Data3,
           worker_IID_IImageList.Data4[0], worker_IID_IImageList.Data4[1], worker_IID_IImageList.Data4[2], worker_IID_IImageList.Data4[3],
           worker_IID_IImageList.Data4[4], worker_IID_IImageList.Data4[5], worker_IID_IImageList.Data4[6], worker_IID_IImageList.Data4[7]);

    IImageList* test_jumbo = nullptr;
    HRESULT hr_sh = SHGetImageList(SHIL_JUMBO, IID_IImageList, (void**)&test_jumbo);
    printf("SHGetImageList with official IID_IImageList : hr=0x%08X, ptr=%p\n", hr_sh, (void*)test_jumbo);

    IImageList* test_jumbo2 = nullptr;
    HRESULT hr_sh2 = SHGetImageList(SHIL_JUMBO, worker_IID_IImageList, (void**)&test_jumbo2);
    printf("SHGetImageList with worker s_IID_IImageList : hr=0x%08X (E_NOINTERFACE=0x80004002), ptr=%p\n", hr_sh2, (void*)test_jumbo2);

    if (hr_sh2 == E_NOINTERFACE && hr_sh == S_OK) {
        printf("CRITICAL BUG CONFIRMED: Worker s_IID_IImageList has corrupted last 4 bytes! SHIL_JUMBO is permanently NULL!\n");
    }

    if (test_jumbo) test_jumbo->Release();
    if (test_jumbo2) test_jumbo2->Release();

    wchar_t notepad_path[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\notepad.exe", notepad_path, MAX_PATH);

    TE_TaskbarItemInfo item = {};
    item.app_id = notepad_path;

    HBITMAP bmp = NULL;
    BOOL has_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &bmp, &has_alpha);
    printf("Extraction of notepad.exe via TE_IconCaptureExtract: hr=0x%08X (fails because s_jumbo_list is NULL)\n", hr);

    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test 2: Concurrent Cold-Cache Extraction Race Condition (Tier 4 Screen Capture)
// ----------------------------------------------------------------------------
void TestConcurrentColdCacheRaceTier4() {
    printf("\n=== TEST 2: Concurrent Cold-Cache Extraction Race Condition (Tier 4) ===\n");
    TE_IconCaptureInit();

    RECT r = { 50, 50, 98, 98 };
    TE_TaskbarItemInfo item = {};
    item.bounds = r;

    // Verify single-threaded extraction works first
    HBITMAP probe_bmp = NULL;
    BOOL probe_alpha = FALSE;
    HRESULT hr_probe = TE_IconCaptureExtract(&item, &probe_bmp, &probe_alpha);
    printf("Tier 4 probe extract result: hr=0x%08X, bmp=%p, alpha=%d\n", hr_probe, (void*)probe_bmp, probe_alpha);

    // Reset cache so all threads race on a cold cache
    TE_IconCaptureClearCache();

    DWORD gdi_before = GetGdiCount();
    printf("GDI baseline before cold-cache concurrent extraction: %u\n", gdi_before);

    const int THREAD_COUNT = 8;
    const int CALLS_PER_THREAD = 10;
    std::atomic<bool> start_signal{false};
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    std::vector<std::thread> threads;
    std::vector<HBITMAP> results(THREAD_COUNT * CALLS_PER_THREAD, NULL);

    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([t, THREAD_COUNT, CALLS_PER_THREAD, &start_signal, &success_count, &fail_count, &results, r]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!start_signal.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            TE_TaskbarItemInfo it = {};
            it.bounds = r;

            for (int i = 0; i < CALLS_PER_THREAD; i++) {
                HBITMAP bmp = NULL;
                BOOL has_alpha = FALSE;
                HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &has_alpha);
                if (SUCCEEDED(hr) && bmp) {
                    results[t * CALLS_PER_THREAD + i] = bmp;
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    fail_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    printf("Signaling %d threads to simultaneously extract same cold item...\n", THREAD_COUNT);
    start_signal.store(true, std::memory_order_release);

    for (auto& th : threads) {
        th.join();
    }

    DWORD gdi_after_threads = GetGdiCount();
    printf("Multithreaded execution complete: %d successes, %d failures\n",
           success_count.load(), fail_count.load());
    printf("GDI objects after concurrent extraction: %u (baseline was %u, delta: +%d)\n",
           gdi_after_threads, gdi_before, (int)(gdi_after_threads - gdi_before));

    // Check how many distinct handles were handed out
    std::vector<HBITMAP> unique_handles;
    for (int i = 0; i < THREAD_COUNT * CALLS_PER_THREAD; i++) {
        HBITMAP b = results[i];
        if (b && std::find(unique_handles.begin(), unique_handles.end(), b) == unique_handles.end()) {
            unique_handles.push_back(b);
        }
    }
    printf("Distinct HBITMAP handles returned to callers: %zu (expected 1 if deduplicated properly!)\n",
           unique_handles.size());

    // Now call TE_IconCaptureClearCache
    TE_IconCaptureClearCache();
    DWORD gdi_after_clear = GetGdiCount();
    printf("GDI objects after TE_IconCaptureClearCache(): %u (baseline was %u, delta from baseline: +%d)\n",
           gdi_after_clear, gdi_before, (int)(gdi_after_clear - gdi_before));

    if (gdi_after_clear > gdi_before) {
        printf("CRITICAL BUG CONFIRMED: Leaked %d GDI bitmap handles due to uncoordinated (*s_bitmap_cache)[key] = hbmp overwrites!\n",
               (int)(gdi_after_clear - gdi_before));
    } else {
        printf("PASS: No GDI handles leaked after concurrent extraction & clear.\n");
    }

    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test 3: Concurrent Cold-Cache Extraction Race Condition (Tier 3 HWND with Active Pump)
// ----------------------------------------------------------------------------
void TestConcurrentColdCacheRaceTier3() {
    printf("\n=== TEST 3: Concurrent Cold-Cache Extraction Race Condition (Tier 3 HWND) ===\n");
    TE_IconCaptureInit();

    std::atomic<bool> wnd_ready{false};
    std::atomic<bool> wnd_exit{false};
    HWND hwnd_test = NULL;

    std::thread wnd_thread([&]() {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandle(NULL);
        wc.lpszClassName = L"TE_StressTestWndClass_Active";
        RegisterClassW(&wc);

        hwnd_test = CreateWindowW(wc.lpszClassName, L"TE_TestWindowActive",
                                  WS_OVERLAPPEDWINDOW,
                                  100, 100, 200, 200, NULL, NULL, wc.hInstance, NULL);
        HICON hicon_app = LoadIcon(NULL, IDI_APPLICATION);
        SendMessageW(hwnd_test, WM_SETICON, ICON_BIG, (LPARAM)hicon_app);
        wnd_ready.store(true);

        MSG msg;
        while (!wnd_exit.load()) {
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (hwnd_test) {
            DestroyWindow(hwnd_test);
            hwnd_test = NULL;
        }
    });

    while (!wnd_ready.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Warm-up extract
    TE_TaskbarItemInfo probe_item = {};
    probe_item.hwnd = hwnd_test;
    HBITMAP probe_bmp = NULL;
    BOOL probe_alpha = FALSE;
    HRESULT hr_probe = TE_IconCaptureExtract(&probe_item, &probe_bmp, &probe_alpha);
    printf("Tier 3 active window probe extract: hr=0x%08X, bmp=%p, alpha=%d\n",
           hr_probe, (void*)probe_bmp, probe_alpha);

    TE_IconCaptureClearCache();

    DWORD gdi_before = GetGdiCount();
    printf("GDI baseline before cold-cache HWND concurrent extraction: %u\n", gdi_before);

    const int THREAD_COUNT = 8;
    const int CALLS_PER_THREAD = 10;
    std::atomic<bool> start_signal{false};
    std::atomic<int> success_count{0};
    std::atomic<int> fail_count{0};

    std::vector<std::thread> threads;
    std::vector<HBITMAP> results(THREAD_COUNT * CALLS_PER_THREAD, NULL);

    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([t, THREAD_COUNT, CALLS_PER_THREAD, &start_signal, &success_count, &fail_count, &results, hwnd_test]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!start_signal.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            TE_TaskbarItemInfo it = {};
            it.hwnd = hwnd_test;

            for (int i = 0; i < CALLS_PER_THREAD; i++) {
                HBITMAP bmp = NULL;
                BOOL has_alpha = FALSE;
                HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &has_alpha);
                if (SUCCEEDED(hr) && bmp) {
                    results[t * CALLS_PER_THREAD + i] = bmp;
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    fail_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    printf("Signaling %d threads to concurrently extract Tier 3 from cold cache...\n", THREAD_COUNT);
    start_signal.store(true, std::memory_order_release);

    for (auto& th : threads) {
        th.join();
    }

    DWORD gdi_after_threads = GetGdiCount();
    printf("Multithreaded execution complete: %d successes, %d failures\n",
           success_count.load(), fail_count.load());
    printf("GDI objects after concurrent extraction: %u (baseline was %u, delta: +%d)\n",
           gdi_after_threads, gdi_before, (int)(gdi_after_threads - gdi_before));

    std::vector<HBITMAP> unique_handles;
    for (int i = 0; i < THREAD_COUNT * CALLS_PER_THREAD; i++) {
        HBITMAP b = results[i];
        if (b && std::find(unique_handles.begin(), unique_handles.end(), b) == unique_handles.end()) {
            unique_handles.push_back(b);
        }
    }
    printf("Distinct HBITMAP handles returned to callers: %zu (expected 1 if deduplicated properly!)\n",
           unique_handles.size());

    TE_IconCaptureClearCache();
    DWORD gdi_after_clear = GetGdiCount();
    printf("GDI objects after TE_IconCaptureClearCache(): %u (baseline was %u, delta from baseline: +%d)\n",
           gdi_after_clear, gdi_before, (int)(gdi_after_clear - gdi_before));

    if (gdi_after_clear > gdi_before) {
        printf("CRITICAL BUG CONFIRMED: Leaked %d GDI bitmap handles during cold-cache HWND concurrent extraction!\n",
               (int)(gdi_after_clear - gdi_before));
    } else {
        printf("PASS: No GDI handles leaked after concurrent HWND extraction & clear.\n");
    }

    wnd_exit.store(true);
    wnd_thread.join();
    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test 4: Repeated Extraction & Eviction Lifecycle (Warm Cache No Leaks)
// ----------------------------------------------------------------------------
void TestRepeatedExtractionLifecycle() {
    printf("\n=== TEST 4: Repeated Extraction & Eviction Cycles ===\n");
    TE_IconCaptureInit();

    RECT r = { 10, 10, 58, 58 };
    TE_TaskbarItemInfo item = {};
    item.bounds = r;

    DWORD gdi_baseline = GetGdiCount();
    printf("GDI baseline before 50 repeated clear-and-extract cycles: %u\n", gdi_baseline);

    for (int cycle = 0; cycle < 50; cycle++) {
        HBITMAP b = NULL;
        BOOL a = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&item, &b, &a);
        if (FAILED(hr) || !b) {
            printf("Extraction failed at cycle %d\n", cycle);
        }
        TE_IconCaptureClearCache();
    }

    DWORD gdi_after = GetGdiCount();
    printf("GDI count after 50 cycles: %u (delta from baseline: %d)\n",
           gdi_after, (int)(gdi_after - gdi_baseline));

    if (gdi_after == gdi_baseline) {
        printf("PASS: Perfect GDI lifecycle reclamation across sequential clear-and-extract cycles.\n");
    } else {
        printf("FAIL: Sequential clear-and-extract leaked %d GDI objects!\n", (int)(gdi_after - gdi_baseline));
    }

    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test 5: Invalidate Use-After-Free / Handle Invalidation Race
// ----------------------------------------------------------------------------
void TestInvalidateCallerRace() {
    printf("\n=== TEST 5: Cache Invalidation vs Caller Handle Lifetime ===\n");
    TE_IconCaptureInit();

    RECT r = { 20, 20, 68, 68 };
    TE_TaskbarItemInfo item = {};
    item.bounds = r;

    HBITMAP bmp = NULL;
    BOOL alpha = FALSE;
    TE_IconCaptureExtract(&item, &bmp, &alpha);

    // Verify bmp is valid GDI handle
    BITMAP bm = {};
    int res1 = GetObject(bmp, sizeof(bm), &bm);
    printf("Before Invalidate: GetObject returned %d (width=%d, height=%d)\n", res1, bm.bmWidth, bm.bmHeight);

    // Now call TE_IconCaptureInvalidate()
    TE_IconCaptureInvalidate();

    // Verify bmp is now destroyed (DeleteObject was called)
    BITMAP bm2 = {};
    int res2 = GetObject(bmp, sizeof(bm2), &bm2);
    printf("After Invalidate: GetObject returned %d (0 indicates handle is deleted/invalid)\n", res2);

    if (res2 == 0) {
        printf("NOTE: Caller HBITMAP is invalidated immediately upon Invalidate/ClearCache.\n");
        printf("      Callers must NOT retain HBITMAP handles across frame boundaries or invalidations.\n");
    }

    TE_IconCaptureShutdown();
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("TaskbarEngine Milestone 1 Icon Capture Empirical Stress Test\n");
    printf("=================================================================\n");

    TestIIDAndTier1Extraction();
    TestConcurrentColdCacheRaceTier4();
    TestConcurrentColdCacheRaceTier3();
    TestRepeatedExtractionLifecycle();
    TestInvalidateCallerRace();

    printf("\n=================================================================\n");
    printf("Empirical Stress Test Completed.\n");
    printf("=================================================================\n");

    CoUninitialize();
    return 0;
}
