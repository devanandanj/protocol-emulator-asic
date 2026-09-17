# Behavioural (cycle-accurate) model of the PEmu ISA.
#
# Two artifacts:
#   pemu_model       - static library, the ISA simulator
#   pemu_sim         - CLI that runs a program and dumps a trace
#   pemu_model_tests - unit tests (no external framework)

add_library(pemu_model
    src/isa.cpp
)
target_include_directories(pemu_model PUBLIC include)
target_compile_features(pemu_model PUBLIC cxx_std_17)

add_executable(pemu_sim src/main.cpp)
target_link_libraries(pemu_sim PRIVATE pemu_model)

add_executable(pemu_model_tests tests/test_isa.cpp)
target_link_libraries(pemu_model_tests PRIVATE pemu_model)
add_test(NAME pemu_model_tests COMMAND pemu_model_tests)
