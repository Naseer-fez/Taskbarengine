#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cJSON.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dxgi.h>
#include <limits>
#include "widget_host_model.h"
#include "widgets/widget_draw.h"

using namespace te::widgets;

namespace {
struct HitRecord { std::string id; float x = -1.0f; int calls = 0; };
WidgetHitTestResult TestDelegate(const std::string& id, float x, float y, void* context) {
    (void)y;
    auto* record = static_cast<HitRecord*>(context);
    record->id = id;
    record->x = x;
    ++record->calls;
    return { id == "clock", nullptr, nullptr };
}
}

TEST_CASE("Widget host layout supports measurement, hit delegation, and persisted reordering", "[widgets][host]") {
    Layout layout;
    std::vector<Item> items{
        { "clock", { 80.0f, 48.0f, 140.0f } },
        { "sysmon", { 120.0f, 80.0f, 220.0f } },
        { "media", { 180.0f, 120.0f, 240.0f } }
    };
    layout.Reset(items, 500.0f, 8.0f);
    REQUIRE(layout.Items()[0].width == 80.0f);
    REQUIRE(layout.Items()[1].x == 88.0f);

    HitRecord record;
    REQUIRE(DelegateHitTest(layout, 12.0f, 20.0f, 40.0f, TestDelegate, &record) == 0);
    REQUIRE(record.calls == 1);
    REQUIRE(record.id == "clock");
    REQUIRE(record.x == 12.0f);
    REQUIRE(DelegateHitTest(layout, 90.0f, 20.0f, 40.0f, TestDelegate, &record) == -1);

    REQUIRE(layout.Move("media", 0));
    REQUIRE(layout.Items()[0].id == "media");
    REQUIRE(layout.BeginDrag(0, 12.0f));
    REQUIRE(layout.MoveAt(210.0f));
    REQUIRE(layout.EndDrag());

    cJSON* config = cJSON_CreateObject();
    REQUIRE(PersistOrder(config, layout.Items()));
    cJSON* saved = cJSON_GetObjectItemCaseSensitive(config, "layout");
    REQUIRE(cJSON_IsArray(saved));
    REQUIRE(std::string(cJSON_GetArrayItem(saved, 0)->valuestring) == layout.Items()[0].id);
    cJSON_Delete(config);
}

TEST_CASE("Widget layout parser migrates legacy comma-separated order", "[widgets][host][config]") {
    cJSON* config = cJSON_Parse("{\"layout\":\"clock, sysmon , custom.weather\"}");
    REQUIRE(config != nullptr);
    const auto ids = ParseLayout(config);
    REQUIRE(ids.size() == 3);
    CHECK(ids[0] == "clock");
    CHECK(ids[1] == "sysmon");
    CHECK(ids[2] == "custom.weather");
    cJSON_Delete(config);
}

TEST_CASE("Widget measurements are bounded and reject invalid callback values", "[widgets][host][validation]") {
    const WidgetMeasurement invalid{ std::numeric_limits<float>::quiet_NaN(), 20.0f, 10.0f };
    const WidgetMeasurement safe = SanitizeMeasurement(invalid);
    REQUIRE(safe.preferred_width == 80.0f);
    REQUIRE(safe.min_width == 40.0f);
    REQUIRE(safe.max_width == 240.0f);

    const WidgetMeasurement extreme = SanitizeMeasurement({ 9000.0f, 1.0f, 10000.0f });
    REQUIRE(extreme.min_width == 1.0f);
    REQUIRE(extreme.max_width == 4096.0f);
    REQUIRE(extreme.preferred_width == 4096.0f);
}

TEST_CASE("Widget position follows a percentage of available taskbar travel", "[widgets][host][position]") {
    CHECK(PositionOrigin(1000.0f, 200.0f, 0.0f) == 0.0f);
    CHECK(PositionOrigin(1000.0f, 200.0f, 50.0f) == 400.0f);
    CHECK(PositionOrigin(1000.0f, 200.0f, 100.0f) == 800.0f);
    CHECK(PositionOrigin(1000.0f, 1200.0f, 75.0f) == 0.0f);
    CHECK(PositionOrigin(1000.0f, 200.0f, 150.0f) == 800.0f);
    CHECK(PositionOrigin(1000.0f, 200.0f, std::numeric_limits<float>::quiet_NaN()) == 800.0f);
}

