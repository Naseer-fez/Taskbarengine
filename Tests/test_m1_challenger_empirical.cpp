/**
 * @file test_m1_challenger_empirical.cpp
 * @brief Empirical Challenger Test Suite for TaskbarEngine Milestone 1 Iteration 2 Gate.
 *
 * Rigorously challenges:
 * 1. Pure black RGB(0,0,0) pixel preservation with full opacity (0xFF) under 1-bit masks.
 * 2. 1-bit monochrome icon mask decoding across multiple resolutions (16x16, 32x32, 48x48),
 *    verifying proper AND/XOR mask isolation without bottom-half corruption.
 * 3. Tier 4 screen capture alpha isolation, color preservation, and absence of double-premultiplication.
 * 4. Adversarial inputs: null pointers, inverted bounds, invalid PIDs/HWNDs, non-existent files.
 * 5. High-concurrency multithreaded cold-cache race conditions and GDI handle reclamation.
 */

#include "icon_capture.h"
#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <thread>
#include <atomic>
#include <cmath>
#include <cassert>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uuid.lib")

static DWORD GetCurrentGdiCount() {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static uint32_t ReadPixel(HBITMAP hbmp, int x, int y) {
    DIBSECTION ds = {};
    if (GetObject(hbmp, sizeof(ds), &ds) != sizeof(DIBSECTION) || !ds.dsBm.bmBits) {
        return 0xFFFFFFFF;
    }
    int w = ds.dsBm.bmWidth;
    int h = std::abs(ds.dsBm.bmHeight);
    if (x < 0 || x >= w || y < 0 || y >= h) return 0xFFFFFFFF;
    uint32_t* pixels = (uint32_t*)ds.dsBm.bmBits;
    return pixels[y * w + x];
}

static HBITMAP CreateTestDIBSection32(int width, int height, void** out_bits) {
    HDC hdc = GetDC(NULL);
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hbmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, hdc);
    if (out_bits) *out_bits = bits;
    return hbmp;
}

// ----------------------------------------------------------------------------
// Challenge 1: Pure Black RGB(0,0,0) Pixel Opacity & Punch-Hole Elimination
// ----------------------------------------------------------------------------
bool Challenge1_PureBlackPixelPreservation() {
    printf("\n[CHALLENGE 1] Pure Black RGB(0,0,0) Pixel Opacity Preservation\n");
    bool passed = true;

    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    // 1A. Test 32x32 color icon with 1-bit mask containing various black and non-black pixels
    const int size = 32;
    void* color_bits = nullptr;
    HBITMAP hbmColor = CreateTestDIBSection32(size, size, &color_bits);
    uint32_t* c_pixels = (uint32_t*)color_bits;

    // Fill color buffer:
    // (10, 10): Pure black RGB(0,0,0)
    // (11, 10): Dark off-black RGB(1,1,1)
    // (12, 10): Pure white RGB(255,255,255)
    // (13, 10): Pure red RGB(255,0,0)
    // All others: White
    for (int i = 0; i < size * size; i++) c_pixels[i] = 0x00FFFFFF;
    c_pixels[10 * size + 10] = 0x00000000;
    c_pixels[10 * size + 11] = 0x00010101;
    c_pixels[10 * size + 12] = 0x00FFFFFF;
    c_pixels[10 * size + 13] = 0x00FF0000;

    // 1-bit mask: 0 = opaque, 1 = transparent
    // Stride for 32 pixels 1-bpp is 4 bytes (32 bits)
    std::vector<uint8_t> mask(size * 4, 0xFF); // Default: transparent
    // Row 10: make pixels 10, 11, 12, 13 opaque (bit value 0)
    // Byte 1 covers pixels 8..15.
    // Pixel 10 is bit (7 - 2) = bit 5
    // Pixel 11 is bit 4
    // Pixel 12 is bit 3
    // Pixel 13 is bit 2
    // Mask byte 1: clear bits 5, 4, 3, 2 -> 11000011b = 0xC3
    mask[10 * 4 + 1] = 0xC3;

    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, mask.data());

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;
    HICON hicon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);

    if (!hicon) {
        printf("  FAIL: Unable to create test HICON\n");
        return false;
    }

    // Wrap in test window for Tier 3 extraction
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Chal1_WndClass";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Chal1", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);

    TE_TaskbarItemInfo item = {};
    item.hwnd = hwnd;
    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    if (FAILED(hr) || !out_bmp) {
        printf("  FAIL: Extraction failed hr=0x%08X\n", hr);
        passed = false;
    } else {
        // Sample mapped coordinates in 256x256 result.
        // Input is 32x32 scaled to 256x256 (scale factor 8x).
        // Pixel (10, 10) maps to center of block: x = 10*8 + 4 = 84, y = 10*8 + 4 = 84.
        uint32_t p_black = ReadPixel(out_bmp, 84, 84);
        uint32_t p_offblack = ReadPixel(out_bmp, 84 + 8, 84);
        uint32_t p_white = ReadPixel(out_bmp, 84 + 16, 84);
        uint32_t p_red = ReadPixel(out_bmp, 84 + 24, 84);
        uint32_t p_trans = ReadPixel(out_bmp, 20, 20); // Outside mask area

        printf("  Pixel (10,10) [Pure Black]: 0x%08X\n", p_black);
        printf("  Pixel (11,10) [Off-Black ]: 0x%08X\n", p_offblack);
        printf("  Pixel (12,10) [Pure White]: 0x%08X\n", p_white);
        printf("  Pixel (13,10) [Pure Red  ]: 0x%08X\n", p_red);
        printf("  Pixel (2, 2 ) [Transpar. ]: 0x%08X\n", p_trans);

        // Verification 1: Pure black pixel MUST have alpha 0xFF and RGB (0,0,0)
        if (p_black != 0xFF000000) {
            printf("  CRITICAL FAIL: Pure black pixel punched hole! Expected 0xFF000000, got 0x%08X\n", p_black);
            passed = false;
        } else {
            printf("  PASS: Pure black pixel correctly retained full opacity 0xFF000000.\n");
        }

        // Verification 2: Off-black pixel must have alpha 0xFF
        if ((p_offblack >> 24) != 0xFF) {
            printf("  FAIL: Off-black pixel alpha is not 0xFF (got 0x%08X)\n", p_offblack);
            passed = false;
        }

        // Verification 3: Transparent pixel must have alpha 0x00 and zero color
        if (p_trans != 0x00000000) {
            printf("  FAIL: Transparent area has non-zero pixel: 0x%08X\n", p_trans);
            passed = false;
        }
    }

    DestroyWindow(hwnd);
    DestroyIcon(hicon);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
    return passed;
}

