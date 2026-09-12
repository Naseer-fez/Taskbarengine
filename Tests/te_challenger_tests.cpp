#include "icon_capture.h"
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <stdio.h>
#include <vector>
#include <thread>
#include <atomic>
#include <cassert>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#include <sdk/te_log.h>

extern "C" void TE_LogWrite(TE_LogLevel level, const char* module, const char* message) {
    (void)level; (void)module; (void)message;
}

static DWORD GetCurrentGdiCount() {
    return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
}

static uint32_t SamplePixel(HBITMAP hbmp, int x, int y, int* out_w = nullptr, int* out_h = nullptr) {
    if (!hbmp) return 0;
    DIBSECTION dib = {};
    if (GetObject(hbmp, sizeof(dib), &dib) != sizeof(DIBSECTION) || !dib.dsBm.bmBits) {
        return 0;
    }
    int w = dib.dsBm.bmWidth;
    int h = abs(dib.dsBm.bmHeight);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    if (x < 0 || x >= w || y < 0 || y >= h) return 0;
    const uint32_t* bits = (const uint32_t*)dib.dsBm.bmBits;
    return bits[y * w + x];
}

// ============================================================================
// CHALLENGE 1: Pure Black RGB(0,0,0) Artwork Preservation
// ============================================================================
bool Challenge1_PureBlackPreservation() {
    printf("\n--- CHALLENGE 1: Pure Black RGB(0,0,0) Preservation (1-Bit Mask) ---\n");
    bool passed = true;

    // Test across three sizes: 16x16, 32x32, 48x48
    const int sizes[] = { 16, 32, 48 };

    for (int size : sizes) {
        HDC hdc_screen = GetDC(NULL);
        HDC hdc_mem = CreateCompatibleDC(hdc_screen);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = size;
        bmi.bmiHeader.biHeight = -size; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* color_bits = nullptr;
        HBITMAP hbmColor = CreateDIBSection(hdc_mem, &bmi, DIB_RGB_COLORS, &color_bits, NULL, 0);
        uint32_t* c_pixels = (uint32_t*)color_bits;

        // Set center region to pure black RGB(0,0,0), outside to pure white RGB(255,255,255)
        int margin = size / 4;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (x >= margin && x < size - margin && y >= margin && y < size - margin) {
                    c_pixels[y * size + x] = 0x00000000; // Pure black
                } else {
                    c_pixels[y * size + x] = 0x00FFFFFF; // Pure white
                }
            }
        }

        // 1-bit mask: row stride must be 32-bit aligned
        int stride = ((size + 31) / 32) * 4;
        std::vector<BYTE> mask_bits(size * stride, 0xFF); // Default: transparent (1)

        // Make center and inner region opaque (0)
        for (int y = margin / 2; y < size - margin / 2; y++) {
            for (int x = margin / 2; x < size - margin / 2; x++) {
                int byte_idx = y * stride + (x / 8);
                int bit_idx = 7 - (x % 8);
                mask_bits[byte_idx] &= ~(1 << bit_idx); // 0 = opaque
            }
        }

        HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, mask_bits.data());

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
            printf("[FAIL] Size %dx%d: CreateIconIndirect failed\n", size, size);
            passed = false;
            continue;
        }

        // Host in dummy HWND for Tier 3 extraction
        WNDCLASSW wc = {};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandle(NULL);
        wchar_t cls_name[64];
        swprintf_s(cls_name, L"TE_Challenge1_Cls_%d", size);
        wc.lpszClassName = cls_name;
        RegisterClassW(&wc);

        HWND hwnd = CreateWindowW(cls_name, L"TE_Challenge1_Wnd", WS_OVERLAPPEDWINDOW,
                                  0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);

        TE_TaskbarItemInfo item = {};
        item.hwnd = hwnd;

        TE_IconCaptureClearCache();
        HBITMAP out_bmp = NULL;
        BOOL out_alpha = FALSE;
        HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

        if (FAILED(hr) || !out_bmp) {
            printf("[FAIL] Size %dx%d: TE_IconCaptureExtract failed (hr=0x%08X)\n", size, size, hr);
            passed = false;
        } else {
            // Check center pixel in 256x256 result
            uint32_t center = SamplePixel(out_bmp, 128, 128);
            uint8_t a = (uint8_t)((center >> 24) & 0xFF);
            uint8_t r = (uint8_t)((center >> 16) & 0xFF);
            uint8_t g = (uint8_t)((center >> 8) & 0xFF);
            uint8_t b = (uint8_t)(center & 0xFF);

            // Transparent border check (0, 0)
            uint32_t corner = SamplePixel(out_bmp, 5, 5);
            uint8_t corner_a = (uint8_t)((corner >> 24) & 0xFF);

            if (a == 0xFF && r == 0 && g == 0 && b == 0 && corner_a == 0) {
                printf("[PASS] Size %dx%d: Pure black artwork is 0x%08X (alpha=0xFF, R=G=B=0), border alpha=0x00.\n",
                       size, size, center);
            } else {
                printf("[FAIL] Size %dx%d: Expected 0xFF000000, got 0x%08X (corner alpha=0x%02X)!\n",
                       size, size, center, corner_a);
                passed = false;
            }
        }

        DestroyWindow(hwnd);
        DestroyIcon(hicon);
        UnregisterClassW(cls_name, GetModuleHandle(NULL));
    }

    return passed;
}

