/**
 * @file test_m2_challenger2_stress.cpp
 * @brief Empirical Challenger 2 Stress Harness & Boundary Value Analysis for Feature F6.
 *
 * Tests:
 * 1. Boundary Value Analysis (BVA) strictly around 1.0010f (+/- 0.0001f, underflow, negatives, NaNs).
 * 2. TE_DCompIsOverlayActive predicate (empty/null, single, 64-icon array, 10,000-icon stress).
 * 3. Dirty-state caching of IDCompositionEffectGroup::SetOpacity (resting, transition, steady-hover, multi-icon).
 */

#include <windows.h>
#include <dcomp.h>
#include <d2d1.h>
#include <d3d11.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <limits>
#include <vector>
#include <atomic>
#include <cassert>

// Stub logging for dcomp_overlay
#include <sdk/te_log.h>
extern "C" {
void TE_LogWrite(TE_LogLevel level, const char* tag, const char* message) {
    (void)level; (void)tag; (void)message;
}
}

// Global state required by icon_hover_internal.h
#include "icon_hover_internal.h"
TE_IconHoverState g_hover_state = {};

// Mock COM Objects for DComp Transform and EffectGroup
class MockEffectGroup : public IDCompositionEffectGroup {
public:
    std::atomic<ULONG> ref_count{1};
    int set_opacity_calls = 0;
    float last_opacity = -999.0f;
    std::vector<float> opacity_history;

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDCompositionEffectGroup) || riid == __uuidof(IDCompositionEffect)) {
            *ppv = static_cast<IDCompositionEffectGroup*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++ref_count; }
    STDMETHOD_(ULONG, Release)() override {
        ULONG r = --ref_count;
        if (r == 0) delete this;
        return r;
    }

    // IDCompositionEffectGroup
    STDMETHOD(SetOpacity)(float opacity) override {
        set_opacity_calls++;
        last_opacity = opacity;
        opacity_history.push_back(opacity);
        return S_OK;
    }
    STDMETHOD(SetOpacity)(IDCompositionAnimation* anim) override {
        (void)anim;
        return E_NOTIMPL;
    }
    STDMETHOD(SetTransform3D)(IDCompositionTransform3D* transform) override {
        (void)transform;
        return S_OK;
    }
};

class MockScaleTransform : public IDCompositionScaleTransform {
public:
    std::atomic<ULONG> ref_count{1};
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    float center_x = 0.0f;
    float center_y = 0.0f;

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDCompositionScaleTransform) || riid == __uuidof(IDCompositionTransform)) {
            *ppv = static_cast<IDCompositionScaleTransform*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++ref_count; }
    STDMETHOD_(ULONG, Release)() override {
        ULONG r = --ref_count;
        if (r == 0) delete this;
        return r;
    }

    STDMETHOD(SetScaleX)(float sx) override { scale_x = sx; return S_OK; }
    STDMETHOD(SetScaleX)(IDCompositionAnimation* a) override { (void)a; return E_NOTIMPL; }
    STDMETHOD(SetScaleY)(float sy) override { scale_y = sy; return S_OK; }
    STDMETHOD(SetScaleY)(IDCompositionAnimation* a) override { (void)a; return E_NOTIMPL; }
    STDMETHOD(SetCenterX)(float cx) override { center_x = cx; return S_OK; }
    STDMETHOD(SetCenterX)(IDCompositionAnimation* a) override { (void)a; return E_NOTIMPL; }
    STDMETHOD(SetCenterY)(float cy) override { center_y = cy; return S_OK; }
    STDMETHOD(SetCenterY)(IDCompositionAnimation* a) override { (void)a; return E_NOTIMPL; }
};

class MockTranslateTransform : public IDCompositionTranslateTransform {
public:
    std::atomic<ULONG> ref_count{1};
    float offset_x = 0.0f;
    float offset_y = 0.0f;

    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDCompositionTranslateTransform) || riid == __uuidof(IDCompositionTransform)) {
            *ppv = static_cast<IDCompositionTranslateTransform*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++ref_count; }
    STDMETHOD_(ULONG, Release)() override {
        ULONG r = --ref_count;
        if (r == 0) delete this;
        return r;
    }

    STDMETHOD(SetOffsetX)(float ox) override { offset_x = ox; return S_OK; }
    STDMETHOD(SetOffsetX)(IDCompositionAnimation* a) override { (void)a; return E_NOTIMPL; }
    STDMETHOD(SetOffsetY)(float oy) override { offset_y = oy; return S_OK; }
    STDMETHOD(SetOffsetY)(IDCompositionAnimation* a) override { (void)a; return E_NOTIMPL; }
};

