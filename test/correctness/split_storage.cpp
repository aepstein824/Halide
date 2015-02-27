#include <stdio.h>
#include <Halide.h>
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {

    Var x("x"), y("y");
    Var xi("xi"), yi("yi");
    Var xo("xo"), yo("yo");
    Target target = get_jit_target_from_environment();
    Image<int> img;


    Func f("f"), g("g");
    printf("Defining easy case...\n");
    f(x, y) = max(x, y);
    g(x, y) = f(x, y) + 10;
    f.compute_root();
    f.split_storage(x, xo, xi, 2);
    f.split_storage(y, yo, yi, 8);
    f.reorder_storage(xi, yi, xo, yo);

    printf("Realizing easy case ...\n");
    img = g.realize(32, 32, target);

    printf("Checking Results...\n");
    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (img(i, j) != 10 + (i < j ? j : i)) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return -1;
            }
        }
    }

    Func f2("f2"), g2("g2");
    printf("Defining non multiple divisions...\n");
    f2(x, y) = max(x, y);
    g2(x, y) = f2(x, y) + 10;
    f2.compute_root();
    f2.split_storage(x, xo, xi, 3);
    f2.split_storage(y, yo, yi, 7);
    f2.reorder_storage(xi, yi, xo, yo);

    printf("Realizing ...\n");
    img = g2.realize(32, 32, target);

    printf("Checking Results...\n");
    // Check the result was what we expected
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 32; j++) {
            if (img(i, j) != 10 + (i < j ? j : i)) {
                printf("img[%d, %d] = %d\n", i, j, img(i, j));
                return -1;
            }
        }
    } 

    Func f3("f3"), g3("g3");
    printf("Defining bounds required!=computed!=allocated...\n");
    f3(x) = x;
    g3(x) = 2*f3(x);
    f3.compute_root().vectorize(x, 4);
    f3.split_storage(x, xo, xi, 2);

    printf("Realizing ...\n");
    img = g3.realize(3);

    printf("Checking Results...\n");
    // Check the result was what we expected
    for (int i = 0; i < 3; i++) {
        if (img(i) != 2 * i) {
            printf("img[%d] = %d\n", i, img(i));
            return -1;
        }
    } 



    printf("Success!\n");
    return 0;
}
