#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi.h>
#include <dwrite.h>
#include <sdk/te_widget.h>
#include <sdk/te_jsonc.h>
#include <cJSON.h>

#include "widget_host_model.h"
#include "widget_host_runtime.h"
#include "dcomp_overlay.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace te::widgets {
namespace {

constexpr UINT_PTR kTimerId = 0x54455747u;
constexpr UINT kFrameIntervalMs = 1000;
constexpr UINT kAnimationIntervalMs = 16;
constexpr UINT_PTR kSubclassId = 0x57494447u;
constexpr float kWidgetHeight = 40.0f;

struct WidgetEntry {
    HMODULE module = nullptr;
    const WidgetInterface* api = nullptr;
    const PluginMetadata* metadata = nullptr;
    std::string id;
    bool enabled = false;
    bool initialized = false;
    bool faulted = false;
    float surface_width = 0.0f;
    float surface_height = 0.0f;
    float surface_dpi_scale = 0.0f;
    bool needs_render = true;
    IDCompositionSurface* surface = nullptr;
    IDCompositionVisual* visual = nullptr;
    ID2D1DeviceContext* context = nullptr;
    WidgetMeasurement measurement{};
};

struct Runtime {
    const PluginContext* plugin_context = nullptr;
    PluginContext widget_context{};
    cJSON* widget_config = nullptr;
    HWND taskbar = nullptr;
    HWND tooltip = nullptr;
    TOOLINFOW tooltip_info{};
    std::wstring tooltip_text;
    RECT taskbar_rect{};
    uint32_t dpi = 96;
    float dpi_scale = 1.0f;
    float widget_height = kWidgetHeight;
    int faults = 0;
    bool initialized = false;
    bool enabled = false;
    bool dragging = false;
    bool layout_animating = false;
    int dragged_widget = -1;
    float drag_origin_x = 0.0f;
    float last_pointer_x = 0.0f;
    uint64_t suppress_click_until = 0;
    float layout_origin = 0.0f;
    float position_percent = 100.0f;
    uint64_t last_frame = 0;
    std::wstring widgets_dir;
    std::wstring config_path;
    std::wstring default_config_path;
    std::vector<WidgetEntry> widgets;
    Layout layout;
    TE_DCompWidgetResources graphics{};
    HRESULT (*acquire_graphics)(HWND, TE_DCompWidgetResources*) = nullptr;
    void (*release_graphics)(TE_DCompWidgetResources*) = nullptr;
    HRESULT (*commit_graphics)(void) = nullptr;
    HWND (*get_overlay_window)(void) = nullptr;
    uint64_t (*get_generation)(void) = nullptr;
    uint64_t graphics_generation = 0;
    HMODULE bridge_module = nullptr;
    bool bridge_loaded_here = false;
} g_runtime;

static bool GuardInitialize(WidgetEntry* entry, const PluginContext* context) {
    if (!entry || !entry->api || !entry->api->Initialize) return false;
    __try { return SUCCEEDED(entry->api->Initialize(context)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; return false; }
}
static bool GuardEnable(WidgetEntry* entry) {
    if (!entry || !entry->api || !entry->api->Enable) return false;
    __try { return SUCCEEDED(entry->api->Enable()); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; return false; }
}
static void GuardDisable(WidgetEntry* entry) {
    if (!entry || !entry->api || !entry->api->Disable || entry->faulted) return;
    __try { entry->api->Disable(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; }
}
static void GuardShutdown(WidgetEntry* entry) {
    if (!entry || !entry->api || !entry->api->Shutdown || entry->faulted) return;
    __try { entry->api->Shutdown(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; }
}
static WidgetMeasurement GuardMeasure(WidgetEntry* entry, float height, float dpi_scale) {
    WidgetMeasurement result{ 80.0f, 40.0f, 240.0f };
    if (!entry || !entry->api || !entry->api->Measure || entry->faulted) return result;
    __try { result = entry->api->Measure(height, dpi_scale); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; }
    return result;
}
static bool GuardRender(WidgetEntry* entry, const WidgetRenderContext* context) {
    if (!entry || !entry->api || !entry->api->Render || entry->faulted) return false;
    __try { return SUCCEEDED(entry->api->Render(context)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; return false; }
}
static WidgetHitTestResult GuardHitTest(WidgetEntry* entry, float x, float y) {
    WidgetHitTestResult result{};
    if (!entry || !entry->api || !entry->api->HitTest || entry->faulted) return result;
    __try { result = entry->api->HitTest(x, y); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; }
    return result;
}
static bool GuardIsDirty(WidgetEntry* entry) {
    if (!entry || !entry->api || entry->faulted) return false;
    constexpr size_t callback_size = offsetof(WidgetInterface, IsDirty) + sizeof(((WidgetInterface*)nullptr)->IsDirty);
    if (entry->api->struct_size < callback_size || !entry->api->IsDirty) return true;
    __try { return entry->api->IsDirty(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; return false; }
}
static void GuardClick(WidgetEntry* entry, float x, float y, uint32_t button) {
    if (!entry || !entry->api || !entry->api->OnClick || entry->faulted) return;
    __try { entry->api->OnClick(x, y, button); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; }
}
static void GuardScroll(WidgetEntry* entry, float x, float y, float delta) {
    if (!entry || !entry->api || !entry->api->OnScroll || entry->faulted) return;
    __try { entry->api->OnScroll(x, y, delta); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; entry->faulted = true; }
}

static const WidgetInterface* GuardFactory(GetWidgetInterfaceFunc factory) {
    const WidgetInterface* result = nullptr;
    __try { result = factory ? factory() : nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; }
    return result;
}

static const PluginMetadata* GuardMetadata(const WidgetInterface* api) {
    const PluginMetadata* result = nullptr;
    __try { result = api && api->GetMetadata ? api->GetMetadata() : nullptr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { ++g_runtime.faults; }
    return result;
}

static uint32_t GetDpi(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto fn = user32 ? reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(user32, "GetDpiForWindow")) : nullptr;
    const UINT dpi = fn ? fn(hwnd) : 96;
    return dpi ? dpi : 96;
}

static void Log(TE_LogLevel level, const char* message);

static std::string Utf8Path(const std::wstring& path) {
    if (path.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}

static void SetPaths() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&SetPaths), &self);
    wchar_t module_path[MAX_PATH]{};
    GetModuleFileNameW(self, module_path, MAX_PATH);
    std::wstring folder(module_path);
    for (int i = 0; i < 3; ++i) {
        const size_t separator = folder.find_last_of(L"\\/");
        if (separator == std::wstring::npos) break;
        folder.resize(separator);
    }
    g_runtime.widgets_dir = folder + L"\\Widgets";
    g_runtime.default_config_path = folder + L"\\Config\\default_config.jsonc";

    wchar_t local_appdata[MAX_PATH]{};
    const DWORD appdata_len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata, MAX_PATH);
    if (appdata_len > 0 && appdata_len < MAX_PATH) {
        g_runtime.config_path = std::wstring(local_appdata) + L"\\TaskbarEngine\\config.jsonc";
        if (GetFileAttributesW(g_runtime.config_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
            const std::string paths = "Widget paths: packages=" + Utf8Path(g_runtime.widgets_dir) +
                " config=" + Utf8Path(g_runtime.config_path) +
                " defaults=" + Utf8Path(g_runtime.default_config_path);
            Log(TE_LOG_INFO, paths.c_str());
            return;
        }
    }
    g_runtime.config_path = folder + L"\\Config\\default_config.jsonc";
    const std::string paths = "Widget paths: packages=" + Utf8Path(g_runtime.widgets_dir) +
        " config=" + Utf8Path(g_runtime.config_path) +
        " defaults=" + Utf8Path(g_runtime.default_config_path);
    Log(TE_LOG_INFO, paths.c_str());
}

static bool ReadWidgetsConfig(cJSON** root_out, const cJSON** widgets_out) {
    if (!root_out || !widgets_out) return false;
    *root_out = nullptr;
    *widgets_out = nullptr;
    if (g_runtime.plugin_context && g_runtime.plugin_context->config) {
        *widgets_out = static_cast<const cJSON*>(g_runtime.plugin_context->config);
        return true;
    }
    cJSON* root = nullptr;
    if (SUCCEEDED(TE_JsoncParseFile(g_runtime.config_path.c_str(), &root)) && root) {
        const cJSON* section = cJSON_GetObjectItemCaseSensitive(root, "widgets");
        if (section && cJSON_IsObject(section)) {
            *root_out = root;
            *widgets_out = section;
            return true;
        }
        TE_JsoncFree(root);
        root = nullptr;
    }

    if (SUCCEEDED(TE_JsoncParseFile(g_runtime.default_config_path.c_str(), &root)) && root) {
        const cJSON* section = cJSON_GetObjectItemCaseSensitive(root, "widgets");
        if (section && cJSON_IsObject(section)) {
            *root_out = root;
            *widgets_out = section;
            return true;
        }
        TE_JsoncFree(root);
    }
    return false;
}

static cJSON* LoadDefaultWidgetsConfig() {
    cJSON* root = nullptr;
    if (FAILED(TE_JsoncParseFile(g_runtime.default_config_path.c_str(), &root)) || !root) return cJSON_CreateObject();
    const cJSON* section = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    cJSON* copy = section && cJSON_IsObject(section) ? cJSON_Duplicate(section, 1) : cJSON_CreateObject();
    TE_JsoncFree(root);
    return copy;
}

static bool WidgetEnabled(const cJSON* config, const char* id, bool fallback) {
    const cJSON* section = config ? cJSON_GetObjectItemCaseSensitive(config, id) : nullptr;
    const cJSON* enabled = section ? cJSON_GetObjectItemCaseSensitive(section, "enabled") : nullptr;
    return cJSON_IsBool(enabled) ? cJSON_IsTrue(enabled) : fallback;
}

static float GetGap(const cJSON* config) {
    const cJSON* gap = config ? cJSON_GetObjectItemCaseSensitive(config, "gap") : nullptr;
    return cJSON_IsNumber(gap) ? std::clamp(static_cast<float>(gap->valuedouble), 0.0f, 64.0f) : 8.0f;
}

static void Log(TE_LogLevel level, const char* message) {
    if (g_runtime.plugin_context && g_runtime.plugin_context->log) {
        g_runtime.plugin_context->log(level, "WidgetHost", message);
    }
}

static void LogWindowFailure(const char* api, HWND hwnd, DWORD error) {
    wchar_t class_name[128] = L"?";
    DWORD pid = 0;
    DWORD tid = 0;
    if (hwnd && IsWindow(hwnd)) {
        GetClassNameW(hwnd, class_name, (int)(sizeof(class_name) / sizeof(class_name[0])));
        tid = GetWindowThreadProcessId(hwnd, &pid);
    }
    char class_utf8[128] = "?";
    WideCharToMultiByte(CP_UTF8, 0, class_name, -1, class_utf8, sizeof(class_utf8), nullptr, nullptr);
    char message[320]{};
    sprintf_s(message, "%s failed: HWND=%p class=%s pid=%lu tid=%lu GetLastError=%lu",
              api, static_cast<void*>(hwnd), class_utf8, pid, tid, error);
    Log(TE_LOG_ERROR, message);
}

static void LogHrFailure(const char* api, HRESULT hr) {
    static HRESULT last_hr = S_OK;
    static char last_api[64]{};
    if (SUCCEEDED(hr)) {
        last_hr = S_OK;
        last_api[0] = '\0';
        return;
    }
    if (last_hr == hr && strcmp(last_api, api) == 0) return;
    last_hr = hr;
    strncpy_s(last_api, api, _TRUNCATE);
    char message[224]{};
    sprintf_s(message, "%s failed: HRESULT=0x%08lX", api, static_cast<unsigned long>(hr));
    Log(TE_LOG_ERROR, message);
}

static HRESULT CommitGraphics() {
    if (!g_runtime.commit_graphics) return E_POINTER;
    const HRESULT hr = g_runtime.commit_graphics();
    LogHrFailure("TE_DCompWidgetCommit/IDCompositionDevice::Commit", hr);
    return hr;
}

static void DiscoverWidgets() {
    struct Candidate { std::wstring path; std::string id; int api_version; };
    std::vector<Candidate> candidates;
    Log(TE_LOG_INFO, ("Scanning widget packages under " + Utf8Path(g_runtime.widgets_dir)).c_str());
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((g_runtime.widgets_dir + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        char message[256]{};
        sprintf_s(message, "Widget directory unavailable; GetLastError=%lu", GetLastError());
        Log(TE_LOG_WARNING, message);
        return;
    }
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || data.cFileName[0] == L'.') continue;
        const std::wstring package_dir = g_runtime.widgets_dir + L"\\" + data.cFileName;
        const std::wstring manifest_path = package_dir + L"\\widget.json";
        cJSON* manifest_root = nullptr;
        if (FAILED(TE_JsoncParseFile(manifest_path.c_str(), &manifest_root)) || !manifest_root) continue;
        const cJSON* id_value = cJSON_GetObjectItemCaseSensitive(manifest_root, "id");
        const cJSON* module_value = cJSON_GetObjectItemCaseSensitive(manifest_root, "module");
        const cJSON* api_value = cJSON_GetObjectItemCaseSensitive(manifest_root, "api_version");
        const cJSON* manifest_version_value = cJSON_GetObjectItemCaseSensitive(manifest_root, "manifest_version");
        std::string id = cJSON_IsString(id_value) && id_value->valuestring ? id_value->valuestring : "";
        std::string module_name = cJSON_IsString(module_value) && module_value->valuestring ? module_value->valuestring : "";
        const int api_version = cJSON_IsNumber(api_value) ? api_value->valueint : 0;
        const int manifest_version = cJSON_IsNumber(manifest_version_value) ? manifest_version_value->valueint : 0;
        const bool valid_id = !id.empty() && id.size() <= 64 && std::all_of(id.begin(), id.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
        });
        const bool valid_module = !module_name.empty() && module_name.find_first_of("\\/:*") == std::string::npos &&
                                  module_name.find("..") == std::string::npos;
        if (valid_id && valid_module && api_version == TE_WIDGET_API_VERSION && manifest_version == 1) {
            const int chars = MultiByteToWideChar(CP_UTF8, 0, module_name.c_str(), -1, nullptr, 0);
            std::wstring module_wide(chars > 1 ? static_cast<size_t>(chars) : 0, L'\0');
            if (chars > 1) {
                MultiByteToWideChar(CP_UTF8, 0, module_name.c_str(), -1, module_wide.data(), chars);
                module_wide.pop_back();
            }
            candidates.push_back({ package_dir + L"\\" + module_wide, id, api_version });
        } else {
            Log(TE_LOG_WARNING, "Widget manifest rejected: invalid ID, module filename, or API version");
        }
        TE_JsoncFree(manifest_root);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    // Flat DLL loading remains for existing installations during migration.
    find = FindFirstFileW((g_runtime.widgets_dir + L"\\*.dll").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            candidates.push_back({ g_runtime.widgets_dir + L"\\" + data.cFileName, "", TE_WIDGET_API_VERSION });
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return left.id.empty() ? (!right.id.empty() ? false : left.path < right.path) :
            (right.id.empty() ? true : left.id < right.id);
    });
    for (const auto& candidate : candidates) {
        const std::wstring& full_path = candidate.path;
        HMODULE module = LoadLibraryW(full_path.c_str());
        if (!module) {
            char message[320]{};
            sprintf_s(message, "Widget module rejected at load (Win32 error %lu)", GetLastError());
            Log(TE_LOG_WARNING, message);
            continue;
        }
        auto factory = reinterpret_cast<GetWidgetInterfaceFunc>(GetProcAddress(module, "GetWidgetInterface"));
        if (!factory) { Log(TE_LOG_WARNING, "Widget module has no GetWidgetInterface export"); FreeLibrary(module); continue; }
        const WidgetInterface* api = GuardFactory(factory);
        constexpr size_t required_interface_size = offsetof(WidgetInterface, GetMetadata) + sizeof(((WidgetInterface*)nullptr)->GetMetadata);
        if (!api || api->struct_size < required_interface_size || api->api_version != TE_WIDGET_API_VERSION ||
            !api->GetMetadata || !api->Measure || !api->Render) {
            Log(TE_LOG_WARNING, "Widget module rejected: incompatible ABI version/size or required callbacks missing");
            FreeLibrary(module);
            continue;
        }
        const PluginMetadata* metadata = GuardMetadata(api);
        if (!metadata || !metadata->name || !metadata->name[0] || (!candidate.id.empty() && candidate.id != metadata->name)) {
            Log(TE_LOG_WARNING, "Widget manifest ID does not match module metadata"); FreeLibrary(module); continue;
        }
        const bool duplicate = std::any_of(g_runtime.widgets.begin(), g_runtime.widgets.end(), [&](const WidgetEntry& item) {
            return item.id == metadata->name;
        });
        if (duplicate) { Log(TE_LOG_WARNING, "Duplicate widget ID rejected"); FreeLibrary(module); continue; }
        WidgetEntry entry;
        entry.module = module;
        entry.api = api;
        entry.metadata = metadata;
        entry.id = metadata->name;
        g_runtime.widgets.emplace_back(std::move(entry));
        char message[MAX_PATH * 4 + 160]{};
        const std::string path = Utf8Path(full_path);
        sprintf_s(message, "Accepted widget ID=%s manifest=%s API=%d path=%s",
                  metadata->name, candidate.id.empty() ? "legacy" : "v1", candidate.api_version, path.c_str());
        Log(TE_LOG_INFO, message);
    }
}

static WidgetEntry* FindWidget(const std::string& id) {
    const auto found = std::find_if(g_runtime.widgets.begin(), g_runtime.widgets.end(), [&](const WidgetEntry& item) {
        return item.id == id;
    });
    return found == g_runtime.widgets.end() ? nullptr : &*found;
}

static bool ResolveOverlayApi() {
    HMODULE overlay = GetModuleHandleW(L"icon_hover.dll");
    if (!overlay) {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ResolveOverlayApi), &self);
        wchar_t self_path[MAX_PATH]{};
        if (!self || !GetModuleFileNameW(self, self_path, MAX_PATH)) return false;
        std::wstring root(self_path);
        for (int i = 0; i < 3; ++i) {
            const size_t slash = root.find_last_of(L"\\/");
            if (slash == std::wstring::npos) return false;
            root.resize(slash);
        }
        const std::wstring bridge_path = root + L"\\Modules\\icon_hover\\icon_hover.dll";
        overlay = LoadLibraryW(bridge_path.c_str());
        if (!overlay) {
            char message[320]{};
            sprintf_s(message, "Could not load widget graphics bridge (Win32 error %lu)", GetLastError());
            Log(TE_LOG_ERROR, message);
            return false;
        }
        g_runtime.bridge_loaded_here = true;
        g_runtime.bridge_module = overlay;
    }
    g_runtime.acquire_graphics = reinterpret_cast<decltype(g_runtime.acquire_graphics)>(GetProcAddress(overlay, "TE_DCompWidgetAcquire"));
    g_runtime.release_graphics = reinterpret_cast<decltype(g_runtime.release_graphics)>(GetProcAddress(overlay, "TE_DCompWidgetRelease"));
    g_runtime.commit_graphics = reinterpret_cast<decltype(g_runtime.commit_graphics)>(GetProcAddress(overlay, "TE_DCompWidgetCommit"));
    g_runtime.get_overlay_window = reinterpret_cast<decltype(g_runtime.get_overlay_window)>(GetProcAddress(overlay, "TE_DCompWidgetGetOverlayWindow"));
    g_runtime.get_generation = reinterpret_cast<decltype(g_runtime.get_generation)>(GetProcAddress(overlay, "TE_DCompWidgetGetGeneration"));
    wchar_t bridge_path[MAX_PATH]{};
    GetModuleFileNameW(overlay, bridge_path, MAX_PATH);
    const std::string path = Utf8Path(bridge_path);
    char message[768]{};
    const bool complete = g_runtime.acquire_graphics && g_runtime.release_graphics && g_runtime.commit_graphics &&
                          g_runtime.get_overlay_window && g_runtime.get_generation;
    sprintf_s(message, "Graphics bridge path=%s exports acquire=%u release=%u commit=%u overlay=%u generation=%u",
              path.c_str(), g_runtime.acquire_graphics ? 1u : 0u,
              g_runtime.release_graphics ? 1u : 0u, g_runtime.commit_graphics ? 1u : 0u,
              g_runtime.get_overlay_window ? 1u : 0u, g_runtime.get_generation ? 1u : 0u);
    Log(complete ? TE_LOG_INFO : TE_LOG_ERROR, message);
    return complete;
}