// Include dcomp_overlay.cpp directly so we can inspect and wire static arrays
#include "dcomp_overlay.cpp"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define EXPECT_TRUE(cond, desc) do { \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] %s (line %d): %s\n", desc, __LINE__, #cond); \
    } \
} while (0)

#define EXPECT_FLOAT_EQ(actual, expected, desc) do { \
    if (fabsf((actual) - (expected)) < 1e-6f) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] %s (line %d): actual=%f, expected=%f\n", desc, __LINE__, (double)(actual), (double)(expected)); \
    } \
} while (0)

// ============================================================================
// TEST 1: Boundary Value Analysis (BVA) around 1.0010f
// ============================================================================
static void TestBVAThreshold() {
    printf("\n--- TEST 1: Boundary Value Analysis (BVA) strictly around 1.0010f ---\n");

    // Exact BVA points specified by prompt
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(1.0000f), 0.0f, "S = 1.0000f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(1.0009f), 0.0f, "S = 1.0009f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(1.0010f), 0.0f, "S = 1.0010f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(1.0011f), 1.0f, "S = 1.0011f -> opacity = 1.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(1.0500f), 1.0f, "S = 1.0500f -> opacity = 1.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(2.0000f), 1.0f, "S = 2.0000f -> opacity = 1.0f");

    // 1-ULP (Unit in Last Place) exact bit-level analysis
    float s_center = 1.0010f;
    uint32_t bits_center;
    memcpy(&bits_center, &s_center, sizeof(uint32_t));

    uint32_t bits_minus_1_ulp = bits_center - 1;
    uint32_t bits_plus_1_ulp = bits_center + 1;
    float s_minus_1_ulp, s_plus_1_ulp;
    memcpy(&s_minus_1_ulp, &bits_minus_1_ulp, sizeof(float));
    memcpy(&s_plus_1_ulp, &bits_plus_1_ulp, sizeof(float));

    printf("  ULP Analysis: center=0x%08X (%.10f)\n", bits_center, (double)s_center);
    printf("               -1 ULP=0x%08X (%.10f) -> opacity=%.1f\n", bits_minus_1_ulp, (double)s_minus_1_ulp, (double)TE_DCompComputeVisualOpacity(s_minus_1_ulp));
    printf("               center=0x%08X (%.10f) -> opacity=%.1f\n", bits_center, (double)s_center, (double)TE_DCompComputeVisualOpacity(s_center));
    printf("               +1 ULP=0x%08X (%.10f) -> opacity=%.1f\n", bits_plus_1_ulp, (double)s_plus_1_ulp, (double)TE_DCompComputeVisualOpacity(s_plus_1_ulp));

    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(s_minus_1_ulp), 0.0f, "-1 ULP (0x3F8020C4) -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(s_center), 0.0f, "Exact 1.0010f (0x3F8020C5) -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(s_plus_1_ulp), 1.0f, "+1 ULP (0x3F8020C6) -> opacity = 1.0f");

    // Sub-epsilon step testing via nextafterf
    float s_below = nextafterf(1.0010f, 0.0f);
    float s_above = nextafterf(1.0010f, 2.0f);
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(s_below), 0.0f, "nextafterf(1.0010f, 0.0f) -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(s_above), 1.0f, "nextafterf(1.0010f, 2.0f) -> opacity = 1.0f");

    // Underflow and negative scales clamped gracefully to 0.0f
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(0.0f), 0.0f, "S = 0.0f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(-0.00001f), 0.0f, "S = -0.00001f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(-1.0f), 0.0f, "S = -1.0f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(-100.0f), 0.0f, "S = -100.0f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(-FLT_MAX), 0.0f, "S = -FLT_MAX -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(-std::numeric_limits<float>::infinity()), 0.0f, "S = -inf -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(FLT_MIN), 0.0f, "S = FLT_MIN -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(0.5000f), 0.0f, "S = 0.5000f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(0.9990f), 0.0f, "S = 0.9990f -> opacity = 0.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(1.000999f), 0.0f, "S = 1.000999f -> opacity = 0.0f");

    // IEEE 754 NaN edge case
    float nan_val = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(nan_val), 0.0f, "S = NaN -> opacity = 0.0f");

    // Extreme positive scales
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(100.0f), 1.0f, "S = 100.0f -> opacity = 1.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(FLT_MAX), 1.0f, "S = FLT_MAX -> opacity = 1.0f");
    EXPECT_FLOAT_EQ(TE_DCompComputeVisualOpacity(std::numeric_limits<float>::infinity()), 1.0f, "S = +inf -> opacity = 1.0f");

    // Verify macro constant definition and type
    EXPECT_TRUE(sizeof(TE_DIFF_SCALE_THRESHOLD) == sizeof(float), "TE_DIFF_SCALE_THRESHOLD has float type");
    EXPECT_TRUE(TE_DIFF_SCALE_THRESHOLD == 1.001f, "TE_DIFF_SCALE_THRESHOLD equals 1.001f");
}

