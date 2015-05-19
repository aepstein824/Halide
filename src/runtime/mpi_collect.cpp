#include "runtime_internal.h"
#include "HalideRuntime.h"


WEAK extern "C" int32_t halide_aaron_print() {
    halide_print(0, "lol the game\n");
    return 0;
}