// ----------------------------------------------------------------------------
// Challenge 2: 1-Bit Monochrome Mask Decoding (AND/XOR Isolation)
// ----------------------------------------------------------------------------
bool TestMonochromeResolution(int dim) {
    printf("  Testing monochrome resolution: %dx%d (mask height %d)...\n", dim, dim, dim * 2);
    bool ok = true;
    int mask_h = dim * 2;
    int stride = (dim + 31) / 32 * 4;

    std::vector<uint8_t> mask(mask_h * stride, 0xFF); // Default: transparent background

    // Top half (rows 0 to dim-1) = AND mask.
    // Make central rows (from dim/4 to 3*dim/4) opaque (0) for all columns.
    int y_start = dim / 4;
    int y_end = (3 * dim) / 4;
    for (int y = y_start; y < y_end; y++) {
        for (int b = 0; b < stride; b++) {
            mask[y * stride + b] = 0x00; // Opaque
        }
    }

    // Bottom half (rows dim to 2*dim-1) = XOR mask.
    // In XOR mask, fill with alternating black (0) and white (1) bands:
    // Rows dim + y_start to dim + dim/2: XOR = 0 (Black artwork)
    // Rows dim + dim/2 to dim + y_end:   XOR = 1 (White artwork)
    for (int y = dim; y < 2 * dim; y++) {
        for (int b = 0; b < stride; b++) {
            mask[y * stride + b] = 0x00; // All black default
        }
    }
    int xor_white_y_start = dim + dim / 2;
    int xor_white_y_end = dim + y_end;
    for (int y = xor_white_y_start; y < xor_white_y_end; y++) {
        for (int b = 0; b < stride; b++) {
            mask[y * stride + b] = 0xFF; // White artwork
        }
    }

    HBITMAP hbmMask = CreateBitmap(dim, mask_h, 1, 1, mask.data());
    if (!hbmMask) {
        printf("    FAIL: Failed to create monochrome mask bitmap\n");
        return false;
    }

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = NULL; // Monochrome icon: null color bitmap!
    ii.hbmMask = hbmMask;

    HICON hicon = CreateIconIndirect(&ii);
    DeleteObject(hbmMask);
    if (!hicon) {
        printf("    FAIL: Failed to create monochrome HICON\n");
        return false;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wchar_t clsname[64];
    swprintf_s(clsname, L"TE_MonoWnd_%d", dim);
    wc.lpszClassName = clsname;
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Mono", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);

    TE_TaskbarItemInfo item = {};
    item.hwnd = hwnd;
    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    if (FAILED(hr) || !out_bmp) {
        printf("    FAIL: TE_IconCaptureExtract failed hr=0x%08X\n", hr);
        ok = false;
    } else {
        // Output is 256x256.
        // In the top-opaque band (y from dim/4 to dim/2):
        // AND = 0 (opaque), XOR = 0 (black). Result must be OPAQUE BLACK: 0xFF000000.
        int test_y_black = (int)((y_start + (dim / 8.0f)) * 256.0f / dim);
        uint32_t p_black = ReadPixel(out_bmp, 128, test_y_black);

        // In the bottom-opaque band (y from dim/2 to y_end):
        // AND = 0 (opaque), XOR = 1 (white). Result must be OPAQUE WHITE: 0xFFFFFFFF.
        int test_y_white = (int)(((dim / 2.0f) + (dim / 8.0f)) * 256.0f / dim);
        uint32_t p_white = ReadPixel(out_bmp, 128, test_y_white);

        // In the transparent margin (y near 0 or dim-1):
        // AND = 1 (transparent). Result must be TRANSPARENT: 0x00000000.
        uint32_t p_trans_top = ReadPixel(out_bmp, 128, 5);
        uint32_t p_trans_bot = ReadPixel(out_bmp, 128, 250);

        printf("    Black band pixel  (y=%d): 0x%08X\n", test_y_black, p_black);
        printf("    White band pixel  (y=%d): 0x%08X\n", test_y_white, p_white);
        printf("    Transparent top   (y=5  ): 0x%08X\n", p_trans_top);
        printf("    Transparent bottom(y=250): 0x%08X\n", p_trans_bot);

        if (p_black != 0xFF000000) {
            printf("    FAIL: Monochrome black artwork corrupted! Expected 0xFF000000, got 0x%08X\n", p_black);
            ok = false;
        }
        if (p_white != 0xFFFFFFFF) {
            printf("    FAIL: Monochrome white artwork corrupted! Expected 0xFFFFFFFF, got 0x%08X\n", p_white);
            ok = false;
        }
        if (p_trans_top != 0x00000000) {
            printf("    FAIL: Monochrome transparent top corrupted! Expected 0x00000000, got 0x%08X\n", p_trans_top);
            ok = false;
        }
        if (p_trans_bot != 0x00000000) {
            printf("    FAIL: Monochrome transparent bottom corrupted! Expected 0x00000000, got 0x%08X\n", p_trans_bot);
            ok = false;
        }
    }

    DestroyWindow(hwnd);
    DestroyIcon(hicon);
    TE_IconCaptureClearCache();
    return ok;
}

bool Challenge2_MonochromeMaskDecoding() {
    printf("\n[CHALLENGE 2] 1-Bit Monochrome Mask Decoding Across Multiple Resolutions\n");
    TE_IconCaptureInit();

    bool p16 = TestMonochromeResolution(16);
    bool p32 = TestMonochromeResolution(32);
    bool p48 = TestMonochromeResolution(48);

    TE_IconCaptureShutdown();
    bool passed = p16 && p32 && p48;
    if (passed) {
        printf("  PASS: All monochrome icon resolutions decoded AND/XOR masks accurately.\n");
    } else {
        printf("  FAIL: One or more monochrome icon resolutions failed!\n");
    }
    return passed;
}

// ----------------------------------------------------------------------------
// Challenge 3: Tier 4 Screen Capture Alpha Isolation & Color Preservation
// ----------------------------------------------------------------------------
bool Challenge3_Tier4ScreenCapture() {
    printf("\n[CHALLENGE 3] Tier 4 Screen Capture Alpha Isolation & Color Fidelity\n");
    bool passed = true;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Chal3_ScreenWnd";
    RegisterClassW(&wc);

    const int W = 64;
    const int H = 64;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Chal3",
                              WS_POPUP | WS_VISIBLE,
                              300, 300, W, H, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        printf("  FAIL: Could not create popup window for screen capture test\n");
        return false;
    }

    HDC hdc = GetDC(hwnd);
    // 1. Taskbar background: RGB(32, 32, 32)
    COLORREF bg_color = RGB(32, 32, 32);
    HBRUSH bg_brush = CreateSolidBrush(bg_color);
    RECT rc_bg = { 0, 0, W, H };
    FillRect(hdc, &rc_bg, bg_brush);
    DeleteObject(bg_brush);

    // 2. Center glyph: Bright Cyan RGB(0, 255, 255) in rectangle [16, 16, 48, 48]
    HBRUSH cyan_brush = CreateSolidBrush(RGB(0, 255, 255));
    RECT rc_cyan = { 16, 16, 48, 48 };
    FillRect(hdc, &rc_cyan, cyan_brush);
    DeleteObject(cyan_brush);

    // 3. Inner core: Bright Red RGB(255, 0, 0) in rectangle [24, 24, 40, 40]
    HBRUSH red_brush = CreateSolidBrush(RGB(255, 0, 0));
    RECT rc_red = { 24, 24, 40, 40 };
    FillRect(hdc, &rc_red, red_brush);
    DeleteObject(red_brush);

    ReleaseDC(hwnd, hdc);
    UpdateWindow(hwnd);
    Sleep(50); // Ensure window compositor has presented

    TE_IconCaptureInit();
    TE_IconCaptureClearCache();

    TE_TaskbarItemInfo item = {};
    item.bounds.left = 300;
    item.bounds.top = 300;
    item.bounds.right = 300 + W;
    item.bounds.bottom = 300 + H;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    if (FAILED(hr) || !out_bmp) {
        printf("  FAIL: Tier 4 extract failed hr=0x%08X\n", hr);
        passed = false;
    } else {
        printf("  Tier 4 Extract returned hr=0x%08X, out_bmp=%p, has_alpha=%d\n", hr, (void*)out_bmp, out_alpha);

        DIBSECTION ds = {};
        GetObject(out_bmp, sizeof(ds), &ds);
        uint32_t* pixels = (uint32_t*)ds.dsBm.bmBits;

        // Sample corner pixel (background): must be transparent alpha = 0
        uint32_t p_corner = ReadPixel(out_bmp, 5, 5);
        uint8_t a_corner = (uint8_t)((p_corner >> 24) & 0xFF);

        // Sample center pixel (Red core): must be opaque red (alpha ~ 255, R ~ 255, G ~ 0, B ~ 0)
        uint32_t p_center = ReadPixel(out_bmp, 128, 128);
        uint8_t a_center = (uint8_t)((p_center >> 24) & 0xFF);
        uint8_t r_center = (uint8_t)((p_center >> 16) & 0xFF);
        uint8_t g_center = (uint8_t)((p_center >> 8) & 0xFF);
        uint8_t b_center = (uint8_t)(p_center & 0xFF);

        // Sample cyan area (e.g. at x = 90, y = 128)
        uint32_t p_cyan = ReadPixel(out_bmp, 90, 128);
        uint8_t a_cyan = (uint8_t)((p_cyan >> 24) & 0xFF);
        uint8_t r_cyan = (uint8_t)((p_cyan >> 16) & 0xFF);
        uint8_t g_cyan = (uint8_t)((p_cyan >> 8) & 0xFF);
        uint8_t b_cyan = (uint8_t)(p_cyan & 0xFF);

        printf("  Corner background pixel (5,5)   : 0x%08X (alpha=%d)\n", p_corner, a_corner);
        printf("  Center Red core pixel   (128,128): 0x%08X (A=%d, R=%d, G=%d, B=%d)\n",
               p_center, a_center, r_center, g_center, b_center);
        printf("  Cyan ring pixel         (90,128) : 0x%08X (A=%d, R=%d, G=%d, B=%d)\n",
               p_cyan, a_cyan, r_cyan, g_cyan, b_cyan);

        // Verification 1: Corner background must be completely transparent
        if (a_corner != 0) {
            printf("  FAIL: Tier 4 background pixel has non-zero alpha: %d\n", a_corner);
            passed = false;
        } else {
            printf("  PASS: Tier 4 background successfully subtracted to alpha = 0.\n");
        }

        // Verification 2: Center red core must have high opacity and red dominance
        if (a_center < 250 || r_center < 200 || g_center > 50 || b_center > 50) {
            printf("  FAIL: Tier 4 red core color fidelity corrupted!\n");
            passed = false;
        } else {
            printf("  PASS: Tier 4 red core color fidelity preserved.\n");
        }

        // Verification 3: Cyan area must have high opacity, cyan dominance, and NO double premultiplication dimming
        if (a_cyan < 250 || g_cyan < 200 || b_cyan < 200 || r_cyan > 50) {
            printf("  FAIL: Tier 4 cyan color fidelity corrupted or dimmed!\n");
            passed = false;
        } else {
            printf("  PASS: Tier 4 cyan color fidelity intact (no double-premultiplication darkening).\n");
        }

        // Verification 4: Premultiplied alpha invariant across all 256x256 pixels
        bool premul_ok = true;
        for (int i = 0; i < 256 * 256; i++) {
            uint32_t p = pixels[i];
            uint8_t a = (uint8_t)((p >> 24) & 0xFF);
            uint8_t r = (uint8_t)((p >> 16) & 0xFF);
            uint8_t g = (uint8_t)((p >> 8) & 0xFF);
            uint8_t b = (uint8_t)(p & 0xFF);
            if (r > a + 1 || g > a + 1 || b > a + 1) { // 1 tolerance for rounding
                premul_ok = false;
                printf("  FAIL: Premultiplied invariant violated at index %d: 0x%08X\n", i, p);
                break;
            }
        }
        if (premul_ok) {
            printf("  PASS: All pixels satisfy premultiplied alpha condition (R<=A, G<=A, B<=A).\n");
        } else {
            passed = false;
        }
    }

    DestroyWindow(hwnd);
    TE_IconCaptureClearCache();
    TE_IconCaptureShutdown();
    return passed;
}

