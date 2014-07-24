/**
 * \file tea.c
 * \brief Reasonably lightwieght floating point calculator for embedding
 *
 * Built for a need of doing small generic function out on embedded devices.
 * 
 * Core of this was taken from TinyBasicPlus and modified from there.
 *
 * This is MIT licensed.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tea.h"

#ifndef AVR
#define PROGMEM
#define pgm_read_byte(x) *(x)
#else /*AVR*/
#include <avr/pgmspace.h>
#endif /*AVR*/

#define HAS_RING 1

static char *txtpos; // initialize with command buffer.
static float vars[TEA_VARS_COUNT];

#ifdef HAS_RING
#include <float.h>
static struct ring_s {
    char size;
    char idx;
    float r[10];
} ring;
#endif /*HAS_RING*/

static unsigned char func_tab[] PROGMEM = {
    'a','b','s'+0x80,
    'a','c','o','s'+0x80,
    'a','s','i','n'+0x80,
    'a','t','a','n'+0x80,
    'c','e','i','l'+0x80,
    'c','o','s'+0x80,
    'c','o','s','h'+0x80,
    'e','x','p'+0x80,
    'f','l','o','o','r'+0x80,
    'l','o','g'+0x80,
    'l','o','g','1','0'+0x80,
    'r','o','u','n','d'+0x80,
    's','i','n'+0x80,
    's','i','n','h'+0x80,
    's','q','r','t'+0x80,
    't','a','n'+0x80,
    't','a','n','h'+0x80,
    'a','t','a','n','2'+0x80,
    'h','y','p','o','t'+0x80,
    'p','o','w'+0x80,
#ifdef HAS_RING
    'r','i','n','g'+0x80,
    'i','n','s','e','r','t'+0x80,
    'm','a','x'+0x80,
    'm','i','n'+0x80,
    's','u','m'+0x80,
#endif /*HAS_RING*/

    0
};
enum {
    FN_abs=0,
    FN_acos,
    FN_asin,
    FN_atan,
    FN_ceil,
    FN_cos,
    FN_cosh,
    FN_exp,
    FN_floor,
    FN_log,
    FN_log10,
    FN_round,
    FN_sin,
    FN_sinh,
    FN_sqrt,
    FN_tan,
    FN_tanh,
    FN_atan2,
    FN_hypot,
    FN_pow,
#ifdef HAS_RING
    FN_ring,
    FN_insert,
    FN_max,
    FN_min,
    FN_sum,
#endif /*HAS_RING*/
    FN_UNKNOWN,
};

float expr8(void);

/***************************************************************************/
void ignore_blanks(void)
{
    while(*txtpos == ' ' || *txtpos == '\t') {
        txtpos++;
    }
}

/***************************************************************************/
int scantable(unsigned char *table)
{
	int i = 0;
	int table_index = 0;
	while(1)
	{
		// Run out of table entries?
		if(pgm_read_byte(table) == 0) {
            return table_index;
        }

		// Do we match this character?
		if(txtpos[i] == pgm_read_byte(table)) {
			i++;
			table++;
		} else {
			// do we match the last character of keyword (with 0x80 added)? If so, return
			if(txtpos[i]+0x80 == pgm_read_byte(table)) {
				txtpos += i+1;  // Advance the pointer to following the keyword
                ignore_blanks();
				return table_index;
			}

			// Forward to the end of this keyword
			while((pgm_read_byte(table) & 0x80) == 0) {
				table++;
            }

			// Now move on to the first character of the next word, and reset the position index
			table++;
			table_index++;
            ignore_blanks();
			i = 0;
		}
	}
}

/***************************************************************************/
int getVariableIndex(void)
{
    if(*txtpos >= 'A' && *txtpos < ('A' + TEA_VARS_COUNT)) {
        return (*txtpos++ - 'A');
    }
    return -1; // Not a variable
}

