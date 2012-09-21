
#include <stdlib.h> /* strtoul, */
#include <stdint.h>
#include <stddef.h> /* ptrdiff_t */
#include <string.h> /* strlen, memmove, strcpy, strncpy */
#include <ctype.h> /* isalpha, isspace, isalnum, isdigit */
#include <stdio.h> /* printf, fgets */

/*
 * Memory for teash goes in three places.
 * 1. The C Stack. Function local variables managed by the C compiler.
 * 2. Session data. Stuff that is only useful for this session.
 * 3. Savable data. Things that could be written out to persistant memory and recalled.
 *
 *
 * Savable Memory
 * --------------
 *
 * Inital savable memory layout:
 *  - Script space, grows up
 *  - Dictionary space, grows down
 *  - Fixed vars
 * Gives us the following pointers:
 * - teash_mem_start    : fixed
 * - teash_script_end   : moving
 * - teash_dict_start   : moving
 * - teash_dict_end     : fixed
 * - teash_num_vars     : fixed
 * - teash_mem_end      : fixed
 *
 * Free memory is (teash_dict_start - teash_sript_end)
 *
 * The script space is a series of lines.  Each line is two bytes BigE for
 * the line number. Some number of bytes for the line code, then a NUL
 * byte. As many lines as will fit is allowed.  Line numbers should always
 * be in increasing order.
 *
 * The dictionary is a bunch of key-value stings.  Primarily lets you save
 * strings for later use.
 *
 * Number Variables are 28 variables ?@A-Z. They are raw signed integers
 * (rather than the ascii version of them).
 *
 * ? and @ are special variables. The return code for every command is
 * written to ?.  @ is reserved for a future use.
 *
 * You should be able to save and restore this memory byte for byte.
 *
 * Session Memory
 * --------------
 *
 * LP is the line pointer for executing the script.  When this is NULL, no
 * script is running.  Otherwise it points the next line to run.
 *
 * RS and returnStack are a stack of line numbers used for gosub/return and
 * possibly other similar things. (looping)
 *
 * root is the commands that can be run
 *
 */

#define TEASH_LINE_MAX      80
#define TEASH_PARAM_MAX     10
#define TEASH_CMD_DEPTH_MAX 10
#define TEASH_RS_SIZE       10

struct teash_memory_s {
    char *mem_start;
    char *script_end;
    char *dict_start;
    char *dict_end;
    int32_t *vars; /* '?','@','A'...'Z' look at ASCII/UTF8 map. */
    char *mem_end;
};
#define teash_var2idx(v) ((v)-'?')
#define TEASH_VAR_COUNT ('Z'-'?'+1)
#define teash_isvar(v) ((v) >= '?' && (v) <= 'Z')

typedef struct teash_state_s teash_state_t;
typedef int(*teash_f)(int,char**,teash_state_t*);

typedef struct teash_cmd_s teash_cmd_t;
struct teash_cmd_s {
    char *name;
    teash_f cmd;
    teash_cmd_t *sub;
};

struct teash_state_s {
    struct teash_memory_s mem;
    uint16_t returnStack[TEASH_RS_SIZE];

    teash_cmd_t *root;
    char *LP; /* The next line to eval */
    uint16_t *RS;
};


/**
 * \breif How many bytes left for script or dict?
 */
#define teash_has_free(teash) ((teash)->mem.dict_start - (teash)->mem.script_end)

/* Function Prototypes */
int teash_clear_script(int argc, char **argv, teash_state_t *teash);
int teash_run_script(int argc, char **argv, teash_state_t *teash);
int teash_goto(int argc, char **argv, teash_state_t *teash);
int teash_gojump(int argc, char **argv, teash_state_t *teash);
int teash_let(int argc, char **argv, teash_state_t *teash);
int teash_skip(int argc, char **argv, teash_state_t *teash);
int teash_list(int argc, char **argv, teash_state_t *teash);
int teash_puts(int argc, char **argv, teash_state_t *teash);

int teash_init_memory(uint8_t *memory, unsigned size, struct teash_memory_s *mem);
void teash_goto_line(uint16_t ln, teash_state_t *teash);
void teash_next_line(teash_state_t *teash);
int teash_load_line(uint16_t ln, char *newline, teash_state_t *teash);
int teash_exec(int argc, char **argv, teash_state_t *teash);
int teash_subst(char *in, char *out, teash_state_t *teash);
int teash_eval(char *line, teash_state_t *teash);
int teash_do_line(char *line, teash_state_t *teash);
int teash_mloop(teash_state_t *teash);


