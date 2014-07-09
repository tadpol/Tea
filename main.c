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

    assert(0.0 == tea_calc("0", constants));

    assert(42.0 == tea_calc("42", constants));

    assert(11.0 == tea_calc("6+5", constants));

    assert(11.0 == tea_calc(" 6 + 5 ", constants));


    puts("All tests passed.");
    return 0;
}
