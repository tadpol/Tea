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
 */
#include <string.h>
#ifdef USE_STDOUT
#include <stdio.h>
#define tea_printf printf
#else
#define tea_printf(...) do{}while(0)
#endif

typedef unsigned int teaint; /*!< a 32 bit number */
typedef unsigned short teashort; /*!< a 16 bit number */
typedef unsigned char teabyte; /*!< a 8 bit number */

/**
 * How big is the stack
 */
#define tea_stack_depth 10

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
    static teaint stack[tea_stack_depth];
    static teaint *SP = stack;

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
        a = *SP;
        b = *(SP-1);
        c = *(SP-2);
        adjust = -1;
        pushback = 1;
        base = 10;

        /* Now do the command */
        switch( *cmd ) {
            case '0': // push number ( -- value )
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
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
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
                adjust=1;
                break;

            case 's': // swap ( a b -- b a )
                c = b;
                b = a;
                a = c;
                adjust = 0;
                pushback = 2;
                break;
            case 'x': // drop ( a -- )
                pushback = 0;
                break;
            case 'v': // dup ( a -- a a )
                adjust = 1;
                break;
            case 'l': // ndup ( n a ... b -- b a ... b )
                a++; 
                a = *(SP - a);
                adjust = 0;
                break;
            case 'n': // nswap ( n a ... b -- b ... a )
                /* top is index into stack, pop, then swap top and nth */
                a++; // add one because we haven't dropped the index yet.
                c = *(SP-a);
                *(SP-a) = b;
                a = c;
                break;

            case '+': // add ( a b -- b+a )
                a = b + a;
                break;
            case '-': // sub ( a b -- b-a )
                a = b - a;
                break;
            case '*': // multiply ( a b -- b*a )
                a = b * a;
                break;
            case '/': // divide ( a b -- b/a )
                a = b / a;
                break;
            case '%': // modulo ( a b -- b%a )
                a = b % a;
                break;

            case '|':// Bit OR ( a b -- b|a )
                a = b | a;
                break;
            case '^': // Bit XOR ( a b -- b^a )
                a = b ^ a;
                break;
            case '&': // Bit AND ( a b -- b&a )
                a = b & a;
                break;
            case '~': // Bit invert ( a -- ~a )
                a = ~a;
                adjust = 0;
                break;

            case '=': // Test equal to ( a b -- a==b )
                a = a == b;
                break;

            case '>':
                cmd++;
                switch(*cmd) {
                    case '>': // Bit shift right ( a b -- b>>a )
                        a = b >> a;
                        break;
                    case '=': // Test Greater than equalto ( a b -- b>=a )
                        a = b >= a;
                        break;
                    default: // Test Greater than ( a b -- b>a )
                        a = b > a;
                        cmd--;
                        break;
                }
                break;
            case '<':
                cmd++;
                switch(*cmd) {
                    case '<': // Bit shift left ( a b -- b<<a )
                        a = b << a;
                        break;
                    case '=': // Test Less Than equalto ( a b -- b<=a )
                        a = b <= a;
                        break;
                    case '>': // Test not equal to ( a b -- a<>b )
                        a = a != b;
                        break;
                    default: // Test Less Than ( a b -- b<a )
                        a = b < a;
                        cmd--;
                        break;
                }
                break;

            case '@':
                cmd++;
                adjust = 0;
                switch(*cmd) {
                    case 'c': // Read Byte ( ptr -- value )
                        a = *((teabyte*)a);
                        break;
                    case 's': // Read short ( ptr -- value )
                        a = *((teashort*)a);
                        break;
                    default: // Read word ( ptr -- value )
                        a = *((teaint*)a);
                        cmd--;
                        break;
                    case 'x': // dump range ( length ptr -- )
                        adjust = -2;
                        pushback = 0;
                        for(; a > 0; a--, b++) {
                            if( (a%16) == 0 ) tea_printf("\n%08x: ", b);
                            tea_printf("%02x ", *((teabyte*)b));
                        }
                        break;
                }
                break;

            case '!':
                cmd++;
                adjust = -2;
                pushback = 0;
                switch(*cmd) {
                    case 'c': // Write byte ( value ptr -- )
                        *((teabyte*)b) = a;
                        break;
                    case 's': // Write short ( value ptr -- )
                        *((teashort*)b) = a;
                        break;
                    default: // Write word ( value ptr -- )
                        *((teaint*)b) = a;
                        cmd--;
                        break;
                    case '+': // Increment word ( value ptr -- )
                        *((teaint*)b) += a;
                        break;
                    case '-': // Decrement word ( value ptr -- )
                        *((teaint*)b) -= a;
                        break;
                    case '@': // Memcpy ( length src dest -- )
                        memcpy((void*)c, (void*)b, a);
                        adjust = -3;
                        break;
                    case '!': // Memset ( value length dest -- )
                        memset((void*)c, a, b);
                        adjust = -3;
                        break;
                }
                break;

            case '(': // Loop begin ( -- )
                loop = cmd;
                adjust = 0;
                pushback = 0;
                break;
            case ')': // Loop end ( test -- )
                if( loop != 0 && a != 0 )
                    cmd = loop;
                pushback = 0;
                break;
            case ':': // IF end ( -- )
                adjust = 0;
                pushback = 0;
                break;
            case '?': // IF ( test -- )
                if( a == 0 ) {
                    // false, skip to :
                    for(; *cmd != ':' && *cmd != '\0'; cmd++)
                    {}
                }
                pushback = 0;
                break;

            case '#': // Eval ( ptr -- )
                /* Need to adjust stack before calling so evalled sees
                 * the correct stack
                 */
                SP--;
                (void)tea_eval((char*)a);
                adjust = 0;
                pushback = 0;
                break;
            case '`': // Jump ( ptr -- )
                /* Need to adjust stack before calling so C func sees
                 * the correct stack
                 */
                SP--;
                ((void(*)(void))a)();
                adjust = 0;
                pushback = 0;
                break;

            case '{': // push token ( -- length ptr )
                a = 0; // length
                b = (teaint)(cmd+1); // ptr
                c = 0;
                for(; *cmd != '\0'; cmd++, a++ ) {
                    switch(*cmd) {
                        case '{': c++; break;
                        case '}': c--; break;
                        default: break;
                    }
                    if(c==0) break;
                }
                a--; // don't count trailing bracket.
                adjust = 2;
                pushback = 2;
                break;

            case '.':
                cmd++;
                pushback = 0;
                switch(*cmd) {
                    case '.': // print top as number ( num -- )
                        tea_printf("%u\n", a);
                        break;
                    default: // print top2 as string ( length ptr -- )
                        cmd--;
                        adjust = -2;
                        tea_printf("%.*s", a, (char*)b);
                        /* WARNING the * modifier isn't always implemented
                         * in mini-printf implementations!
                         */
                        break;
                }
                break;

            default: // NOP
                adjust = 0;
                pushback = 0;
                break;
        }

        /* Now that the command has been completed, put things back into the
         * stack and adjust it as required.
         */
        SP += adjust;
        switch(pushback) {
            case 3: *(SP-2) = c;
            case 2: *(SP-1) = b;
            case 1: *SP = a;
            default:
                break;
        }
    }

    return *SP;
}


#ifdef TEST_IT
#include <stdio.h>
int main(int argc, char **argv)
{
    teabyte buf[160];
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