/* stuff above here will get moved to a header file someday */
/*****************************************************************************/

/**
 * \brief Setup a memory area to store script, dictionary and variables.
 */
int teash_init_memory(uint8_t *memory, unsigned size, struct teash_memory_s *mem)
{
    /* script and dict are byte addressed, so no alignment needed */
    mem->mem_start = (char*)memory;
    /* vars need to be 32bit aligned, so line up the end and stay inside. */
    mem->mem_end = (char*)(((ptrdiff_t)memory+size) & ~0x3);

    /* check if too small */
    if( size <= (sizeof(uint32_t)*TEASH_VAR_COUNT) + (sizeof(uint16_t)*2) + 10 )
        return -1;

    mem->script_end = mem->mem_start;
    mem->vars = (int32_t*)(mem->mem_end - sizeof(int32_t)*TEASH_VAR_COUNT);
    mem->dict_end = (char*)mem->vars;
    mem->dict_start = mem->dict_end;

    return 0;
}

/*****************************************************************************/
/**
 * \brief Erase the current script memory.
 *
 * \note doesn't actually erase anything, just moves the end.
 */
int teash_clear_script(int argc, char **argv, teash_state_t *teash)
{
    teash->mem.script_end = teash->mem.mem_start;
    return 0;
}

/**
 * \brief Goto, Gosub, Return, Run, and End all together since they're mostly the same
 *
 * This jumps to the next equal-or-greater line. (or to the end.)
 *
 * FE: if the script only has lines 10 and 20, and sees a "goto 15", it will
 * jump to line 20.  If it sees a "goto 30", then it stops. (it jumped off
 * the end of the script.) This is the same for gosub.
 *
 * This is written so that it works from the command prompt.  It jumps into 
 * the script to the line requested.  
 *
 * The difference between goto and gosub is that gosub pushes the next line 
 * onto the return stack. (unless the return stack is full)
 *
 * Return pops the return stack and goes to that line.  If the return stack 
 * is empty, return exits the script and goes to the command prompt.
 *
 * Run resets the return stack and otherwise is identical to "goto 0"
 *
 * End stops the script
 */
int teash_gojump(int argc, char **argv, teash_state_t *teash)
{
    int ln = 0;
    int ret = 0;
    if(argc > 1) {
        ln = strtoul(argv[1], NULL, 0);
    }

    if( argv[0][0] == 'e' ) { /* called as 'End' */
        ret = ln; /* argv[1] is actually return value */
        teash->LP = NULL;
        return ret;
    } else if( argv[0][1] == 'u' ) { /* called as 'rUn' */
        teash->RS = teash->returnStack; /* reset RS */
        ln = 0;
    } else if( argv[0][1] == 'e' ) { /* called as 'rEturn' */
        ret = ln; /* argv[1] is actually return value */
        if(teash->RS <= teash->returnStack) {
            return ret;
        } else {
            teash->RS--;
            ln = *(teash->RS);
        }
    } else if(argv[0][2] == 's') { /* called as 'goSub' */
        if(teash->RS > &teash->returnStack[TEASH_RS_SIZE]) return -2; /* no return stack space left. */
        if(teash->LP) {
            /* LP is the next line to run, not current. */
            *(teash->RS) = (*(teash->LP-2) <<8) | *(teash->LP-1);
            teash->RS++;
        }
    }
    /* else is goto. */

    teash_goto_line(ln, teash);
    return ret;
}

/****************************************************************************/
#if 0
/**
 * \brief Evaluate a math expression and maybe save the result to a variable
 *
 * If the first param is just a variable, then the final result will get
 * saved there.
 *
 * This is a left associative accumilator style math parser. For simple one
 * operand expressions, it works just like infix notation.
 *
 * This parser has two numbers, the accumilator and the immeadiate.  The
 * accumilator is initialized to zero.  The immeadiate is the number that
 * was just parsed from the input.  There is one operand, and it is
 * replaced as new once appear in the input.  The initial operand sets the
 * accumilator to the immeadiate.  Once an immeadiate is parsed, the
 * current operand is used to combine it into the accumilator.
 *
 * Numbers are in base ten unless the start with 0x, then they are in base
 * sixteen.  The variables A thru Z can also be used (must be uppercase).
 *
 * This style of parser is simpler than a full infix, but cannot do very
 * complex equations, since there is no nesting.  LET might get upgraded
 * someday, or maybe I'll add a postfix notation along side for when more
 * powerful equation are needed.
 *
 * Some examples:
 * - Add four numbers: 1 + 2 3 4
 *   Or: + 1 2 3 4
 *   Both give 10
 * - Subtract: 10 - 5 3
 *   gives 2
 *   However: - 10 5 3
 *   gives -18 since the ACC starts with 0
 * - Multiply: 2 * 3
 */
