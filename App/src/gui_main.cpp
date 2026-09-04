#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <windows.h>
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
        
        Frame contentFrame;
        
        NavigationViewItem aboutItem;
        aboutItem.Content(box_value(L"About"));
        aboutItem.Icon(SymbolIcon(Symbol::Help));
        aboutItem.Tag(box_value(L"About"));
        nav.MenuItems().Append(aboutItem);

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
                            NavigationViewItem item;
                            item.Content(box_value(to_hstring(nameStr)));
                            item.Icon(SymbolIcon(Symbol::Setting));
                            item.Tag(box_value(to_hstring(nameStr)));
                            nav.MenuItems().Append(item);
                        }
                    }
                }
                cJSON_Delete(root);
            }
        }
        
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
        
        nav.Content(contentFrame);
        nav.SelectedItem(aboutItem);
        
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
