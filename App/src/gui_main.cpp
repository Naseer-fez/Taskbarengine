#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <windows.h>
#include <MddBootstrap.h>
#include <WindowsAppSDK-VersionInfo.h>
#include <string>
#include <cJSON.h>
#include "settings_page.h"
#include "about_page.h"
#include "gui_ipc_client.h"

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;

namespace winrt::TaskbarEngine {

struct App : ApplicationT<App>
{
    Window m_window{ nullptr };

    void OnLaunched(LaunchActivatedEventArgs const&)
    {
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
        
        nav.SelectionChanged([this, contentFrame](NavigationView const& sender, NavigationViewSelectionChangedEventArgs const& args) {
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

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    init_apartment(winrt::apartment_type::single_threaded);

    HRESULT hr = MddBootstrapInitialize(
        WINDOWSAPPSDK_RELEASE_MAJORMINOR,
        WINDOWSAPPSDK_RELEASE_VERSION_SHORTTAG_W,
        PACKAGE_VERSION{ 0 }
    );
    
    if (FAILED(hr)) {
        MessageBoxW(NULL, L"Failed to initialize Windows App SDK. Please install the Windows App SDK runtime.", L"TaskbarEngine", MB_ICONERROR);
        return 1;
    }

    try {
        winrt::Microsoft::UI::Xaml::Application::Start([](auto&&) {
            winrt::make<winrt::TaskbarEngine::App>();
        });
    } catch (winrt::hresult_error const& e) {
        MessageBoxW(NULL, e.message().c_str(), L"TaskbarEngine Error", MB_ICONERROR);
    } catch (...) {
        MessageBoxW(NULL, L"Unknown WinUI 3 error occurred.", L"TaskbarEngine Error", MB_ICONERROR);
    }

    MddBootstrapShutdown();
    return 0;
}