int teash_let(int argc, char **argv, teash_state_t *teash)
{
    char *p;
    char set = '\0';
    int acc=0;
    int imd=0;
    char op = '='; 

    /* drop "let" */
    argc--, argv++;

    /* simplistic variable setting.  If first param is just a var, then we
     * set to it.
     * Not liking this; think about how to fix.
     */
    if( isalpha(argv[0][0]) && argv[0][1] == '\0' ) {
        set = argv[0][0];
        argc--, argv++;
    }

    /* process exressions */
    for(; argc > 0; argc--, argv++) {
        p = *argv;

        /* Very simple, left associating math expressions. */
        for(; *p != '\0'; ) {
            for(; isspace(*p) && *p != '\0'; p++) {}

            if( isalnum(*p) ) {
                if( isdigit(*p) ) {
                    imd = strtoul(p, &p, 0);
                } else {
                    imd = teash->mem.vars[teash_var2idx(*p)];
                    p++;
                }
                switch(op) {
                    case '=': acc = imd; break;
                    case '+': acc += imd; break;
                    case '-': acc -= imd; break;
                    case '*': acc *= imd; break;
                    case '/': acc /= imd; break;
                    case '%': acc %= imd; break;
                    case '&': acc &= imd; break;
                    case '|': acc |= imd; break;
                    case '^': acc ^= imd; break;
                    case '>': acc = (acc > imd); break;
                    case 'L': acc = (acc >= imd); break;
                    case 'r': acc >>= imd; break;
                    case '<': acc = (acc < imd); break;
                    case 'G': acc = (acc <= imd); break;
                    case 'l': acc <<= imd; break;
                    case 'e': acc = (acc == imd); break;
                }
            } else {
                switch(*p) {
                    case '+':
                    case '-':
                    case '*': 
                    case '/':
                    case '%':
                    case '&':
                    case '|':
                    case '^':
                        op = *p;
                        break;
                    case '=':
                        p++;
                        switch(*p) {
                            case '=': op = 'e'; break;
                            default: p--; op = '='; break;
                        }
                        break;
                    case '>':
                        p++;
                        switch(*p) {
                            case '>': op = 'r'; break;
                            case '=': op = 'L'; break;
                            default: p--; op = '>'; break;
                        }
                        break;
                    case '<':
                        p++;
                        switch(*p) {
                            case '<': op = 'l'; break;
                            case '=': op = 'G'; break;
                            default: p--; op = '<'; break;
                        }
                        break;
                    default:
                        /* unknown symbol, skip it */
                        break;
                }
                p++;
            }
        }
    }

    if(set != '\0') {
        teash->mem.vars[teash_var2idx(set)] = acc;
    }

    return acc;
}
#else
/*
 * Usage: let <val> [<op> <val>] [-> <var>]
 * Where val is a number or var
 * var is A-Z (and ?@)
 * op is: + - * / % & | ^ > >= >> < <= << <> 
 * -> stores result to var
 */
int teash_let(int argc, char **argv, teash_state_t *teash)
{
    int a=0, b=0;
    if(argc == 1) return 0;

    if(isdigit(argv[1][0])) {
        a = strtol(argv[1], NULL, 0);
    } else if(teash_isvar(argv[1][0])) {
        a = teash->mem.vars[teash_var2idx(argv[1][0])];
    }
    if(argc < 4) return a;

    if(isdigit(argv[3][0])) {
        b = strtol(argv[3], NULL, 0);
    } else if(teash_isvar(argv[3][0])) {
        b = teash->mem.vars[teash_var2idx(argv[3][0])];
    }
    
    switch(argv[2][0]) {
        case '+': a += b; break;
        case '-': a -= b; break;
        case '*': a *= b; break;
        case '/': a /= b; break;
        case '%': a %= b; break;
        case '&': a &= b; break;
        case '|': a |= b; break;
        case '^': a ^= b; break;
        case '=': a= a==b; break;
        case '>':
            switch(argv[2][1]) {
                case '>': a >>= b; break;
                case '=': a = a>=b; break;
                default: a = a>b; break;
            }
            break;
        case '<':
            switch(argv[2][1]) {
                case '<': a <<= b; break;
                case '=': a = a<=b; break;
                case '>': a = a!=b; break;
                default: a = a<b; break;
            }
            break;
        default:
            break;
    }

    if( strcmp("->", argv[argc-2]) == 0) { /* a store command */
        if(teash_isvar(argv[argc-1][0])) {
            teash->mem.vars[teash_var2idx(argv[argc-1][0])] = a;
        }
    }
    return a;
}
#endif

