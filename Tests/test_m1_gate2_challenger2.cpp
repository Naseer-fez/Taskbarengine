#include "icon_capture.h"
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")

static DWORD GetGdiCount() {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

// Warm up Win32 Shell & ComCtl subsystem once per process so internal OS static
// caches (which allocate ~40 GDI objects inside shell32/comctl32) do not taint
// application-level leak tracking.
static void WarmupShellSubsystem() {
    TE_IconCaptureInit();
    HBITMAP dummy = NULL;
    TE_IconCaptureGetBitmap(NULL, 0, &dummy);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test 1: Fallback Queries (icon_index >= 0) Caching, Deduplication, and Flush
// ----------------------------------------------------------------------------
bool TestFallbackIconIndexCaching() {
    printf("\n=== TEST 1: Fallback icon_index >= 0 Caching & Flush ===\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI count (post-warmup): %u\n", gdi_baseline);

    // Query icon_index = 0
    HBITMAP bmp0_a = NULL;
    HBITMAP bmp0_b = NULL;
    HRESULT hr1 = TE_IconCaptureGetBitmap(NULL, 0, &bmp0_a);
    HRESULT hr2 = TE_IconCaptureGetBitmap(NULL, 0, &bmp0_b);

    printf("  icon_index=0: hr1=0x%08X, bmp0_a=%p; hr2=0x%08X, bmp0_b=%p\n",
           hr1, (void*)bmp0_a, hr2, (void*)bmp0_b);

    bool dedup0 = (SUCCEEDED(hr1) && SUCCEEDED(hr2) && bmp0_a != NULL && bmp0_a == bmp0_b);
    printf("  icon_index=0 deduplicated: %s\n", dedup0 ? "YES" : "NO");

    // Query icon_index = 1
    HBITMAP bmp1_a = NULL;
    HBITMAP bmp1_b = NULL;
    HRESULT hr3 = TE_IconCaptureGetBitmap(NULL, 1, &bmp1_a);
    HRESULT hr4 = TE_IconCaptureGetBitmap(NULL, 1, &bmp1_b);

    printf("  icon_index=1: hr3=0x%08X, bmp1_a=%p; hr4=0x%08X, bmp1_b=%p\n",
           hr3, (void*)bmp1_a, hr4, (void*)bmp1_b);

    bool dedup1 = (SUCCEEDED(hr3) && SUCCEEDED(hr4) && bmp1_a != NULL && bmp1_a == bmp1_b);
    printf("  icon_index=1 deduplicated: %s\n", dedup1 ? "YES" : "NO");

    DWORD gdi_cached = GetGdiCount();
    printf("  GDI count with 2 cached indices: %u (delta: +%d)\n",
           gdi_cached, (int)(gdi_cached - gdi_baseline));

    // Clear cache
    TE_IconCaptureClearCache();
    DWORD gdi_cleared = GetGdiCount();
    printf("  GDI count after TE_IconCaptureClearCache(): %u (delta from baseline: +%d)\n",
           gdi_cleared, (int)(gdi_cleared - gdi_baseline));

    // Check that handles were truly deleted in the OS
    BITMAP bm = {};
    int valid0 = (bmp0_a && GetObject(bmp0_a, sizeof(bm), &bm) != 0) ? 1 : 0;
    int valid1 = (bmp1_a && GetObject(bmp1_a, sizeof(bm), &bm) != 0) ? 1 : 0;
    printf("  Handles still valid after clear: bmp0=%d, bmp1=%d (expected 0, 0)\n", valid0, valid1);

    TE_IconCaptureShutdown();
    DWORD gdi_shutdown = GetGdiCount();
    printf("  GDI count after TE_IconCaptureShutdown(): %u (delta from baseline: +%d)\n",
           gdi_shutdown, (int)(gdi_shutdown - gdi_baseline));

    bool pass = dedup0 && dedup1 && (gdi_cached == gdi_baseline + 2) &&
                (gdi_cleared == gdi_baseline) && (gdi_shutdown == gdi_baseline) &&
                valid0 == 0 && valid1 == 0;

    if (pass) {
        printf("PASS: Fallback icon_index >= 0 properly cached, deduplicated, and cleanly flushed (delta: +0).\n");
    } else {
        printf("FAIL: Fallback icon_index caching or flush failed!\n");
    }
    return pass;
}

// ----------------------------------------------------------------------------
// Test 2: Multithreaded Cold-Cache Concurrency (16 Threads, 100 extractions each)
// ----------------------------------------------------------------------------
bool TestMultithreadedColdCacheContention() {
    printf("\n=== TEST 2: Multithreaded Cold-Cache Concurrency (16 Threads) ===\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI count: %u\n", gdi_baseline);

    wchar_t notepad_path[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\notepad.exe", notepad_path, MAX_PATH);

    const int THREAD_COUNT = 16;
    const int RUNS_PER_THREAD = 100;
    std::atomic<bool> gate{false};
    std::atomic<int> success_count{0};
    std::vector<std::thread> workers;
    std::vector<HBITMAP> results(THREAD_COUNT * RUNS_PER_THREAD, NULL);

    for (int t = 0; t < THREAD_COUNT; t++) {
        workers.emplace_back([t, RUNS_PER_THREAD, &gate, &success_count, &results, notepad_path]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!gate.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            TE_TaskbarItemInfo item = {};
            item.app_id = notepad_path;
            item.icon_index = -1;

            for (int i = 0; i < RUNS_PER_THREAD; i++) {
                HBITMAP bmp = NULL;
                BOOL alpha = FALSE;
                if (SUCCEEDED(TE_IconCaptureExtract(&item, &bmp, &alpha)) && bmp) {
                    results[t * RUNS_PER_THREAD + i] = bmp;
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    gate.store(true, std::memory_order_release);
    for (auto& th : workers) th.join();

    DWORD gdi_during = GetGdiCount();
    printf("  1600 extractions complete: successes=%d. Active GDI count: %u (delta: +%d)\n",
           success_count.load(), gdi_during, (int)(gdi_during - gdi_baseline));

    // Verify deduplication: how many unique handles?
    std::vector<HBITMAP> unique_handles;
    for (HBITMAP b : results) {
        if (b && std::find(unique_handles.begin(), unique_handles.end(), b) == unique_handles.end()) {
            unique_handles.push_back(b);
        }
    }
    printf("  Unique handles distributed to 16 threads: %zu (expected 1)\n", unique_handles.size());

    // Clear cache
    TE_IconCaptureClearCache();
    DWORD gdi_cleared = GetGdiCount();
    printf("  GDI count after TE_IconCaptureClearCache(): %u (delta from baseline: +%d)\n",
           gdi_cleared, (int)(gdi_cleared - gdi_baseline));

    TE_IconCaptureShutdown();
    DWORD gdi_shutdown = GetGdiCount();
    printf("  GDI count after TE_IconCaptureShutdown(): %u (delta from baseline: +%d)\n",
           gdi_shutdown, (int)(gdi_shutdown - gdi_baseline));

    bool pass = (success_count.load() == THREAD_COUNT * RUNS_PER_THREAD) &&
                (unique_handles.size() == 1) &&
                (gdi_during == gdi_baseline + 1) &&
                (gdi_cleared == gdi_baseline) &&
                (gdi_shutdown == gdi_baseline);

    if (pass) {
        printf("PASS: Multithreaded cold-cache race deduplicated to 1 handle with 0 leaks.\n");
    } else {
        printf("FAIL: Multithreaded cold-cache concurrency leaked or failed deduplication!\n");
    }
    return pass;
}

// ----------------------------------------------------------------------------
// Test 3: Multiple Distinct Keys Concurrently Extracted Across 16 Threads
// ----------------------------------------------------------------------------
bool TestMultiKeyConcurrentExtractions() {
    printf("\n=== TEST 3: Multi-Key Concurrent Extractions (16 Threads, 8 Distinct Keys) ===\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI count: %u\n", gdi_baseline);

    const int THREAD_COUNT = 16;
    const int RUNS = 50;
    std::atomic<bool> gate{false};
    std::atomic<int> success_count{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < THREAD_COUNT; t++) {
        workers.emplace_back([t, RUNS, &gate, &success_count]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!gate.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            int key_idx = t % 8;
            for (int i = 0; i < RUNS; i++) {
                TE_TaskbarItemInfo item = {};
                item.icon_index = key_idx; // test fallback icon index caching 0..7

                HBITMAP bmp = NULL;
                BOOL alpha = FALSE;
                if (SUCCEEDED(TE_IconCaptureExtract(&item, &bmp, &alpha)) && bmp) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    gate.store(true, std::memory_order_release);
    for (auto& th : workers) th.join();

    DWORD gdi_during = GetGdiCount();
    printf("  Completed %d extractions across 8 keys. Active GDI count: %u (delta: +%d)\n",
           success_count.load(), gdi_during, (int)(gdi_during - gdi_baseline));

    TE_IconCaptureClearCache();
    DWORD gdi_cleared = GetGdiCount();
    printf("  GDI count after ClearCache: %u (delta from baseline: +%d)\n",
           gdi_cleared, (int)(gdi_cleared - gdi_baseline));

    TE_IconCaptureShutdown();
    DWORD gdi_shutdown = GetGdiCount();
    printf("  GDI count after Shutdown: %u (delta from baseline: +%d)\n",
           gdi_shutdown, (int)(gdi_shutdown - gdi_baseline));

    bool pass = (success_count.load() == THREAD_COUNT * RUNS) &&
                (gdi_during == gdi_baseline + 8) &&
                (gdi_cleared == gdi_baseline) &&
                (gdi_shutdown == gdi_baseline);

    if (pass) {
        printf("PASS: Multi-key concurrent extractions cleanly reclaimed (delta: +0).\n");
    } else {
        printf("FAIL: Multi-key concurrent extractions leaked handles!\n");
    }
    return pass;
}

// ----------------------------------------------------------------------------
// Test 4: Adversarial Bounds and Arguments
// ----------------------------------------------------------------------------
bool TestAdversarialBoundsAndArgs() {
    printf("\n=== TEST 4: Adversarial Bounds and Arguments ===\n");
    TE_IconCaptureInit();

    DWORD gdi_baseline = GetGdiCount();

    // 1. NULL item
    HBITMAP bmp = NULL;
    BOOL alpha = FALSE;
    HRESULT hr_null = TE_IconCaptureExtract(NULL, &bmp, &alpha);
    printf("  NULL item: hr=0x%08X (expected TE_E_INVALIDARG 0x80070057)\n", hr_null);

    // 2. NULL out_bitmap
    TE_TaskbarItemInfo valid_item = {};
    valid_item.icon_index = -1;
    valid_item.bounds = { 0, 0, 32, 32 };
    HRESULT hr_out_null = TE_IconCaptureExtract(&valid_item, NULL, &alpha);
    printf("  NULL out_bitmap: hr=0x%08X (expected TE_E_INVALIDARG 0x80070057)\n", hr_out_null);

    // 3. Inverted bounds right < left
    TE_TaskbarItemInfo inv_item = {};
    inv_item.icon_index = -1;
    inv_item.bounds.left = 100;
    inv_item.bounds.right = 50;
    inv_item.bounds.top = 100;
    inv_item.bounds.bottom = 150;
    HRESULT hr_inv = TE_IconCaptureExtract(&inv_item, &bmp, &alpha);
    printf("  Inverted bounds right < left: hr=0x%08X (expected TE_E_INVALIDARG 0x80070057)\n", hr_inv);

    // 4. Inverted bounds bottom < top
    inv_item.bounds.right = 150;
    inv_item.bounds.bottom = 50;
    HRESULT hr_inv_b = TE_IconCaptureExtract(&inv_item, &bmp, &alpha);
    printf("  Inverted bounds bottom < top: hr=0x%08X (expected TE_E_INVALIDARG 0x80070057)\n", hr_inv_b);

    // 5. Non-existent path
    TE_TaskbarItemInfo nonexist_item = {};
    nonexist_item.icon_index = -1;
    nonexist_item.app_id = L"C:\\DoesNotExist_Fake_Path_12345.exe";
    HRESULT hr_ne = TE_IconCaptureExtract(&nonexist_item, &bmp, &alpha);
    printf("  Non-existent path: hr=0x%08X (expected TE_E_FAIL 0x80004005)\n", hr_ne);

    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();

    DWORD gdi_final = GetGdiCount();
    printf("  GDI delta after edge cases: +%d\n", (int)(gdi_final - gdi_baseline));

    bool ok = (hr_null == TE_E_INVALIDARG) &&
              (hr_out_null == TE_E_INVALIDARG) &&
              (hr_inv == TE_E_INVALIDARG) &&
              (hr_inv_b == TE_E_INVALIDARG) &&
              (hr_ne == TE_E_FAIL) &&
              (gdi_final == gdi_baseline);

    if (ok) {
        printf("PASS: All edge cases and inverted bounds correctly rejected.\n");
    } else {
        printf("FAIL: Edge cases not handled as expected!\n");
    }
    return ok;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("TaskbarEngine Milestone 1 Gate 2: Challenger 2 Empirical Test\n");
    printf("=================================================================\n");

    WarmupShellSubsystem();

    bool t1 = TestFallbackIconIndexCaching();
    bool t2 = TestMultithreadedColdCacheContention();
    bool t3 = TestMultiKeyConcurrentExtractions();
    bool t4 = TestAdversarialBoundsAndArgs();

    printf("\n=================================================================\n");
    printf("CHALLENGER 2 SUMMARY:\n");
    printf("  Test 1 (Fallback icon_index Cache & Flush): %s\n", t1 ? "PASS" : "FAIL");
    printf("  Test 2 (16-Thread Cold Concurrency Dedup) : %s\n", t2 ? "PASS" : "FAIL");
    printf("  Test 3 (16-Thread Multi-Key Reclamation)  : %s\n", t3 ? "PASS" : "FAIL");
    printf("  Test 4 (Adversarial Bounds & Arg Safety) : %s\n", t4 ? "PASS" : "FAIL");

    bool overall = t1 && t2 && t3 && t4;
    printf("OVERALL VERDICT: %s\n", overall ? "APPROVE" : "REQUEST_CHANGES");
    printf("=================================================================\n");

    CoUninitialize();
    return overall ? 0 : 1;
}