// ----------------------------------------------------------------------------
// Challenge 4: Adversarial Edge Cases (Nulls, Inverted Bounds, Bogus Identifiers)
// ----------------------------------------------------------------------------
bool Challenge4_AdversarialEdgeCases() {
    printf("\n[CHALLENGE 4] Adversarial Edge Cases & Fault Handling\n");
    bool passed = true;

    TE_IconCaptureInit();

    // 4A. Null pointer checks
    HBITMAP bmp = NULL;
    BOOL alpha = FALSE;
    if (TE_IconCaptureExtract(NULL, &bmp, &alpha) != TE_E_INVALIDARG) {
        printf("  FAIL: Expected TE_E_INVALIDARG for NULL item\n");
        passed = false;
    }
    TE_TaskbarItemInfo valid_item = {};
    if (TE_IconCaptureExtract(&valid_item, NULL, &alpha) != TE_E_INVALIDARG) {
        printf("  FAIL: Expected TE_E_INVALIDARG for NULL out_bitmap\n");
        passed = false;
    }

    // 4B. Inverted bounds
    TE_TaskbarItemInfo inv_item = {};
    inv_item.bounds.left = 100;
    inv_item.bounds.right = 50;  // right < left
    inv_item.bounds.top = 100;
    inv_item.bounds.bottom = 50; // bottom < top
    HRESULT hr_inv = TE_IconCaptureExtract(&inv_item, &bmp, &alpha);
    if (SUCCEEDED(hr_inv)) {
        printf("  FAIL: Inverted bounds should fail extraction!\n");
        passed = false;
    } else {
        printf("  PASS: Inverted bounds rejected gracefully (hr=0x%08X).\n", hr_inv);
    }

    // 4C. Bogus non-existent file path
    TE_TaskbarItemInfo bogus_file_item = {};
    bogus_file_item.app_id = L"C:\\DoesNotExist_FakePath_12345\\app.exe";
    HRESULT hr_file = TE_IconCaptureExtract(&bogus_file_item, &bmp, &alpha);
    if (SUCCEEDED(hr_file)) {
        printf("  FAIL: Bogus file path should fail extraction!\n");
        passed = false;
    } else {
        printf("  PASS: Bogus executable path handled safely (hr=0x%08X).\n", hr_file);
    }

    // 4D. Dead PID and Invalid HWND
    TE_TaskbarItemInfo dead_pid_item = {};
    dead_pid_item.process_id = 99999999;
    dead_pid_item.hwnd = (HWND)(uintptr_t)0xDEADBEEF;
    HRESULT hr_dead = TE_IconCaptureExtract(&dead_pid_item, &bmp, &alpha);
    if (SUCCEEDED(hr_dead)) {
        printf("  FAIL: Dead PID and bogus HWND should fail extraction!\n");
        passed = false;
    } else {
        printf("  PASS: Dead PID and bogus HWND failed safely without crash (hr=0x%08X).\n", hr_dead);
    }

    TE_IconCaptureShutdown();
    return passed;
}