static HRESULT DrawWidget(WidgetEntry* entry, float x, float width);

static HRESULT CreateSurface(WidgetEntry* entry, float width_dips) {
    if (!entry || !g_runtime.graphics.dcomp_device || !g_runtime.graphics.d2d_device) return TE_E_FAIL;
    const UINT width = std::max(1u, static_cast<UINT>(std::ceil(width_dips * g_runtime.dpi_scale)));
    const UINT height = std::max(1u, static_cast<UINT>(std::ceil(g_runtime.widget_height * g_runtime.dpi_scale)));
    IDCompositionSurface* surface = nullptr;
    IDCompositionVisual* visual = nullptr;
    ID2D1DeviceContext* context = nullptr;
    HRESULT hr = g_runtime.graphics.dcomp_device->CreateSurface(width, height, DXGI_FORMAT_B8G8R8A8_UNORM,
        DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    LogHrFailure("IDCompositionDevice::CreateSurface(widget)", hr);
    if (FAILED(hr)) return hr;
    hr = g_runtime.graphics.dcomp_device->CreateVisual(&visual);
    LogHrFailure("IDCompositionDevice::CreateVisual(widget)", hr);
    if (SUCCEEDED(hr)) hr = visual->SetContent(surface);
    LogHrFailure("IDCompositionVisual::SetContent(widget)", hr);
    if (SUCCEEDED(hr)) hr = g_runtime.graphics.d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context);
    LogHrFailure("ID2D1Device::CreateDeviceContext(widget)", hr);
    if (FAILED(hr)) {
        if (context) context->Release();
        if (visual) visual->Release();
        if (surface) surface->Release();
        return hr;
    }

    WidgetEntry replacement = *entry;
    replacement.surface = surface;
    replacement.visual = visual;
    replacement.context = context;
    replacement.surface_width = width_dips;
    replacement.surface_height = g_runtime.widget_height;
    replacement.surface_dpi_scale = g_runtime.dpi_scale;
    replacement.needs_render = true;
    hr = DrawWidget(&replacement, 0.0f, width_dips);
    if (FAILED(hr)) {
        if (context) { context->SetTarget(nullptr); context->Release(); }
        if (visual) visual->Release();
        if (surface) surface->Release();
        return hr;
    }
    hr = g_runtime.graphics.widget_root->AddVisual(visual, TRUE, nullptr);
    LogHrFailure("IDCompositionVisual::AddVisual(widget)", hr);
    if (FAILED(hr)) {
        if (context) { context->SetTarget(nullptr); context->Release(); }
        if (visual) visual->Release();
        if (surface) surface->Release();
        return hr;
    }

    /* Keep the previous rendered visual until the replacement is ready to commit. */
    if (entry->visual) g_runtime.graphics.widget_root->RemoveVisual(entry->visual);
    if (entry->context) { entry->context->SetTarget(nullptr); entry->context->Release(); }
    if (entry->visual) entry->visual->Release();
    if (entry->surface) entry->surface->Release();
    entry->surface = surface;
    entry->visual = visual;
    entry->context = context;
    entry->surface_width = width_dips;
    entry->surface_height = g_runtime.widget_height;
    entry->surface_dpi_scale = g_runtime.dpi_scale;
    entry->needs_render = replacement.needs_render;
    return hr;
}

