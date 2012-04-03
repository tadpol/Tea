/**
 * \file tea18.c
 *
 * A tiny litle language designed with the following goals:
 * - Small ROM/RAM useage
 * - Powerful
 * - Portable
 * - Usable
 *
 * Primarily driven as something I use on little systems and attached
 * to someone else's shell type environment.
 *
 * ---------
 *
 * Looping:
 * - Very basic looping.
 * - Can NOT nest loops.
 * - Cannot span commands (even though the stack does)
 * - Think "do {} while()" from C
 *
 * Conditional:
 * - Skip if false.
 * - Can NOT nest IFs.
 * - Cannot span commands (even though the stack does)
 * - If top is 0(false), then jump to next :
 *
 * Unrecognized charaters in the command string are ignored and skipped
 * over. This allows for using whitespace for readablity.
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
 * Copyright (c) 2012 Michael Conrad Tadpol Tilstra 
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
#else
#define tea_printf(...) do{}while(0)
#endif

typedef unsigned long teaint; /*!< a 64 bit number */
//typedef unsigned int teaint; /*!< a 32 bit number */
typedef unsigned short teashort; /*!< a 16 bit number */
typedef unsigned char teabyte; /*!< a 8 bit number */

#define USE_DICT
#ifdef USE_DICT
teabyte tea_dict_base[2024];
teabyte *tea_dict_head = tea_dict_base;
#define alignPointer(p) (p) = ((void*)(((teaint)(p)+sizeof(teaint)-1UL)&~(sizeof(teaint)-1UL)))
#endif

/**
 * How big is the stack
 */
#define tea_stack_depth 10
teaint tea_stack[tea_stack_depth];
teaint *tea_SP = tea_stack;

/* XXX These need to move out into a header. */
/**
 * Push a number onto the stack
 */
#define tea_push(t) (*(++tea_SP) = (t))
/**
 * Pop a number off of the stack
 */
#define tea_pop() (*tea_SP--)

/**
 * Operate on a command string and return the top of the stack.
 *
 * Note that depending on the commands, the return value may or may not be
 * meaningful.
 * 
 * \param[in] commands An ASCII string of numbers and commands
 *
 * \return The value that is at the top of the stack.
 */