/****************************************************************************/

/**
 * \brief Skip the next line if not zero
 *
 * \note "skip A" won't do what you might think.  should fix.
 */
int teash_skip(int argc, char **argv, teash_state_t *teash)
{
    if( argc < 2 ) return -1;
    if( teash_let(argc, argv, teash) == 0 ) return 0;

    if( teash->LP == NULL ) return 0;

    /* Jump to next line */
    teash_next_line(teash);

    return 0;
}

/**
 * \brief List what is in the script space.
 */
int teash_list(int argc, char **argv, teash_state_t *teash)
{
    char *p;
    uint16_t ln;
    for(p = teash->mem.mem_start; p < teash->mem.script_end; ) {
        ln = *(uint8_t*)p++;
        ln <<= 8;
        ln |= *(uint8_t*)p++;
        printf("%5u %s\n", ln, p);
        p += strlen(p) + 1;
    }
    return 0;
}

/**
 * \brief Print the rest of the line to stdout
 */
int teash_puts(int argc, char **argv, teash_state_t *teash)
{
    argc--, argv++;
    for(; argc > 0; argc--, argv++) {
        printf("%s", *argv);
    }
    printf("\n");
    return 0;
}

/*****************************************************************************/

/**
 * \brief Goto a line (or the next one if not exact)
 *
 * Finds a line or the next following.  If looking for line 22, but only
 * lines 20 and 25 exist, will return line 25.
 *
 */
void teash_goto_line(uint16_t ln, teash_state_t *teash)
{
    char *p = teash->mem.mem_start;
    uint16_t tln;

    while(p < teash->mem.script_end) {
        tln = *(uint8_t*)p++;
        tln <<=8;
        tln |= *(uint8_t*)p++;

        if( tln >= ln ) {
            teash->LP = p;
            return;
        }

        tln = strlen(p) + 1;
        p += tln;
    }

    teash->LP = NULL;
}

/**
 * \brief Jump to the next line in the script.
 */
void teash_next_line(teash_state_t *teash)
{
    teash->LP += strlen(teash->LP) + 3;
    if( teash->LP >= teash->mem.script_end)
        teash->LP = NULL;
}

/**
 * \breif load a new line into the script space
 *
 * This keeps the script space sorted by line number. Inserting and replacing
 * as needed.
 */
int teash_load_line(uint16_t ln, char *newline, teash_state_t *teash)
{
    char *oldline = NULL;
    uint16_t tln;
    int oldlen=0;
    int newlen = strlen(newline) + 3;

    if(newlen == 3) /* oh, actually is deleting the whole line. */
        newlen = 0;

    /* set oldline to where we want to insert. */
    for(oldline = teash->mem.mem_start;
        oldline < teash->mem.script_end; ) {
        tln  = (*(uint8_t*)oldline++) << 8;
        tln |= *(uint8_t*)oldline++;
        if( tln > ln ) {
            /* inserting a new line */
            oldlen = 0;
            oldline -= 2;
            break;
        } else if( tln == ln ) {
            /* replacing an old line */
            oldlen = strlen(oldline) + 3;
            oldline -= 2;
            break;
        }
        /* else keep looking */
        oldline += strlen(oldline) + 1;
    }
    if( oldline >= teash->mem.script_end ) {
        /* at the end, so just append. */
        oldline = teash->mem.script_end;
        if( teash_has_free(teash) < newlen) 
            return -2;
        teash->mem.script_end += newlen;
    } else if( oldlen < newlen ) {
        /* growing */
        if( teash_has_free(teash) < newlen-oldlen) 
            return -2;

        memmove(oldline+newlen, oldline+oldlen, teash->mem.script_end - oldline+oldlen);

        teash->mem.script_end += newlen-oldlen; /* grew by this much */
    } else if( oldlen > newlen ) {
        /* shrinking */
        memmove(oldline+newlen, oldline+oldlen, teash->mem.script_end - oldline+oldlen);

        teash->mem.script_end -= oldlen-newlen; /*shrunk by this much */
    }
    /* its the right size! */

    /* now we can copy it in (unless there is nothing to copy) */
    if( newlen > 3 ) {
        *(uint8_t*)oldline++ = (ln >> 8)&0xff;
        *(uint8_t*)oldline++ = ln & 0xff;
        strcpy(oldline, newline);
    }

    return 0;
}

