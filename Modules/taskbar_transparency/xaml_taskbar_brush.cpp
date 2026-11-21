#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <xamlOM.h>
#include <strsafe.h>
#undef GetCurrentTime

#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include <winrt/base.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <sdk/te_log.h>
#include "xaml_taskbar_brush.h"

namespace wux = winrt::Windows::UI::Xaml;
namespace wuxh = winrt::Windows::UI::Xaml::Hosting;
namespace wuxm = winrt::Windows::UI::Xaml::Media;
namespace wuxs = winrt::Windows::UI::Xaml::Shapes;
namespace wsys = winrt::Windows::System;

namespace {

constexpr CLSID CLSID_TaskbarEngineXamlTap =
    { 0x9cb93c63, 0xc272, 0x48f0, { 0x96, 0x53, 0x49, 0x6e, 0xd8, 0x1c, 0x41, 0x27 } };

std::mutex g_settings_mutex;
TE_TransparencySettings g_desired_settings{};
std::mutex g_dispatcher_mutex;
std::mutex g_log_mutex;
wsys::DispatcherQueue g_xaml_dispatcher{ nullptr };
std::atomic<class XamlTap*> g_tap{ nullptr };
std::atomic<long> g_start_state{ 0 }; /* 0=not started, 1=starting, 2=attached, 3=failed */
std::atomic<uint32_t> g_taskbar_count{ 0 };
std::atomic<unsigned long> g_endpoint_sequence{ 1 };

void Log(TE_LogLevel level, const char* message)
{
    const char* level_name = level == TE_LOG_ERROR ? "ERROR" :
                             level == TE_LOG_WARNING ? "WARN" :
                             level == TE_LOG_DEBUG ? "DEBUG" : "INFO";
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char line[1200]{};
    const int length = sprintf_s(line, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s] [TaskbarTransparency.Xaml] %s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
        now.wMilliseconds, level_name, message ? message : "");
    if (length <= 0) return;

    wchar_t local_app_data[MAX_PATH]{};
    const DWORD path_length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, ARRAYSIZE(local_app_data));
    if (path_length == 0 || path_length >= ARRAYSIZE(local_app_data)) {
        OutputDebugStringA(line);
        return;
    }

    wchar_t log_directory[MAX_PATH]{};
    if (FAILED(StringCchPrintfW(log_directory, ARRAYSIZE(log_directory), L"%s\\TaskbarEngine\\logs", local_app_data))) {
        OutputDebugStringA(line);
        return;
    }
    CreateDirectoryW((std::wstring(local_app_data) + L"\\TaskbarEngine").c_str(), nullptr);
    CreateDirectoryW(log_directory, nullptr);
    const std::wstring log_path = std::wstring(log_directory) + L"\\xaml-taskbar.log";

    std::lock_guard<std::mutex> lock(g_log_mutex);
    HANDLE file = CreateFileW(log_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        CloseHandle(file);
    } else {
        OutputDebugStringA(line);
    }
}

std::string HResultText(const char* operation, HRESULT hr)
{
    char message[320]{};
    sprintf_s(message, "%s HRESULT=0x%08lX", operation, static_cast<unsigned long>(hr));
    return message;
}

TE_TransparencySettings DesiredSettings()
{
    std::lock_guard<std::mutex> lock(g_settings_mutex);
    return g_desired_settings;
}

float EffectiveTintOpacity(uint32_t argb, float opacity)
{
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    const uint8_t alpha = static_cast<uint8_t>((argb >> 24) & 0xff);
    const float configured_alpha = alpha ? static_cast<float>(alpha) / 255.0f : 1.0f;
    return configured_alpha * opacity;
}

winrt::Windows::UI::Color OpaqueTint(uint32_t argb)
{
    return winrt::Windows::UI::Color{
        255,
        static_cast<uint8_t>((argb >> 16) & 0xff),
        static_cast<uint8_t>((argb >> 8) & 0xff),
        static_cast<uint8_t>(argb & 0xff)
    };
}

void QueueApply();

template<typename T>
T GetXamlObject(IXamlDiagnostics* diagnostics, InstanceHandle handle)
{
    winrt::com_ptr<::IInspectable> inspectable;
    winrt::check_hresult(diagnostics->GetIInspectableFromHandle(handle, inspectable.put()));
    return inspectable.as<T>();
}

