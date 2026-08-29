#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <windows.h>
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

namespace winrt::TaskbarEngine {

struct App : ApplicationT<App, winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
{
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider m_provider;

    App()
    {
        UnhandledException([this](winrt::Windows::Foundation::IInspectable const&, winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& e) {
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
        m_window.Activate();
    }
};

}

#include <DispatcherQueue.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try {
        init_apartment(winrt::apartment_type::single_threaded);

        DispatcherQueueOptions options = {
            sizeof(DispatcherQueueOptions),
            DQTYPE_THREAD_CURRENT,
            DQTAT_COM_NONE
        };
        winrt::com_ptr<ABI::Windows::System::IDispatcherQueueController> controller;
        CreateDispatcherQueueController(options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller)));

        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            winrt::make<winrt::TaskbarEngine::App>();
        });
    } catch (winrt::hresult_error const& e) {
        MessageBoxW(NULL, e.message().c_str(), L"TaskbarEngine Error", MB_ICONERROR);
    } catch (...) {
        MessageBoxW(NULL, L"Unknown WinUI 3 error occurred.", L"TaskbarEngine Error", MB_ICONERROR);
    }

    return 0;
}
