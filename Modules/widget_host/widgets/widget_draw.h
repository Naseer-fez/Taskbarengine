#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <winreg.h>
#include <sdk/te_widget.h>
#include <cJSON.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>

#ifdef DrawText
#undef DrawText
#endif

inline const cJSON* WidgetSection(const PluginContext* context, const char* id) {
    const cJSON* root = context ? static_cast<const cJSON*>(context->config) : nullptr;
    return root ? cJSON_GetObjectItemCaseSensitive(root, id) : nullptr;
}

inline std::string WidgetString(const cJSON* section, const char* key, const char* fallback) {
    const cJSON* item = section ? cJSON_GetObjectItemCaseSensitive(section, key) : nullptr;
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

inline float WidgetFloat(const cJSON* section, const char* key, float fallback) {
    const cJSON* item = section ? cJSON_GetObjectItemCaseSensitive(section, key) : nullptr;
    return cJSON_IsNumber(item) ? static_cast<float>(item->valuedouble) : fallback;
}

inline bool WidgetBool(const cJSON* section, const char* key, bool fallback) {
    const cJSON* item = section ? cJSON_GetObjectItemCaseSensitive(section, key) : nullptr;
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

inline uint32_t WidgetColor(const cJSON* section, const char* key, uint32_t fallback) {
    const cJSON* item = section ? cJSON_GetObjectItemCaseSensitive(section, key) : nullptr;
    if (cJSON_IsNumber(item)) return static_cast<uint32_t>(item->valuedouble);
    if (!cJSON_IsString(item) || !item->valuestring) return fallback;
    const char* value = item->valuestring;
    if (value[0] == '#') ++value;
    else if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value += 2;
    return static_cast<uint32_t>(strtoul(value, nullptr, 16));
}

inline uint32_t WidgetSystemTextColor() {
    DWORD light_theme = 0;
    DWORD size = sizeof(light_theme);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &light_theme, &size);
    if (status == ERROR_SUCCESS) return light_theme ? 0xff202020u : 0xfff5f5f5u;
    const COLORREF system_text = GetSysColor(COLOR_BTNTEXT);
    return 0xff000000u | (static_cast<uint32_t>(GetRValue(system_text)) << 16) |
        (static_cast<uint32_t>(GetGValue(system_text)) << 8) | GetBValue(system_text);
}

inline uint32_t WidgetTextColor(const cJSON* section, const char* key = "text_color") {
    const cJSON* item = section ? cJSON_GetObjectItemCaseSensitive(section, key) : nullptr;
    if (cJSON_IsNumber(item) || cJSON_IsString(item)) return WidgetColor(section, key, 0xffffffffu);
    return WidgetSystemTextColor();
}

inline D2D1_COLOR_F WidgetD2DColor(uint32_t argb) {
    return D2D1::ColorF(static_cast<float>((argb >> 16) & 0xff) / 255.0f,
                        static_cast<float>((argb >> 8) & 0xff) / 255.0f,
                        static_cast<float>(argb & 0xff) / 255.0f,
                        static_cast<float>((argb >> 24) & 0xff) / 255.0f);
}

inline std::wstring WidgetWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
    wide.resize(static_cast<size_t>(size - 1));
    return wide;
}

inline HRESULT WidgetCreateTextFormat(const std::string& family, float size, DWRITE_FONT_WEIGHT weight,
                                     IDWriteTextFormat** out_format) {
    if (!out_format) return E_POINTER;
    *out_format = nullptr;
    IDWriteFactory* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(&factory));
    if (FAILED(hr)) return hr;
    const std::wstring family_wide = WidgetWide(family);
    hr = factory->CreateTextFormat(family_wide.empty() ? L"Segoe UI Variable" : family_wide.c_str(),
        nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", out_format);
    factory->Release();
    return hr;
}

inline HRESULT WidgetDrawText(const WidgetRenderContext* context, IDWriteTextFormat* format,
                              const std::wstring& text, uint32_t color, float width, float height) {
    if (!context || !context->d2d_context || !format) return E_INVALIDARG;
    ID2D1SolidColorBrush* brush = nullptr;
    HRESULT hr = context->d2d_context->CreateSolidColorBrush(WidgetD2DColor(color), &brush);
    if (FAILED(hr)) return hr;
    const D2D1_RECT_F bounds = D2D1::RectF(10.0f, 0.0f, std::max(width - 8.0f, 10.0f), height);
    IDWriteFactory* factory = nullptr;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(&factory));
    if (SUCCEEDED(hr)) {
        IDWriteTextLayout* layout = nullptr;
        hr = factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), format,
                                       bounds.right - bounds.left, bounds.bottom - bounds.top, &layout);
        if (SUCCEEDED(hr)) {
            context->d2d_context->DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), layout, brush,
                                                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
            layout->Release();
        }
        factory->Release();
    }
    brush->Release();
    return hr;
}