// ----------------------------------------------------------------------------
// Challenge 5: Multithreaded Concurrency Stress & Zero-GDI Leakage
// ----------------------------------------------------------------------------
bool Challenge5_MultithreadedStressAndGDIReclamation() {
    printf("\n[CHALLENGE 5] Multithreaded Cold-Cache Stress & GDI Leak Verification\n");
    bool passed = true;

    DWORD gdi_initial = GetCurrentGdiCount();
    printf("  Initial GDI handle count: %u\n", gdi_initial);

    TE_IconCaptureInit();

    // Create test window with standard system icon
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_StressWndClass";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Stress", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    const int NUM_THREADS = 12;
    const int ITERATIONS = 50;
    std::atomic<bool> start_signal{false};
    std::atomic<int> success_count{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < NUM_THREADS; t++) {
        workers.emplace_back([&start_signal, &success_count, hwnd, ITERATIONS, t]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!start_signal.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            for (int i = 0; i < ITERATIONS; i++) {
                TE_TaskbarItemInfo item = {};
                if (t % 2 == 0) {
                    item.hwnd = hwnd;
                } else {
                    item.app_id = L"C:\\Windows\\explorer.exe";
                }

                HBITMAP b = NULL;
                BOOL a = FALSE;
                if (SUCCEEDED(TE_IconCaptureExtract(&item, &b, &a)) && b) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }

                if (i % 15 == 0 && t == 0) {
                    TE_IconCaptureClearCache();
                }
            }
            CoUninitialize();
        });
    }

    start_signal.store(true, std::memory_order_release);
    for (auto& th : workers) th.join();

    DWORD gdi_during = GetCurrentGdiCount();
    printf("  Concurrent extractions completed: %d. Active GDI count: %u (delta: +%d)\n",
           success_count.load(), gdi_during, (int)(gdi_during - gdi_initial));

    TE_IconCaptureClearCache();
    DestroyWindow(hwnd);
    TE_IconCaptureShutdown();

    DWORD gdi_final = GetCurrentGdiCount();
    printf("  Final GDI handle count after Shutdown: %u (delta from start: %d)\n",
           gdi_final, (int)(gdi_final - gdi_initial));

    if (gdi_final > gdi_initial) {
        printf("  FAIL: Leaked %d GDI handles across concurrent cycles!\n", (int)(gdi_final - gdi_initial));
        passed = false;
    } else {
        printf("  PASS: Zero GDI handles leaked. Perfect lifecycle reclamation.\n");
    }

    return passed;
}