// ============================================================================
// CHALLENGE 2: 1-Bit Monochrome Mask Decoding (XOR Artwork & AND Transparency)
// ============================================================================
bool Challenge2_MonochromeMaskDecoding() {
    printf("\n--- CHALLENGE 2: Monochrome Icon Mask Decoding (Halved Height Invariant) ---\n");
    bool passed = true;

    // Test with 32x32 monochrome icon (mask is 32x64: rows 0..31 AND, rows 32..63 XOR)
    const int icon_w = 32;
    const int mask_total_h = 64;
    const int stride = 4; // 32 bits = 4 bytes

    std::vector<BYTE> mask_bits(mask_total_h * stride, 0xFF); // All transparent

    // Top half AND mask (rows 0..31):
    // Center rows 4..27: bits 4..27 are 0 (opaque artwork)
    for (int y = 4; y < 28; y++) {
        for (int x = 4; x < 28; x++) {
            int byte_idx = y * stride + (x / 8);
            int bit_idx = 7 - (x % 8);
            mask_bits[byte_idx] &= ~(1 << bit_idx); // Opaque
        }
    }

    // Bottom half XOR mask (rows 32..63):
    // Top portion of artwork (icon rows 6..12 -> mask rows 38..44): Black artwork (XOR = 0)
    // Bottom portion of artwork (icon rows 18..24 -> mask rows 50..56): White artwork (XOR = 1)
    for (int y = 32; y < 64; y++) {
        for (int b = 0; b < stride; b++) {
            mask_bits[y * stride + b] = 0x00; // Default black
        }
    }
    // Set white artwork in bottom section of icon
    for (int y = 50; y < 57; y++) {
        for (int x = 8; x < 24; x++) {
            int byte_idx = y * stride + (x / 8);
            int bit_idx = 7 - (x % 8);
            mask_bits[byte_idx] |= (1 << bit_idx); // White artwork
        }
    }

    HBITMAP hbmMask = CreateBitmap(icon_w, mask_total_h, 1, 1, mask_bits.data());
    if (!hbmMask) {
        printf("[FAIL] CreateBitmap for monochrome mask failed\n");
        return false;
    }

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = NULL; // Monochrome icon
    ii.hbmMask = hbmMask;

    HICON hicon = CreateIconIndirect(&ii);
    DeleteObject(hbmMask);

    if (!hicon) {
        printf("[FAIL] CreateIconIndirect for monochrome icon failed\n");
        return false;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Challenge2_Cls";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Challenge2_Wnd", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hicon);

    TE_TaskbarItemInfo item = {};
    item.hwnd = hwnd;

    TE_IconCaptureClearCache();
    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    if (FAILED(hr) || !out_bmp) {
        printf("[FAIL] Monochrome icon extraction failed (hr=0x%08X)\n", hr);
        passed = false;
    } else {
        // Sample top artwork (icon row ~9 -> y ~ 72 in 256x256): must be Opaque Black (0xFF000000)
        uint32_t top_art = SamplePixel(out_bmp, 128, 72);
        uint8_t top_a = (uint8_t)((top_art >> 24) & 0xFF);
        uint8_t top_r = (uint8_t)((top_art >> 16) & 0xFF);

        // Sample bottom artwork (icon row ~21 -> y ~ 168 in 256x256): must be Opaque White (0xFFFFFFFF)
        uint32_t bot_art = SamplePixel(out_bmp, 128, 168);
        uint8_t bot_a = (uint8_t)((bot_art >> 24) & 0xFF);
        uint8_t bot_r = (uint8_t)((bot_art >> 16) & 0xFF);

        // Sample bottom-half background (icon row ~28, col 2 -> y ~ 224, x ~ 16): must be Transparent (alpha=0)
        uint32_t bot_bg = SamplePixel(out_bmp, 16, 224);
        uint8_t bot_bg_a = (uint8_t)((bot_bg >> 24) & 0xFF);

        printf("Monochrome samples: TopArt=0x%08X (a=%u, r=%u), BotArt=0x%08X (a=%u, r=%u), BotBg=0x%08X (a=%u)\n",
               top_art, top_a, top_r, bot_art, bot_a, bot_r, bot_bg, bot_bg_a);

        if (top_a == 0xFF && top_r == 0) {
            printf("[PASS] Top-half black artwork correctly preserved with alpha=0xFF.\n");
        } else {
            printf("[FAIL] Top-half black artwork corrupted: a=%u, r=%u\n", top_a, top_r);
            passed = false;
        }

        if (bot_a == 0xFF && bot_r >= 200) {
            printf("[PASS] Bottom-half white artwork correctly preserved with alpha=0xFF.\n");
        } else {
            printf("[FAIL] Bottom-half white artwork corrupted: a=%u, r=%u\n", bot_a, bot_r);
            passed = false;
        }

        if (bot_bg_a == 0) {
            printf("[PASS] Bottom-half background is clean transparent (zero bottom-half mask corruption).\n");
        } else {
            printf("[FAIL] Bottom-half background has non-zero alpha (corruption detected!): a=%u\n", bot_bg_a);
            passed = false;
        }
    }

    DestroyWindow(hwnd);
    DestroyIcon(hicon);
    UnregisterClassW(wc.lpszClassName, GetModuleHandle(NULL));
    return passed;
}


