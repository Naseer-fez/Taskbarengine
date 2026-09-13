/**
 * @file test_m1_challenger2_stress.cpp
 * @brief Empirical Challenger 2 Stress & Concurrency Test Suite for Milestone 1 Iteration 2.
 *
 * Exercises:
 * 1. High-contention (16 threads) cold-cache extraction race conditions.
 * 2. Handle deduplication across all 5 key variants (app_id, hwnd, pid, icon_index, bounds).
 * 3. Leak-free cache reclamation (GDI handle delta = +0) across clear and shutdown cycles.
 * 4. Interleaved concurrent extraction and cache invalidation.
 * 5. Adversarial edge cases and invalid parameters.
 */

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
#include <cassert>

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

// ----------------------------------------------------------------------------
// Section 1: Cache Hit Handle Deduplication across All 5 Key Types
// ----------------------------------------------------------------------------
static bool TestHandleDeduplicationAllKeyTypes() {
    printf("\n--- [CHALLENGER CHECK 1] Handle Deduplication Across All Key Types ---\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    bool all_passed = true;

    // Sub-check 1.1: app_id key
    {
        wchar_t notepad_path[MAX_PATH];
        ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\notepad.exe", notepad_path, MAX_PATH);
        TE_TaskbarItemInfo it = {};
        it.app_id = notepad_path;

        HBITMAP bmp1 = NULL, bmp2 = NULL;
        BOOL a1 = FALSE, a2 = FALSE;
        HRESULT hr1 = TE_IconCaptureExtract(&it, &bmp1, &a1);
        HRESULT hr2 = TE_IconCaptureExtract(&it, &bmp2, &a2);

        if (SUCCEEDED(hr1) && SUCCEEDED(hr2) && bmp1 && (bmp1 == bmp2)) {
            printf("  [PASS] Key app_id: bmp1 == bmp2 (%p), alpha=%d\n", (void*)bmp1, (int)a1);
        } else {
            printf("  [FAIL] Key app_id: hr1=0x%08lX, hr2=0x%08lX, bmp1=%p, bmp2=%p\n",
                   (unsigned long)hr1, (unsigned long)hr2, (void*)bmp1, (void*)bmp2);
            all_passed = false;
        }
    }

    // Sub-check 1.2: hwnd key
    {
        HWND test_wnd = CreateWindowExW(0, L"STATIC", L"TE_ChallengerWnd", WS_POPUP,
                                        0, 0, 10, 10, NULL, NULL, GetModuleHandle(NULL), NULL);
        HICON icon = LoadIcon(NULL, IDI_INFORMATION);
        SendMessageW(test_wnd, WM_SETICON, ICON_BIG, (LPARAM)icon);

        TE_TaskbarItemInfo it = {};
        it.hwnd = test_wnd;

        HBITMAP bmp1 = NULL, bmp2 = NULL;
        BOOL a1 = FALSE, a2 = FALSE;
        HRESULT hr1 = TE_IconCaptureExtract(&it, &bmp1, &a1);
        HRESULT hr2 = TE_IconCaptureExtract(&it, &bmp2, &a2);

        if (SUCCEEDED(hr1) && SUCCEEDED(hr2) && bmp1 && (bmp1 == bmp2)) {
            printf("  [PASS] Key hwnd: bmp1 == bmp2 (%p), alpha=%d\n", (void*)bmp1, (int)a1);
        } else {
            printf("  [FAIL] Key hwnd: hr1=0x%08lX, hr2=0x%08lX, bmp1=%p, bmp2=%p\n",
                   (unsigned long)hr1, (unsigned long)hr2, (void*)bmp1, (void*)bmp2);
            all_passed = false;
        }
        DestroyWindow(test_wnd);
    }

    // Sub-check 1.3: process_id key
    {
        TE_TaskbarItemInfo it = {};
        it.process_id = GetCurrentProcessId();

        HBITMAP bmp1 = NULL, bmp2 = NULL;
        BOOL a1 = FALSE, a2 = FALSE;
        HRESULT hr1 = TE_IconCaptureExtract(&it, &bmp1, &a1);
        HRESULT hr2 = TE_IconCaptureExtract(&it, &bmp2, &a2);

        if (SUCCEEDED(hr1) && SUCCEEDED(hr2) && bmp1 && (bmp1 == bmp2)) {
            printf("  [PASS] Key process_id: bmp1 == bmp2 (%p), alpha=%d\n", (void*)bmp1, (int)a1);
        } else {
            printf("  [FAIL] Key process_id: hr1=0x%08lX, hr2=0x%08lX, bmp1=%p, bmp2=%p\n",
                   (unsigned long)hr1, (unsigned long)hr2, (void*)bmp1, (void*)bmp2);
            all_passed = false;
        }
    }

    // Sub-check 1.4: icon_index key (verifies idx_%d caching)
    {
        TE_TaskbarItemInfo it = {};
        it.icon_index = 0;

        HBITMAP bmp1 = NULL, bmp2 = NULL;
        BOOL a1 = FALSE, a2 = FALSE;
        HRESULT hr1 = TE_IconCaptureExtract(&it, &bmp1, &a1);
        HRESULT hr2 = TE_IconCaptureExtract(&it, &bmp2, &a2);

        if (SUCCEEDED(hr1) && SUCCEEDED(hr2) && bmp1 && (bmp1 == bmp2)) {
            printf("  [PASS] Key icon_index: bmp1 == bmp2 (%p), alpha=%d\n", (void*)bmp1, (int)a1);
        } else {
            printf("  [FAIL] Key icon_index: hr1=0x%08lX, hr2=0x%08lX, bmp1=%p, bmp2=%p\n",
                   (unsigned long)hr1, (unsigned long)hr2, (void*)bmp1, (void*)bmp2);
            all_passed = false;
        }
    }

    // Sub-check 1.5: bounds key
    {
        TE_TaskbarItemInfo it = {};
        it.bounds = { 100, 100, 148, 148 };

        HBITMAP bmp1 = NULL, bmp2 = NULL;
        BOOL a1 = FALSE, a2 = FALSE;
        HRESULT hr1 = TE_IconCaptureExtract(&it, &bmp1, &a1);
        HRESULT hr2 = TE_IconCaptureExtract(&it, &bmp2, &a2);

        if (SUCCEEDED(hr1) && SUCCEEDED(hr2) && bmp1 && (bmp1 == bmp2)) {
            printf("  [PASS] Key bounds: bmp1 == bmp2 (%p), alpha=%d\n", (void*)bmp1, (int)a1);
        } else {
            printf("  [FAIL] Key bounds: hr1=0x%08lX, hr2=0x%08lX, bmp1=%p, bmp2=%p\n",
                   (unsigned long)hr1, (unsigned long)hr2, (void*)bmp1, (void*)bmp2);
            all_passed = false;
        }
    }

    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
    return all_passed;
}

// ----------------------------------------------------------------------------
// Section 2: Multithreaded Cold-Cache Extraction Race Condition (16 Threads)
// ----------------------------------------------------------------------------
static bool TestColdCache16ThreadsContention() {
    printf("\n--- [CHALLENGER CHECK 2] 16-Thread High-Contention Cold-Cache Race ---\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI object count: %u\n", (unsigned int)gdi_baseline);

    const int THREAD_COUNT = 16;
    const int ITERS_PER_THREAD = 10;
    std::atomic<bool> start_gate{false};
    std::atomic<int> success_count{0};
    std::atomic<int> failure_count{0};

    std::vector<HBITMAP> returned_handles(THREAD_COUNT * ITERS_PER_THREAD, NULL);
    std::vector<std::thread> workers;

    wchar_t notepad_path[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\notepad.exe", notepad_path, MAX_PATH);

    for (int t = 0; t < THREAD_COUNT; t++) {
        workers.emplace_back([t, ITERS_PER_THREAD, &start_gate, &success_count, &failure_count, &returned_handles, notepad_path]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

            while (!start_gate.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            TE_TaskbarItemInfo item = {};
            item.app_id = notepad_path;

            for (int i = 0; i < ITERS_PER_THREAD; i++) {
                HBITMAP bmp = NULL;
                BOOL has_alpha = FALSE;
                HRESULT hr = TE_IconCaptureExtract(&item, &bmp, &has_alpha);
                if (SUCCEEDED(hr) && bmp) {
                    returned_handles[t * ITERS_PER_THREAD + i] = bmp;
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    failure_count.fetch_add(1, std::memory_order_relaxed);
                }
            }

            CoUninitialize();
        });
    }

    // Unleash all 16 threads simultaneously
    start_gate.store(true, std::memory_order_release);

    for (auto& th : workers) {
        th.join();
    }

    DWORD gdi_after_race = GetGdiCount();
    printf("  16-thread race finished: %d successes, %d failures\n",
           success_count.load(), failure_count.load());
    printf("  GDI objects after race: %u (delta: +%d)\n",
           (unsigned int)gdi_after_race, (int)(gdi_after_race - gdi_baseline));

    // Deduplication check
    std::vector<HBITMAP> unique_bmps;
    for (HBITMAP b : returned_handles) {
        if (b && std::find(unique_bmps.begin(), unique_bmps.end(), b) == unique_bmps.end()) {
            unique_bmps.push_back(b);
        }
    }

    printf("  Unique handles distributed to 16 threads: %zu (expected exactly 1)\n", unique_bmps.size());

    // Clear cache and verify clean reclamation
    TE_IconCaptureClearCache();
    DWORD gdi_after_clear = GetGdiCount();
    printf("  GDI objects after TE_IconCaptureClearCache(): %u (delta from baseline: +%d)\n",
           (unsigned int)gdi_after_clear, (int)(gdi_after_clear - gdi_baseline));

    TE_IconCaptureShutdown();

    bool pass = (success_count.load() == THREAD_COUNT * ITERS_PER_THREAD) &&
                (failure_count.load() == 0) &&
                (unique_bmps.size() == 1) &&
                (gdi_after_clear == gdi_baseline);

    if (pass) {
        printf("  [PASS] 16-thread cold-cache race: zero leaks, single canonical handle deduplicated.\n");
    } else {
        printf("  [FAIL] 16-thread cold-cache race failed criteria!\n");
    }
    return pass;
}

// ----------------------------------------------------------------------------
// Section 3: High-Volume Cache Churn & Leak-Free Reclamation
// ----------------------------------------------------------------------------
static bool TestHighVolumeCacheReclamation() {
    printf("\n--- [CHALLENGER CHECK 3] High-Volume Population & Reclamation (GDI Delta = +0) ---\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI count: %u\n", (unsigned int)gdi_baseline);

    const int NUM_ITEMS = 64;
    std::vector<HBITMAP> cached_handles;
    cached_handles.reserve(NUM_ITEMS);

    for (int i = 0; i < NUM_ITEMS; i++) {
        TE_TaskbarItemInfo it = {};
        it.bounds = { i * 5, 0, i * 5 + 32, 32 };
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (SUCCEEDED(hr) && bmp) {
            cached_handles.push_back(bmp);
        }
    }

    DWORD gdi_populated = GetGdiCount();
    printf("  Inserted %zu items into cache. GDI count: %u (delta: +%d)\n",
           cached_handles.size(), (unsigned int)gdi_populated, (int)(gdi_populated - gdi_baseline));

    // Clear cache
    TE_IconCaptureClearCache();
    DWORD gdi_cleared = GetGdiCount();
    printf("  GDI count after TE_IconCaptureClearCache(): %u (delta from baseline: +%d)\n",
           (unsigned int)gdi_cleared, (int)(gdi_cleared - gdi_baseline));

    // Verify all previous handles are invalidated in the OS GDI table
    int still_valid_count = 0;
    for (HBITMAP b : cached_handles) {
        BITMAP bm = {};
        if (GetObject(b, sizeof(bm), &bm) != 0) {
            still_valid_count++;
        }
    }

    printf("  Handles still valid after clear: %d (expected 0)\n", still_valid_count);

    TE_IconCaptureShutdown();

    bool pass = (gdi_cleared == gdi_baseline) && (still_valid_count == 0);
    if (pass) {
        printf("  [PASS] Perfect reclamation: GDI delta exactly +0, all handles invalidated.\n");
    } else {
        printf("  [FAIL] Leaks detected or handles retained!\n");
    }
    return pass;
}

// ----------------------------------------------------------------------------
// Section 4: Concurrent Extractions Interleaved with Invalidate/Clear
// ----------------------------------------------------------------------------
static bool TestInterleavedExtractionAndClear() {
    printf("\n--- [CHALLENGER CHECK 4] Interleaved Concurrent Extraction and Invalidation ---\n");
    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI count: %u\n", (unsigned int)gdi_baseline);

    std::atomic<bool> running{true};
    std::atomic<int> extract_count{0};
    std::atomic<int> clear_count{0};

    // 4 extraction worker threads
    std::vector<std::thread> extractors;
    for (int t = 0; t < 4; t++) {
        extractors.emplace_back([t, &running, &extract_count]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (running.load(std::memory_order_relaxed)) {
                TE_TaskbarItemInfo it = {};
                it.bounds = { (t % 8) * 10, 0, (t % 8) * 10 + 32, 32 };
                HBITMAP bmp = NULL;
                BOOL alpha = FALSE;
                TE_IconCaptureExtract(&it, &bmp, &alpha);
                extract_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            CoUninitialize();
        });
    }

    // 2 invalidation worker threads
    std::vector<std::thread> invalidators;
    for (int t = 0; t < 2; t++) {
        invalidators.emplace_back([&running, &clear_count]() {
            while (running.load(std::memory_order_relaxed)) {
                TE_IconCaptureClearCache();
                clear_count.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    // Run heavy churn for 500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    running.store(false, std::memory_order_release);

    for (auto& th : extractors) th.join();
    for (auto& th : invalidators) th.join();

    printf("  Churn complete: %d extractions, %d clear operations\n",
           extract_count.load(), clear_count.load());

    // Final clean
    TE_IconCaptureClearCache();
    DWORD gdi_final = GetGdiCount();
    printf("  GDI objects after final clear: %u (delta from baseline: +%d)\n",
           (unsigned int)gdi_final, (int)(gdi_final - gdi_baseline));

    TE_IconCaptureShutdown();

    bool pass = (gdi_final == gdi_baseline);
    if (pass) {
        printf("  [PASS] Interleaved extraction and clear survived with 0 leaks and no deadlocks.\n");
    } else {
        printf("  [FAIL] Interleaved test leaked %d handles!\n", (int)(gdi_final - gdi_baseline));
    }
    return pass;
}

// ----------------------------------------------------------------------------
// Section 5: Adversarial Parameter Edge Cases
// ----------------------------------------------------------------------------
static bool TestAdversarialEdgeCases() {
    printf("\n--- [CHALLENGER CHECK 5] Adversarial Parameter Stress ---\n");
    TE_IconCaptureInit();

    DWORD gdi_before = GetGdiCount();
    bool all_ok = true;

    // 1. NULL item
    {
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(NULL, &bmp, &alpha);
        if (hr != TE_E_INVALIDARG) {
            printf("  [FAIL] Expected TE_E_INVALIDARG for NULL item, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 2. NULL out_bitmap
    {
        TE_TaskbarItemInfo it = {};
        it.bounds = { 0, 0, 32, 32 };
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, NULL, &alpha);
        if (hr != TE_E_INVALIDARG) {
            printf("  [FAIL] Expected TE_E_INVALIDARG for NULL out_bitmap, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 3. Empty item (no app_id, hwnd, pid, bounds)
    {
        TE_TaskbarItemInfo it = {};
        it.icon_index = -1;
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (hr != TE_E_FAIL) {
            printf("  [FAIL] Expected TE_E_FAIL for empty item, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 4. Inverted bounds
    {
        TE_TaskbarItemInfo it = {};
        it.bounds = { 100, 100, 50, 50 }; // right < left, bottom < top
        it.icon_index = -1;
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (hr != TE_E_FAIL) {
            printf("  [FAIL] Expected TE_E_FAIL for inverted bounds, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 5. Non-existent path
    {
        TE_TaskbarItemInfo it = {};
        it.app_id = L"Z:\\non_existent_drive\\fake_app_12345.exe";
        it.icon_index = -1;
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (hr != TE_E_FAIL) {
            printf("  [FAIL] Expected TE_E_FAIL for non-existent path, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 6. Non-existent AUMID
    {
        TE_TaskbarItemInfo it = {};
        it.app_id = L"Fake.NonExistent.App.Package!App";
        it.icon_index = -1;
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (hr != TE_E_FAIL) {
            printf("  [FAIL] Expected TE_E_FAIL for non-existent AUMID, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 7. Bogus HWND
    {
        TE_TaskbarItemInfo it = {};
        it.hwnd = (HWND)(uintptr_t)0xDEADBEEF;
        it.icon_index = -1;
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (hr != TE_E_FAIL) {
            printf("  [FAIL] Expected TE_E_FAIL for bogus HWND, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    // 8. Bogus PID
    {
        TE_TaskbarItemInfo it = {};
        it.process_id = 99999999;
        it.icon_index = -1;
        HBITMAP bmp = NULL;
        BOOL alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&it, &bmp, &alpha);
        if (hr != TE_E_FAIL) {
            printf("  [FAIL] Expected TE_E_FAIL for bogus PID, got 0x%08lX\n", (unsigned long)hr);
            all_ok = false;
        }
    }

    TE_IconCaptureClearCache();
    DWORD gdi_after = GetGdiCount();
    if (gdi_after != gdi_before) {
        printf("  [FAIL] GDI leaked during adversarial edge case testing: delta = +%d\n",
               (int)(gdi_after - gdi_before));
        all_ok = false;
    }

    TE_IconCaptureShutdown();

    if (all_ok) {
        printf("  [PASS] All adversarial edge cases handled safely with 0 leaks.\n");
    }
    return all_ok;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("TaskbarEngine Milestone 1 Challenger 2 Empirical Stress Test\n");
    printf("=================================================================\n");

    bool r1 = TestHandleDeduplicationAllKeyTypes();
    bool r2 = TestColdCache16ThreadsContention();
    bool r3 = TestHighVolumeCacheReclamation();
    bool r4 = TestInterleavedExtractionAndClear();
    bool r5 = TestAdversarialEdgeCases();

    printf("\n=================================================================\n");
    printf("SUMMARY OF CHALLENGER RESULTS:\n");
    printf("  Check 1 (Handle Deduplication)   : %s\n", r1 ? "PASS" : "FAIL");
    printf("  Check 2 (16-Thread Cold Contention): %s\n", r2 ? "PASS" : "FAIL");
    printf("  Check 3 (High-Volume Reclamation): %s\n", r3 ? "PASS" : "FAIL");
    printf("  Check 4 (Interleaved Churn & Clear): %s\n", r4 ? "PASS" : "FAIL");
    printf("  Check 5 (Adversarial Edge Cases) : %s\n", r5 ? "PASS" : "FAIL");

    bool overall = r1 && r2 && r3 && r4 && r5;
    printf("OVERALL VERDICT: %s\n", overall ? "APPROVE" : "REQUEST_CHANGES");
    printf("=================================================================\n");

    CoUninitialize();
    return overall ? 0 : 1;
}