/***************************************************************************/
float doFunction(int fidx)
{
    float a,b;
    if(*txtpos != '(') {
        return NAN;
    }
    txtpos++;

    switch(fidx) {
        case FN_abs:
            a = expr8();
            a = fabsf(a);
            break;
        case FN_acos:
            a = expr8();
            a = acosf(a);
            break;
        case FN_asin:
            a = expr8();
            a = asinf(a);
            break;
        case FN_atan:
            a = expr8();
            a = atanf(a);
            break;
        case FN_ceil:
            a = expr8();
            a = ceilf(a);
            break;
        case FN_cos:
            a = expr8();
            a = cosf(a);
            break;
        case FN_cosh:
            a = expr8();
            a = coshf(a);
            break;
        case FN_exp:
            a = expr8();
            a = expf(a);
            break;
        case FN_floor:
            a = expr8();
            a = floorf(a);
            break;
        case FN_log:
            a = expr8();
            a = logf(a);
            break;
        case FN_log10:
            a = expr8();
            a = log10f(a);
            break;
        case FN_round:
            a = expr8();
            a = roundf(a);
            break;
        case FN_sin:
            a = expr8();
            a = sinf(a);
            break;
        case FN_sinh:
            a = expr8();
            a = sinhf(a);
            break;
        case FN_sqrt:
            a = expr8();
            a = sqrtf(a);
            break;
        case FN_tan:
            a = expr8();
            a = tanf(a);
            break;
        case FN_tanh:
            a = expr8();
            a = tanhf(a);
            break;

        case FN_atan2:
            a = expr8();
            if(*txtpos != ',') return NAN;
            txtpos++;
            b = expr8();
            a = atan2f(a, b);
            break;
        case FN_hypot:
            a = expr8();
            if(*txtpos != ',') return NAN;
            txtpos++;
            b = expr8();
            a = hypotf(a, b);
            break;
        case FN_pow:
            a = expr8();
            if(*txtpos != ',') return NAN;
            txtpos++;
            b = expr8();
            a = powf(a, b);
            break;

#ifdef HAS_RING
        case FN_ring:
            a = expr8();
            ring.size = a;
            ring.idx = 0;
            break;
        case FN_insert:
            a = expr8();
            ring.r[ring.idx++] = a;
            if (ring.idx >= ring.size) {
                ring.idx = 0;
            }
            break;
        case FN_max:
            a = -DBL_MAX;
            for(int i=0; i < ring.size; i++) {
                if(ring.r[i] > a) {
                    a = ring.r[i];
                }
            }
            break;
        case FN_min:
            a = DBL_MAX;
            for(int i=0; i < ring.size; i++) {
                if(ring.r[i] < a) {
                    a = ring.r[i];
                }
            }
            break;
        case FN_sum:
            a = 0;
            for(int i=0; i < ring.size; i++) {
                a += ring.r[i];
            }
            break;
#endif /*HAS_RING*/

        default:
            a = NAN;
            break;
    }
    if(*txtpos != ')') {
        return NAN;
    }
    txtpos++;
    return a;
}

/***************************************************************************/
float expr1(void)
{
    float a;
    ignore_blanks();

	if(*txtpos == '-') {
        txtpos++;
        return -expr1();
    }

    // Load numbers
    {
        char *found=NULL;
        a = strtof(txtpos, &found);
        if(a == 0 && found == txtpos) {
            // No number found.
        } else {
            txtpos = found;
            return a;
        }
    }

    // Variables are single UPPERCASE alphas.
    if(*txtpos >= 'A' && *txtpos < ('A' + TEA_VARS_COUNT)) {
        a = vars[*txtpos - 'A'];
        txtpos++;
        return a;
    }

    // Functions are multiple lowercase alphas.
	if(*txtpos >= 'a' && *txtpos <= 'z')
	{
		// Is it a function?
		int tidx = scantable(func_tab);
		if(tidx != FN_UNKNOWN) {
            return doFunction(tidx);
        }
        return NAN;
	}

	if(*txtpos == '(') {
		txtpos++;
		a = expr8();
		if(*txtpos != ')') {
            return NAN;
        }

		txtpos++;
		return a;
	}

    return NAN;
}