// ============================================================================
// TEST 2: TE_DCompIsOverlayActive Predicate
// ============================================================================
static void TestOverlayActivePredicate() {
    printf("\n--- TEST 2: TE_DCompIsOverlayActive Predicate ---\n");

    // Null and degenerate inputs
    EXPECT_TRUE(TE_DCompIsOverlayActive(nullptr, 10) == 0, "Null scales returns 0");
    float dummy_s = 1.5f;
    EXPECT_TRUE(TE_DCompIsOverlayActive(&dummy_s, 0) == 0, "Count 0 returns 0");
    EXPECT_TRUE(TE_DCompIsOverlayActive(&dummy_s, -1) == 0, "Negative count returns 0");

    // Single element boundary tests
    float s_at_thresh = 1.0010f;
    float s_above_thresh = 1.0011f;
    EXPECT_TRUE(TE_DCompIsOverlayActive(&s_at_thresh, 1) == 0, "Single scale 1.0010f returns 0");
    EXPECT_TRUE(TE_DCompIsOverlayActive(&s_above_thresh, 1) == 1, "Single scale 1.0011f returns 1");

    // 64-element array (TE_HOVER_MAX_ICONS)
    std::vector<float> icon_scales(64, 1.0000f);
    EXPECT_TRUE(TE_DCompIsOverlayActive(icon_scales.data(), 64) == 0, "All 64 at 1.0000f returns 0");

    // Mixed resting/negative array
    for (size_t i = 0; i < 64; i++) {
        icon_scales[i] = (i % 2 == 0) ? 1.0009f : 1.0010f;
    }
    EXPECT_TRUE(TE_DCompIsOverlayActive(icon_scales.data(), 64) == 0, "All 64 at 1.0009f/1.0010f returns 0");

    // Positional gating: index 0
    icon_scales[0] = 1.0011f;
    EXPECT_TRUE(TE_DCompIsOverlayActive(icon_scales.data(), 64) == 1, "Active at index 0 returns 1");
    icon_scales[0] = 1.0010f;

    // Positional gating: index 31 (middle)
    icon_scales[31] = 1.0011f;
    EXPECT_TRUE(TE_DCompIsOverlayActive(icon_scales.data(), 64) == 1, "Active at index 31 returns 1");
    icon_scales[31] = 1.0010f;

    // Positional gating: index 63 (last)
    icon_scales[63] = 1.0011f;
    EXPECT_TRUE(TE_DCompIsOverlayActive(icon_scales.data(), 64) == 1, "Active at index 63 returns 1");
    icon_scales[63] = 1.0010f;

    // Stress: 10,000 icons all at rest
    std::vector<float> large_scales(10000, 1.0010f);
    EXPECT_TRUE(TE_DCompIsOverlayActive(large_scales.data(), 10000) == 0, "10,000 icons at 1.0010f returns 0");

    // Stress: 10,000 icons with final icon active
    large_scales[9999] = 1.0011f;
    EXPECT_TRUE(TE_DCompIsOverlayActive(large_scales.data(), 10000) == 1, "10,000 icons with last active returns 1");
}

