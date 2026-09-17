# PEmu assembler.
#
# Turns human-readable .pemu text into the hex-per-line format
# consumed by both pemu_sim and the RTL program-loader.

add_library(pemu_asm src/asm.cpp)
target_include_directories(pemu_asm PUBLIC include)
target_compile_features(pemu_asm PUBLIC cxx_std_17)
# Uses pemu::kNumRegs from the model's ISA header.
target_link_libraries(pemu_asm PUBLIC pemu_model)

add_executable(pemuasm src/main.cpp)
target_link_libraries(pemuasm PRIVATE pemu_asm)

add_executable(pemu_asm_tests tests/test_asm.cpp)
target_link_libraries(pemu_asm_tests PRIVATE pemu_asm)
add_test(NAME pemu_asm_tests COMMAND pemu_asm_tests)