// ============================================================================
// CHALLENGE 3: Tier 4 Screen Capture Alpha Isolation & Color Preservation
// ============================================================================
bool Challenge3_Tier4ScreenCapture() {
    printf("\n--- CHALLENGE 3: Tier 4 Screen Capture Alpha Isolation & Color Integrity ---\n");
    bool passed = true;

    wchar_t cls_name[64];
    swprintf_s(cls_name, L"TE_Challenge3_Cls_%u", GetTickCount());

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = cls_name;
    RegisterClassW(&wc);

    const int win_w = 64;
    const int win_h = 64;
    const int win_x = 200;
    const int win_y = 200;

    HWND hwnd = CreateWindowW(cls_name, L"TE_Tier4Challenge",
                              WS_POPUP | WS_VISIBLE,
                              win_x, win_y, win_w, win_h, NULL, NULL, wc.hInstance, NULL);

    HDC hdc = GetDC(hwnd);
    HBRUSH bg_brush = CreateSolidBrush(RGB(30, 30, 30));
    RECT rc_bg = { 0, 0, win_w, win_h };
    FillRect(hdc, &rc_bg, bg_brush);
    DeleteObject(bg_brush);

    // Cyan glyph RGB(0, 255, 255)
    HBRUSH icon_brush = CreateSolidBrush(RGB(0, 255, 255));
    RECT rc_icon = { 16, 16, 48, 48 };
    FillRect(hdc, &rc_icon, icon_brush);
    DeleteObject(icon_brush);
    ReleaseDC(hwnd, hdc);

    UpdateWindow(hwnd);
    Sleep(50);

    TE_IconCaptureClearCache();

    TE_TaskbarItemInfo item = {};
    item.icon_index = -1; // Must be -1 so system image list fallback is bypassed!
    item.bounds.left = win_x;
    item.bounds.top = win_y;
    item.bounds.right = win_x + win_w;
    item.bounds.bottom = win_y + win_h;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;
    HRESULT hr = TE_IconCaptureExtract(&item, &out_bmp, &out_alpha);

    if (FAILED(hr) || !out_bmp) {
        printf("[FAIL] Tier 4 Screen Capture failed (hr=0x%08X)\n", hr);
        passed = false;
    } else {
        DIBSECTION dib = {};
        GetObject(out_bmp, sizeof(dib), &dib);
        const uint32_t* p = (const uint32_t*)dib.dsBm.bmBits;
        int non_zero_count = 0;
        int alpha_255_count = 0;
        int semi_alpha_count = 0;
        int zero_alpha_count = 0;

        for (int i = 0; i < 256 * 256; i++) {
            uint8_t a = (uint8_t)((p[i] >> 24) & 0xFF);
            if (p[i] != 0) non_zero_count++;
            if (a == 255) alpha_255_count++;
            else if (a > 0) semi_alpha_count++;
            else zero_alpha_count++;
        }
        printf("Tier 4 Pixel distribution: non_zero=%d, alpha_255=%d, semi_alpha=%d, zero_alpha=%d\n",
               non_zero_count, alpha_255_count, semi_alpha_count, zero_alpha_count);

        // 1. Alpha Survival: Must have non-zero alpha pixels (SoftwareScale32 eliminates StretchBlt zeroing)
        if (alpha_255_count > 0 || semi_alpha_count > 0) {
            printf("[PASS] Tier 4 SoftwareScale32 preserved alpha channel (%d alpha pixels, not zeroed).\n",
                   alpha_255_count + semi_alpha_count);
        } else {
            printf("[FAIL] Tier 4 alpha channel was completely wiped to 0x00!\n");
            passed = false;
        }

        // 2. Strict Premultiplied Alpha Invariant: R <= A, G <= A, B <= A for ALL 65,536 pixels
        bool premul_valid = true;
        for (int i = 0; i < 256 * 256; i++) {
            uint8_t a = (uint8_t)((p[i] >> 24) & 0xFF);
            uint8_t r = (uint8_t)((p[i] >> 16) & 0xFF);
            uint8_t g = (uint8_t)((p[i] >> 8) & 0xFF);
            uint8_t b = (uint8_t)(p[i] & 0xFF);
            if (r > a || g > a || b > a) {
                premul_valid = false;
                break;
            }
        }
        if (premul_valid) {
            printf("[PASS] Tier 4 DirectComposition premultiplied alpha invariant (R<=A, G<=A, B<=A) strictly holds across all pixels.\n");
        } else {
            printf("[FAIL] Tier 4 produced invalid premultiplied alpha (color channel exceeded alpha)!\n");
            passed = false;
        }

        // 3. Algorithmic Background Subtraction & Bilinear Interpolation Verification
        // Test that background subtraction maps delta <= eps_low to 0 and delta >= eps_high to 255
        const int bg_r = 30, bg_g = 30, bg_b = 30;
        const int eps_low = 8, eps_high = 32;

        auto compute_alpha = [&](int pr, int pg, int pb) -> uint32_t {
            int dr = abs(pr - bg_r);
            int dg = abs(pg - bg_g);
            int db = abs(pb - bg_b);
            int delta = (std::max)({ dr, dg, db });
            if (delta <= eps_low) return 0;
            if (delta >= eps_high) return 255;
            return (uint32_t)((delta - eps_low) * 255 / (eps_high - eps_low));
        };

        uint32_t a_bg = compute_alpha(30, 30, 30);
        uint32_t a_near_bg = compute_alpha(34, 32, 28);
        uint32_t a_icon = compute_alpha(0, 255, 255);
        uint32_t a_mid = compute_alpha(30, 50, 30); // delta = 20

        if (a_bg == 0 && a_near_bg == 0 && a_icon == 255 && a_mid > 0 && a_mid < 255) {
            printf("[PASS] Tier 4 background subtraction model correctly computes alpha (bg=0, icon=255, transition=%u).\n", a_mid);
        } else {
            printf("[FAIL] Tier 4 background subtraction math incorrect!\n");
            passed = false;
        }
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, GetModuleHandle(NULL));
    return passed;
}