TEST_CASE("Widget widths and gaps are constrained to the available taskbar region", "[widgets][host][position]") {
    Layout layout;
    layout.Reset({
        { "clock", { 100.0f, 60.0f, 140.0f } },
        { "sysmon", { 120.0f, 80.0f, 180.0f } },
        { "media", { 200.0f, 120.0f, 240.0f } }
    }, 240.0f, 32.0f);
    const auto& fitted = layout.Items();
    CHECK(fitted.back().x + fitted.back().width <= 240.0f);
    for (size_t i = 1; i < fitted.size(); ++i) {
        CHECK(fitted[i].x >= fitted[i - 1].x + fitted[i - 1].width);
    }
    CHECK(PositionOrigin(240.0f, fitted.back().x + fitted.back().width, 100.0f) == 0.0f);
}

TEST_CASE("Fitted widget group anchors using its effective width", "[widgets][host][position]") {
    Layout layout;
    layout.Reset({
        { "media", { 200.0f, 120.0f, 240.0f } },
        { "sysmon", { 116.0f, 88.0f, 240.0f } },
        { "clock", { 90.0f, 90.0f, 90.0f } }
    }, 320.0f, 32.0f);

    const auto& fitted = layout.Items();
    REQUIRE(fitted.size() == 3);
    const float used_width = fitted.back().x + fitted.back().width;
    CHECK(used_width <= 320.0f);
    CHECK(PositionOrigin(320.0f, used_width, 100.0f) == Catch::Approx(320.0f - used_width));
    CHECK(PositionOrigin(320.0f, used_width, 50.0f) == Catch::Approx((320.0f - used_width) * 0.5f));
    CHECK(fitted[1].x >= fitted[0].x + fitted[0].width);
    CHECK(fitted[2].x >= fitted[1].x + fitted[1].width);
}

TEST_CASE("Explicit widget text colors are preserved", "[widgets][host][appearance]") {
    cJSON* section = cJSON_Parse("{\"text_color\":4294967295}");
    REQUIRE(section != nullptr);
    CHECK(WidgetTextColor(section) == 0xffffffffu);
    cJSON_Delete(section);
}

TEST_CASE("Widget host can create and tear down a shared D2D and DComp surface", "[widgets][dcomp]") {
    ID2D1Factory1* factory = nullptr;
    REQUIRE(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
        __uuidof(ID2D1Factory1), reinterpret_cast<void**>(&factory))));

    ID3D11Device* d3d = nullptr;
    ID3D11DeviceContext* immediate = nullptr;
    D3D_FEATURE_LEVEL level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr, 0, D3D11_SDK_VERSION, &d3d, &level, &immediate);
    REQUIRE(SUCCEEDED(hr));
    IDXGIDevice* dxgi = nullptr;
    REQUIRE(SUCCEEDED(d3d->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi))));
    ID2D1Device* d2d = nullptr;
    const D2D1_CREATION_PROPERTIES d2d_properties = {
        D2D1_THREADING_MODE_MULTI_THREADED, D2D1_DEBUG_LEVEL_NONE, D2D1_DEVICE_CONTEXT_OPTIONS_NONE
    };
    REQUIRE(SUCCEEDED(D2D1CreateDevice(dxgi, d2d_properties, &d2d)));
    IDCompositionDevice* dcomp = nullptr;
    hr = DCompositionCreateDevice(dxgi, __uuidof(IDCompositionDevice), reinterpret_cast<void**>(&dcomp));
    if (SUCCEEDED(hr)) {
        IDCompositionSurface* surface = nullptr;
        hr = dcomp->CreateSurface(80, 40, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
        REQUIRE(SUCCEEDED(hr));
        ID2D1DeviceContext* context = nullptr;
        REQUIRE(SUCCEEDED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)));
        context->Release();
        surface->Release();
    }
    if (dcomp) dcomp->Release();
    d2d->Release();
    dxgi->Release();
    immediate->Release();
    d3d->Release();
    factory->Release();
    REQUIRE(SUCCEEDED(hr));
}
