/**
 * \file main.c
 * \brief Test cases
 *
 */
#include <stdio.h>
#include <assert.h>
#include "tea.h"

int main(int argc, char **argv)
{
    float constants[TEA_VARS_COUNT];
    float res, expected;
    int i;
    for(i=0; i < TEA_VARS_COUNT; i++) {
        constants[i] = 0.0;
    }

    assert(0.0f == tea_calc("0", NULL));
    assert(0.0f == tea_calc("0.0", NULL));
    assert(42.0f == tea_calc("42", NULL));
    assert(-42.0f == tea_calc("-42", NULL));
    assert(42.0f == tea_calc("--42", NULL));
    assert(42.0f == tea_calc("----42", NULL));
    assert(0.1256f == tea_calc("0.1256", NULL));
    assert(isnan(tea_calc("nan", NULL)));
    assert(isnan(tea_calc("NAN", NULL)));
    assert(isinf(tea_calc("infinity", NULL)));
    assert(isinf(tea_calc("inf", NULL)));
    assert(isinf(tea_calc("INF", NULL)));

    assert(11.0f == tea_calc("6+5", NULL));
    assert(11.0f == tea_calc(" 6 + 5 ", NULL));

    assert(5.0f == tea_calc("14 - 9", NULL));

    assert(12.0f == tea_calc("24/2", NULL));

    assert(36.0f == tea_calc("9*4", NULL));

    assert(4.0f == tea_calc("14%10", NULL));

    assert(81.0f == tea_calc("9^2", NULL));

    assert(21.0f == tea_calc("7 * 4 - 7", NULL));
    assert(27.0f == tea_calc("45 - 6 * 3", NULL));
    assert(27.0f == tea_calc("45 - (6 * 3)", NULL));
    assert(117.0f == tea_calc("(45 - 6) * 3", NULL));

    constants[0] = 200.0f;
    constants[1] = 55.0f;
    assert(200.0f == tea_calc("A", constants));
    assert(255.0f == tea_calc("A + B", constants));
    assert(isnan(tea_calc("Z", NULL)));

    assert(42.0 == tea_calc("abs(-42)", NULL));
    assert(42.0 == tea_calc(" abs( -42 ) ", NULL));
    assert(81.0f == tea_calc("pow(9,2)", NULL));
    assert(81.0f == tea_calc(" pow ( 9 , 2 ) ", NULL));

    puts("All tests passed.");
    return 0;
}
/* vim: set ai cin et sw=4 ts=4 : */
