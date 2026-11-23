#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <windows.h>
#undef FindText
#undef FindTextW
#undef FindTextA
#include <winrt/Windows.UI.Text.h>
#include <microsoft.ui.xaml.window.h>
#undef GetCurrentTime
#include <string>
#include <vector>
#include <atomic>
#include <cJSON.h>
#include "settings_page.h"
#include "about_page.h"
#include "gui_ipc_client.h"

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>
#include <winrt/Microsoft.UI.Dispatching.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

#include <fstream>
#include <tlhelp32.h>
#include <thread>

static void LogGui(const std::string& msg);

static bool IsEngineRunning()
{
    if (GuiIpcIsConnected()) return true;
    if (FindWindowW(L"TaskbarEngineHost", NULL) != NULL) return true;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"TaskbarEngine.exe") == 0 ||
                    _wcsicmp(pe.szExeFile, L"TaskbarEngineHost.exe") == 0) {
                    CloseHandle(hSnap);
                    return true;
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return false;
}

static bool StartEngineProcess()
{
    wchar_t exe_path[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH)) return false;
    wchar_t* last_slash = wcsrchr(exe_path, L'\\');
    if (last_slash) *last_slash = L'\0';

    const wchar_t* candidates[] = {
        L"\\TaskbarEngine.exe",
        L"\\..\\TaskbarEngine.exe",
        L"\\..\\bin\\TaskbarEngine.exe",
        L"\\..\\..\\bin\\TaskbarEngine.exe"
    };

    std::wstring found_exe;
    for (const wchar_t* cand : candidates) {
        std::wstring full = std::wstring(exe_path) + cand;
        wchar_t canon[MAX_PATH] = { 0 };
        if (GetFullPathNameW(full.c_str(), MAX_PATH, canon, NULL)) {
            if (GetFileAttributesW(canon) != INVALID_FILE_ATTRIBUTES) {
                found_exe = canon;
                break;
            }
        }
    }

    if (found_exe.empty()) {
        found_exe = L"TaskbarEngine.exe";
    }

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    wchar_t work_dir[MAX_PATH] = { 0 };
    wcscpy_s(work_dir, MAX_PATH, found_exe.c_str());
    wchar_t* p = wcsrchr(work_dir, L'\\');
    if (p) *p = L'\0';

    BOOL ok = CreateProcessW(
        found_exe.c_str(),
        NULL,
        NULL,
        NULL,
        FALSE,
        0,
        NULL,
        work_dir[0] ? work_dir : NULL,
        &si,
        &pi
    );

    if (ok) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

static bool StopEngineProcess()
{
    // Try clean IPC shutdown first so plugins unload cleanly from explorer.exe
    if (GuiIpcIsConnected()) {
        GuiIpcShutdown();
    }

    HWND hHost = FindWindowW(L"TaskbarEngineHost", NULL);
    if (hHost) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hHost, &pid);
        PostMessageW(hHost, WM_CLOSE, 0, 0);
        if (pid) {
            HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
            if (hProc) {
                DWORD waitRes = WaitForSingleObject(hProc, 1500);
                if (waitRes == WAIT_OBJECT_0) {
                    CloseHandle(hProc);
                    return true;
                }
                TerminateProcess(hProc, 0);
                CloseHandle(hProc);
                return true;
            }
        }
    }

    bool killed = false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"TaskbarEngine.exe") == 0 ||
                    _wcsicmp(pe.szExeFile, L"TaskbarEngineHost.exe") == 0) {
                    HANDLE hProc = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        if (WaitForSingleObject(hProc, 500) != WAIT_OBJECT_0) {
                            TerminateProcess(hProc, 0);
                        }
                        CloseHandle(hProc);
                        killed = true;
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
    return killed;
}

