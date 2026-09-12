#include "icon_capture.h"
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <atomic>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uuid.lib")

static DWORD GetGdiCount() {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

// ----------------------------------------------------------------------------
// Test A: Tier 1 Jumbo ImageList IID & Executable Extraction
// ----------------------------------------------------------------------------
void TestTier1Extraction() {
    printf("\n=== ADVERSARIAL TEST A: Tier 1 Jumbo Extraction ===\n");
    HRESULT hr_init = TE_IconCaptureInit();
    printf("TE_IconCaptureInit: hr=0x%08X\n", hr_init);

    wchar_t notepad_path[MAX_PATH];
    ExpandEnvironmentStringsW(L"%SystemRoot%\\System32\\notepad.exe", notepad_path, MAX_PATH);

    TE_TaskbarItemInfo item = {};
    item.app_id = notepad_path;

    HBITMAP hbmp = NULL;
    BOOL has_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &hbmp, &has_alpha);
    printf("Extract for '%ls': hr=0x%08X, hbmp=%p, has_alpha=%d\n",
           notepad_path, hr, (void*)hbmp, has_alpha);

    if (FAILED(hr) || !hbmp) {
        printf("CRITICAL BUG: Tier 1 extraction failed for '%ls' (hr=0x%08X)!\n", notepad_path, hr);
    } else {
        printf("PASS: Tier 1 extraction succeeded for '%ls'.\n", notepad_path);
    }

    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test B: 1-Bit Mask Pure Black Pixel Preservation in HICON Conversion
// ----------------------------------------------------------------------------
void TestBlackPixelPreservation() {
    printf("\n=== ADVERSARIAL TEST B: 1-Bit Mask Pure Black Pixel Preservation ===\n");
    // Create an HICON with 1-bit mask containing:
    // (0,0): Black RGB(0,0,0) with mask 0 (Opaque artwork) -> MUST be 0xFF000000
    // (1,0): White RGB(255,255,255) with mask 0 (Opaque artwork) -> MUST be 0xFFFFFFFF
    // (0,1): Black RGB(0,0,0) with mask 1 (Transparent background) -> MUST be 0x00000000
    // (1,1): White RGB(255,255,255) with mask 1 (Transparent background) -> MUST be 0x00000000
    HDC hdc_screen = GetDC(NULL);
    HDC hdc_mem = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 32;
    bmi.bmiHeader.biHeight = -32;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* color_bits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &color_bits, NULL, 0);
    uint32_t* c_pixels = (uint32_t*)color_bits;
    memset(c_pixels, 0, 32 * 32 * sizeof(uint32_t));
    c_pixels[0] = 0x00000000; // (0,0) Black
    c_pixels[1] = 0x00FFFFFF; // (1,0) White
    c_pixels[32] = 0x00000000; // (0,1) Black
    c_pixels[33] = 0x00FFFFFF; // (1,1) White

    // 1-bit monochrome mask: 0 = opaque, 1 = transparent
    // 32x32 monochrome bitmap: row stride = 4 bytes (32 bits)
    BYTE mask_bits[32 * 4];
    memset(mask_bits, 0xFF, sizeof(mask_bits)); // Default all transparent (1)
    // Row 0: pixel 0 (bit 7) = 0 (opaque), pixel 1 (bit 6) = 0 (opaque)
    mask_bits[0] = 0x3F; // 00111111b -> bits 7 and 6 are 0
    // Row 1: bits 7 and 6 are 1 (transparent)
    mask_bits[4] = 0xFF;

    HBITMAP hbmMask = CreateBitmap(32, 32, 1, 1, mask_bits);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;

    HICON hicon = CreateIconIndirect(&ii);
    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    DeleteDC(hdc_mem);
    ReleaseDC(NULL, hdc_screen);

    if (!hicon) {
        printf("Failed to create test HICON\n");
        return;
    }

    // Now call Tier 3 extraction via a test window holding this icon
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_BlackTestWndClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_BlackTest", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);

    TE_IconCaptureInit();
    TE_TaskbarItemInfo item = {};
    item.hwnd = hwnd;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);
    printf("Extract HICON with 1-bit mask: hr=0x%08X, out_bmp=%p, out_alpha=%d\n",
           hr, (void*)out_bmp, out_alpha);

    if (SUCCEEDED(hr) && out_bmp) {
        DIBSECTION dib = {};
        GetObject(out_bmp, sizeof(dib), &dib);
        uint32_t* p = (uint32_t*)dib.dsBm.bmBits;
        if (p) {
            printf("Top-left pixel (0,0) [Opaque Black]: 0x%08X\n", p[0]);
            printf("Pixel (8,0) [Opaque White]: 0x%08X\n", p[8]);
            printf("Pixel (0,8) [Transparent Black]: 0x%08X\n", p[8 * 256]);
            printf("Pixel (8,8) [Transparent White]: 0x%08X\n", p[8 * 256 + 8]);

            if (p[0] == 0xFF000000) {
                printf("PASS: Pure black pixel with mask bit 0 correctly preserved as 0xFF000000.\n");
            } else {
                printf("CRITICAL BUG: Pure black pixel is 0x%08X (punch-hole bug!)\n", p[0]);
            }
        }
    }

    DestroyWindow(hwnd);
    DestroyIcon(hicon);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test C: Tier 4 Screen Capture & StretchBlt Alpha Channel Survival