// ============================================================================
// TEST 3: Dirty-State Caching of IDCompositionEffectGroup::SetOpacity
// ============================================================================
static void TestDirtyStateCaching() {
    printf("\n--- TEST 3: Dirty-State Caching of IDCompositionEffectGroup::SetOpacity ---\n");

    // Setup 10 mock icons in static arrays of dcomp_overlay.cpp
    ReleaseVisualTree();

    MockEffectGroup* mock_effects[10] = {};
    MockScaleTransform* mock_scales[10] = {};
    MockTranslateTransform* mock_trans[10] = {};

    for (int i = 0; i < 10; i++) {
        mock_effects[i] = new MockEffectGroup();
        mock_scales[i] = new MockScaleTransform();
        mock_trans[i] = new MockTranslateTransform();

        s_icon_effects[i] = mock_effects[i];
        s_scale_transforms[i] = mock_scales[i];
        s_translate_transforms[i] = mock_trans[i];
        s_icon_opacities[i] = 0.0f; // Initial rest opacity
    }
    s_visual_count = 10;

    // Scenario 3A: Steady-State Resting for 1,000 frames
    // Invariant: zero redundant SetOpacity calls while icon remains at rest
    float resting_scales[10] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    for (int frame = 0; frame < 1000; frame++) {
        HRESULT hr = TE_DCompUpdateTransforms(10, resting_scales, nullptr, nullptr);
        assert(SUCCEEDED(hr));
    }
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(mock_effects[i]->set_opacity_calls == 0, "1000 resting frames produce exactly 0 SetOpacity calls");
    }

    // Scenario 3B: Hover begins on Icon 3 (transitions from 1.0f to 1.4f)
    float active_scales[10] = { 1.0f, 1.0f, 1.15f, 1.40f, 1.15f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    HRESULT hr_step1 = TE_DCompUpdateTransforms(10, active_scales, nullptr, nullptr);
    EXPECT_TRUE(SUCCEEDED(hr_step1), "TE_DCompUpdateTransforms succeeded");

    EXPECT_TRUE(mock_effects[2]->set_opacity_calls == 1, "Icon 2 transitioned to hover: exactly 1 SetOpacity call");
    EXPECT_FLOAT_EQ(mock_effects[2]->last_opacity, 1.0f, "Icon 2 opacity set to 1.0f");
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 1, "Icon 3 transitioned to hover: exactly 1 SetOpacity call");
    EXPECT_FLOAT_EQ(mock_effects[3]->last_opacity, 1.0f, "Icon 3 opacity set to 1.0f");
    EXPECT_TRUE(mock_effects[4]->set_opacity_calls == 1, "Icon 4 transitioned to hover: exactly 1 SetOpacity call");
    EXPECT_FLOAT_EQ(mock_effects[4]->last_opacity, 1.0f, "Icon 4 opacity set to 1.0f");

    // All resting icons MUST remain untouched (0 calls)
    EXPECT_TRUE(mock_effects[0]->set_opacity_calls == 0, "Resting icon 0: 0 calls");
    EXPECT_TRUE(mock_effects[1]->set_opacity_calls == 0, "Resting icon 1: 0 calls");
    EXPECT_TRUE(mock_effects[5]->set_opacity_calls == 0, "Resting icon 5: 0 calls");
    EXPECT_TRUE(mock_effects[9]->set_opacity_calls == 0, "Resting icon 9: 0 calls");

    // Scenario 3C: Steady-State Hover for 1,000 frames with varying spring oscillation
    // Invariant: zero redundant SetOpacity calls while icon remains in steady hover
    for (int frame = 0; frame < 1000; frame++) {
        float t = (float)frame / 1000.0f;
        active_scales[3] = 1.35f + 0.10f * sinf(t * 10.0f); // Scales between 1.25f and 1.45f
        active_scales[2] = 1.10f + 0.05f * cosf(t * 10.0f);
        active_scales[4] = 1.10f + 0.05f * cosf(t * 10.0f);
        TE_DCompUpdateTransforms(10, active_scales, nullptr, nullptr);
    }
    EXPECT_TRUE(mock_effects[2]->set_opacity_calls == 1, "Icon 2: 1000 oscillating hover frames added ZERO calls (count=1)");
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 1, "Icon 3: 1000 oscillating hover frames added ZERO calls (count=1)");
    EXPECT_TRUE(mock_effects[4]->set_opacity_calls == 1, "Icon 4: 1000 oscillating hover frames added ZERO calls (count=1)");
    EXPECT_TRUE(mock_effects[0]->set_opacity_calls == 0, "Icon 0 still 0 calls");

    // Scenario 3D: Micro-jitter within hover range (1.5000f -> 1.5001f -> 1.4999f)
    active_scales[3] = 1.5000f;
    TE_DCompUpdateTransforms(10, active_scales, nullptr, nullptr);
    active_scales[3] = 1.5001f;
    TE_DCompUpdateTransforms(10, active_scales, nullptr, nullptr);
    active_scales[3] = 1.4999f;
    TE_DCompUpdateTransforms(10, active_scales, nullptr, nullptr);
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 1, "Micro-jitter within hover range added ZERO calls (count=1)");

    // Scenario 3E: Transition back to rest
    HRESULT hr_return = TE_DCompUpdateTransforms(10, resting_scales, nullptr, nullptr);
    EXPECT_TRUE(SUCCEEDED(hr_return), "TE_DCompUpdateTransforms return to rest succeeded");
    EXPECT_TRUE(mock_effects[2]->set_opacity_calls == 2, "Icon 2 returned to rest: exactly 1 call (total=2)");
    EXPECT_FLOAT_EQ(mock_effects[2]->last_opacity, 0.0f, "Icon 2 opacity set to 0.0f");
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 2, "Icon 3 returned to rest: exactly 1 call (total=2)");
    EXPECT_FLOAT_EQ(mock_effects[3]->last_opacity, 0.0f, "Icon 3 opacity set to 0.0f");
    EXPECT_TRUE(mock_effects[4]->set_opacity_calls == 2, "Icon 4 returned to rest: exactly 1 call (total=2)");
    EXPECT_FLOAT_EQ(mock_effects[4]->last_opacity, 0.0f, "Icon 4 opacity set to 0.0f");

    // Scenario 3F: Steady-State Rest for another 1,000 frames
    for (int frame = 0; frame < 1000; frame++) {
        TE_DCompUpdateTransforms(10, resting_scales, nullptr, nullptr);
    }
    EXPECT_TRUE(mock_effects[2]->set_opacity_calls == 2, "Icon 2 steady-state rest: ZERO additional calls (total=2)");
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 2, "Icon 3 steady-state rest: ZERO additional calls (total=2)");
    EXPECT_TRUE(mock_effects[4]->set_opacity_calls == 2, "Icon 4 steady-state rest: ZERO additional calls (total=2)");

    // Scenario 3G: TE_DCompOverlayUpdateVisuals interface test
    TE_VisualState states[10] = {};
    for (int i = 0; i < 10; i++) {
        states[i].scale = 1.0000f;
        states[i].offset_x = 0.0f;
        states[i].offset_y = 0.0f;
        states[i].opacity = 1.0f; // Intentionally passing opacity 1.0f while scale=1.0f
    }
    // Gating check: scale=1.0f <= 1.001f MUST clamp target_opacity to 0.0f, so no calls should be made since cache is 0.0f
    TE_DCompOverlayUpdateVisuals(states, 10);
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 2, "VisualState with scale=1.0f and opacity=1.0f clamped to 0.0f (no call)");

    // Now magnify icon 3 with opacity 0.85f
    states[3].scale = 1.5000f;
    states[3].opacity = 0.85f;
    TE_DCompOverlayUpdateVisuals(states, 10);
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 3, "VisualState with scale=1.5f and opacity=0.85f sets opacity (call=3)");
    EXPECT_FLOAT_EQ(mock_effects[3]->last_opacity, 0.85f, "VisualState opacity matches 0.85f");

    // Run 500 frames with identical visual state -> zero redundant calls
    for (int frame = 0; frame < 500; frame++) {
        TE_DCompOverlayUpdateVisuals(states, 10);
    }
    EXPECT_TRUE(mock_effects[3]->set_opacity_calls == 3, "VisualState 500 steady hover frames added ZERO calls (count=3)");

    // Clean up
    for (int i = 0; i < 10; i++) {
        s_icon_effects[i] = nullptr;
        s_scale_transforms[i] = nullptr;
        s_translate_transforms[i] = nullptr;
        mock_effects[i]->Release();
        mock_scales[i]->Release();
        mock_trans[i]->Release();
    }
    s_visual_count = 0;
}

int main() {
    printf("===============================================================================\n");
    printf("TaskbarEngine Milestone 2 Challenger 2: Feature F6 Stress & BVA Harness\n");
    printf("===============================================================================\n");

    TestBVAThreshold();
    TestOverlayActivePredicate();
    TestDirtyStateCaching();

    printf("\n===============================================================================\n");
    printf("SUMMARY: Passed: %d, Failed: %d\n", g_tests_passed, g_tests_failed);
    printf("===============================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