// ----------------------------------------------------------------------------
// Main Runner
// ----------------------------------------------------------------------------
int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("TaskbarEngine Milestone 1 Iteration 2 Gate: Challenger Harness\n");
    printf("=================================================================\n");

    bool c1 = Challenge1_PureBlackPixelPreservation();
    bool c2 = Challenge2_MonochromeMaskDecoding();
    bool c3 = Challenge3_Tier4ScreenCapture();
    bool c4 = Challenge4_AdversarialEdgeCases();
    bool c5 = Challenge5_MultithreadedStressAndGDIReclamation();

    printf("\n=================================================================\n");
    printf("SUMMARY OF EMPIRICAL CHALLENGES:\n");
    printf("  Challenge 1 (Pure Black Pixel Opacity) : %s\n", c1 ? "PASS" : "FAIL");
    printf("  Challenge 2 (Monochrome Mask Decoding) : %s\n", c2 ? "PASS" : "FAIL");
    printf("  Challenge 3 (Tier 4 Screen Capture)    : %s\n", c3 ? "PASS" : "FAIL");
    printf("  Challenge 4 (Adversarial Edge Cases)   : %s\n", c4 ? "PASS" : "FAIL");
    printf("  Challenge 5 (Stress & GDI Reclamation) : %s\n", c5 ? "PASS" : "FAIL");
    printf("=================================================================\n");

    bool all_passed = c1 && c2 && c3 && c4 && c5;
    if (all_passed) {
        printf("FINAL CHALLENGER VERDICT: ALL 5 EMPIRICAL CHALLENGES PASSED.\n");
    } else {
        printf("FINAL CHALLENGER VERDICT: EMPIRICAL CHALLENGE FAILED.\n");
    }
    printf("=================================================================\n");

    CoUninitialize();
    return all_passed ? 0 : 1;
}
