/**
 * \file tea.c
 *
 * A tiny interface language.
 * Can be used directly, but does assume that there is a frontend more then
 * just a terminal running on the other side.
 *
 * A very thin balancing act to be both just a protocol, but also something
 * that can be typed out.
 *
 * Primarily driven as something I use on little systems and attached
 * to someone else's shell type environment.
 *
 * ---------
 *
 * Memory access byte order is whatever the CPU is.
 *
 * The stack is static, so it remains between calls.
 *
 * Always returns the top of the stack. (without popping it.)
 *
 * There is no error checking; it makes things faster and smaller.
 * You need to be smart.
 * - There is no checking to see if you push or pop past the bounds of the
 *   stack.
 * - Digits are read in without limit checks.  Numbers will slilently
 *   overflow if you are not checking.
 * - Memory read/writes are not checked for alignment.
 * - If the stack goes too deep, it runs into unknown land.
 *
 *
 *
 * Copyright (c) 2012-2013 Michael Conrad Tadpol Tilstra 
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <string.h>
#ifdef USE_STDOUT
#include <stdio.h>
#define tea_printf printf
#define tea_putc(c) fputc((c), stdout)
#else
#define tea_printf(...) do{}while(0)
#define tea_putc(...) do{}while(0)
#endif

#define TEA_INTERNAL_USAGE
#include "tea.h"


/*****************************************************************************/
/**
 * How big is the stack
 */
teaint tea_stack[tea_stack_depth];
teaint *tea_SP = tea_stack;

teabyte tea_token[32];
teabyte tea_token_idx=0;


/*****************************************************************************/
/**
 * \breif Push a number onto the stack
 */
void tea_push(teaint t)
{
    *(++tea_SP) = t;
}
/**
 * \breif Pop a number off of the stack
 */
teaint tea_pop(void)
{
    return *tea_SP--;
}

/*****************************************************************************/

#if 0 /* or printf? or what? not all have printf.... */
void tea_out(teaint v, teabyte rdx, teabyte tail)
{
    teabyte buf[32]; //??? or use token?
    teabyte *b;
    _ltoa(v, buf, rdx);

    for(b=buf, *b!= '\0'; b++) {
        tea_putc(*b);
    }
    if(tail != '\0') {
        tea_putc(tail);
    }
}
#endif

/*****************************************************************************/
/**
 * \brief Get a token by being pushed
 * \param[in] t A byte to process as part of an incoming token.
 *
 * \retval 0 Byte processed, middle of token.
 * \retval 1 Byte processes, end of token.
 */
teaint tea_get_token(teabyte t)
{
    tea_token[tea_token_idx++] = t;

    if(t == ' ' || t == '\r' || t == '\n' || t == '\t' || t == '\0') {
        tea_token[tea_token_idx-1] = '\0';
        tea_token_idx = 0;
        return 1;
    }
    if(tea_token_idx >= (sizeof(tea_token))) {
        tea_token[tea_token_idx-1] = '\0';
        tea_token_idx = 0;
        return 1;
    }
    return 0;
}


/**
 * \brief Operate on a token
 *
 * \return The value that is at the top of the stack.
 */