static float WidgetTopOffset(const RECT& taskbar_rect, const RECT& overlay_rect) {
    const float taskbar_height = static_cast<float>(taskbar_rect.bottom - taskbar_rect.top);
    const float widget_height_px = g_runtime.widget_height * g_runtime.dpi_scale;
    const float centered_y = (taskbar_height - widget_height_px) * 0.5f;
    return std::round(static_cast<float>(taskbar_rect.top - overlay_rect.top) + centered_y);
}

static HRESULT SetWidgetPosition(WidgetEntry* entry, float x) {
    if (!entry || !entry->visual || !g_runtime.get_overlay_window) return E_HANDLE;
    const HWND overlay_hwnd = g_runtime.get_overlay_window();
    RECT overlay_rect{};
    if (!overlay_hwnd || !GetWindowRect(overlay_hwnd, &overlay_rect)) {
        const DWORD error = GetLastError();
        LogWindowFailure("GetWindowRect(overlay)", overlay_hwnd, error);
        return HRESULT_FROM_WIN32(error);
    }
    const float offset_x = std::round(x * g_runtime.dpi_scale +
        static_cast<float>(g_runtime.taskbar_rect.left - overlay_rect.left));
    const float offset_y = WidgetTopOffset(g_runtime.taskbar_rect, overlay_rect);
    HRESULT hr = entry->visual->SetOffsetX(offset_x);
    LogHrFailure("IDCompositionVisual::SetOffsetX(widget)", hr);
    if (SUCCEEDED(hr)) hr = entry->visual->SetOffsetY(offset_y);
    LogHrFailure("IDCompositionVisual::SetOffsetY(widget)", hr);
    return hr;
}

