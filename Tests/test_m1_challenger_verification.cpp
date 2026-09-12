#include "icon_capture.h"
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comctl32.lib")

static DWORD GetGdiCount() {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static uint32_t GetPixelDIB(HBITMAP hbmp, int x, int y) {
    if (!hbmp) return 0xFFFFFFFF;
    DIBSECTION ds = {};
    if (GetObject(hbmp, sizeof(ds), &ds) != sizeof(DIBSECTION) || !ds.dsBm.bmBits) {
        return 0xFFFFFFFF;
    }
    int w = ds.dsBm.bmWidth;
    int h = abs(ds.dsBm.bmHeight);
    if (x < 0 || x >= w || y < 0 || y >= h) return 0xFFFFFFFF;
    uint32_t* p = (uint32_t*)ds.dsBm.bmBits;
    return p[y * w + x];
}

// ============================================================================
// VERIFICATION 1: Tier 1 Jumbo Shell Extraction (notepad.exe, explorer.exe, cmd.exe)
// ============================================================================
bool VerifyTier1Jumbo() {
    printf("\n=== VERIFICATION 1: Tier 1 Jumbo Shell Extraction ===\n");
    bool pass = true;

    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    const wchar_t* test_exes[] = {
        L"C:\\WINDOWS\\System32\\notepad.exe",
        L"C:\\WINDOWS\\explorer.exe",
        L"C:\\WINDOWS\\System32\\cmd.exe"
    };

    for (const wchar_t* exe_path : test_exes) {
        TE_TaskbarItemInfo item = {};
        item.app_id = exe_path;
        item.icon_index = -1;

        HBITMAP hbmp = NULL;
        BOOL has_alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&item, &hbmp, &has_alpha);

        printf("  Testing '%ls': hr=0x%08X, hbmp=%p, has_alpha=%d\n", exe_path, hr, (void*)hbmp, has_alpha);

        if (FAILED(hr) || !hbmp) {
            printf("    FAIL: Failed to extract icon for '%ls'!\n", exe_path);
            pass = false;
            continue;
        }

        if (has_alpha != TRUE) {
            printf("    FAIL: has_alpha is not TRUE for '%ls'!\n", exe_path);
            pass = false;
        }

        DIBSECTION ds = {};
        if (GetObject(hbmp, sizeof(ds), &ds) != sizeof(DIBSECTION)) {
            printf("    FAIL: Handle is not a valid DIBSection!\n", exe_path);
            pass = false;
            continue;
        }

        if (ds.dsBm.bmWidth != 256 || abs(ds.dsBm.bmHeight) != 256 || ds.dsBm.bmBitsPixel != 32) {
            printf("    FAIL: Dimensions not 256x256 32-bit (got %dx%d %d-bpp)!\n",
                   ds.dsBm.bmWidth, abs(ds.dsBm.bmHeight), ds.dsBm.bmBitsPixel);
            pass = false;
        }

        // Verify premultiplied alpha condition: R<=A, G<=A, B<=A for all pixels
        uint32_t* pixels = (uint32_t*)ds.dsBm.bmBits;
        int non_zero_alpha = 0;
        bool valid_premul = true;
        for (int i = 0; i < 256 * 256; i++) {
            uint32_t p = pixels[i];
            uint8_t a = (p >> 24) & 0xFF;
            uint8_t r = (p >> 16) & 0xFF;
            uint8_t g = (p >> 8) & 0xFF;
            uint8_t b = p & 0xFF;
            if (a > 0) non_zero_alpha++;
            if (r > a || g > a || b > a) {
                valid_premul = false;
                break;
            }
        }

        if (non_zero_alpha < 100) {
            printf("    FAIL: Insufficient alpha pixels in icon (%d pixels)!\n", non_zero_alpha);
            pass = false;
        }
        if (!valid_premul) {
            printf("    FAIL: Premultiplied alpha invariant violated in '%ls'!\n", exe_path);
            pass = false;
        }

        if (pass) {
            printf("    PASS: Valid 256x256 32-bit ARGB DIB with %d alpha pixels, premul valid.\n", non_zero_alpha);
        }
    }

    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
    return pass;
}

