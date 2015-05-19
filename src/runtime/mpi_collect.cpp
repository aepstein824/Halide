#include "runtime_internal.h"
#include "HalideRuntime.h"

extern "C" void ashprint(int *);

WEAK int32_t halide_aaron_print() {
    int mutated = 0;
    ashprint(&mutated);
    if(mutated) {
        halide_print(0, "lol the game\n");
    } else {
        halide_print(0, "rekt\n");
    }

    return 0;
}