static HRESULT DrawWidget(WidgetEntry* entry, float x, float width) {
    if (!entry || !entry->surface || !entry->context || !entry->visual || entry->faulted) return TE_E_FAIL;
    const UINT px_width = std::max(1u, static_cast<UINT>(std::ceil(width * g_runtime.dpi_scale)));
    const UINT px_height = std::max(1u, static_cast<UINT>(std::ceil(g_runtime.widget_height * g_runtime.dpi_scale)));
    POINT offset{};
    IDXGISurface* dxgi_surface = nullptr;
    HRESULT hr = entry->surface->BeginDraw(nullptr, __uuidof(IDXGISurface), reinterpret_cast<void**>(&dxgi_surface), &offset);
    LogHrFailure("IDCompositionSurface::BeginDraw(widget)", hr);
    if (FAILED(hr)) {
        /* A busy DirectComposition surface can reject a frame transiently.
           Keep the widget dirty so the next normal timer tick retries it. */
        entry->needs_render = true;
        return hr;
    }
    ID2D1Bitmap1* target = nullptr;
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * g_runtime.dpi_scale, 96.0f * g_runtime.dpi_scale);
    hr = entry->context->CreateBitmapFromDxgiSurface(dxgi_surface, &properties, &target);
    LogHrFailure("ID2D1DeviceContext::CreateBitmapFromDxgiSurface(widget)", hr);
    dxgi_surface->Release();
    if (SUCCEEDED(hr)) {
        entry->context->SetTarget(target);
        entry->context->BeginDraw();
        entry->context->Clear(D2D1::ColorF(0, 0.0f));
        entry->context->SetTransform(D2D1::Matrix3x2F::Identity());
        WidgetRenderContext render_context{ entry->context, width, g_runtime.widget_height, g_runtime.dpi_scale };
        if (!GuardRender(entry, &render_context)) hr = TE_E_FAIL;
        const HRESULT end_hr = entry->context->EndDraw();
        LogHrFailure("ID2D1DeviceContext::EndDraw(widget)", end_hr);
        if (SUCCEEDED(hr) && FAILED(end_hr)) hr = end_hr;
        entry->context->SetTarget(nullptr);
        target->Release();
        if (SUCCEEDED(hr)) entry->needs_render = false;
    }
    const HRESULT surface_hr = entry->surface->EndDraw();
    LogHrFailure("IDCompositionSurface::EndDraw(widget)", surface_hr);
    if (FAILED(surface_hr)) {
        entry->needs_render = true;
        return surface_hr;
    }
    if (FAILED(hr)) {
        entry->needs_render = true;
        return hr;
    }
    /* Layout is in DIPs; DirectComposition offsets are physical pixels. */
    hr = SetWidgetPosition(entry, x);
    (void)px_width;
    (void)px_height;
    return hr;
}