// ============================================================================
// VERIFICATION 2: 1-Bit Monochrome Mask Pure Black RGB(0,0,0) Preservation
// ============================================================================
bool VerifyBlackPixelPreservation() {
    printf("\n=== VERIFICATION 2: 1-Bit Monochrome Mask Pure Black Preservation ===\n");
    bool pass = true;

    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    // Create 32x32 color DIB with pure black, pure white, and pure red
    const int size = 32;
    HDC hdc_screen = GetDC(NULL);
    HDC hdc_mem = CreateCompatibleDC(hdc_screen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* color_bits = nullptr;
    HBITMAP hbmColor = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &color_bits, NULL, 0);
    uint32_t* c = (uint32_t*)color_bits;

    // Default white
    for (int i = 0; i < size * size; i++) c[i] = 0x00FFFFFF;
    // Set (16, 16) to pure black RGB(0,0,0)
    c[16 * size + 16] = 0x00000000;
    // Set (16, 17) to red RGB(255,0,0)
    c[16 * size + 17] = 0x00FF0000;

    // 1-bit mask: stride = 4 bytes
    BYTE mask_bits[size * 4];
    memset(mask_bits, 0xFF, sizeof(mask_bits)); // Default transparent (1)
    // Row 16: make pixels 16 and 17 opaque (0)
    // Pixel 16 is byte 2 bit 7 (0x80)
    // Pixel 17 is byte 2 bit 6 (0x40)
    mask_bits[16 * 4 + 2] = 0x3F; // clear bits 7 and 6 -> opaque

    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, mask_bits);

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
        printf("  FAIL: CreateIconIndirect failed!\n");
        return false;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Chal_VerifyBlack";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_VerifyBlack", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);

    TE_TaskbarItemInfo item = {};
    item.hwnd = hwnd;
    item.icon_index = -1;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    printf("  Extract returned hr=0x%08X, out_bmp=%p, out_alpha=%d\n", hr, (void*)out_bmp, out_alpha);

    if (SUCCEEDED(hr) && out_bmp) {
        // Sample pixel at (128, 128) corresponding to icon (16, 16) [Pure Black]
        uint32_t p_black = GetPixelDIB(out_bmp, 128, 128);
        uint32_t p_red = GetPixelDIB(out_bmp, 136, 128);
        uint32_t p_trans = GetPixelDIB(out_bmp, 10, 10);

        printf("  Pure Black Artwork Pixel: 0x%08X (expected 0xFF000000)\n", p_black);
        printf("  Pure Red Artwork Pixel  : 0x%08X (expected 0xFFFF0000)\n", p_red);
        printf("  Transparent Pixel       : 0x%08X (expected 0x00000000)\n", p_trans);

        if (p_black == 0xFF000000) {
            printf("  PASS: Pure black pixel with mask bit 0 preserved as 0xFF000000 (no punch-hole).\n");
        } else {
            printf("  FAIL: Pure black pixel punched hole or corrupted: 0x%08X!\n", p_black);
            pass = false;
        }

        if (p_trans != 0x00000000) {
            printf("  FAIL: Transparent area not 0x00000000!\n");
            pass = false;
        }
    } else {
        printf("  FAIL: Extraction failed!\n");
        pass = false;
    }

    DestroyWindow(hwnd);
    DestroyIcon(hicon);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
    return pass;
}

// ============================================================================
// VERIFICATION 3: Tier 4 Screen Capture Alpha Preservation Through Resampling
// ============================================================================
bool VerifyTier4ScreenCapture() {
    printf("\n=== VERIFICATION 3: Tier 4 Screen Capture Alpha Survival ===\n");
    bool pass = true;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Chal_Tier4Wnd";
    RegisterClassW(&wc);

    const int W = 64;
    const int H = 64;
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                wc.lpszClassName, L"TE_Tier4",
                                WS_POPUP | WS_VISIBLE,
                                250, 250, W, H, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("  FAIL: CreateWindowExW failed!\n");
        return false;
    }

    HDC hdc = GetDC(hwnd);
    HBRUSH bg_brush = CreateSolidBrush(RGB(25, 25, 25));
    RECT rc_bg = { 0, 0, W, H };
    FillRect(hdc, &rc_bg, bg_brush);
    DeleteObject(bg_brush);

    // Bright cyan glyph
    HBRUSH icon_brush = CreateSolidBrush(RGB(0, 255, 255));
    RECT rc_icon = { 16, 16, 48, 48 };
    FillRect(hdc, &rc_icon, icon_brush);
    DeleteObject(icon_brush);
    ReleaseDC(hwnd, hdc);

    UpdateWindow(hwnd);
    DwmFlush();
    Sleep(50);

    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    TE_TaskbarItemInfo item = {};
    item.bounds.left = 250;
    item.bounds.top = 250;
    item.bounds.right = 250 + W;
    item.bounds.bottom = 250 + H;
    item.icon_index = -1;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    printf("  Tier 4 Extract: hr=0x%08X, out_bmp=%p, has_alpha=%d\n", hr, (void*)out_bmp, out_alpha);

    if (SUCCEEDED(hr) && out_bmp) {
        DIBSECTION ds = {};
        GetObject(out_bmp, sizeof(ds), &ds);
        uint32_t* p = (uint32_t*)ds.dsBm.bmBits;

        int non_zero_count = 0;
        int non_zero_alpha_count = 0;
        bool valid_premul = true;

        for (int i = 0; i < 256 * 256; i++) {
            uint32_t c = p[i];
            uint8_t a = (c >> 24) & 0xFF;
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = c & 0xFF;

            if (c != 0) non_zero_count++;
            if (a != 0) non_zero_alpha_count++;
            if (r > a || g > a || b > a) {
                valid_premul = false;
            }
        }

        printf("  Total pixels=65536, non-zero=%d, non-zero alpha=%d, premul_valid=%d\n",
               non_zero_count, non_zero_alpha_count, valid_premul ? 1 : 0);

        if (non_zero_alpha_count == 0) {
            printf("  FAIL: Alpha channel zeroed out!\n");
            pass = false;
        } else {
            printf("  PASS: Alpha channel preserved across SoftwareScale32.\n");
        }

        if (!valid_premul) {
            printf("  FAIL: Premultiplied alpha invariant violated in Tier 4!\n");
            pass = false;
        } else {
            printf("  PASS: Premultiplied alpha invariant satisfied.\n");
        }
    } else {
        printf("  FAIL: Tier 4 extraction returned error!\n");
        pass = false;
    }

    DestroyWindow(hwnd);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
    return pass;
}