/***************************************************************************/
float expr2(void)
{
	float a,b;

	a = expr1();
    ignore_blanks();
	while(1) {
		if(*txtpos == '^') {
			txtpos++;
			b = expr1();
            a = powf(a, b);
		} else {
			return a;
        }
	}
}

/***************************************************************************/
float expr3(void)
{
	float a,b;

	a = expr2();
    ignore_blanks();
	while(1) {
		if(*txtpos == '*') {
			txtpos++;
			b = expr2();
			a *= b;
		} else if(*txtpos == '/') {
			txtpos++;
			b = expr2();
			if(b != 0) {
				a /= b;
            } else {
				a = NAN;
            }
        } else if(*txtpos == '%') {
            txtpos++;
			b = expr2();
            a = fmodf(a,b);
		} else {
			return a;
        }
	}
}

/***************************************************************************/
float expr4(void)
{
	float a,b;

	if(*txtpos == '-' || *txtpos == '+') {
		a = 0;
    } else {
		a = expr3();
        ignore_blanks();
    }

	while(1) {
		if(*txtpos == '-') {
			txtpos++;
			b = expr3();
			a -= b;
		} else if(*txtpos == '+') {
			txtpos++;
			b = expr3();
			a += b;
		} else {
			return a;
        }
	}
}

/***************************************************************************/
float expr5(void)
{
    float a,b;

	a = expr4();
    ignore_blanks();
	while(1) {
		if(txtpos[0] == '&' && txtpos[1] == '&') {
			txtpos+=2;
            b = expr4();
			a = (a && b);
		} else if(txtpos[0] == '|' && txtpos[1] == '|') {
			txtpos+=2;
            b = expr4();
            a = (a || b);
		} else {
			return a;
        }
	}
}

/***************************************************************************/
float expr6(void)
{
    float a,b;

	a = expr5();
    ignore_blanks();
	while(1) {
		if(txtpos[0] == '<' && txtpos[1] == '=') {
			txtpos+=2;
            b = expr5();
            a = (a <= b);
		} else if(txtpos[0] == '>' && txtpos[1] == '=') {
			txtpos+=2;
            b = expr5();
            a = (a >= b);
		} else if(txtpos[0] == '!' && txtpos[1] == '=') {
			txtpos+=2;
            b = expr5();
            a = (a != b);
		} else if(txtpos[0] == '=' && txtpos[1] == '=') {
			txtpos++;
            b = expr5();
            a = (a == b);
		} else if(txtpos[0] == '<') {
			txtpos++;
            b = expr5();
            a = (a < b);
		} else if(txtpos[0] == '>') {
			txtpos++;
            b = expr5();
            a = (a > b);
		} else {
			return a;
        }
	}
}

/***************************************************************************/
float expr7(void)
{
    // A = expr
    int var;
    float a;
    char *bounce;

    ignore_blanks();
    bounce = txtpos;
    var = getVariableIndex();
    if( var < 0) {
        return expr6();
    }
    ignore_blanks();
    if(*txtpos != '=') {
        txtpos = bounce;
        return expr6();
    }
    txtpos++;
    a = expr6();
    vars[var] = a;

    return a;
}

/***************************************************************************/
float expr8(void)
{
    return expr7();
}

/***************************************************************************/
float tea_calc(char *command, float constants[TEA_VARS_COUNT])
{
    txtpos = command;

    if(constants != NULL) {
        memcpy(vars, constants, sizeof(vars));
    } else {
        memset(vars, 0, sizeof(vars));
    }

    return expr8();
}


/* vim: set ai cin et sw=4 ts=4 : */
