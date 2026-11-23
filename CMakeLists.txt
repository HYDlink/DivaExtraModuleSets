cmake_minimum_required(VERSION 3.24)
project(DivaExtraModuleSets VERSION 0.1.2 LANGUAGES CXX ASM_MASM)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(
  $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
  $<$<COMPILE_LANGUAGE:CXX>:/W4>
  $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

set(DETOURS_DIR third_party/vendor/detours-source/src)
add_library(detours STATIC
  ${DETOURS_DIR}/detours.cpp
  ${DETOURS_DIR}/modules.cpp
  ${DETOURS_DIR}/disasm.cpp
  ${DETOURS_DIR}/image.cpp
  ${DETOURS_DIR}/creatwth.cpp
  ${DETOURS_DIR}/disolx86.cpp
  ${DETOURS_DIR}/disolx64.cpp
  ${DETOURS_DIR}/disolia64.cpp
  ${DETOURS_DIR}/disolarm.cpp
  ${DETOURS_DIR}/disolarm64.cpp)
target_include_directories(detours PUBLIC ${DETOURS_DIR})
set_property(TARGET detours PROPERTY CXX_STANDARD 14)
target_compile_options(detours PRIVATE /permissive)

add_executable(I18nGen tools/i18n_gen.cpp)
target_include_directories(I18nGen PRIVATE third_party/vendor/toml)

file(GLOB I18N_LOCALES CONFIGURE_DEPENDS locales/*.toml)
set(I18N_OUT ${CMAKE_CURRENT_BINARY_DIR}/generated)
add_custom_command(OUTPUT ${I18N_OUT}/i18n_keys.h ${I18N_OUT}/i18n_data.inc
  COMMAND I18nGen ${CMAKE_CURRENT_SOURCE_DIR}/locales ${I18N_OUT}
  DEPENDS I18nGen ${I18N_LOCALES})
add_custom_target(i18n_generated DEPENDS ${I18N_OUT}/i18n_keys.h ${I18N_OUT}/i18n_data.inc)

add_library(extra_slots_core STATIC
  src/i18n.cpp
  src/settings.cpp
  src/slot_store.cpp
  src/native_hooks.cpp
  src/native_thunks.asm)
add_dependencies(extra_slots_core i18n_generated)
target_include_directories(extra_slots_core PUBLIC src ${I18N_OUT} PRIVATE third_party/vendor/toml)
target_link_libraries(extra_slots_core PUBLIC detours)

add_library(extra_slots_ui STATIC
  src/ui.cpp
  src/ui_theme.cpp
  src/host.cpp
  src/ui_color_output.cpp
  src/ui_color_profile.cpp
  third_party/vendor/imgui/imgui.cpp
  third_party/vendor/imgui/imgui_draw.cpp
  third_party/vendor/imgui/imgui_tables.cpp
  third_party/vendor/imgui/imgui_widgets.cpp
  third_party/vendor/imgui/backends/imgui_impl_win32.cpp
  third_party/vendor/imgui/backends/imgui_impl_dx11.cpp)
target_include_directories(extra_slots_ui PUBLIC src third_party/vendor/imgui third_party/vendor/imgui/backends)
target_link_libraries(extra_slots_ui PUBLIC extra_slots_core d3d11 dxgi d3dcompiler imm32 dwmapi)

add_library(DivaExtraModuleSets SHARED src/mod.cpp)
target_link_libraries(DivaExtraModuleSets PRIVATE extra_slots_ui)

add_executable(ExtraSlotsPreview WIN32 src/preview.cpp)
target_link_libraries(ExtraSlotsPreview PRIVATE extra_slots_ui windowscodecs shell32 ole32)

enable_testing()

add_executable(SlotStoreTests tests/slot_store_tests.cpp)
target_link_libraries(SlotStoreTests PRIVATE extra_slots_core)
add_test(NAME 槽位存储与非破坏隐藏验收 COMMAND SlotStoreTests)

add_executable(I18nCheck tests/i18n_check.cpp)
target_link_libraries(I18nCheck PRIVATE extra_slots_core)
add_test(NAME 多语言运行时验收 COMMAND I18nCheck)

add_test(NAME 文案硬编码检查 COMMAND pwsh -NoProfile -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/check-i18n.ps1")

add_executable(DllCheck tests/dll_check.cpp)
target_link_libraries(DllCheck PRIVATE d3d11 dxgi)
add_test(NAME DML导出与DX11宿主验收 COMMAND DllCheck "$<TARGET_FILE:DivaExtraModuleSets>")

add_executable(ThunkRegressionTests tests/thunk_regression_tests.cpp tests/thunk_test_helper.asm)
target_link_libraries(ThunkRegressionTests PRIVATE extra_slots_core)
add_test(NAME LTCG寄存器保护与重入防死锁验收 COMMAND ThunkRegressionTests)

add_executable(FontAtlasTests tests/font_atlas_tests.cpp)
target_link_libraries(FontAtlasTests PRIVATE extra_slots_ui)
add_test(NAME 字体图集尺寸与DX11纹理验收 COMMAND FontAtlasTests)
