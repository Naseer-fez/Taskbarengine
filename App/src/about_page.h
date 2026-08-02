#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace winrt::TaskbarEngine {

    /**
     * @brief Create the WinUI 3 About page.
     * @return The generated WinUI 3 Page object containing about info and perf stats.
     */
    winrt::Microsoft::UI::Xaml::Controls::Page CreateAboutPage();

}
