#pragma once

#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.h>

namespace winrt::TaskbarEngine {

    /**
     * @brief Create a WinUI 3 settings page for a specific plugin or section.
     * @param plugin_name The name of the plugin or section tag.
     * @param schema_json The JSON schema for dynamic plugin settings.
     * @return The generated WinUI 3 Page object.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateSettingsPage(const std::string& plugin_name, const std::string& schema_json = "{}");

    /**
     * @brief Create dedicated page for Taskbar Resize & Geometry, including the Default Taskbar option.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateTaskbarResizePage();

    /**
     * @brief Create dedicated page for macOS Dock-style Icon Magnification.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateIconMagnificationPage();

    /**
     * @brief Create dedicated page for Notification Bouncing, 3D Tilt, and Drag-and-Drop Physics.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreatePhysicsSettingsPage();

    /**
     * @brief Create dedicated page for Custom Start Button image picker and configuration.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateStartButtonPage();

    /**
     * @brief Create dedicated page for Dynamic Island (Media Pill) acrylic capsule controls.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateDynamicIslandPage();
}