static void RefreshWidgets(const cJSON* config) {
    if (!config) return;
    RECT rect{};
    if (!GetWindowRect(g_runtime.taskbar, &rect)) {
        LogWindowFailure("GetWindowRect(taskbar)", g_runtime.taskbar, GetLastError());
        return;
    }
    g_runtime.taskbar_rect = rect;
    const float taskbar_height_dips = static_cast<float>(rect.bottom - rect.top) / g_runtime.dpi_scale;
    g_runtime.widget_height = std::clamp(taskbar_height_dips, 20.0f, kWidgetHeight);
    std::vector<std::string> order = ParseLayout(config);
    if (order.empty()) order = { "clock", "sysmon", "media" };
    // Keep a newly installed package visible to the user without requiring a
    // source edit or a manual config migration; append undiscovered-order IDs
    // deterministically after the persisted choices.
    for (const auto& entry : g_runtime.widgets) {
        if (std::find(order.begin(), order.end(), entry.id) == order.end()) order.push_back(entry.id);
    }
    for (auto& entry : g_runtime.widgets) {
        const bool should_enable = WidgetEnabled(config, entry.id.c_str(), true);
        if (entry.initialized && entry.enabled && !should_enable) {
            GuardDisable(&entry);
            entry.enabled = false;
            if (entry.visual && g_runtime.graphics.widget_root) g_runtime.graphics.widget_root->RemoveVisual(entry.visual);
            if (entry.context) { entry.context->SetTarget(nullptr); entry.context->Release(); entry.context = nullptr; }
            if (entry.surface) { entry.surface->Release(); entry.surface = nullptr; }
            if (entry.visual) { entry.visual->Release(); entry.visual = nullptr; }
        } else if (!entry.initialized) {
            entry.initialized = GuardInitialize(&entry, &g_runtime.widget_context);
            if (entry.initialized && should_enable) entry.enabled = GuardEnable(&entry);
        } else if (!entry.enabled && should_enable) {
            entry.enabled = GuardEnable(&entry);
        }
    }
    std::vector<Item> items;
    const float height = g_runtime.widget_height;
    const float dpi = g_runtime.dpi_scale;
    for (const auto& id : order) {
        WidgetEntry* entry = FindWidget(id);
        if (!entry) continue;
        if (!WidgetEnabled(config, id.c_str(), true) || !entry->enabled) continue;
        WidgetMeasurement measurement = GuardMeasure(entry, height, dpi);
        measurement = SanitizeMeasurement(measurement);
        Item item;
        item.id = id;
        item.measure = measurement;
        items.emplace_back(std::move(item));
    }
    const cJSON* pos_percent = cJSON_GetObjectItemCaseSensitive(config, "position_percent");
    const cJSON* legacy_pos = cJSON_GetObjectItemCaseSensitive(config, "position");
    if (cJSON_IsNumber(pos_percent)) {
        g_runtime.position_percent = std::clamp(static_cast<float>(pos_percent->valuedouble), 0.0f, 100.0f);
    } else if (cJSON_IsString(legacy_pos) && legacy_pos->valuestring) {
        const std::string legacy(legacy_pos->valuestring);
        g_runtime.position_percent = legacy == "left" ? 0.0f : legacy == "center" ? 50.0f : 100.0f;
    } else {
        g_runtime.position_percent = 100.0f;
    }
    float available_width = static_cast<float>(rect.right - rect.left) / g_runtime.dpi_scale;
    HWND tray = FindWindowExW(g_runtime.taskbar, nullptr, L"TrayNotifyWnd", nullptr);
    RECT tray_rect{};
    if (tray && IsWindowVisible(tray) && GetWindowRect(tray, &tray_rect) &&
        tray_rect.left > rect.left && tray_rect.left < rect.right) {
        const float usable_right_px = static_cast<float>(tray_rect.left - rect.left);
        available_width = std::min(available_width,
            std::max(0.0f, usable_right_px / g_runtime.dpi_scale - GetGap(config)));
    }
    g_runtime.taskbar_rect = rect;
    g_runtime.layout.Reset(items, available_width, GetGap(config));
    float content_width = 0.0f;
    if (!g_runtime.layout.Items().empty()) {
        const auto& last = g_runtime.layout.Items().back();
        content_width = last.x + last.width;
    }
    g_runtime.layout_origin = PositionOrigin(available_width, content_width, g_runtime.position_percent);
    for (auto& item : g_runtime.layout.Items()) {
        item.x += g_runtime.layout_origin;
        item.spring_position = item.x;
        item.spring_velocity = 0.0f;
    }
    for (auto& item : g_runtime.layout.Items()) {
        WidgetEntry* entry = FindWidget(item.id);
        if (!entry) continue;
        if (!entry->context || std::fabs(entry->surface_width - item.width) > 0.5f ||
            std::fabs(entry->surface_height - g_runtime.widget_height) > 0.5f ||
            std::fabs(entry->surface_dpi_scale - g_runtime.dpi_scale) > 0.001f) {
            const HRESULT init_hr = CreateSurface(entry, std::max(item.width, 1.0f));
            if (FAILED(init_hr)) {
                Log(TE_LOG_WARNING, "Could not create widget surface; retrying on the next frame");
                continue;
            }
        }
        if (entry->context && entry->needs_render) {
            const HRESULT hr = DrawWidget(entry, item.spring_position, item.width);
            if (FAILED(hr)) Log(TE_LOG_WARNING, "Widget drawing failed; shared graphics will be retried");
        } else if (entry && entry->visual) {
            const HRESULT position_hr = SetWidgetPosition(entry, item.spring_position);
            if (FAILED(position_hr)) Log(TE_LOG_WARNING, "Widget positioning failed");
        }
    }
    CommitGraphics();
}

static void PersistLayout() {
    cJSON* root = nullptr;
    if (FAILED(TE_JsoncParseFile(g_runtime.config_path.c_str(), &root)) || !root) return;
    cJSON* widgets = cJSON_GetObjectItemCaseSensitive(root, "widgets");
    if (!widgets) {
        widgets = cJSON_CreateObject();
        if (!widgets || !cJSON_AddItemToObject(root, "widgets", widgets)) {
            if (widgets) cJSON_Delete(widgets);
            TE_JsoncFree(root);
            return;
        }
    }
    if (!cJSON_IsObject(widgets) || !PersistOrder(widgets, g_runtime.layout.Items())) {
        TE_JsoncFree(root);
        return;
    }
    char* json = cJSON_Print(root);
    if (!json) { TE_JsoncFree(root); return; }
    const std::wstring temp = g_runtime.config_path + L".widgets.tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        const DWORD bytes = static_cast<DWORD>(strlen(json));
        const BOOL ok = WriteFile(file, json, bytes, &written, nullptr) && written == bytes;
        FlushFileBuffers(file);
        CloseHandle(file);
        if (ok) MoveFileExW(temp.c_str(), g_runtime.config_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        else DeleteFileW(temp.c_str());
    }
    cJSON_free(json);
    TE_JsoncFree(root);
}

static int HitWidget(float x, float y, WidgetHitTestResult* hit) {
    if (hit) *hit = {};
    const float local_x = x / g_runtime.dpi_scale;
    const float local_y = y / g_runtime.dpi_scale;
    struct DelegateContext { WidgetHitTestResult* result; } context{ hit };
    return DelegateHitTest(g_runtime.layout, local_x, local_y, g_runtime.widget_height,
        [](const std::string& id, float x_pos, float y_pos, void* opaque) {
            auto* delegate_context = static_cast<DelegateContext*>(opaque);
            WidgetEntry* entry = FindWidget(id);
            WidgetHitTestResult result = GuardHitTest(entry, x_pos, y_pos);
            if (delegate_context && delegate_context->result) *delegate_context->result = result;
            return result;
        }, &context);
}

