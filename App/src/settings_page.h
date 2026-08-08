#pragma once

#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::TaskbarEngine {

    /**
     * @brief Create a WinUI 3 settings page for a specific plugin.
     * @param plugin_name The name of the plugin.
     * @param schema_json The JSON schema for the plugin's settings.
     * @return The generated WinUI 3 Page object.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateSettingsPage(const std::string& plugin_name, const std::string& schema_json);

}