// ----------------------------------------------------------------------------
void TestTier4ScreenCaptureAlpha() {
    printf("\n=== ADVERSARIAL TEST C: Tier 4 Screen Capture Alpha Survival ===\n");
    // Create a colored test window on screen
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Tier4TestWndClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Tier4Test",
                              WS_POPUP | WS_VISIBLE,
                              200, 200, 64, 64, NULL, NULL, wc.hInstance, NULL);

    HDC hdc = GetDC(hwnd);
    HBRUSH bg_brush = CreateSolidBrush(RGB(30, 30, 30)); // Taskbar bg
    RECT rc = { 0, 0, 64, 64 };
    FillRect(hdc, &rc, bg_brush);
    DeleteObject(bg_brush);

    // Inner icon glyph in bright cyan RGB(0, 255, 255)
    HBRUSH icon_brush = CreateSolidBrush(RGB(0, 255, 255));
    RECT rc_icon = { 16, 16, 48, 48 };
    FillRect(hdc, &rc_icon, icon_brush);
    DeleteObject(icon_brush);
    ReleaseDC(hwnd, hdc);

    // Wait for window to paint
    UpdateWindow(hwnd);
    Sleep(50);

    TE_IconCaptureInit();
    TE_TaskbarItemInfo item = {};
    item.bounds.left = 200;
    item.bounds.top = 200;
    item.bounds.right = 264;
    item.bounds.bottom = 264;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);
    printf("Tier 4 Extract: hr=0x%08X, out_bmp=%p, out_alpha=%d\n", hr, (void*)out_bmp, out_alpha);

    if (SUCCEEDED(hr) && out_bmp) {
        DIBSECTION dib = {};
        GetObject(out_bmp, sizeof(dib), &dib);
        uint32_t* p = (uint32_t*)dib.dsBm.bmBits;
        if (p) {
            int non_zero_count = 0;
            int non_zero_alpha_count = 0;
            for (int i = 0; i < 256 * 256; i++) {
                if (p[i] != 0) non_zero_count++;
                if (((p[i] >> 24) & 0xFF) != 0) non_zero_alpha_count++;
            }
            printf("Tier 4 Result: total pixels=%d, non-zero color pixels=%d, non-zero alpha pixels=%d\n",
                   256 * 256, non_zero_count, non_zero_alpha_count);

            if (non_zero_count == 0 || non_zero_alpha_count == 0) {
                printf("CRITICAL BUG CONFIRMED: Entire Tier 4 extracted icon was wiped to 0x00000000!\n");
            } else {
                printf("PASS: Tier 4 extracted icon preserved alpha and color pixels.\n");
            }
        }
    }

    DestroyWindow(hwnd);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
}

// ----------------------------------------------------------------------------
// Test D: Multithreaded Cache Concurrency & Handle Leak
// ----------------------------------------------------------------------------
void TestMultithreadedCacheLeak() {
    printf("\n=== ADVERSARIAL TEST D: Multithreaded Cold-Cache Concurrency Leak ===\n");
    TE_IconCaptureInit();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_MTCacheWndClass";
    wc.hIcon = LoadIcon(NULL, IDI_SHIELD); // Use class icon so GetClassLongPtrW succeeds without window message pump
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_MTCacheTest", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    DWORD gdi_baseline = GetGdiCount();
    printf("GDI baseline before multithreaded test: %u\n", gdi_baseline);

    const int THREAD_COUNT = 8;
    const int RUNS = 25;
    std::atomic<bool> go{false};
    std::atomic<int> success_count{0};
    std::vector<std::thread> threads;
    std::vector<HBITMAP> results(THREAD_COUNT * RUNS, NULL);

    for (int t = 0; t < THREAD_COUNT; t++) {
        threads.emplace_back([t, RUNS, &go, &success_count, &results, hwnd]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!go.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            TE_TaskbarItemInfo item = {};
            item.hwnd = hwnd;

            for (int i = 0; i < RUNS; i++) {
                HBITMAP b = NULL;
                BOOL a = FALSE;
                if (SUCCEEDED(TE_IconCaptureExtract(&item, &b, &a)) && b) {
                    results[t * RUNS + i] = b;
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    go.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();

    DWORD gdi_after = GetGdiCount();
    printf("Multithreaded run: %d extractions succeeded. GDI objects: %u (delta: +%d)\n",
           success_count.load(), gdi_after, (int)(gdi_after - gdi_baseline));

    TE_IconCaptureClearCache();
    DWORD gdi_cleared = GetGdiCount();
    printf("After ClearCache: GDI objects: %u (delta from baseline: +%d)\n",
           gdi_cleared, (int)(gdi_cleared - gdi_baseline));

    if (gdi_cleared > gdi_baseline) {
        printf("CRITICAL BUG CONFIRMED: Multithreaded cold-cache race leaked %d GDI handles!\n",
               (int)(gdi_cleared - gdi_baseline));
    } else {
        printf("PASS: No GDI handles leaked under multithreaded cold-cache access.\n");
    }

    DestroyWindow(hwnd);
    TE_IconCaptureShutdown();
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("Milestone 1 Empirical Challenger Harness\n");
    printf("=================================================================\n");

    TestTier1Extraction();
    TestBlackPixelPreservation();
    TestTier4ScreenCaptureAlpha();
    TestMultithreadedCacheLeak();

    printf("\n=================================================================\n");
    printf("Empirical Challenger Harness Complete.\n");
    printf("=================================================================\n");

    CoUninitialize();
    return 0;
}