static void PositionWidgetVisuals() {
    if (!g_runtime.graphics.widget_root || !g_runtime.get_overlay_window) return;
    const HWND overlay = g_runtime.get_overlay_window();
    RECT overlay_rect{};
    if (!overlay || !GetWindowRect(overlay, &overlay_rect)) return;
    RECT taskbar_rect{};
    if (!GetWindowRect(g_runtime.taskbar, &taskbar_rect)) return;
    g_runtime.taskbar_rect = taskbar_rect;
    const float offset_y = WidgetTopOffset(taskbar_rect, overlay_rect);
    for (const auto& item : g_runtime.layout.Items()) {
        WidgetEntry* entry = FindWidget(item.id);
        if (!entry || !entry->visual) continue;
        const float position = g_runtime.dragging ? item.x : item.spring_position;
        const float offset_x = std::round(position * g_runtime.dpi_scale +
            static_cast<float>(taskbar_rect.left - overlay_rect.left));
        HRESULT hr = entry->visual->SetOffsetX(offset_x);
        if (SUCCEEDED(hr)) hr = entry->visual->SetOffsetY(offset_y);
        LogHrFailure("IDCompositionVisual::SetOffset(widget drag)", hr);
    }
    CommitGraphics();
}

static void RenderFrame() {
    if (!g_runtime.enabled) return;
    if (g_runtime.get_generation) {
        const uint64_t generation = g_runtime.get_generation();
        if (generation != g_runtime.graphics_generation) {
            for (auto& entry : g_runtime.widgets) {
                if (entry.visual && g_runtime.graphics.widget_root) g_runtime.graphics.widget_root->RemoveVisual(entry.visual);
                if (entry.context) { entry.context->SetTarget(nullptr); entry.context->Release(); entry.context = nullptr; }
                if (entry.surface) { entry.surface->Release(); entry.surface = nullptr; }
                if (entry.visual) { entry.visual->Release(); entry.visual = nullptr; }
            }
            if (g_runtime.release_graphics) g_runtime.release_graphics(&g_runtime.graphics);
            if (!g_runtime.acquire_graphics || FAILED(g_runtime.acquire_graphics(g_runtime.taskbar, &g_runtime.graphics))) {
                Log(TE_LOG_ERROR, "Shared graphics resources could not be reacquired after device loss");
                return;
            }
            g_runtime.graphics_generation = g_runtime.get_generation();
            RefreshWidgets(g_runtime.widget_config);
            return;
        }
    }
    const uint64_t now = GetTickCount64();
    const float dt = g_runtime.last_frame ? static_cast<float>(now - g_runtime.last_frame) / 1000.0f : 0.0f;
    g_runtime.last_frame = now;
    const bool animating = g_runtime.layout.Tick(dt);
    g_runtime.layout_animating = animating;
    bool changed = false;
    for (auto& item : g_runtime.layout.Items()) {
        WidgetEntry* entry = FindWidget(item.id);
        if (entry && (!entry->context || std::fabs(entry->surface_width - item.width) > 0.5f ||
                      std::fabs(entry->surface_height - g_runtime.widget_height) > 0.5f ||
                      std::fabs(entry->surface_dpi_scale - g_runtime.dpi_scale) > 0.001f)) {
            const HRESULT init_hr = CreateSurface(entry, std::max(item.width, 1.0f));
            if (FAILED(init_hr)) {
                LogHrFailure("CreateSurface(widget retry)", init_hr);
                continue;
            }
            const HRESULT position_hr = SetWidgetPosition(entry, item.spring_position);
            if (FAILED(position_hr)) LogHrFailure("SetWidgetPosition(widget retry)", position_hr);
            changed = true;
        }
        if (entry && entry->context && (entry->needs_render || GuardIsDirty(entry))) {
            const HRESULT hr = DrawWidget(entry, item.spring_position, item.width);
            if (FAILED(hr)) Log(TE_LOG_WARNING, "Widget frame update failed");
            else changed = true;
        } else if (entry && entry->visual && animating) {
            const HRESULT hr = SetWidgetPosition(entry, item.spring_position);
            if (SUCCEEDED(hr)) changed = true;
        }
    }
    if (changed) CommitGraphics();
    if (!animating && !g_runtime.dragging) {
        /* SetTimer remains one second for the clock; media state itself is event driven. */
    }
}

static void DispatchClick(int index, LPARAM lparam, uint32_t button) {
    if (index < 0 || static_cast<size_t>(index) >= g_runtime.layout.Items().size()) return;
    const auto& item = g_runtime.layout.Items()[static_cast<size_t>(index)];
    POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    const float x = static_cast<float>(pt.x) / g_runtime.dpi_scale - item.x;
    const float y = static_cast<float>(pt.y) / g_runtime.dpi_scale;
    GuardClick(FindWidget(item.id), x, y, button);
}

static void UpdateHover(HWND hwnd, LPARAM lparam) {
    WidgetHitTestResult hit{};
    const int index = HitWidget(static_cast<float>(GET_X_LPARAM(lparam)),
                                static_cast<float>(GET_Y_LPARAM(lparam)), &hit);
    if (index < 0 || !hit.handled) {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)));
        if (g_runtime.tooltip) SendMessageW(g_runtime.tooltip, TTM_TRACKACTIVATE, FALSE,
                                             reinterpret_cast<LPARAM>(&g_runtime.tooltip_info));
        return;
    }

    SetCursor(hit.cursor ? hit.cursor : LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
    if (!g_runtime.tooltip || !hit.tooltip || !hit.tooltip[0]) return;
    const int length = MultiByteToWideChar(CP_UTF8, 0, hit.tooltip, -1, nullptr, 0);
    if (length <= 1) return;
    g_runtime.tooltip_text.resize(static_cast<size_t>(length));
    MultiByteToWideChar(CP_UTF8, 0, hit.tooltip, -1, g_runtime.tooltip_text.data(), length);
    g_runtime.tooltip_text.resize(static_cast<size_t>(length - 1));
    g_runtime.tooltip_info.lpszText = g_runtime.tooltip_text.data();
    SendMessageW(g_runtime.tooltip, TTM_UPDATETIPTEXTW, 0,
                 reinterpret_cast<LPARAM>(&g_runtime.tooltip_info));
    POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
    ClientToScreen(hwnd, &point);
    SendMessageW(g_runtime.tooltip, TTM_TRACKPOSITION, 0,
                 MAKELPARAM(point.x + 12, point.y + 20));
    SendMessageW(g_runtime.tooltip, TTM_TRACKACTIVATE, TRUE,
                 reinterpret_cast<LPARAM>(&g_runtime.tooltip_info));
}

static void HandleTaskbarMouse(const TE_TaskbarMouseData* data) {
    if (!g_runtime.enabled || !data || data->taskbar_hwnd != g_runtime.taskbar) return;
    POINT client = data->cursor_pos;
    if (!ScreenToClient(g_runtime.taskbar, &client)) return;
    UpdateHover(g_runtime.taskbar, MAKELPARAM(client.x, client.y));
}

