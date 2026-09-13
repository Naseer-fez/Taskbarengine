project(TaskbarPayload LANGUAGES CXX)

# We need C++17 at least, C++/WinRT requires /await or C++20 for some features, but C++17 is often enough.
add_library(TaskbarPayload SHARED XamlBridge.cpp XamlBridge.h)
target_link_libraries(TaskbarPayload PRIVATE windowsapp)