/*****************************************************************************/

/**
 * \breif Search for a command, and call it when found.
 */
int teash_exec(int argc, char **argv, teash_state_t *teash)
{
    int ac = 0;
    teash_cmd_t *parents[TEASH_CMD_DEPTH_MAX];
    teash_cmd_t *current = teash->root;

    /* params passed must include command name as argv[0].
     * For nested commands, only the right most is passed in.
     * So for a cmd "spi flash dump 256 32" argv[0] is "dump"
     */
    for(; current->name != NULL; ) {
        if( strcmp(current->name, argv[ac]) == 0) {
            /* Matched name. */
            if( current->sub == NULL || (argc-ac) == 1) {
                /* Cannot go deeper, try to call */
                if( current->cmd ) {
                    return current->cmd((argc-ac), &argv[ac], teash);
                }
                break;
            } else {
                /* try going deeper. */
                if(ac == TEASH_CMD_DEPTH_MAX) return -2;
                parents[ac++] = current;
                current = current->sub;
                continue;
            }
        }
        current++;
    }

    /* No command to call at deepest find, backtrack up to see if we missed
     * one.
     *
     * This is to handle the following:
     * - Have tree with commands defined at the following points:
     *   A
     *   A B C
     * - The command "A B" is executed.
     * - So a depth search we find node B, but there are no commands there,
     *   so we need to backtrack to A.
     *   XXX Why not just fail? Why even allow this?
     */
    for(ac--; ac > 0; ac--) {
        if( parents[ac]->cmd ) {
            return parents[ac]->cmd((argc-ac), &argv[ac], teash);
        }
    }

    /* No commands anywhere to run what was requested. */
    return -1;
}

/**
 * \brief put ASCII form of number into stream.
 *
 * Not sure why doing it this way instead of sprintf
 */
char* teash_itoa(int i, char *b, unsigned max)
{
    char tb[12];
    char *t = tb;
    char sign = '+';
    if(max == 0) return b;

    /* check sign */
    if( i < 0 ) {
        sign = '-';
        i = -i;
    }

    /* ascii-fy (backwards.) */
    do {
        *t++ = (i%10) + '0';
        i /= 10;
    }while(i>0);

    if( (t-tb) > max ) {
        /* not enough room, replace with underbar. */
        *b++ = '_';
        return b;
    }

    /* Put number into buffer */
    if(sign == '-')
        *b++ = '-';
    for(t--; t >= tb;) {
        *b++ = *t--;
    }
    return b;
}

/**
 * \brief Find the $vars and replace them
 *
 * This is a in-string replacement; similar to how shells work.
 *
 * This works within the line length limits. If a replacement is too long
 * to fit, it will get either nothing, or a '_'.
 * XXX This behavor needs to be cleaned up.
 *
 */
int teash_subst(char *in, char *out, teash_state_t *teash)
{
    char *varname;
    int varlen;
    char *oute = out+TEASH_LINE_MAX;

    /* Look for variables (they start with $) */
    for(; *in != '\0'; in++, out++) {
        if( *in != '$' ) {
            *out = *in;
        } else {
            in++;
            if( *in == '$' ) {
                *out = '$';
            } else {
                /* Find the var name in the buffer */
                varname = in;
                if( *in =='{' ) {
                    varname++;
                    in++;
                    for(varlen=0; *in != '}' && *in != '\0'; in++, varlen++) {}
                } else {
                    for(varlen=0;(isalnum(*in)||teash_isvar(*in)) && *in!='\0';
                            in++, varlen++) {}
                }
                /* Now look up variable */
                if( varlen == 1 && teash_isvar(*varname) ) {
                    /* Number variable. grab it and ascii-fy it */
                    out = teash_itoa(teash->mem.vars[teash_var2idx(*varname)], out, oute-out);
                    out--;
#if 0
                } else if( dict ) {
                    /* Is it in the dictionary? */
#endif
                } else {
                    /* Variable not found, replace with nothing */
                }
            }
        }
    }

    *out = '\0';
    return 0;
}