static void HandleTaskbarClick(const TE_TaskbarClickData* data) {
    if (!g_runtime.enabled || !data || data->taskbar_hwnd != g_runtime.taskbar) return;
    const uint64_t now = GetTickCount64();
    if (g_runtime.suppress_click_until && now <= g_runtime.suppress_click_until) {
        g_runtime.suppress_click_until = 0;
        return;
    }
    g_runtime.suppress_click_until = 0;
    POINT client = data->cursor_pos;
    if (!ScreenToClient(g_runtime.taskbar, &client)) return;
    WidgetHitTestResult hit{};
    const int index = HitWidget(static_cast<float>(client.x), static_cast<float>(client.y), &hit);
    if (index < 0) return;
    const LPARAM coords = MAKELPARAM(static_cast<SHORT>(client.x), static_cast<SHORT>(client.y));
    DispatchClick(index, coords, data->button);
}

static LRESULT CALLBACK TaskbarSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                                             UINT_PTR subclass_id, DWORD_PTR ref_data) {
    (void)subclass_id; (void)ref_data;
    switch (msg) {
    case WM_TIMER:
        if (wparam == kTimerId) {
            RenderFrame();
            SetTimer(hwnd, kTimerId, (g_runtime.layout_animating || g_runtime.dragging) ? kAnimationIntervalMs : kFrameIntervalMs, nullptr);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        WidgetHitTestResult hit{};
        const int index = HitWidget(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)), &hit);
        if (index >= 0) {
            const float x = static_cast<float>(GET_X_LPARAM(lparam)) / g_runtime.dpi_scale;
            g_runtime.drag_origin_x = x;
            g_runtime.last_pointer_x = x;
            g_runtime.dragged_widget = index;
            g_runtime.dragging = false;
            g_runtime.layout.BeginDrag(static_cast<size_t>(index), x);
            SetCapture(hwnd);
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (g_runtime.dragged_widget >= 0) {
            const float x = static_cast<float>(GET_X_LPARAM(lparam)) / g_runtime.dpi_scale;
            if (std::fabs(x - g_runtime.drag_origin_x) > 4.0f) g_runtime.dragging = true;
            if (g_runtime.dragging) {
                g_runtime.last_pointer_x = x;
                g_runtime.layout.MoveAt(x);
                /* Drag motion only changes visual offsets; do not run plugin/D2D
                   rendering callbacks for every Explorer WM_MOUSEMOVE. */
                PositionWidgetVisuals();
            }
            return 0;
        }
        UpdateHover(hwnd, lparam);
        break;
    case WM_LBUTTONUP:
        if (g_runtime.dragged_widget >= 0) {
            const int index = g_runtime.dragged_widget;
            if (g_runtime.dragging) {
                g_runtime.suppress_click_until = GetTickCount64() + 500;
                g_runtime.layout.EndDrag();
                g_runtime.layout_animating = true;
                SetTimer(hwnd, kTimerId, kAnimationIntervalMs, nullptr);
                PersistLayout();
                RenderFrame();
            } else {
                g_runtime.layout.EndDrag();
            }
            (void)index;
            g_runtime.dragged_widget = -1;
            g_runtime.dragging = false;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        ScreenToClient(hwnd, &pt);
        WidgetHitTestResult hit{};
        const int index = HitWidget(static_cast<float>(pt.x), static_cast<float>(pt.y), &hit);
        if (index >= 0) {
            const auto& item = g_runtime.layout.Items()[static_cast<size_t>(index)];
            GuardScroll(FindWidget(item.id), static_cast<float>(pt.x) / g_runtime.dpi_scale - item.x,
                        static_cast<float>(pt.y) / g_runtime.dpi_scale,
                        static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)));
            return 0;
        }
        break;
    }
    case WM_DPICHANGED:
        g_runtime.dpi = GetDpi(hwnd);
        g_runtime.dpi_scale = static_cast<float>(g_runtime.dpi) / 96.0f;
        RefreshWidgets(g_runtime.widget_config);
        break;
    case WM_DISPLAYCHANGE:
        g_runtime.dpi = GetDpi(hwnd);
        g_runtime.dpi_scale = static_cast<float>(g_runtime.dpi) / 96.0f;
        RefreshWidgets(g_runtime.widget_config);
        break;
    case WM_WINDOWPOSCHANGED:
        RefreshWidgets(g_runtime.widget_config);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, TaskbarSubclassProc, kSubclassId);
        break;
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static void ApplyConfig(const cJSON* config) {
    if (!g_runtime.enabled || !config) return;
    RefreshWidgets(config);
    size_t active_count = 0;
    for (const auto& item : g_runtime.layout.Items()) {
        const WidgetEntry* entry = FindWidget(item.id);
        if (entry && entry->enabled && entry->context) ++active_count;
    }
    char message[224]{};
    const float first_x = g_runtime.layout.Items().empty() ? -1.0f : g_runtime.layout.Items().front().x;
    sprintf_s(message, "Applied widget config: %zu active, %zu discovered, origin=%.1f position=%.1f%% first_x=%.1f dpi=%.2f",
              active_count, g_runtime.widgets.size(), g_runtime.layout_origin, g_runtime.position_percent, first_x, g_runtime.dpi_scale);
    Log(TE_LOG_INFO, message);
}

} // namespace

void OnTaskbarMouse(const TE_TaskbarMouseData* data) { HandleTaskbarMouse(data); }
void OnTaskbarClick(const TE_TaskbarClickData* data) { HandleTaskbarClick(data); }

HRESULT Initialize(const PluginContext* context) {
    if (!context || !context->taskbar_hwnd) return TE_E_INVALIDARG;
    g_runtime = Runtime{};
    g_runtime.plugin_context = context;
    g_runtime.taskbar = context->taskbar_hwnd;
    g_runtime.dpi = GetDpi(context->taskbar_hwnd);
    g_runtime.dpi_scale = static_cast<float>(g_runtime.dpi) / 96.0f;
    SetPaths();
    DiscoverWidgets();
    cJSON* root = nullptr;
    const cJSON* config = nullptr;
    ReadWidgetsConfig(&root, &config);
    if (config) g_runtime.widget_config = cJSON_Duplicate(config, 1);
    if (root) TE_JsoncFree(root);
    g_runtime.widget_context = *context;
    g_runtime.widget_context.config = g_runtime.widget_config;
    g_runtime.initialized = true;
    if (g_runtime.plugin_context->log) {
        char message[128]{};
        sprintf_s(message, "Discovered %zu widget DLLs", g_runtime.widgets.size());
        Log(TE_LOG_INFO, message);
    }
    return TE_S_OK;
}

