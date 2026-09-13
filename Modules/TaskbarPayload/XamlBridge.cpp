#include "XamlBridge.h"
#undef GetCurrentTime
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.Foundation.Numerics.h>

using namespace winrt;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Hosting;
using namespace Windows::UI::Composition;
using namespace Windows::Foundation::Numerics;

namespace TaskbarPayload {

    XamlBridge::XamlBridge() {}
    XamlBridge::~XamlBridge() {}

    void XamlBridge::Disable() {
        m_isDisabled = true;
    }

    bool XamlBridge::CanaryPreFlightCheck() {
        if (!m_rootElement) return false;
        try {
            int count = VisualTreeHelper::GetChildrenCount(m_rootElement);
            (void)count;
            return true;
        } catch (winrt::hresult_error const&) {
            return false;
        } catch (...) {
            return false;
        }
    }

    bool XamlBridge::Initialize() {
        m_xamlWindow = FindTaskbarXamlWindow();
        if (!m_xamlWindow) return false;

        m_rootElement = GetRootElement(m_xamlWindow);
        if (!m_rootElement) return false;

        if (!CanaryPreFlightCheck()) {
            Disable();
            return false;
        }

        return true;
    }

    HWND XamlBridge::FindTaskbarXamlWindow() {
        HWND taskbarWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (!taskbarWnd) return nullptr;

        HWND xamlWindow = nullptr;
        EnumChildWindows(taskbarWnd, [](HWND hwnd, LPARAM lParam) -> BOOL {
            wchar_t className[256];
            if (GetClassNameW(hwnd, className, 256)) {
                if (wcscmp(className, L"Windows.UI.Composition.DesktopWindowContentBridge") == 0) {
                    *reinterpret_cast<HWND*>(lParam) = hwnd;
                    return FALSE; // Stop enumeration
                }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&xamlWindow));

        return xamlWindow;
    }

    UIElement XamlBridge::GetRootElement(HWND /*xamlWindow*/) {
        // To be implemented: Intercept or query IDesktopWindowXamlSourceNative
        // Note: Real implementation would likely require hooking DesktopWindowXamlSource creation 
        // to capture the instance or using specific COM interfaces.
        return nullptr;
    }

    namespace {
        struct IRunnable { virtual void Run() = 0; };
#ifdef _MSC_VER
        void ExecuteWithSEH(IRunnable* runnable, XamlBridge* bridge) {
            __try {
                runnable->Run();
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                if (bridge) bridge->Disable();
            }
        }
#else
        void ExecuteWithSEH(IRunnable* runnable, XamlBridge* bridge) {
            try {
                runnable->Run();
            } catch (...) {
                if (bridge) bridge->Disable();
            }
        }
#endif
    }

    void XamlBridge::ApplyMacOsDockScaling() {
        if (m_isDisabled || !m_rootElement) return;

        struct Runnable : IRunnable {
            XamlBridge* b;
            void Run() override {
                try {
                    b->TraverseVisualTreeAndApplyScaling(b->m_rootElement);
                } catch (winrt::hresult_error const&) {
                    b->Disable();
                } catch (...) {
                    b->Disable();
                }
            }
        } runnable;
        runnable.b = this;

        ExecuteWithSEH(&runnable, this);
    }

    void XamlBridge::TraverseVisualTreeAndApplyScaling(DependencyObject const& node) {
        if (!node || m_isDisabled) return;

        try {
            hstring className = get_class_name(node);
            std::wstring wsClassName(className.c_str());
            
            if (wsClassName.find(L"TaskbarItem") != std::wstring::npos || 
                wsClassName.find(L"IconView") != std::wstring::npos) {
                
                if (auto element = node.try_as<UIElement>()) {
                    ApplyScalingToElement(element);
                }
            }

            int childCount = VisualTreeHelper::GetChildrenCount(node);
            for (int i = 0; i < childCount; ++i) {
                auto child = VisualTreeHelper::GetChild(node, i);
                TraverseVisualTreeAndApplyScaling(child);
            }
        } catch (winrt::hresult_error const&) {
            Disable();
        } catch (...) {
            Disable();
        }
    }

    void XamlBridge::ApplyScalingToElement(UIElement const& element) {
        try {
            Visual visual = ElementCompositionPreview::GetElementVisual(element);
            if (visual) {
                float actualWidth = 0.0f;
                float actualHeight = 0.0f;
                
                if (auto fwElement = element.try_as<FrameworkElement>()) {
                    actualWidth = static_cast<float>(fwElement.ActualWidth());
                    actualHeight = static_cast<float>(fwElement.ActualHeight());
                }

                visual.CenterPoint(float3(actualWidth / 2.0f, actualHeight, 0.0f));
            }
        } catch (winrt::hresult_error const&) {
            Disable();
        } catch (...) {
            Disable();
        }
    }
}