class XamlTap : public winrt::implements<XamlTap,
    IObjectWithSite,
    IVisualTreeServiceCallback2,
    winrt::non_agile>
{
public:
    HRESULT STDMETHODCALLTYPE SetSite(IUnknown* site) override
    {
        if (!site) return E_INVALIDARG;

        IXamlDiagnostics* diagnostics = nullptr;
        HRESULT hr = site->QueryInterface(__uuidof(IXamlDiagnostics), reinterpret_cast<void**>(&diagnostics));
        if (FAILED(hr)) return hr;
        m_diagnostics.attach(diagnostics);
        g_tap.store(this, std::memory_order_release);

        auto self = get_strong();
        std::thread([self = std::move(self)]() mutable {
            try {
                auto service = self->m_diagnostics.as<IVisualTreeService3>();
                HRESULT advise_hr = service->AdviseVisualTreeChange(
                    static_cast<IVisualTreeServiceCallback*>(self.get()));
                if (SUCCEEDED(advise_hr)) {
                    g_start_state.store(2, std::memory_order_release);
                    Log(TE_LOG_INFO, "XAML Diagnostics TAP attached; waiting for taskbar background elements (visual_pixels=unverified)");
                } else {
                    Log(TE_LOG_ERROR, HResultText("IVisualTreeService3.AdviseVisualTreeChange", advise_hr).c_str());
                    g_start_state.store(3, std::memory_order_release);
                }
            } catch (winrt::hresult_error const& error) {
                Log(TE_LOG_ERROR, HResultText("XAML Diagnostics AdviseVisualTreeChange", error.code()).c_str());
                g_start_state.store(3, std::memory_order_release);
            } catch (...) {
                Log(TE_LOG_ERROR, "XAML Diagnostics AdviseVisualTreeChange raised an unknown exception");
                g_start_state.store(3, std::memory_order_release);
            }
        }).detach();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSite(REFIID riid, void** site) noexcept override
    {
        if (!site) return E_POINTER;
        *site = nullptr;
        if (!m_diagnostics) return E_FAIL;
        return m_diagnostics->QueryInterface(riid, site);
    }

    HRESULT STDMETHODCALLTYPE OnVisualTreeChange(
        ParentChildRelation relation,
        VisualElement element,
        VisualMutationType mutation_type) override
    {
        struct Bstrs {
            BSTR file_name;
            BSTR hash;
            BSTR type;
            BSTR name;
            ~Bstrs() { SysFreeString(file_name); SysFreeString(hash); SysFreeString(type); SysFreeString(name); }
        } strings{ element.SrcInfo.FileName, element.SrcInfo.Hash, element.Type, element.Name };

        try {
            const std::wstring_view type(element.Type ? element.Type : L"");
            const std::wstring_view name(element.Name ? element.Name : L"");
            if (type == winrt::name_of<wuxh::DesktopWindowXamlSource>() ||
                type == L"Taskbar.TaskbarFrame" || name == L"BackgroundFill") {
                char message[640]{};
                sprintf_s(message, "VisualTree mutation=%s handle=%llu parent=%llu type=%ls name=%ls",
                    mutation_type == Add ? "Add" : mutation_type == Remove ? "Remove" : "Other",
                    static_cast<unsigned long long>(element.Handle),
                    static_cast<unsigned long long>(relation.Parent),
                    element.Type ? element.Type : L"", element.Name ? element.Name : L"");
                Log(TE_LOG_INFO, message);
            }
            {
                std::lock_guard<std::mutex> lock(g_dispatcher_mutex);
                if (!g_xaml_dispatcher) g_xaml_dispatcher = wsys::DispatcherQueue::GetForCurrentThread();
            }

            if (mutation_type == Add) {
                if (type == winrt::name_of<wuxh::DesktopWindowXamlSource>()) {
                    m_xaml_sources.insert(element.Handle);
                    char message[160]{};
                    sprintf_s(message, "DesktopWindowXamlSource discovered handle=%llu", static_cast<unsigned long long>(element.Handle));
                    Log(TE_LOG_INFO, message);
                } else if (type == L"Taskbar.TaskbarFrame") {
                    RegisterTaskbarFrame(relation.Parent, element.Handle);
                } else if (type == winrt::name_of<wuxs::Rectangle>() && element.Name &&
                           wcscmp(element.Name, L"BackgroundFill") == 0) {
                    RegisterBackground(relation.Parent, element.Handle);
                }
            } else {
                auto shape = m_shapes.find(element.Handle);
                if (shape != m_shapes.end()) {
                    auto target = m_taskbars.find(shape->second);
                    if (target != m_taskbars.end()) {
                        UnregisterBackground(target->second);
                    }
                    m_shapes.erase(shape);
                }
                auto taskbar = m_taskbars.find(element.Handle);
                if (taskbar != m_taskbars.end()) {
                    UnregisterBackground(taskbar->second);
                    m_taskbars.erase(taskbar);
                    g_taskbar_count.store(static_cast<uint32_t>(m_taskbars.size()), std::memory_order_release);
                }
                m_xaml_sources.erase(element.Handle);
            }
        } catch (winrt::hresult_error const& error) {
            Log(TE_LOG_WARNING, HResultText("XAML visual tree callback", error.code()).c_str());
        } catch (...) {
            Log(TE_LOG_WARNING, "XAML visual tree callback failed while registering taskbar elements");
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnElementStateChanged(
        InstanceHandle,
        VisualElementState,
        LPCWSTR) noexcept override
    {
        return S_OK;
    }

    void ApplyCurrentSettings()
    {
        for (auto& item : m_taskbars) ApplyTarget(item.second, DesiredSettings());
    }

private:
    struct TaskbarTarget {
        HWND hwnd = nullptr;
        bool is_primary = false;
        wuxs::Rectangle background{ nullptr };
        wuxm::Brush original_fill{ nullptr };
        wuxm::Brush applied_fill{ nullptr };
        int64_t fill_changed_token = 0;
    };

    void RegisterTaskbarFrame(InstanceHandle parent_handle, InstanceHandle frame_handle)
    {
        (void)parent_handle;
        const auto frame = GetXamlObject<wux::UIElement>(m_diagnostics.get(), frame_handle);
        for (const InstanceHandle source_handle : m_xaml_sources) {
            try {
                const auto source = GetXamlObject<wuxh::DesktopWindowXamlSource>(m_diagnostics.get(), source_handle);
                const auto content = source.Content();
                if (!content || !IsDescendantOrSelf(content, frame)) continue;

                const auto native = source.as<IDesktopWindowXamlSourceNative>();
                HWND source_hwnd = nullptr;
                winrt::check_hresult(native->get_WindowHandle(&source_hwnd));
                if (!source_hwnd || !IsWindow(source_hwnd)) continue;

                TaskbarTarget target;
                target.hwnd = source_hwnd;
                const HWND root = GetAncestor(source_hwnd, GA_ROOT);
                wchar_t root_class[128] = L"";
                if (root) GetClassNameW(root, root_class, ARRAYSIZE(root_class));
                target.is_primary = wcscmp(root_class, L"Shell_TrayWnd") == 0;
                m_taskbars.insert_or_assign(frame_handle, std::move(target));
                g_taskbar_count.store(static_cast<uint32_t>(m_taskbars.size()), std::memory_order_release);
                LogTarget(source_hwnd, "XAML taskbar frame registered");
                return;
            } catch (winrt::hresult_wrong_thread const&) {
                continue;
            }
        }
        char message[192]{};
        sprintf_s(message, "TaskbarFrame did not match any DesktopWindowXamlSource content handle=%llu sourceCount=%zu",
                  static_cast<unsigned long long>(frame_handle), m_xaml_sources.size());
        Log(TE_LOG_WARNING, message);
    }

    bool IsDescendantOrSelf(wux::UIElement const& root, wux::UIElement const& element)
    {
        auto current = element;
        for (unsigned int depth = 0; current && depth < 128; ++depth) {
            if (current == root) return true;
            current = wuxm::VisualTreeHelper::GetParent(current).try_as<wux::UIElement>();
        }
        return false;
    }

    wux::FrameworkElement FindParent(std::wstring_view name, wux::FrameworkElement element)
    {
        auto current = element;
        for (unsigned int depth = 0; current && depth < 128; ++depth) {
            if (current.Name() == name) return current;
            current = wuxm::VisualTreeHelper::GetParent(current).try_as<wux::FrameworkElement>();
        }
        return nullptr;
    }

    void RegisterBackground(InstanceHandle parent_handle, InstanceHandle shape_handle)
    {
        const auto parent = GetXamlObject<wux::FrameworkElement>(m_diagnostics.get(), parent_handle);
        const auto frame = FindParent(L"TaskbarFrame", parent);
        if (!frame) return;

        InstanceHandle frame_handle = 0;
        winrt::check_hresult(m_diagnostics->GetHandleFromIInspectable(
            static_cast<::IInspectable*>(winrt::get_abi(frame)), &frame_handle));
        auto target = m_taskbars.find(frame_handle);
        if (target == m_taskbars.end()) return;

        auto rectangle = GetXamlObject<wuxs::Rectangle>(m_diagnostics.get(), shape_handle);
        if (target->second.background) UnregisterBackground(target->second);
        target->second.background = rectangle;
        target->second.original_fill = rectangle.Fill();
        target->second.fill_changed_token = rectangle.RegisterPropertyChangedCallback(
            wuxs::Shape::FillProperty(),
            [this, frame_handle](wux::DependencyObject const&, wux::DependencyProperty const&) {
                OnBackgroundFillChanged(frame_handle);
            });
        m_shapes.insert_or_assign(shape_handle, frame_handle);
        ApplyTarget(target->second, DesiredSettings());
    }

    void OnBackgroundFillChanged(InstanceHandle frame_handle)
    {
        auto target = m_taskbars.find(frame_handle);
        if (target == m_taskbars.end() || !target->second.background) return;

        auto current = target->second.background.Fill();
        if (target->second.applied_fill && current == target->second.applied_fill) return;
        if (current) target->second.original_fill = current;
        ApplyTarget(target->second, DesiredSettings());
    }

    void ApplyTarget(TaskbarTarget& target, const TE_TransparencySettings& settings)
    {
        if (!target.background) return;
        auto current = target.background.Fill();
        if (target.applied_fill && current != target.applied_fill && current) {
            target.original_fill = current;
        } else if (!target.original_fill && current && current != target.applied_fill) {
            target.original_fill = current;
        }

        if (!settings.enabled || settings.mode == TE_MODE_DISABLED ||
            (!settings.apply_secondary && !target.is_primary)) {
            RestoreTarget(target);
            return;
        }

        const float tint_opacity = EffectiveTintOpacity(settings.color_argb, settings.opacity);
        wuxm::Brush brush{ nullptr };
        if (settings.mode == TE_MODE_ACRYLIC || settings.mode == TE_MODE_BLUR) {
            wuxm::AcrylicBrush acrylic;
            // The taskbar is an XAML island hosted by Explorer. Backdrop only
            // samples the island's own content (often a solid fill); HostBackdrop
            // samples the desktop behind the taskbar and produces the expected blur.
            acrylic.BackgroundSource(wuxm::AcrylicBackgroundSource::HostBackdrop);
            acrylic.TintColor(OpaqueTint(settings.color_argb));
            acrylic.TintOpacity(settings.mode == TE_MODE_BLUR ? 0.0 : tint_opacity);
            brush = std::move(acrylic);
        } else if (settings.mode == TE_MODE_TINT) {
            wuxm::SolidColorBrush solid(OpaqueTint(settings.color_argb));
            solid.Opacity(tint_opacity);
            brush = std::move(solid);
        } else {
            wuxm::SolidColorBrush clear(winrt::Windows::UI::Color{ 0, 0, 0, 0 });
            brush = std::move(clear);
        }

        target.applied_fill = brush;
        target.background.Fill(brush);
        const char* brush_type = settings.mode == TE_MODE_ACRYLIC ? "AcrylicBrush" :
                                 settings.mode == TE_MODE_BLUR ? "AcrylicBrush(untinted)" :
                                 settings.mode == TE_MODE_TINT ? "SolidColorBrush" : "TransparentBrush";
        char message[320]{};
        sprintf_s(message,
            "Shape.Fill applied target=%p brush=%s source=HostBackdrop tint=0x%08X tintOpacity=%.4f result=HRESULT_S_OK visual_pixels=unverified",
            target.hwnd, brush_type, settings.color_argb, tint_opacity);
        Log(TE_LOG_INFO, message);
    }

    void RestoreTarget(TaskbarTarget& target)
    {
        if (!target.background || !target.applied_fill) return;
        const auto original = target.original_fill;
        target.applied_fill = nullptr;
        if (original) target.background.Fill(original);
        char message[192]{};
        sprintf_s(message, "Shape.Fill restored target=%p originalBrush=%s result=HRESULT_S_OK",
                  target.hwnd, original ? "captured" : "missing");
        Log(original ? TE_LOG_INFO : TE_LOG_WARNING, message);
    }

    void UnregisterBackground(TaskbarTarget& target)
    {
        if (target.background && target.fill_changed_token) {
            target.background.UnregisterPropertyChangedCallback(
                wuxs::Shape::FillProperty(), target.fill_changed_token);
        }
        target.fill_changed_token = 0;
        RestoreTarget(target);
        target.background = nullptr;
        target.original_fill = nullptr;
        target.applied_fill = nullptr;
    }

    void LogTarget(HWND hwnd, const char* operation)
    {
        wchar_t child_class[128] = L"?";
        wchar_t root_class[128] = L"?";
        DWORD child_pid = 0, root_pid = 0;
        const DWORD child_tid = GetWindowThreadProcessId(hwnd, &child_pid);
        const HWND root = GetAncestor(hwnd, GA_ROOT);
        const DWORD root_tid = root ? GetWindowThreadProcessId(root, &root_pid) : 0;
        GetClassNameW(hwnd, child_class, ARRAYSIZE(child_class));
        if (root) GetClassNameW(root, root_class, ARRAYSIZE(root_class));
        char message[512]{};
        sprintf_s(message,
            "%s XAML HWND=%p class=%ls pid=%lu tid=%lu taskbarRoot=%p class=%ls pid=%lu tid=%lu",
            operation, hwnd, child_class, child_pid, child_tid, root, root_class, root_pid, root_tid);
        Log(TE_LOG_INFO, message);
    }

    winrt::com_ptr<IXamlDiagnostics> m_diagnostics;
    std::unordered_set<InstanceHandle> m_xaml_sources;
    std::unordered_map<InstanceHandle, InstanceHandle> m_shapes;
    std::unordered_map<InstanceHandle, TaskbarTarget> m_taskbars;
};

void QueueApply()
{
    wsys::DispatcherQueue dispatcher{ nullptr };
    {
        std::lock_guard<std::mutex> lock(g_dispatcher_mutex);
        dispatcher = g_xaml_dispatcher;
    }
    if (!dispatcher) return;

    if (!dispatcher.TryEnqueue([] {
            XamlTap* tap = g_tap.load(std::memory_order_acquire);
            if (tap) tap->ApplyCurrentSettings();
        })) {
        Log(TE_LOG_WARNING, "DispatcherQueue.TryEnqueue failed while applying taskbar XAML brush");
    }
}

class TapFactory : public winrt::implements<TapFactory, IClassFactory, winrt::non_agile>
{
public:
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            return winrt::make<XamlTap>().as(riid, object);
        } catch (winrt::hresult_error const& error) {
            return error.code();
        } catch (...) {
            return E_FAIL;
        }
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL) noexcept override { return S_OK; }
};

DWORD WINAPI InitializeXamlDiagnostics(void* parameter)
{
    HMODULE module = static_cast<HMODULE>(parameter);
    wchar_t tap_path[MAX_PATH]{};
    if (!GetModuleFileNameW(module, tap_path, ARRAYSIZE(tap_path))) {
        const DWORD error = GetLastError();
        g_start_state.store(3, std::memory_order_release);
        Log(TE_LOG_ERROR, HResultText("GetModuleFileNameW(TAP)", HRESULT_FROM_WIN32(error)).c_str());
        FreeLibraryAndExitThread(module, error);
    }
    HMODULE xaml = LoadLibraryExW(L"Windows.UI.Xaml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!xaml) {
        const DWORD error = GetLastError();
        g_start_state.store(3, std::memory_order_release);
        Log(TE_LOG_ERROR, HResultText("LoadLibraryExW(Windows.UI.Xaml.dll)", HRESULT_FROM_WIN32(error)).c_str());
        FreeLibraryAndExitThread(module, error);
    }

    auto initialize = reinterpret_cast<decltype(&InitializeXamlDiagnosticsEx)>(
        GetProcAddress(xaml, "InitializeXamlDiagnosticsEx"));
    if (!initialize) {
        const DWORD error = GetLastError();
        g_start_state.store(3, std::memory_order_release);
        Log(TE_LOG_ERROR, HResultText("GetProcAddress(InitializeXamlDiagnosticsEx)", HRESULT_FROM_WIN32(error)).c_str());
        FreeLibrary(xaml);
        FreeLibraryAndExitThread(module, error);
    }

    HRESULT hr = E_FAIL;
    const DWORD pid = GetCurrentProcessId();
    {
        wchar_t process_path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, process_path, ARRAYSIZE(process_path));
        char message[640]{};
        sprintf_s(message, "Starting XAML Diagnostics targetPid=%lu process=%ls tap=%ls",
                  pid, process_path, tap_path);
        Log(TE_LOG_INFO, message);
    }
    for (unsigned int attempt = 1; attempt <= 60; ++attempt) {
        const unsigned long connection = g_endpoint_sequence.fetch_add(1, std::memory_order_relaxed);
        const std::wstring endpoint = L"VisualDiagConnection" + std::to_wstring(connection);
        std::thread initialize_thread([&] {
            hr = initialize(endpoint.c_str(), pid, nullptr, tap_path, CLSID_TaskbarEngineXamlTap, nullptr);
        });
        initialize_thread.join();
        if (SUCCEEDED(hr)) {
            char message[256]{};
            sprintf_s(message, "InitializeXamlDiagnosticsEx accepted endpoint=%lu pid=%lu attempt=%u; visual_pixels=unverified",
                      connection, pid, attempt);
            Log(TE_LOG_INFO, message);
            FreeLibrary(xaml);
            FreeLibraryAndExitThread(module, 0);
        }
        if (attempt == 1 || attempt % 10 == 0) {
            char message[384]{};
            sprintf_s(message, "InitializeXamlDiagnosticsEx attempt=%u endpoint=%ls failed HRESULT=0x%08lX",
                      attempt, endpoint.c_str(), static_cast<unsigned long>(hr));
            Log(TE_LOG_WARNING, message);
        }
        Sleep(500);
    }

    g_start_state.store(3, std::memory_order_release);
    Log(TE_LOG_ERROR, HResultText("InitializeXamlDiagnosticsEx exhausted 60 attempts", hr).c_str());
    FreeLibrary(xaml);
    FreeLibraryAndExitThread(module, static_cast<DWORD>(hr));
}

} // namespace