HRESULT Enable(void) {
    if (!g_runtime.initialized) return TE_E_FAIL;
    if (!ResolveOverlayApi()) {
        Log(TE_LOG_ERROR, "Shared overlay exports missing: icon_hover.dll or TE_DCompWidgetAcquire/Release/Commit/GetOverlayWindow/GetGeneration");
        return TE_E_FAIL;
    }
    const HRESULT acquire_hr = g_runtime.acquire_graphics(g_runtime.taskbar, &g_runtime.graphics);
    LogHrFailure("TE_DCompWidgetAcquire", acquire_hr);
    if (FAILED(acquire_hr)) {
        Log(TE_LOG_ERROR, "Shared DirectComposition overlay unavailable; widgets remain inactive");
        return acquire_hr;
    }
    g_runtime.graphics_generation = g_runtime.get_generation();
    {
        DWORD pid = 0;
        const DWORD tid = g_runtime.graphics.overlay_hwnd ? GetWindowThreadProcessId(g_runtime.graphics.overlay_hwnd, &pid) : 0;
        wchar_t class_name[128] = L"?";
        if (g_runtime.graphics.overlay_hwnd) GetClassNameW(g_runtime.graphics.overlay_hwnd, class_name, 128);
        char class_utf8[128] = "?";
        WideCharToMultiByte(CP_UTF8, 0, class_name, -1, class_utf8, sizeof(class_utf8), nullptr, nullptr);
        char message[256]{};
        sprintf_s(message, "Acquired shared overlay HWND=%p class=%s pid=%lu tid=%lu IsWindow=%u generation=%llu",
                  static_cast<void*>(g_runtime.graphics.overlay_hwnd),
                  class_utf8, pid, tid,
                  g_runtime.graphics.overlay_hwnd && IsWindow(g_runtime.graphics.overlay_hwnd) ? 1u : 0u,
                  static_cast<unsigned long long>(g_runtime.graphics_generation));
        Log(TE_LOG_INFO, message);
    }
    if (!g_runtime.widget_config) g_runtime.widget_config = LoadDefaultWidgetsConfig();
    if (!g_runtime.widget_config) return TE_E_OUTOFMEMORY;
    g_runtime.enabled = true;
    if (!SetWindowSubclass(g_runtime.taskbar, TaskbarSubclassProc, kSubclassId, 0)) {
        LogWindowFailure("SetWindowSubclass(taskbar)", g_runtime.taskbar, GetLastError());
        g_runtime.enabled = true;
        Disable();
        return TE_E_FAIL;
    }
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&controls);
    g_runtime.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, g_runtime.taskbar, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (g_runtime.tooltip) {
        g_runtime.tooltip_info = {};
        g_runtime.tooltip_info.cbSize = sizeof(TOOLINFOW);
        g_runtime.tooltip_info.uFlags = TTF_TRACK | TTF_ABSOLUTE;
        g_runtime.tooltip_info.hwnd = g_runtime.taskbar;
        g_runtime.tooltip_info.uId = 1;
        g_runtime.tooltip_info.lpszText = const_cast<LPWSTR>(L"");
        SendMessageW(g_runtime.tooltip, TTM_ADDTOOLW, 0,
                     reinterpret_cast<LPARAM>(&g_runtime.tooltip_info));
    }
    if (!SetTimer(g_runtime.taskbar, kTimerId, kFrameIntervalMs, nullptr)) {
        LogWindowFailure("SetTimer(taskbar)", g_runtime.taskbar, GetLastError());
    }
    ApplyConfig(g_runtime.widget_config);
    return TE_S_OK;
}

HRESULT Disable(void) {
    if (!g_runtime.enabled) return TE_S_OK;
    g_runtime.enabled = false;
    KillTimer(g_runtime.taskbar, kTimerId);
    if (g_runtime.tooltip) { DestroyWindow(g_runtime.tooltip); g_runtime.tooltip = nullptr; }
    RemoveWindowSubclass(g_runtime.taskbar, TaskbarSubclassProc, kSubclassId);
    if (GetCapture() == g_runtime.taskbar) ReleaseCapture();
    for (auto& entry : g_runtime.widgets) {
        if (entry.enabled) GuardDisable(&entry);
        entry.enabled = false;
        if (entry.initialized) { GuardShutdown(&entry); entry.initialized = false; }
        if (entry.visual && g_runtime.graphics.widget_root) g_runtime.graphics.widget_root->RemoveVisual(entry.visual);
        if (entry.context) { entry.context->SetTarget(nullptr); entry.context->Release(); entry.context = nullptr; }
        if (entry.surface) { entry.surface->Release(); entry.surface = nullptr; }
        if (entry.visual) { entry.visual->Release(); entry.visual = nullptr; }
    }
    CommitGraphics();
    if (g_runtime.release_graphics) g_runtime.release_graphics(&g_runtime.graphics);
    return TE_S_OK;
}

HRESULT Shutdown(void) {
    if (g_runtime.enabled) Disable();
    for (auto& entry : g_runtime.widgets) {
        if (entry.initialized) GuardShutdown(&entry);
        /* A faulted DLL may still own unknown worker/COM state. Keep it loaded
           until Explorer exits instead of unloading code under that state. */
        if (entry.module && !entry.faulted) FreeLibrary(entry.module);
    }
    g_runtime.widgets.clear();
    if (g_runtime.widget_config) { cJSON_Delete(g_runtime.widget_config); g_runtime.widget_config = nullptr; }
    if (g_runtime.bridge_loaded_here && g_runtime.bridge_module) FreeLibrary(g_runtime.bridge_module);
    g_runtime.bridge_module = nullptr;
    g_runtime.bridge_loaded_here = false;
    g_runtime.initialized = false;
    g_runtime.plugin_context = nullptr;
    return TE_S_OK;
}

void OnConfigChanged(const TE_ConfigChangedData* data) {
    /* Core emits widget configuration as a separate "widgets" section event.
       A widget_host plugin event contains only the host's enabled flag. */
    if (!data || !data->plugin_name || strcmp(data->plugin_name, "widgets") != 0) return;
    const auto* config = static_cast<const cJSON*>(data->new_config);
    cJSON* replacement = config ? cJSON_Duplicate(config, 1) : LoadDefaultWidgetsConfig();
    if (!replacement) return;
    if (g_runtime.widget_config && cJSON_Compare(g_runtime.widget_config, replacement, cJSON_True)) {
        cJSON_Delete(replacement);
        return;
    }
    std::vector<std::string> changed_widgets;
    for (auto& entry : g_runtime.widgets) {
        const cJSON* old_section = g_runtime.widget_config ? cJSON_GetObjectItemCaseSensitive(g_runtime.widget_config, entry.id.c_str()) : nullptr;
        const cJSON* new_section = cJSON_GetObjectItemCaseSensitive(replacement, entry.id.c_str());
        if ((old_section == nullptr) != (new_section == nullptr) ||
            (old_section && new_section && !cJSON_Compare(old_section, new_section, cJSON_True))) {
            changed_widgets.push_back(entry.id);
        }
    }
    if (g_runtime.widget_config) cJSON_Delete(g_runtime.widget_config);
    g_runtime.widget_config = replacement;
    g_runtime.widget_context.config = replacement;
    for (const auto& id : changed_widgets) {
        WidgetEntry* entry = FindWidget(id);
        if (!entry) continue;
        if (entry->enabled) GuardDisable(entry);
        if (entry->initialized) GuardShutdown(entry);
        entry->enabled = false;
        entry->initialized = false;
        if (entry->visual && g_runtime.graphics.widget_root) g_runtime.graphics.widget_root->RemoveVisual(entry->visual);
        if (entry->context) { entry->context->SetTarget(nullptr); entry->context->Release(); entry->context = nullptr; }
        if (entry->surface) { entry->surface->Release(); entry->surface = nullptr; }
        if (entry->visual) { entry->visual->Release(); entry->visual = nullptr; }
        entry->needs_render = true;
    }
    ApplyConfig(replacement);
}

} // namespace te::widgets