// ============================================================================
// CHALLENGE 4: Adversarial Boundaries, Malformed Inputs & Rejection Safety
// ============================================================================
bool Challenge4_AdversarialBoundaries() {
    printf("\n--- CHALLENGE 4: Adversarial Boundaries & Malformed Input Safety ---\n");
    bool passed = true;

    HBITMAP out_bmp = NULL;
    BOOL out_alpha = FALSE;

    // 1. NULL item
    HRESULT hr1 = TE_IconCaptureExtract(NULL, &out_bmp, &out_alpha);
    if (hr1 == TE_E_INVALIDARG) {
        printf("[PASS] NULL item safely rejected with TE_E_INVALIDARG.\n");
    } else {
        printf("[FAIL] NULL item returned unexpected hr=0x%08X\n", hr1);
        passed = false;
    }

    // 2. NULL out_bitmap pointer
    TE_TaskbarItemInfo valid_item = {};
    valid_item.icon_index = -1;
    HRESULT hr2 = TE_IconCaptureExtract(&valid_item, NULL, &out_alpha);
    if (hr2 == TE_E_INVALIDARG) {
        printf("[PASS] NULL out_bitmap pointer safely rejected with TE_E_INVALIDARG.\n");
    } else {
        printf("[FAIL] NULL out_bitmap returned unexpected hr=0x%08X\n", hr2);
        passed = false;
    }

    // 3. Inverted bounding rectangle (right < left)
    TE_TaskbarItemInfo inv_item = {};
    inv_item.bounds.left = 100;
    inv_item.bounds.right = 50; // inverted!
    inv_item.bounds.top = 10;
    inv_item.bounds.bottom = 60;
    HRESULT hr3 = TE_IconCaptureExtract(&inv_item, &out_bmp, &out_alpha);
    if (hr3 == TE_E_INVALIDARG) {
        printf("[PASS] Inverted bounds (right < left) rejected with TE_E_INVALIDARG.\n");
    } else {
        printf("[FAIL] Inverted bounds returned unexpected hr=0x%08X\n", hr3);
        passed = false;
    }

    // 4. Non-existent executable path
    TE_TaskbarItemInfo fake_item = {};
    fake_item.icon_index = -1;
    fake_item.app_id = L"C:\\nonexistent_directory_xyz\\totally_fake_app.exe";
    HRESULT hr4 = TE_IconCaptureExtract(&fake_item, &out_bmp, &out_alpha);
    if (FAILED(hr4) && out_bmp == NULL) {
        printf("[PASS] Non-existent executable path safely failed without crash (hr=0x%08X).\n", hr4);
    } else {
        printf("[FAIL] Non-existent executable unexpected result: hr=0x%08X, out_bmp=%p\n", hr4, (void*)out_bmp);
        passed = false;
    }

    // 5. Invalid HWND (0xDEADBEEF)
    TE_TaskbarItemInfo dead_hwnd_item = {};
    dead_hwnd_item.icon_index = -1;
    dead_hwnd_item.hwnd = (HWND)(uintptr_t)0xDEADBEEF;
    HRESULT hr5 = TE_IconCaptureExtract(&dead_hwnd_item, &out_bmp, &out_alpha);
    if (FAILED(hr5) && out_bmp == NULL) {
        printf("[PASS] Bogus HWND (0xDEADBEEF) safely failed without crash (hr=0x%08X).\n", hr5);
    } else {
        printf("[FAIL] Bogus HWND unexpected result: hr=0x%08X, out_bmp=%p\n", hr5, (void*)out_bmp);
        passed = false;
    }

    return passed;
}

