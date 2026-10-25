cmake_minimum_required(VERSION 3.18)
project(standrise_mod C CXX)
set(CMAKE_CXX_STANDARD 17)

# third_party/imgui  and  third_party/and64 (And64InlineHook.cpp/.hpp) are fetched by the workflow
set(IMGUI ${CMAKE_SOURCE_DIR}/third_party/imgui)
set(HOOK  ${CMAKE_SOURCE_DIR}/third_party/and64)
include_directories(${IMGUI} ${IMGUI}/backends ${HOOK})

add_library(cheat SHARED
  main.cpp cheat.cpp touch.cpp ${HOOK}/And64InlineHook.cpp
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
  ${IMGUI}/backends/imgui_impl_opengl3.cpp)

target_compile_definitions(cheat PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_link_libraries(cheat EGL GLESv3 android log)

add_executable(injector injector.cpp)
target_link_libraries(injector dl)
