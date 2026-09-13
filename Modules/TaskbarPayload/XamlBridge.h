#pragma once

#include <windows.h>
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>

#include <string>
#include <vector>

namespace TaskbarPayload {

    class XamlBridge {
    public:
        XamlBridge();
        ~XamlBridge();

        bool Initialize();
        void ApplyMacOsDockScaling();
        bool CanaryPreFlightCheck();
        void Disable();
        bool IsDisabled() const { return m_isDisabled; }

    private:
        HWND FindTaskbarXamlWindow();
        winrt::Windows::UI::Xaml::UIElement GetRootElement(HWND xamlWindow);
        void TraverseVisualTreeAndApplyScaling(winrt::Windows::UI::Xaml::DependencyObject const& node);
        void ApplyScalingToElement(winrt::Windows::UI::Xaml::UIElement const& element);

        HWND m_xamlWindow = nullptr;
        winrt::Windows::UI::Xaml::UIElement m_rootElement = nullptr;
        bool m_isDisabled = false;
    };

}