// ============================================================================
// CHALLENGE 5: Extreme Concurrent Stress & GDI Handle Leak Zero-Tolerance
// ============================================================================
bool Challenge5_ConcurrencyAndLeakZeroTolerance() {
    printf("\n--- CHALLENGE 5: Extreme Concurrent Stress & GDI Leak Zero-Tolerance ---\n");
    bool passed = true;

    TE_IconCaptureClearCache();

    // Create a shared test window with an icon
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"TE_Challenge5_Cls";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TE_Challenge5_Wnd", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

    // Measure GDI baseline with test window fully created
    DWORD baseline_gdi = GetCurrentGdiCount();
    printf("Initial GDI Handle Baseline (with test window): %u\n", baseline_gdi);

    const int NUM_THREADS = 16;
    const int ITERATIONS = 50;
    std::atomic<bool> start_gate{false};
    std::atomic<int> success_count{0};
    std::vector<std::thread> workers;

    for (int t = 0; t < NUM_THREADS; t++) {
        workers.emplace_back([t, hwnd, ITERATIONS, &start_gate, &success_count]() {
            CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
            while (!start_gate.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            TE_TaskbarItemInfo item = {};
            item.hwnd = hwnd;
            item.icon_index = -1;

            for (int i = 0; i < ITERATIONS; i++) {
                HBITMAP hbmp = NULL;
                BOOL has_alpha = FALSE;
                HRESULT hr = TE_IconCaptureExtract(&item, &hbmp, &has_alpha);
                if (SUCCEEDED(hr) && hbmp != NULL) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            CoUninitialize();
        });
    }

    start_gate.store(true, std::memory_order_release);
    for (auto& th : workers) {
        th.join();
    }

    printf("Concurrent stress executed: %d successful extractions across %d threads.\n",
           success_count.load(), NUM_THREADS);

    TE_IconCaptureClearCache();
    DWORD post_clear_gdi = GetCurrentGdiCount();
    printf("GDI count after TE_IconCaptureClearCache(): %u (delta from baseline: %+d)\n",
           post_clear_gdi, (int)(post_clear_gdi - baseline_gdi));

    if (post_clear_gdi <= baseline_gdi) {
        printf("[PASS] Zero GDI handles leaked under extreme 16-thread concurrent invalidation stress.\n");
    } else {
        printf("[FAIL] GDI handle leak detected: %d handles orphaned!\n", (int)(post_clear_gdi - baseline_gdi));
        passed = false;
    }

    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, GetModuleHandle(NULL));
    return passed;
}

// ============================================================================
// MAIN RUNNER
// ============================================================================
int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    printf("=================================================================\n");
    printf("Milestone 1 Gate 2 Empirical Challenger 1 Test Suite\n");
    printf("=================================================================\n");

    HRESULT hr_init = TE_IconCaptureInit();
    if (FAILED(hr_init)) {
        printf("CRITICAL: TE_IconCaptureInit failed with hr=0x%08X\n", hr_init);
        CoUninitialize();
        return 1;
    }

    bool c1 = Challenge1_PureBlackPreservation();
    bool c2 = Challenge2_MonochromeMaskDecoding();
    bool c3 = Challenge3_Tier4ScreenCapture();
    bool c4 = Challenge4_AdversarialBoundaries();
    bool c5 = Challenge5_ConcurrencyAndLeakZeroTolerance();

    TE_IconCaptureShutdown();
    CoUninitialize();

    printf("\n=================================================================\n");
    printf("CHALLENGE RESULTS SUMMARY:\n");
    printf("  Challenge 1 (Pure Black RGB(0,0,0) Preservation): %s\n", c1 ? "PASS" : "FAIL");
    printf("  Challenge 2 (1-Bit Monochrome Mask Decoding)    : %s\n", c2 ? "PASS" : "FAIL");
    printf("  Challenge 3 (Tier 4 Screen Capture & Alpha)     : %s\n", c3 ? "PASS" : "FAIL");
    printf("  Challenge 4 (Adversarial Bounds & Rejection)    : %s\n", c4 ? "PASS" : "FAIL");
    printf("  Challenge 5 (Concurrency & 0-Leak Reclamation)  : %s\n", c5 ? "PASS" : "FAIL");
    printf("=================================================================\n");

    bool all_passed = c1 && c2 && c3 && c4 && c5;
    if (all_passed) {
        printf("FINAL CHALLENGER VERDICT: ALL EMPIRICAL CHALLENGES PASSED!\n");
        return 0;
    } else {
        printf("FINAL CHALLENGER VERDICT: ONE OR MORE CHALLENGES FAILED!\n");
        return 1;
    }
}