/**
 * \brief take a line, do subs, and break it into params.
 *
 * This assumes that #line is read only, and so copies the line into a
 * buffer that can be modified.
 */
int teash_eval(char *line, teash_state_t *teash)
{
    char buf[TEASH_LINE_MAX+1]; // could move into state.
    char *argv[TEASH_PARAM_MAX+1]; // could move into state.
    char *p;
    char *end=NULL;
    int argc;

    /* do substitutions */
    teash_subst(line, buf, teash);

    /* Break up into parameters */
    for(argc=0, p=buf; *p != '\0'; p++) {
        /* skip white space */
        for(; isspace(*p) && *p != '\0'; p++) {}
        if( *p == '\0' ) break;

        if( *p == '"' ) {
            p++; /* skip over the quote */
            argv[argc++] = p;
            for(; *p != '"' && *p != '\0'; p++) {
                if( *p == '\\' ) {
                    if( end == NULL ) {
                        /* We only get the end if we need it.
                         * But once we have it, we don't need to get it
                         * again.
                         */
                        for(end=p; *end != '\0'; end++) {}
                    }
                    /* slide all on down */
                    memmove(p, p+1, end-p);
                    end --;
                }
            }
        } else {
            argv[argc++] = p;
            /* find end */
            for(; !isspace(*p) && *p != '\0'; p++) {}
        }
        if( *p == '\0' ) break;
        *p = '\0';
    }

    return (teash->mem.vars[teash_var2idx('?')] = teash_exec(argc, argv, teash));
}

/** 
 * \breif take a line from user input and figure out what to do
 *
 * Lines are ether executed or loaded into script memory.
 */
int teash_do_line(char *line, teash_state_t *teash)
{
    char *p;
    int ln;

    /* trim whitespace on tail */
    for(p=line; *p != '\0'; p++) {} /* goto end */
    for(p--; isspace(*p); p--) {} /* back up over whitespaces */
    *(++p) = '\0'; /* end the line */

    /* skip whitespace */
    for(p=line; isspace(*p) && *p != '\0'; p++) {}
    if( *p == '\0' ) return 0;

    /* is first word a number? */
    for(ln=0; isdigit(*p) && *p != '\0'; p++) {
        ln *= 10;
        ln += *p - '0';
    }
    if( isspace(*p) || *p == '\0' ) {
        for(; isspace(*p) && *p != '\0'; p++) {}
        return teash_load_line(ln, p, teash);
    }
    return teash_eval(line, teash);
}

int teash_mloop(teash_state_t *teash)
{
    char line[TEASH_LINE_MAX+1]; // could move into state.

    /* initialize remaining state vars */
    teash->LP = NULL;
    teash->RS = teash->returnStack;

    while(1) {
        if( teash->LP == NULL ) {
            printf("> ");
            fflush(stdout);
            if(!fgets(line, sizeof(line), stdin)) break;
            teash_do_line(line, teash);
        } else {
            strncpy(line, teash->LP, TEASH_LINE_MAX);
            line[TEASH_LINE_MAX] = '\0';

            /* Set up LP for the next line after this one */
            teash_next_line(teash);

            /* eval this line */
            teash_eval(line, teash);

        }
    }

    return 0;
}


#ifdef TEST_IT
uint8_t test_memory[4096];
teash_state_t teash_state;
teash_cmd_t teash_root_commands[] = {
    { "clear", teash_clear_script, NULL },
    { "run", teash_gojump, NULL },
    { "end", teash_gojump, NULL },
    { "goto", teash_gojump, NULL },
    { "gosub", teash_gojump, NULL },
    { "return", teash_gojump, NULL },
    { "let", teash_let, NULL },
    { "skip", teash_skip, NULL },
    { "list", teash_list, NULL },
    { "puts", teash_puts, NULL },
    { "deep", NULL, (teash_cmd_t[]){
        { "A", teash_puts, NULL },
        { "B", teash_puts, NULL },
        { "C", teash_puts, NULL },
                    }
    },
    { NULL, NULL, NULL }
};

int main(int argc, char **argv)
{
    teash_init_memory(test_memory, sizeof(test_memory), &teash_state.mem);
    teash_state.root = teash_root_commands;

    return teash_mloop(&teash_state);
}
#endif

/* vim: set ai cin et sw=4 ts=4 : */