// Returns the full path to TaskbarEngine.exe located next to the settings exe.
static std::wstring GetEngineExePath()
{
    wchar_t exe_path[MAX_PATH] = { 0 };
    if (!GetModuleFileNameW(NULL, exe_path, MAX_PATH)) return L"";
    wchar_t* last_slash = wcsrchr(exe_path, L'\\');
    if (!last_slash) return L"";
    *(last_slash + 1) = L'\0';
    return std::wstring(exe_path) + L"TaskbarEngine.exe";
}

static bool IsStartupEnabled()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t buf[MAX_PATH] = { 0 };
    DWORD size = sizeof(buf);
    DWORD type = 0;
    bool found = (RegQueryValueExW(hKey, L"TaskbarEngine", nullptr, &type,
                                   reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS
                  && type == REG_SZ);
    RegCloseKey(hKey);
    return found;
}

static bool SetStartupEnabled(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    bool ok = false;
    if (enable) {
        std::wstring path = GetEngineExePath();
        if (!path.empty()) {
            // Quote the path in case it contains spaces.
            std::wstring quoted = L"\"" + path + L"\"";
            ok = (RegSetValueExW(hKey, L"TaskbarEngine", 0, REG_SZ,
                                 reinterpret_cast<const BYTE*>(quoted.c_str()),
                                 static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)))
                  == ERROR_SUCCESS);
        }
    } else {
        LSTATUS st = RegDeleteValueW(hKey, L"TaskbarEngine");
        ok = (st == ERROR_SUCCESS || st == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(hKey);
    return ok;
}

static bool RestartExplorerProcess()
{
    LogGui("RestartExplorerProcess invoked");
    // Terminate existing explorer.exe processes
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"explorer.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
                        CloseHandle(hProc);
                    }
                }
            } while (Process32NextW(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }

    Sleep(600);

    // Launch explorer.exe cleanly in the interactive shell
    wchar_t winDir[MAX_PATH];
    if (GetWindowsDirectoryW(winDir, MAX_PATH)) {
        std::wstring expPath = std::wstring(winDir) + L"\\explorer.exe";

        SHELLEXECUTEINFOW sei = { sizeof(sei) };
        sei.fMask = SEE_MASK_DOENVSUBST | SEE_MASK_FLAG_NO_UI;
        sei.lpFile = expPath.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&sei)) {
            LogGui("explorer.exe restarted via ShellExecuteExW");
            return true;
        }

        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };
        if (CreateProcessW(expPath.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            LogGui("explorer.exe restarted via CreateProcessW");
            return true;
        }
    }
    return false;
}

static void LogGui(const std::string& msg)
{
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
        wchar_t* last = wcsrchr(path, L'\\');
        if (last) {
            *(last + 1) = L'\0';
            wcscat_s(path, MAX_PATH, L"te_settings.log");
            std::ofstream ofs(path, std::ios::app);
            if (ofs.is_open()) {
                SYSTEMTIME st;
                GetLocalTime(&st);
                char buf[64];
                snprintf(buf, sizeof(buf), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
                ofs << buf << msg << std::endl;
            }
        }
    }
}

namespace winrt::TaskbarEngine {

struct App : ApplicationT<App, winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
{
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider m_provider;

    App()
    {
        LogGui("App() constructor entered");
        UnhandledException([this](winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e) {
            LogGui(std::string("UnhandledException: ") + to_string(e.Message()));
            MessageBoxW(NULL, e.Message().c_str(), L"TaskbarEngine Unhandled XAML Exception", MB_ICONERROR);
            e.Handled(true);
        });
    }

    winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type)
    {
        return m_provider.GetXamlType(type);
    }

    winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::hstring const& fullName)
    {
        return m_provider.GetXamlType(fullName);
    }

    winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions()
    {
        return m_provider.GetXmlnsDefinitions();
    }

    Window m_window{ nullptr };

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
        LogGui("OnLaunched entered");
        winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources resources;
        Resources().MergedDictionaries().Append(resources);
        
        m_window = Window();
        m_window.Title(L"TaskbarEngine Settings");

        NavigationView nav;
        nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Auto);
        nav.IsSettingsVisible(false);
        nav.RequestedTheme(ElementTheme::Dark);
        nav.PaneTitle(L"TaskbarEngine");
        nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        
        Frame contentFrame;
        try {
            auto pageBackground = resources.Lookup(box_value(L"ApplicationPageBackgroundThemeBrush"))
                .as<winrt::Microsoft::UI::Xaml::Media::Brush>();
            nav.Background(pageBackground);
            contentFrame.Background(pageBackground);
        } catch (...) {}
        
        NavigationViewItemHeader taskbarGroup;
        taskbarGroup.Content(box_value(L"TASKBAR"));
        nav.MenuItems().Append(taskbarGroup);

        NavigationViewItem resizeItem;
        resizeItem.Content(box_value(L"Taskbar size"));
        resizeItem.Icon(SymbolIcon(Symbol::DockBottom));
        resizeItem.Tag(box_value(L"taskbar_resize"));
        nav.MenuItems().Append(resizeItem);

        NavigationViewItemHeader interactionGroup;
        interactionGroup.Content(box_value(L"INTERACTIONS"));
        nav.MenuItems().Append(interactionGroup);

        NavigationViewItem hoverItem;
        hoverItem.Content(box_value(L"Icon magnification"));
        hoverItem.Icon(SymbolIcon(Symbol::Zoom));
        hoverItem.Tag(box_value(L"icon_magnification"));
        nav.MenuItems().Append(hoverItem);

        NavigationViewItem physicsItem;
        physicsItem.Content(box_value(L"Motion & gestures"));
        physicsItem.Icon(SymbolIcon(Symbol::Play));
        physicsItem.Tag(box_value(L"icon_physics"));
        nav.MenuItems().Append(physicsItem);

        NavigationViewItem startItem;
        startItem.Content(box_value(L"Start button"));
        startItem.Icon(SymbolIcon(Symbol::Home));
        startItem.Tag(box_value(L"start_button"));
        nav.MenuItems().Append(startItem);

        NavigationViewItem islandItem;
        islandItem.Content(box_value(L"Media pill"));
        islandItem.Icon(SymbolIcon(Symbol::MusicInfo));
        islandItem.Tag(box_value(L"dynamic_island"));
        nav.MenuItems().Append(islandItem);

        // These are first-party pages and must remain discoverable even when the
        // engine IPC endpoint is unavailable during GUI startup.
        NavigationViewItemHeader appearanceGroup;
        appearanceGroup.Content(box_value(L"APPEARANCE"));
        nav.MenuItems().Append(appearanceGroup);

        NavigationViewItem transparencyItem;
        transparencyItem.Content(box_value(L"Frosted glass"));
        transparencyItem.Icon(SymbolIcon(Symbol::Setting));
        transparencyItem.Tag(box_value(L"taskbar_transparency"));
        nav.MenuItems().Append(transparencyItem);

        NavigationViewItem widgetsItem;
        widgetsItem.Content(box_value(L"Widgets"));
        widgetsItem.Icon(SymbolIcon(Symbol::AllApps));
        widgetsItem.Tag(box_value(L"widget_host"));
        nav.MenuItems().Append(widgetsItem);

        NavigationViewItem aboutItem;
        aboutItem.Content(box_value(L"About"));
        aboutItem.Icon(SymbolIcon(Symbol::Help));
        aboutItem.Tag(box_value(L"About"));
        nav.FooterMenuItems().Append(aboutItem);
        
        auto schemaRequest = std::make_shared<std::atomic<uint64_t>>(0);
        nav.SelectionChanged([contentFrame, nav, schemaRequest](NavigationView const&, NavigationViewSelectionChangedEventArgs const& args) {
            if (!args.SelectedItem()) return;
            auto item = args.SelectedItem().try_as<NavigationViewItem>();
            if (!item || !item.Tag()) return;
            auto tag = unbox_value<hstring>(item.Tag());
            
            if (tag == L"About") {
                contentFrame.Content(CreateAboutPage());
            } else if (tag == L"taskbar_resize") {
                contentFrame.Content(CreateTaskbarResizePage());
            } else if (tag == L"icon_magnification" || tag == L"icon_hover") {
                contentFrame.Content(CreateIconMagnificationPage());
            } else if (tag == L"icon_physics") {
                contentFrame.Content(CreatePhysicsSettingsPage());
            } else if (tag == L"start_button") {
                contentFrame.Content(CreateStartButtonPage());
            } else if (tag == L"dynamic_island") {
                contentFrame.Content(CreateDynamicIslandPage());
            } else if (tag == L"taskbar_transparency") {
                contentFrame.Content(CreateSettingsPage("taskbar_transparency"));
            } else {
                std::string pluginName = to_string(tag);
                const uint64_t requestId = ++(*schemaRequest);
                TextBlock loading;
                loading.Text(L"Loading settings...");
                loading.Margin(Thickness{ 24, 24, 24, 24 });
                contentFrame.Content(loading);

                auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
                std::thread([dispatcher, contentFrame, nav, schemaRequest, requestId, pluginName] {
                    std::string schemaStr = "{}";
                    auto settings = GuiIpcGetSettings();
                    if (settings) {
                        cJSON* root = cJSON_Parse(settings->c_str());
                        cJSON* plugins = root ? cJSON_GetObjectItem(root, "plugins") : nullptr;
                        if (plugins && cJSON_IsArray(plugins)) {
                            cJSON* plugin = nullptr;
                            cJSON_ArrayForEach(plugin, plugins) {
                                cJSON* name = cJSON_GetObjectItem(plugin, "name");
                                if (cJSON_IsString(name) && name->valuestring && pluginName == name->valuestring) {
                                    char* printed = cJSON_PrintUnformatted(plugin);
                                    if (printed) { schemaStr = printed; cJSON_free(printed); }
                                    break;
                                }
                            }
                        }
                        if (root) cJSON_Delete(root);
                    }
                    if (dispatcher) dispatcher.TryEnqueue([contentFrame, nav, schemaRequest, requestId, pluginName, schemaStr = std::move(schemaStr)] {
                        if (*schemaRequest != requestId) return;
                        auto selected = nav.SelectedItem().try_as<NavigationViewItem>();
                        if (!selected || !selected.Tag() || to_string(unbox_value<hstring>(selected.Tag())) != pluginName) return;
                        contentFrame.Content(CreateSettingsPage(pluginName, schemaStr));
                    });
                }).detach();
            }
        });

        // Discover optional plugin pages in the background so a slow engine
        // handshake never delays first window presentation.
        auto startupDispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        std::thread([startupDispatcher, nav] {
            std::vector<std::string> names;
            auto settings = GuiIpcGetSettings();
            if (settings) {
                cJSON* root = cJSON_Parse(settings->c_str());
                cJSON* plugins = root ? cJSON_GetObjectItem(root, "plugins") : nullptr;
                if (plugins && cJSON_IsArray(plugins)) {
                    cJSON* plugin = nullptr;
                    cJSON_ArrayForEach(plugin, plugins) {
                        cJSON* name = cJSON_GetObjectItem(plugin, "name");
                        if (!cJSON_IsString(name) || !name->valuestring) continue;
                        std::string value = name->valuestring;
                        if (value != "icon_hover" && value != "taskbar_resize" &&
                            value != "icon_magnification" && value != "icon_physics" &&
                            value != "start_button" && value != "dynamic_island" &&
                            value != "widget_host" && value != "taskbar_transparency") names.push_back(value);
                    }
                }
                if (root) cJSON_Delete(root);
            }
            if (startupDispatcher) startupDispatcher.TryEnqueue([nav, names = std::move(names)] {
                for (const auto& name : names) {
                    bool exists = false;
                    for (const auto& existing : nav.MenuItems()) {
                        auto item = existing.try_as<NavigationViewItem>();
                        if (item && item.Tag() && to_string(unbox_value<hstring>(item.Tag())) == name) { exists = true; break; }
                    }
                    if (exists) continue;
                    NavigationViewItem item;
                    item.Content(box_value(to_hstring(name)));
                    item.Icon(SymbolIcon(Symbol::Setting));
                    item.Tag(box_value(to_hstring(name)));
                    nav.MenuItems().Append(item);
                }
            });
        }).detach();
        
        // Engine Status and Control Header
        StackPanel headerPanel;
        headerPanel.Orientation(Orientation::Horizontal);
        headerPanel.Spacing(10);
        headerPanel.Padding(Thickness{ 24, 12, 28, 12 });
        headerPanel.VerticalAlignment(VerticalAlignment::Center);

        TextBlock statusIndicator;
        statusIndicator.VerticalAlignment(VerticalAlignment::Center);
        statusIndicator.FontSize(14.0);
        statusIndicator.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        statusIndicator.Margin(Thickness{ 0, 0, 0, 0 });

        TextBlock statusLabel;
        statusLabel.Text(L"ENGINE STATUS");
        statusLabel.FontSize(10.0);
        statusLabel.Opacity(0.72);
        statusLabel.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        StackPanel engineStatus;
        engineStatus.Spacing(3);
        engineStatus.VerticalAlignment(VerticalAlignment::Center);
        engineStatus.Margin(Thickness{ 0, 0, 8, 0 });
        engineStatus.Children().Append(statusLabel);
        engineStatus.Children().Append(statusIndicator);

        Button startStopBtn;
        startStopBtn.Padding(Thickness{ 12, 6, 12, 6 });

        Button restartBtn;
        restartBtn.Content(box_value(L"Restart engine"));
        restartBtn.Padding(Thickness{ 12, 6, 12, 6 });

        Button restartExplorerBtn;
        restartExplorerBtn.Content(box_value(L"Restart Explorer"));
        restartExplorerBtn.Padding(Thickness{ 12, 6, 12, 6 });

        auto applyEngineStatusUI = [statusIndicator, startStopBtn, restartBtn, restartExplorerBtn](bool running) {
            if (running) {
                statusIndicator.Text(L"\u25CF Engine running");
                startStopBtn.Content(box_value(L"Stop Engine"));
                restartBtn.Visibility(Visibility::Visible);
                restartExplorerBtn.Visibility(Visibility::Collapsed);
            } else {
                statusIndicator.Text(L"\u25CB Engine stopped");
                startStopBtn.Content(box_value(L"Start Engine"));
                restartBtn.Visibility(Visibility::Collapsed);
                restartExplorerBtn.Visibility(Visibility::Visible);
            }
        };

        auto statusDispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        auto statusProbePending = std::make_shared<std::atomic_bool>(false);
        auto updateEngineStatusUI = [statusDispatcher, statusProbePending, applyEngineStatusUI]() {
            if (statusProbePending->exchange(true)) return;
            std::thread([statusDispatcher, statusProbePending, applyEngineStatusUI] {
                const bool running = IsEngineRunning();
                statusProbePending->store(false);
                if (statusDispatcher) statusDispatcher.TryEnqueue([applyEngineStatusUI, running] { applyEngineStatusUI(running); });
            }).detach();
        };

        updateEngineStatusUI();

        startStopBtn.Click([updateEngineStatusUI](IInspectable const&, RoutedEventArgs const&) {
            auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            std::thread([dispatcher, updateEngineStatusUI]() {
                if (IsEngineRunning()) {
                    StopEngineProcess();
                } else {
                    StartEngineProcess();
                }
                Sleep(250);
                dispatcher.TryEnqueue([updateEngineStatusUI]() { updateEngineStatusUI(); });
            }).detach();
        });

        restartBtn.Click([updateEngineStatusUI](IInspectable const&, RoutedEventArgs const&) {
            auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            std::thread([dispatcher, updateEngineStatusUI]() {
                StopEngineProcess();
                Sleep(400);
                StartEngineProcess();
                Sleep(250);
                dispatcher.TryEnqueue([updateEngineStatusUI]() { updateEngineStatusUI(); });
            }).detach();
        });

        restartExplorerBtn.Click([statusIndicator](IInspectable const&, RoutedEventArgs const&) {
            statusIndicator.Text(L"Restarting Explorer...");
            auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
            std::thread([dispatcher, statusIndicator]() {
                RestartExplorerProcess();
                Sleep(500);
                dispatcher.TryEnqueue([statusIndicator]() { 
                    statusIndicator.Text(L"Engine stopped · taskbar restarted");
                });
            }).detach();
        });

        auto engineTimer = std::make_shared<DispatcherTimer>();
        engineTimer->Interval(std::chrono::seconds(1));
        engineTimer->Tick([updateEngineStatusUI, engineTimer](IInspectable const&, IInspectable const&) {
            updateEngineStatusUI();
        });
        engineTimer->Start();

        headerPanel.Children().Append(engineStatus);
        headerPanel.Children().Append(startStopBtn);
        headerPanel.Children().Append(restartBtn);
        headerPanel.Children().Append(restartExplorerBtn);

        // Separator
        TextBlock sep;
        sep.Text(L"\u2022");
        sep.Opacity(0.45);
        sep.VerticalAlignment(VerticalAlignment::Center);
        sep.Margin(Thickness{ 4, 0, 4, 0 });
        headerPanel.Children().Append(sep);

        // Run at startup toggle
        ToggleSwitch startupToggle;
        startupToggle.Header(box_value(L"Run at startup"));
        startupToggle.OffContent(box_value(L"Off"));
        startupToggle.OnContent(box_value(L"On"));
        startupToggle.IsOn(IsStartupEnabled());
        startupToggle.VerticalAlignment(VerticalAlignment::Center);
        startupToggle.MinWidth(150);
        startupToggle.Toggled([](IInspectable const& sender, RoutedEventArgs const&) {
            SetStartupEnabled(sender.as<ToggleSwitch>().IsOn());
        });
        headerPanel.Children().Append(startupToggle);

        Border engineStatusCard;
        engineStatusCard.CornerRadius(CornerRadius{ 12, 12, 12, 12 });
        engineStatusCard.BorderThickness(Thickness{ 1, 1, 1, 1 });
        engineStatusCard.Padding(Thickness{ 12, 8, 16, 8 });
        engineStatusCard.Child(headerPanel);
        try {
            auto statusResources = Application::Current().Resources();
            if (statusResources.HasKey(box_value(L"CardBackgroundFillColorSecondaryBrush")))
                engineStatusCard.Background(statusResources.Lookup(box_value(L"CardBackgroundFillColorSecondaryBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
            else if (statusResources.HasKey(box_value(L"CardBackgroundFillColorDefaultBrush")))
                engineStatusCard.Background(statusResources.Lookup(box_value(L"CardBackgroundFillColorDefaultBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
            if (statusResources.HasKey(box_value(L"CardStrokeColorDefaultBrush")))
                engineStatusCard.BorderBrush(statusResources.Lookup(box_value(L"CardStrokeColorDefaultBrush")).as<winrt::Microsoft::UI::Xaml::Media::Brush>());
        } catch (...) {}
        winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(engineStatusCard, L"Engine controls and startup setting");
        nav.Header(engineStatusCard);
        
        nav.Content(contentFrame);
        nav.SelectedItem(resizeItem);
        contentFrame.Content(CreateTaskbarResizePage());
        
        m_window.Content(nav);
        LogGui("Activating window...");
        m_window.Activate();
        LogGui("m_window.Activate() completed");

        HWND hwnd = nullptr;
        try {
            auto windowNative = m_window.as<IWindowNative>();
            if (windowNative) {
                windowNative->get_WindowHandle(&hwnd);
                char buf[128];
                snprintf(buf, sizeof(buf), "HWND retrieved: 0x%p", (void*)hwnd);
                LogGui(buf);
                if (hwnd) {
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    UpdateWindow(hwnd);
                    LogGui("Explicit ShowWindow(SW_RESTORE) called");
                }
            }
        } catch (...) {
            LogGui("Failed to get HWND via IWindowNative");
        }
    }
};

}

#include <DispatcherQueue.h>

static LONG WINAPI CrashHandler(PEXCEPTION_POINTERS pExcept)
{
    DWORD code = pExcept->ExceptionRecord->ExceptionCode;
    if (code == 0x40010006) { // OutputDebugStringA
        size_t len = pExcept->ExceptionRecord->ExceptionInformation[0];
        const char* str = (const char*)pExcept->ExceptionRecord->ExceptionInformation[1];
        if (str && len > 0) {
            std::string s(str, len > 1024 ? 1024 : len);
            LogGui("DEBUG_STRING: " + s);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code == 0x4001000A) { // OutputDebugStringW
        const wchar_t* wstr = (const wchar_t*)pExcept->ExceptionRecord->ExceptionInformation[1];
        if (wstr) {
            std::wstring ws(wstr);
            LogGui("DEBUG_STRING_W: " + to_string(ws));
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code == 0x406D1388 || code == 0x000006BA || code == 0x40080201) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char buf[256];
    snprintf(buf, sizeof(buf), "EXCEPTION DETECTED: Code=0x%08X at Address=0x%p", code, pExcept->ExceptionRecord->ExceptionAddress);
    LogGui(buf);
    return EXCEPTION_CONTINUE_SEARCH;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    // WinUI and the C++/WinRT projection use exceptions for recoverable control
    // flow. A vectored handler runs on first chance and logs every caught throw,
    // which floods the settings log and obscures real crashes. Log only failures
    // that reach the process-level unhandled exception filter.
    SetUnhandledExceptionFilter(CrashHandler);

    /* Attach to the interactive user desktop if started from background / sandbox */
    HDESK hDesk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!hDesk) {
        hDesk = OpenDesktopW(L"Default", 0, FALSE, GENERIC_ALL);
    }
    if (hDesk) {
        SetThreadDesktop(hDesk);
    }

    LogGui("wWinMain started");
    try {
        init_apartment(winrt::apartment_type::single_threaded);
        LogGui("init_apartment completed");

        DispatcherQueueOptions options = {
            sizeof(DispatcherQueueOptions),
            DQTYPE_THREAD_CURRENT,
            DQTAT_COM_NONE
        };
        winrt::com_ptr<ABI::Windows::System::IDispatcherQueueController> controller;
        HRESULT hrDq = CreateDispatcherQueueController(options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller)));
        char buf[128];
        snprintf(buf, sizeof(buf), "CreateDispatcherQueueController result: 0x%08X", (unsigned int)hrDq);
        LogGui(buf);

        LogGui("Calling Application::Start...");
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            LogGui("Application::Start callback: creating App...");
            winrt::make<winrt::TaskbarEngine::App>();
            LogGui("Application::Start callback: App created");
        });
        LogGui("Application::Start returned");
    } catch (winrt::hresult_error const& e) {
        std::string err = "winrt::hresult_error: " + to_string(e.message()) + " (0x" + std::to_string(e.code().value) + ")";
        LogGui(err);
        MessageBoxW(NULL, e.message().c_str(), L"TaskbarEngine Error", MB_ICONERROR);
    } catch (std::exception const& e) {
        std::string err = std::string("std::exception: ") + e.what();
        LogGui(err);
        MessageBoxA(NULL, e.what(), "TaskbarEngine Error", MB_ICONERROR);
    } catch (...) {
        LogGui("Unknown exception in wWinMain");
        MessageBoxW(NULL, L"Unknown WinUI 3 error occurred.", L"TaskbarEngine Error", MB_ICONERROR);
    }

    LogGui("wWinMain exiting");
    return 0;
}