STDAPI DllGetClassObject(REFCLSID class_id, REFIID riid, LPVOID* object)
{
    if (!object) return E_POINTER;
    *object = nullptr;
    if (class_id != CLSID_TaskbarEngineXamlTap) return CLASS_E_CLASSNOTAVAILABLE;
    try {
        return winrt::make<TapFactory>().as(riid, object);
    } catch (winrt::hresult_error const& error) {
        return error.code();
    } catch (...) {
        return E_FAIL;
    }
}

extern "C" bool TE_XamlTaskbarIsSupported(void)
{
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto rtl_get_version = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    if (!rtl_get_version) return false;
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    return rtl_get_version(&version) == 0 && version.dwBuildNumber >= 22621;
}

extern "C" void TE_XamlTaskbarSetAppearance(const TE_TransparencySettings* settings)
{
    if (!settings) return;
    {
        std::lock_guard<std::mutex> lock(g_settings_mutex);
        g_desired_settings = *settings;
    }
    QueueApply();
}

extern "C" void TE_XamlTaskbarStart(void)
{
    if (!TE_XamlTaskbarIsSupported()) return;
    long expected = 0;
    if (!g_start_state.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) return;

    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&TE_XamlTaskbarStart), &module)) {
        g_start_state.store(3, std::memory_order_release);
        Log(TE_LOG_ERROR, HResultText("GetModuleHandleExW(pin TAP module)", HRESULT_FROM_WIN32(GetLastError())).c_str());
        return;
    }

    HANDLE thread = CreateThread(nullptr, 0, InitializeXamlDiagnostics, module, 0, nullptr);
    if (!thread) {
        const DWORD error = GetLastError();
        FreeLibrary(module);
        g_start_state.store(3, std::memory_order_release);
        Log(TE_LOG_ERROR, HResultText("CreateThread(XAML Diagnostics)", HRESULT_FROM_WIN32(error)).c_str());
        return;
    }
    CloseHandle(thread);
}

extern "C" bool TE_XamlTaskbarIsReady(void)
{
    return g_start_state.load(std::memory_order_acquire) == 2 &&
           g_taskbar_count.load(std::memory_order_acquire) > 0;
}
