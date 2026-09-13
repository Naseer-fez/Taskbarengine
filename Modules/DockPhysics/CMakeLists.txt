add_library(DockPhysics STATIC DockPhysics.cpp)
target_include_directories(DockPhysics PRIVATE ${CMAKE_SOURCE_DIR}/SDK/include)
target_link_libraries(DockPhysics PRIVATE windowsapp)