teaint tea_do_token(void)
{
    /* Try to do as much work in registers instead of always doing pop and
     * push.  All but a few commands work with three or less items from the
     * stack, so this really cuts down on code size.
     */
    register teaint a;
    register teaint b;
    register teaint c;
    int adjust; // Needs to be signed, so not teaint
    teaint pushback;
    teabyte *cmd = tea_token;

    /* Commands have the following stack description:
     * ( items before -- items after )
     * <-- top of stack is left side for both before and after.
     *
     * This is a flip from how it appears in the command string.
     * FE:  the command "10 5" is ( 5 10 )
     * so "10 5 +" is ( 5 10 -- 15 )
     */

    /* Fetch the top three of the stack, and set the default adjust and
     * pushback values.
     */
    a = *tea_SP;
    b = *(tea_SP-1);
    c = *(tea_SP-2);
    adjust = -1;
    pushback = 1;

    /* Now do the command */
    if( *cmd >= '0' && *cmd <= '9' ) {
        teabyte base;
        /* push number ( -- value ) */
        if( *(cmd+1) == 'b' ) {
            base = 2;
            cmd+=2;
        } else
        if( *(cmd+1) == 'o' ) {
            base = 8;
            cmd+=2;
        } else
        if( *(cmd+1) == 'x' ) {
            base = 16;
            cmd+=2;
        } else
        if( *(cmd+1) == 'z' ) {
            base = 36;
            cmd+=2;
        }
        a = 0;
        for(; *cmd != '\0'; cmd++ ) {
            b = *cmd;
            if( b >= '0' && b <= '9' ) b -= '0';
            else if( b >= 'a' && b <= 'z') b -= 'a' + 10;
            else if( b >= 'A' && b <= 'Z') b -= 'A' + 10;
            else break;
            if( b >= base ) break;
            a *= base;
            a += b;
        }
        cmd--;
        adjust=1;
    } else

    if( *cmd == 's' ) {
        if( *(cmd+1) == 'n' ) { /*  nswap ( n a ... b -- b ... a ) */
            cmd++;
            /* top is index into stack, pop, then swap top and nth */
            a++; // add one because we haven't dropped the index yet.
            c = *(tea_SP-a);
            *(tea_SP-a) = b;
            a = c;
        } else { /*  swap ( a b -- b a ) */
            c = b;
            b = a;
            a = c;
            adjust = 0;
            pushback = 2;
        }
    } else
    if( *cmd == 'v' ) {
        if( *(cmd+1) == 'n' ) { /*  ndup ( n a ... b -- b a ... b ) */
            cmd++;
            a++;
            a = *(tea_SP - a);
            adjust = 0;
        } else { /*  dup ( a -- a a ) */
            adjust = 1;
        }
    } else
    if( *cmd == 'x' ) { /*  drop ( a -- ) */
        pushback = 0;
    } else

    if( *cmd == '+' ) { /*  add ( a b -- b+a ) */
        a = b + a;
    } else
    if( *cmd == '-' ) { /*  sub ( a b -- b-a ) */
        a = b - a;
    } else
    if( *cmd == '*' ) { /*  multiply ( a b -- b*a ) */
        a = b * a;
    } else
    if( *cmd == '/' ) { /*  divide ( a b -- b/a ) */
        a = b / a;
    } else
    if( *cmd == '%' ) { /*  modulo ( a b -- b%a ) */
        a = b % a;
    } else

    if( *cmd == '|' ) {// Bit OR ( a b -- b|a )
        a = b | a;
    } else
    if( *cmd == '^' ) { /*  Bit XOR ( a b -- b^a ) */
        a = b ^ a;
    } else
    if( *cmd == '&' ) { /*  Bit AND ( a b -- b&a ) */
        a = b & a;
    } else
    if( *cmd == '~' ) { /*  Bit invert ( a -- ~a ) */
        a = ~a;
        adjust = 0;
    } else

    if( *cmd == '=' ) { /*  Test equal to ( a b -- a==b ) */
        a = a == b;
    } else

    if( *cmd == '>' ) {
        cmd++;
        if( *cmd == '>' ) { /*  Bit shift right ( a b -- b>>a ) */
            a = b >> a;
        } else
        if( *cmd == '=' ) { /*  Test Greater than equalto ( a b -- b>=a ) */
            a = b >= a;
        } else
        { /*  Test Greater than ( a b -- b>a ) */
            a = b > a;
            cmd--;
        }
    } else
    if( *cmd == '<' ) {
        cmd++;
        if( *cmd == '<' ) { /*  Bit shift left ( a b -- b<<a ) */
            a = b << a;
        } else
        if( *cmd == '=' ) { /*  Test Less Than equalto ( a b -- b<=a ) */
            a = b <= a;
        } else
        if( *cmd == '>' ) { /*  Test not equal to ( a b -- a<>b ) */
            a = a != b;
        } else
        { /*  Test Less Than ( a b -- b<a ) */
            a = b < a;
            cmd--;
        }
    } else

    if( *cmd == 'A' ) { /*  Align ( ptr -- ptr ) */
        a = ((teaint)(a)+sizeof(teaint)-1UL)&~(sizeof(teaint)-1UL);
        adjust = 0;
    } else

    if( *cmd == 't' ) {
        cmd++;
        if( *cmd == 's' ) { /*  Pointer to stack ( -- length ptr ) */
            a = sizeof(tea_stack);
            b = (teaint)&tea_stack;
            adjust = 2;
            pushback = 2;
        } else
        if( *cmd == 't' ) { /*  Pointer to token ( -- length ptr ) */
            a = sizeof(tea_token);
            b = (teaint)&tea_token;
            adjust = 2;
            pushback = 2;
        } else
        {
            cmd--;
            adjust = 0;
            pushback = 0;
        }
    } else

    /* Read memory */
    if( *cmd == '@' ) {
        cmd++;
        adjust = 0;
        if( *cmd == 'c' ) { /*  Read Byte ( ptr -- value ) */
            a = *((teabyte*)a);
        } else
        if( *cmd == 's' ) { /*  Read short ( ptr -- value ) */
            a = *((teashort*)a);
        } else
        if( *cmd == 'x' ) { /*  dump range ( length ptr -- ) */
            adjust = -2;
            pushback = 0;
            for(; a > 0; a--, b++) {
                if( (a%16) == 0 ) tea_printf("\n%p: ", (void*)b);
                tea_printf("%02x ", *((teabyte*)b));
            }
        } else
        if( *cmd == 'r' ) { /*  raw dump range ( length ptr -- ) */
            adjust = -2;
            pushback = 0;
            for(; a > 0; a--, b++) {
                tea_putc( *((teabyte*)b) );
            }
        } else
        { /*  Read word ( ptr -- value ) */
            a = *((teaint*)a);
            cmd--;
        }
    } else

    /* Write memory */
    if( *cmd == '!' ) {
        cmd++;
        adjust = -2;
        pushback = 0;
        if( *cmd == 'c' ) { /*  Write byte ( value ptr -- ) */
            *((teabyte*)b) = a;
        } else
        if( *cmd == 's' ) { /*  Write short ( value ptr -- ) */
            *((teashort*)b) = a;
        } else
        if( *cmd == '+' ) { /*  Increment word ( value ptr -- ) */
            *((teaint*)b) += a;
        } else
        if( *cmd == '-' ) { /*  Decrement word ( value ptr -- ) */
            *((teaint*)b) -= a;
        } else
        if( *cmd == '@' ) { /*  Memcpy ( length src dest -- ) */
            memcpy((void*)c, (void*)b, a);
            adjust = -3;
        } else
        if( *cmd == '!' ) { /*  Memset ( value length dest -- ) */
            memset((void*)c, a, b);
            adjust = -3;
        } else
        { /*  Write word ( value ptr -- ) */
            *((teaint*)b) = a;
            cmd--;
        }
    } else

    if( *cmd == '`' ) { /*  Jump ( ptr -- ) */
        /* Need to adjust stack before calling so C func sees
         * the correct stack
         */
        tea_SP--;
        ((void(*)(void))a)();
        adjust = 0;
        pushback = 0;
    } else

    if( *cmd == '.' ) { /* Print ( a -- ) */
        pushback = 0;
        tea_printf("%tu\n", a);

    } else

    { /* Lookup external token. */
        adjust = 0;
        pushback = 0;

        struct tea_ptr_alias_s *pt = tea_pa_table;
        for(; pt->alias != NULL; pt++) {
            if(strcmp((char*)cmd, pt->alias) == 0) {
                a = (teaint)pt->ptr;
                adjust = 1;
                pushback = 1;
                break;
            }
        }
    }

    /* Now that the command has been completed, put things back into the
     * stack and adjust it as required.
     */
    tea_SP += adjust;
    switch(pushback) {
        case 3: *(tea_SP-2) = c;
        case 2: *(tea_SP-1) = b;
        case 1: *tea_SP = a;
        default:
            break;
    }

    return *tea_SP;
}


#ifdef TEST_IT
int main(int argc, char **argv)
{
    int c;
    while((c = fgetc(stdin)) >= 0) {
        if(tea_get_token(c)) {
            tea_do_token();
        }
    }
    return 0;
}
#endif

/* vim: set ai cin et sw=4 ts=4 : */