// ============================================================================
// VERIFICATION 4: Multithreaded Concurrency & GDI Leak Free Lifecycle
// ============================================================================
bool VerifyMultithreadedZeroLeak() {
    printf("\n=== VERIFICATION 4: Multithreaded Stress & Zero-GDI Leakage ===\n");
    bool pass = true;

    // Warm up COM / Shell subsystem
    TE_IconCaptureInit();
    HBITMAP dummy = NULL;
    TE_IconCaptureGetBitmap(NULL, 0, &dummy);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();

    TE_IconCaptureInit();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Chal_MTStress";
    wc.hIcon = LoadIcon(NULL, IDI_SHIELD);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_MTStress", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    DWORD gdi_baseline = GetGdiCount();
    printf("  Baseline GDI object count (with test window): %u\n", gdi_baseline);

    const int NUM_THREADS = 16;
    const int RUNS = 50;
    std::atomic<bool> start{false};
    std::atomic<int> successes{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < NUM_THREADS; t++) {
        threads.emplace_back([t, RUNS, hwnd, &start, &successes]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!start.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            for (int i = 0; i < RUNS; i++) {
                TE_TaskbarItemInfo item = {};
                item.icon_index = -1;
                if (t % 2 == 0) {
                    item.hwnd = hwnd;
                } else {
                    item.app_id = L"C:\\WINDOWS\\System32\\notepad.exe";
                }

                HBITMAP bmp = NULL;
                BOOL alpha = FALSE;
                if (SUCCEEDED(TE_IconCaptureExtract(&item, &bmp, &alpha)) && bmp) {
                    successes.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    start.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();

    DWORD gdi_active = GetGdiCount();
    printf("  %d extractions completed. Active GDI: %u (delta: +%d)\n",
           successes.load(), gdi_active, (int)(gdi_active - gdi_baseline));

    TE_IconCaptureClearCache();
    DWORD gdi_cleared = GetGdiCount();
    printf("  After ClearCache: GDI count: %u (delta from baseline: +%d)\n",
           gdi_cleared, (int)(gdi_cleared - gdi_baseline));

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, GetModuleHandle(NULL));
    TE_IconCaptureShutdown();

    if (gdi_cleared > gdi_baseline) {
        printf("  FAIL: Leaked %d GDI handles in cache!\n", (int)(gdi_cleared - gdi_baseline));
        pass = false;
    } else {
        printf("  PASS: Zero GDI handles leaked (+0 delta from baseline).\n");
    }
    return pass;
}

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("Challenger 1 Empirical Verification Suite for Milestone 1 Gate 2\n");
    printf("=================================================================\n");

    bool v1 = VerifyTier1Jumbo();
    bool v2 = VerifyBlackPixelPreservation();
    bool v3 = VerifyTier4ScreenCapture();
    bool v4 = VerifyMultithreadedZeroLeak();

    printf("\n=================================================================\n");
    printf("VERIFICATION SUMMARY:\n");
    printf("  Tier 1 Jumbo Extraction (notepad, explorer, cmd) : %s\n", v1 ? "PASS" : "FAIL");
    printf("  1-Bit Monochrome Mask Pure Black Preservation    : %s\n", v2 ? "PASS" : "FAIL");
    printf("  Tier 4 Screen Capture Alpha Preservation         : %s\n", v3 ? "PASS" : "FAIL");
    printf("  Multithreaded Stress & Zero GDI Leakage          : %s\n", v4 ? "PASS" : "FAIL");
    printf("=================================================================\n");

    bool all_ok = v1 && v2 && v3 && v4;
    printf("FINAL VERDICT: %s\n", all_ok ? "APPROVE" : "REQUEST_CHANGES");
    printf("=================================================================\n");

    CoUninitialize();
    return all_ok ? 0 : 1;
}
