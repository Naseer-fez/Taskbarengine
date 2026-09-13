#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <windows.h>
#undef FindText
#undef FindTextW
#undef FindTextA
#include <winrt/Windows.UI.Text.h>
#include <microsoft.ui.xaml.window.h>
#undef GetCurrentTime
#include <string>
#include <cJSON.h>
#include "settings_page.h"
#include "about_page.h"
#include "gui_ipc_client.h"

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

#include <fstream>
#include <tlhelp32.h>

static void LogGui(const std::string& msg);

static bool IsEngineRunning()
{
    if (GuiIpcIsConnected()) return true;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"TaskbarEngine.exe") == 0) {
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
        Sleep(150); // allow short grace period for plugins to unload
    }

    bool killed = false;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe = { sizeof(pe) };
        if (Process32FirstW(hSnap, &pe)) {
            do {
                if (_wcsicmp(pe.szExeFile, L"TaskbarEngine.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 0);
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
        nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        nav.IsSettingsVisible(false);
        
        Frame contentFrame;
        
        NavigationViewItem hoverItem;
        hoverItem.Content(box_value(L"Icon Hover & Physics"));
        hoverItem.Icon(SymbolIcon(Symbol::Zoom));
        hoverItem.Tag(box_value(L"icon_hover"));
        nav.MenuItems().Append(hoverItem);

        NavigationViewItem resizeItem;
        resizeItem.Content(box_value(L"Taskbar Resize"));
        resizeItem.Icon(SymbolIcon(Symbol::DockBottom));
        resizeItem.Tag(box_value(L"taskbar_resize"));
        nav.MenuItems().Append(resizeItem);

        auto schemaOpt = GuiIpcGetSettings();
        if (schemaOpt.has_value()) {
            cJSON* root = cJSON_Parse(schemaOpt.value().c_str());
            if (root) {
                cJSON* plugins = cJSON_GetObjectItem(root, "plugins");
                if (plugins && cJSON_IsArray(plugins)) {
                    cJSON* plugin = nullptr;
                    cJSON_ArrayForEach(plugin, plugins) {
                        cJSON* nameNode = cJSON_GetObjectItem(plugin, "name");
                        if (nameNode && cJSON_IsString(nameNode) && nameNode->valuestring) {
                            std::string nameStr = nameNode->valuestring;
                            if (nameStr != "icon_hover" && nameStr != "taskbar_resize") {
                                NavigationViewItem item;
                                item.Content(box_value(to_hstring(nameStr)));
                                item.Icon(SymbolIcon(Symbol::Setting));
                                item.Tag(box_value(to_hstring(nameStr)));
                                nav.MenuItems().Append(item);
                            }
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }

        NavigationViewItem aboutItem;
        aboutItem.Content(box_value(L"About"));
        aboutItem.Icon(SymbolIcon(Symbol::Help));
        aboutItem.Tag(box_value(L"About"));
        nav.FooterMenuItems().Append(aboutItem);
        
        nav.SelectionChanged([contentFrame](NavigationView const&, NavigationViewSelectionChangedEventArgs const& args) {
            auto item = args.SelectedItem().as<NavigationViewItem>();
            auto tag = unbox_value<hstring>(item.Tag());
            
            if (tag == L"About") {
                contentFrame.Content(CreateAboutPage());
            } else {
                std::string pluginName = to_string(tag);
                auto currentSchemaOpt = GuiIpcGetSettings();
                std::string schemaStr = "{}";
                if (currentSchemaOpt.has_value()) {
                    cJSON* root = cJSON_Parse(currentSchemaOpt.value().c_str());
                    if (root) {
                        cJSON* plugins = cJSON_GetObjectItem(root, "plugins");
                        if (plugins && cJSON_IsArray(plugins)) {
                            cJSON* plugin = nullptr;
                            cJSON_ArrayForEach(plugin, plugins) {
                                cJSON* nameNode = cJSON_GetObjectItem(plugin, "name");
                                if (nameNode && cJSON_IsString(nameNode) && nameNode->valuestring && pluginName == nameNode->valuestring) {
                                    char* print_str = cJSON_PrintUnformatted(plugin);
                                    if (print_str) {
                                        schemaStr = print_str;
                                        cJSON_free(print_str);
                                    }
                                    break;
                                }
                            }
                        }
                        cJSON_Delete(root);
                    }
                }
                contentFrame.Content(CreateSettingsPage(pluginName, schemaStr));
            }
        });
        
        // Engine Status and Control Header
        StackPanel headerPanel;
        headerPanel.Orientation(Orientation::Horizontal);
        headerPanel.Spacing(16);
        headerPanel.Padding(Thickness{ 0, 8, 24, 8 });

        TextBlock statusIndicator;
        statusIndicator.VerticalAlignment(VerticalAlignment::Center);
        statusIndicator.FontSize(14.0);
        statusIndicator.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

        Button startStopBtn;
        startStopBtn.Padding(Thickness{ 16, 6, 16, 6 });

        Button restartBtn;
        restartBtn.Content(box_value(L"Restart Engine"));
        restartBtn.Padding(Thickness{ 16, 6, 16, 6 });

        Button restartExplorerBtn;
        restartExplorerBtn.Content(box_value(L"Restart Explorer"));
        restartExplorerBtn.Padding(Thickness{ 16, 6, 16, 6 });

        auto updateEngineStatusUI = [statusIndicator, startStopBtn, restartBtn, restartExplorerBtn]() {
            bool running = IsEngineRunning();
            if (running) {
                statusIndicator.Text(L"● Engine: Running");
                startStopBtn.Content(box_value(L"Stop Engine"));
                restartBtn.Visibility(Visibility::Visible);
                restartExplorerBtn.Visibility(Visibility::Collapsed);
            } else {
                statusIndicator.Text(L"○ Engine: Stopped");
                startStopBtn.Content(box_value(L"Start Engine"));
                restartBtn.Visibility(Visibility::Collapsed);
                restartExplorerBtn.Visibility(Visibility::Visible);
            }
        };

        updateEngineStatusUI();

        startStopBtn.Click([updateEngineStatusUI](IInspectable const&, RoutedEventArgs const&) {
            if (IsEngineRunning()) {
                StopEngineProcess();
            } else {
                StartEngineProcess();
            }
            Sleep(250);
            updateEngineStatusUI();
        });

        restartBtn.Click([updateEngineStatusUI](IInspectable const&, RoutedEventArgs const&) {
            StopEngineProcess();
            Sleep(400);
            StartEngineProcess();
            Sleep(250);
            updateEngineStatusUI();
        });

        restartExplorerBtn.Click([statusIndicator](IInspectable const&, RoutedEventArgs const&) {
            statusIndicator.Text(L"Restarting Explorer...");
            RestartExplorerProcess();
            Sleep(500);
            statusIndicator.Text(L"○ Engine: Stopped (Explorer Restarted)");
        });

        auto engineTimer = std::make_shared<DispatcherTimer>();
        engineTimer->Interval(std::chrono::seconds(1));
        engineTimer->Tick([updateEngineStatusUI, engineTimer](IInspectable const&, IInspectable const&) {
            updateEngineStatusUI();
        });
        engineTimer->Start();

        headerPanel.Children().Append(statusIndicator);
        headerPanel.Children().Append(startStopBtn);
        headerPanel.Children().Append(restartBtn);
        headerPanel.Children().Append(restartExplorerBtn);

        nav.Header(headerPanel);
        
        nav.Content(contentFrame);
        nav.SelectedItem(hoverItem);
        contentFrame.Content(CreateSettingsPage("icon_hover", "{}"));
        
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
    AddVectoredExceptionHandler(1, CrashHandler);

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
