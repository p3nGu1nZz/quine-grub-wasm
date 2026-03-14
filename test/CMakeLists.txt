# Test targets for WASM Quine Bootloader

# Use Catch2 for unit testing (setup.sh should download & install it)
find_package(Catch2 3.0.0 REQUIRED)

# Simple tests that link against the 'core' library
add_executable(test_state test_state.cpp)
target_include_directories(test_state PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
)
target_link_libraries(test_state PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME state_test COMMAND test_state)

# basic base64 encode/decode checks
add_executable(test_base64 test_base64.cpp)
target_include_directories(test_base64 PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_base64 PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME base64_test COMMAND test_base64)

# wasm parser & kernel tests
add_executable(test_wasm test_wasm.cpp)
target_include_directories(test_wasm PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_wasm PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME wasm_test COMMAND test_wasm)

# additional tests
add_executable(test_util_dpi test_util_dpi.cpp)
# Include ImGui implementation sources so that Gui methods (which call ImGui)
# have their dependencies satisfied without needing the full bootloader.
target_sources(test_util_dpi PRIVATE
    ${IMGUI_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/gui/window.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/panels.cpp
    ${CMAKE_SOURCE_DIR}/src/gui/heatmap.cpp
)
target_include_directories(test_util_dpi PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${IMGUI_DIR}
)
target_link_libraries(test_util_dpi PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME util_dpi_test COMMAND test_util_dpi)

# CLI parsing tests
add_executable(test_cli test_cli.cpp)
target_include_directories(test_cli PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_cli PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME cli_test COMMAND test_cli)

# Export report formatting tests
add_executable(test_export test_export.cpp)
target_include_directories(test_export PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_export PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME export_test COMMAND test_export)

# App logic tests
add_executable(test_app_logic test_app.cpp)
target_include_directories(test_app_logic PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_app_logic PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME app_logic_test COMMAND test_app_logic)

# CLI and behavior tests
add_executable(test_cli_behavior test_cli_behavior.cpp)
target_include_directories(test_cli_behavior PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_cli_behavior PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME cli_behavior_test COMMAND test_cli_behavior)

# Evolution strategy tests
add_executable(test_evolution test_evolution.cpp)
target_include_directories(test_evolution PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_evolution PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME evolution_test COMMAND test_evolution)

# Telemetry/advisor tests
add_executable(test_advisor test_advisor.cpp)
target_include_directories(test_advisor PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_advisor PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME advisor_test COMMAND test_advisor)

# Feature extractor tests
add_executable(test_feature test_feature.cpp)
target_include_directories(test_feature PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_feature PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME feature_test COMMAND test_feature)

# Policy network tests
add_executable(test_policy test_policy.cpp)
target_include_directories(test_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_policy PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME policy_test COMMAND test_policy)

# Loss function tests
add_executable(test_loss test_loss.cpp)
target_include_directories(test_loss PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_loss PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME loss_test COMMAND test_loss)

# Trainer tests
add_executable(test_train test_train.cpp)
target_include_directories(test_train PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_train PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME train_test COMMAND test_train)

# Training-phase / GUI scene logic tests
add_executable(test_training_phase test_training_phase.cpp)
target_include_directories(test_training_phase PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_training_phase PRIVATE
    core
    Catch2::Catch2WithMain
)
add_test(NAME training_phase_test COMMAND test_training_phase)