teaint tea_eval(char* cmd)
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

    teaint base;
    char* loop=0;

    /* Commands have the following stack description:
     * ( items before -- items after )
     * <-- top of stack is left side for both before and after.
     *
     * This is a flip from how it appears in the command string.
     * FE:  the command "10 5" is ( 5 10 )
     * so "10 5 +" is ( 5 10 -- 15 )
     */

    for(; *cmd != '\0'; cmd++ ) {
        /* Fetch the top three of the stack, and set the default adjust and
         * pushback values.
         */
        a = *tea_SP;
        b = *(tea_SP-1);
        c = *(tea_SP-2);
        adjust = -1;
        pushback = 1;
        base = 10;

        /* Now do the command */
        if( *cmd >= '0' && *cmd <= '9' ) {
            // push number ( -- value )
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
            if( *(cmd+1) == 'n' ) { // nswap ( n a ... b -- b ... a )
                cmd++;
                /* top is index into stack, pop, then swap top and nth */
                a++; // add one because we haven't dropped the index yet.
                c = *(tea_SP-a);
                *(tea_SP-a) = b;
                a = c;
            } else { // swap ( a b -- b a )
                c = b;
                b = a;
                a = c;
                adjust = 0;
                pushback = 2;
            }
        } else
        if( *cmd == 'v' ) {
            if( *(cmd+1) == 'n' ) { // ndup ( n a ... b -- b a ... b )
                cmd++;
                a++;
                a = *(tea_SP - a);
                adjust = 0;
            } else { // dup ( a -- a a )
                adjust = 1;
            }
        } else
        if( *cmd == 'x' ) { // drop ( a -- )
            pushback = 0;
        } else

        if( *cmd == '+' ) { // add ( a b -- b+a )
            a = b + a;
        } else
        if( *cmd == '-' ) { // sub ( a b -- b-a )
            a = b - a;
        } else
        if( *cmd == '*' ) { // multiply ( a b -- b*a )
            a = b * a;
        } else
        if( *cmd == '/' ) { // divide ( a b -- b/a )
            a = b / a;
        } else
        if( *cmd == '%' ) { // modulo ( a b -- b%a )
            a = b % a;
        } else

        if( *cmd == '|' ) {// Bit OR ( a b -- b|a )
            a = b | a;
        } else
        if( *cmd == '^' ) { // Bit XOR ( a b -- b^a )
            a = b ^ a;
        } else
        if( *cmd == '&' ) { // Bit AND ( a b -- b&a )
            a = b & a;
        } else
        if( *cmd == '~' ) { // Bit invert ( a -- ~a )
            a = ~a;
            adjust = 0;
        } else

        if( *cmd == '=' ) { // Test equal to ( a b -- a==b )
            a = a == b;
        } else

        if( *cmd == '>' ) {
            cmd++;
            if( *cmd == '>' ) { // Bit shift right ( a b -- b>>a )
                a = b >> a;
            } else
            if( *cmd == '=' ) { // Test Greater than equalto ( a b -- b>=a )
                a = b >= a;
            } else
            { // Test Greater than ( a b -- b>a )
                a = b > a;
                cmd--;
            }
        } else
        if( *cmd == '<' ) {
            cmd++;
            if( *cmd == '<' ) { // Bit shift left ( a b -- b<<a )
                a = b << a;
            } else
            if( *cmd == '=' ) { // Test Less Than equalto ( a b -- b<=a )
                a = b <= a;
            } else
            if( *cmd == '>' ) { // Test not equal to ( a b -- a<>b )
                a = a != b;
            } else
            { // Test Less Than ( a b -- b<a )
                a = b < a;
                cmd--;
            }
        } else

        if( *cmd == '@' ) {
            cmd++;
            adjust = 0;
            if( *cmd == 'c' ) { // Read Byte ( ptr -- value )
                a = *((teabyte*)a);
            } else
            if( *cmd == 's' ) { // Read short ( ptr -- value )
                a = *((teashort*)a);
            } else
            if( *cmd == 'x' ) { // dump range ( length ptr -- )
                adjust = -2;
                pushback = 0;
                for(; a > 0; a--, b++) {
                    if( (a%16) == 0 ) tea_printf("\n%p: ", (void*)b);
                    tea_printf("%02x ", *((teabyte*)b));
                }
            } else
            { // Read word ( ptr -- value )
                a = *((teaint*)a);
                cmd--;
            }
        } else

        if( *cmd == '!' ) {
            cmd++;
            adjust = -2;
            pushback = 0;
            if( *cmd == 'c' ) { // Write byte ( value ptr -- )
                *((teabyte*)b) = a;
            } else
            if( *cmd == 's' ) { // Write short ( value ptr -- )
                *((teashort*)b) = a;
            } else
            if( *cmd == '+' ) { // Increment word ( value ptr -- )
                *((teaint*)b) += a;
            } else
            if( *cmd == '-' ) { // Decrement word ( value ptr -- )
                *((teaint*)b) -= a;
            } else
            if( *cmd == '@' ) { // Memcpy ( length src dest -- )
                memcpy((void*)c, (void*)b, a);
                adjust = -3;
            } else
            if( *cmd == '!' ) { // Memset ( value length dest -- )
                memset((void*)c, a, b);
                adjust = -3;
            } else
            { // Write word ( value ptr -- )
                *((teaint*)b) = a;
                cmd--;
            }
        } else

        if( *cmd == '(' ) { // Loop begin ( -- )
            loop = cmd;
            adjust = 0;
            pushback = 0;
        } else
        if( *cmd == ')' ) { // Loop end ( test -- )
            if( loop != 0 && a != 0 )
                cmd = loop;
            pushback = 0;
        } else
        if( *cmd == ':' ) { // IF end ( -- )
            adjust = 0;
            pushback = 0;
        } else
        if( *cmd == '?' ) { // IF ( test -- )
            if( a == 0 ) {
                // false, skip to :
                for(; *cmd != ':' && *cmd != '\0'; cmd++)
                {}
            }
            pushback = 0;
        } else

        if( *cmd == '#' ) { // Eval ( ptr -- )
            /* Need to adjust stack before calling so evalled sees
             * the correct stack
             */
            tea_SP--;
            (void)tea_eval((char*)a);
            adjust = 0;
            pushback = 0;
        } else
        if( *cmd == '`' ) { // Jump ( ptr -- )
#if 1
            /* Need to adjust stack before calling so C func sees
             * the correct stack
             */
            tea_SP--;
            ((void(*)(void))a)();
            adjust = 0;
            pushback = 0;
#else
            /* alt, for calling int foo(int argc, char **argv) style functions.
             * maybe useful. duno.  saving the idea anyhow.
             * 
             * Needs something to easily load C-string params into stack (or somewhere.)
             * - Use "" as a command to load Cstrings.
             *   The bytes between are copied into a staging buffer and then NUL terminated.
             *   This is mostly for the C function calling above.
             *   so: "-s" "-p" "fooo" 3 0x888888 `
             *   No good ideas on how to jump back to beginning of staging buffer.
             * - Could overload dict, since it NULs all definitions.
             *   That is: [+a|-s] [+b|-p] [+c|fooo] [a] [b] [c] 3 0x888888 `
             *   Though looking at that is reaons enough to not use it.
             *   Also after wards would need: [-c] [-b] [-a]
             */
            a = ((int(*)(int,char**))a)(b, (char**)(tea_SP-2));
            adjust = - (b + 1); /* pop all params and replace one for result */
#endif
        } else

#ifdef USE_DICT
        /* - Use [] for dictionary actions.
         *   - [text] looks up pointer to definition of text
         *   - [+text|definition] creates new term.
         *     - [+text|] creates a term with enough space for use as a variable.
         *       - create variable: [+A|]
         *       - set it to 9:  [A] 9 !
         *       - read it: [A] @
         *   - [-text] deletes term.  This memmove()s to reclaim the space.
         */
        if( *cmd == '[' ) { // Dictionary actions
            teabyte *p = tea_dict_head;
            teashort mark;
            adjust = 0;
            pushback = 0;
            cmd++;
            if( *cmd == '+' ) { /* Create  ( -- ) */
                cmd++;
                for(mark=0; *cmd != '\0' && *cmd != '|'; ++cmd) {
                    *p++ = *cmd;
                }
                *p++ ='\0';
                alignPointer(p);
                ++cmd;
                if( *cmd == ']' ) {
                    /* if empty definiton, give enough space to use as a variable */
                    p += sizeof(teaint);
                } else {
                    c = 1;
                    for(; *cmd != '\0'; cmd++ ) {
                        switch(*cmd) {
                            case '[': c++; break;
                            case ']': c--; break;
                            default: break;
                        }
                        if(c==0) break;
                        *p++ = *cmd;
                    }
                    *p++ ='\0';
                }
                mark = (p - tea_dict_head);
                *p++ = (mark>>8) & 0xff;
                *p++ = mark & 0xff;

                tea_dict_head = p;
            } else { /* Lookup or Delete. */
                teabyte *pl = p;
                if(*cmd == '-') {
                    a = 1; /* Delete  ( -- ) */
                    ++cmd;
                } else {
                    a = 0; /* Lookup  ( -- ptr ) */
                    adjust = 1;
                    pushback = 1;
                }
                for(b=0; *cmd != '\0' && *cmd != ']'; ++cmd, ++b) {}
                cmd -= b;
                while(p > tea_dict_base) {
                    pl=p;
                    --p; mark = *p;
                    --p; mark |= (*p) << 8;

                    p -= mark;

                    if( strncmp(cmd, (char*)p, b) == 0 && *(p+b) == '\0') {
                        if(a) {
                            /* Delete */
                            memmove(p, pl, (tea_dict_head-pl));
                            tea_dict_head -= pl-p;
                            break;
                        } else {
                            /* Lookup */
                            p += b+1;
                            alignPointer(p);
                            a = (teaint)p;
                            break;
                        }
                    }
                }
            }
        } else
#endif

        if( *cmd == '.' ) { /* Print ( a -- ) */
            pushback = 0;
            tea_printf("%tu\n", a);
        } else

        { // NOP
            adjust = 0;
            pushback = 0;
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
    }

    return *tea_SP;
}


#ifdef TEST_IT
#include <stdio.h>
int main(int argc, char **argv)
{
    char buf[160];
    for(;;) {
        printf("> "); fflush(stdout);
        if(fgets(buf, sizeof(buf), stdin) == NULL) break;
        (void)tea_eval(buf);
    }
    return 0;
}
/*
 * add: 2 3+
 *
 * From address 4000 to 5000, write zeros
 * with looping: 4000(v 0! 4+ v 5000<)
 * with memset: 4000 v 5000- 0 !!
 *
 * If word at address 4000 is not 0, set it to 0
 * 4000v@?0!:
 *
 * If short at address 4000 is less than 12, set to 65535
 * 4000 v @s 12 < ? 0xffff !s :
 *
 * Change the byte at 4000 to 0x55
 * 4000 0x55 !c
 *
 */
#endif

