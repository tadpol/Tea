
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/*
 * Inital memory layout:
 *  - Script space, grows up
 *  - Dictionary space, grows down
 *  - Fixed Return stack
 *  - Fixed vars
 * Gives us the following pointers:
 * - teash_mem_start    : fixed
 * - teash_script_end   : moving
 * - teash_dict_start   : moving
 * - teash_dict_end     : fixed
 * - teash_rsp          : moving
 * - teash_rs_end       : fixed
 * - teash_num_vars     : fixed
 * - teash_mem_end      : fixed
 *
 * Free memory is (teash_dict_start - teash_sript_end)
 *
 *
 * The script space is a series of lines.  Each line is two bytes BigE for
 * the line number. Some number of bytes for the line code, then a NUL
 * byte. As many lines as will fit is allowed.  Line numbers should always
 * be in increasing order.
 *
 * The dictionary is a bunch of key-value stings.  Primarily lets you save
 * strings for later use.
 *
 * Number Variables are 26 variables A-Z. They are raw signed integers
 * (rather that the ascii version of them).
 *
 * I'm possibly thinking of merging the NumberVariables into the
 * Dictionary. Leaving this and the dictionary out until other parts are
 * debuged.
 *
 * The Return Stack area is between dict_end and num_vars.  It is intended
 * for things like gosub/return and loops.  Not sure if this is atually how
 * I want to use it, but am keeping a spot for it for now.
 *
 *
 *
 * Also thinking to move 'line' from teash_mloop() and 'argv' from
 * teash_eval() into this memory block.  This would make the eval not
 * nestable; which is in line with the BASIC design.  Making this move
 * would get those two out of the C stack and in with all the other large
 * memory chunks that teash wants.
 *
 */

#define TEASH_LINE_MAX      80
#define TEASH_PARAM_MAX     10
#define TEASH_CMD_DEPTH_MAX 10
#define TEASH_RS_SIZE       10  /* must be even. */

struct teash_memory_s {
    uint8_t *mem_start;
    uint8_t *script_end;
    uint8_t *dict_start;
    uint8_t *dict_end;
    uint16_t *RS;
    int32_t *vars;
    uint8_t *mem_end;
};

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
    teash_cmd_t *root;


    char *LP;
};

teash_state_t teash_state;

/**
 * \breif How many bytes left for script or dict?
 */
#define teash_has_free(teash) ((teash)->mem.dict_start - (teash)->mem.script_end)

int teash_init_memory(uint8_t *memory, unsigned size, struct teash_memory_s *mem)
{
    if( size <= (sizeof(uint32_t)*26)+(sizeof(uint16_t)*TEASH_RS_SIZE) )
        return -1;

    if( (uint32_t)memory & 0x3UL ) /* Start isn't aligned */
        return -2;
    if( (uint32_t)(memory+size) & 0x3UL ) /* End isn't alined */
        return -3;

    mem->mem_start = memory;
    mem->script_end = memory;
    mem->mem_end = memory + size;

    mem->vars = (int32_t*)(mem->mem_end - sizeof(int32_t)*26);
    mem->dict_end = mem->mem_end - (sizeof(int32_t)*26+sizeof(uint16_t)*TEASH_RS_SIZE);
    mem->RS = (uint16_t*)mem->dict_end;
    mem->dict_start = mem->dict_end;

    return 0;
}

/*****************************************************************************/
int teash_clear_script(int argc, char **argv, teash_state_t *teash)
{
    /* where is teash_state_t??? Assuming single global for now. */
    teash->mem.script_end = teash->mem.mem_start;
    return 0;
}

int teash_run_script(int argc, char **argv, teash_state_t *teash)
{
    if( teash->LP != NULL ) return -1; /* already running */

    /* first line is three bytes in. */
    teash->LP = teash->mem.mem_start + 2;

    return 0;
}

char* teash_find_line(uint16_t ln, teash_state_t *teash); // TODO put prototypes somewhere.
/**
 * \brief jump to specific line
 *
 * This jumps to the next equal-or-greater line. (or to the end.)
 *
 * FE: if the script only has lines 10 and 20, and sees a "goto 15", it will
 * jump to line 20.  If it sees a "goto 30", then it stops. (it jumped off
 * the end of the script.)
 *
 * TODO Test this
 */
int teash_goto_line(int argc, char **argv, teash_state_t *teash)
{
    uint16_t tln;
    int ln;

    if( argc != 1 ) return -1;

    ln = strtoul(argv[1], NULL, 0);
    teash->LP = teash_find_line(ln, teash);
    return 0;
}


/****************************************************************************/
#if 0
/* This implementation of let is broken.
 * It cannot handle spanning strings, and it needs to.
 */
int teash_let_expr1(char **p);
int teash_let_expr4(char **p)
{
    int base = 10;
    int a, b;
    if( **p >= '0' && **p <= '9' ) {
        if( **p == '0' ) {
            (*p)++;
            if( **p == 'x' ) base = 16;
            else if( **p == 'o' ) base = 8;
            else if( **p == 'b' ) base = 2;
            (*p)++;
        }
        a = 0;

        for(; **p != '\0'; (*p)++ ) {
            b = **p;
            if( b >= '0' && b <= '9' ) b -= '0';
            else if( b >= 'a' && b <= 'z' ) b -= 'a' + 10;
            else if( b >= 'A' && b <= 'Z' ) b -= 'A' + 10;
            else break;
            if( b >= base ) break;
            a *= base;
            a += b;
        }

        return a;
    }
    if( **p >= 'A' && **p <= 'Z' ) {
        a = teash_state.mem.vars[ 'A' - (**p) ];
        (*p)++;
        return a;
    }
    if( **p == '@' ) {
        (*p)++;
        if( **p == 'c' ) {
            (*p)++;
            a = teash_let_expr4(p);
            a = *((char*)a);
        } else {
            a = teash_let_expr4(p);
            a &= ~0x3; /* Force alignment */
            a = *((int*)a);
        }
        return a;
    }
    if( **p == '(' ) {
        (*p)++;
        a = teash_let_expr1(p);
        if( **p == ')' ) {
            (*p)++;
        }
        return a;
    }
    return 0;
}
int teash_let_expr3(char **p)
{
    int a,b;
    a = teash_let_expr4(p);
    while(1) {
        if( **p == '*' ) {
            (*p)++;
            b = teash_let_expr4(p);
            a *= b;
        } else if( **p == '/' ) {
            (*p)++;
            b = teash_let_expr4(p);
            if(b==0) return 0;
            a /= b;
        } else {
            return a;
        }
    }
}
int teash_let_expr2(char **p)
{
    int a,b;
    if( **p == '-' || **p == '+' )
        a = 0;
    else
        a = teash_let_expr3(p);
    while(1) {
        if( **p == '-' ) {
            (*p)++;
            b = teash_let_expr3(p);
            a -= b;
        } else if( **p == '+' ) {
            (*p)++;
            b = teash_let_expr3(p);
            a += b;
        } else {
            return a;
        }
    }
}
int teash_let_expr1(char **p)
{
    int a,b;

    a = teash_let_expr2(p);
        if( **p == '>' ) {
            (*p)++;
            if( **p == '=' ) {
                (*p)++;
                b = teash_let_expr2(p);
            } else {
            }
        }
    return a;
}
/* This implementation of let is broken.
 * It cannot handle spanning strings, and it needs to.
 */
int teash_let(int argc, char **argv, teash_state_t *teash)
{
    char *p;
    int a;
    int setidx = -1;

    /* drop "let" */
    argc--, argv++;
    
    /* process exressions */
    for(; argc > 0; argc--, argv++) {
        p = *argv;

        /* best way to do a set? no */
        if( *p >= 'A' && *p <= 'Z' && *(p+1) == '=' ) {
            setidx = 'A' - *p;
            p+=2;
        }

        a = teash_let_expr1(&p);

        if( setidx > -1 ) {
            teash->mem.vars[setidx] = a;
        }
    }

    return a;
}
#endif
int teash_let(int argc, char **argv, teash_state_t *teash)
{
    char *p;
    int es=0;
    /* drop "let" */
    argc--, argv++;
    
    /* process exressions */
    for(; argc > 0; argc--, argv++) {
        p = *argv;

    }

    return 0;
}
/****************************************************************************/

/**
 * \brief if test is not zero, then exec rest of line
 *
 * if "A > B" goto 16
 * if A>B goto 16
 *
 * TODO Test this
 *
 */
int teash_if(int argc, char **argv, teash_state_t *teash)
{
    int ret=0;
    if( argc < 3 ) return -1;

    argv[0] = "let";
    ret = teash_let(2, argv, teash);

    if( ret == 0 ) return 0;

    return teash_exec(argc-2, argv+2, teash);
}

/**
 * \brief Skip the next line if not zero
 * TODO Test this
 */
int teash_skip(int argc, char **argv, teash_state_t *teash)
{
    if( argc < 2 ) return -1;
    argv[0] = "let";
    if( teash_let(argc, argv, teash) == 0 ) return 0;

    if( teash->LP == NULL ) return 0;

    /* Find next line */
    teash->LP += strlen(teash->LP) + 3;
    if( teash->LP >= (char*)teash->mem.script_end)
        teash->LP == NULL;

    return 0;
}

/**
 * \brief List what is in the script space.
 */
int teash_list(int argc, char **argv, teash_state_t *teash)
{
    uint8_t *p;
    uint16_t ln;
    for(p = teash->mem.mem_start; p < teash->mem.script_end; ) {
        ln = *p++;
        ln <<= 8;
        ln |= *p++;
        printf("%5u %s\n", ln, p);
        p += strlen(p) + 1;
    }
    return 0;
}

/*****************************************************************************/
teash_cmd_t teash_root_commands[] = {
    { "clear", teash_clear_script, NULL },
    { "run", teash_run_script, NULL },
    { "goto", teash_goto_line, NULL },
    { "let", teash_let, NULL },
    { "if", teash_if, NULL },
    { "skip", teash_skip, NULL },
    { "list", teash_list, NULL },

    { NULL, NULL, NULL }
};

/*****************************************************************************/

/**
 * \brief Find a line (or the next one if not exact)
 *
 * Finds a line or the next following.  If looking for line 22, but only
 * lines 20 and 25 exist, will return line 25.
 *
 */
char* teash_find_line(uint16_t ln, teash_state_t *teash)
{
    uint8_t *p = teash->mem.mem_start;
    uint16_t tln;

    while(p < teash->mem.script_end) {
        tln = *p++;
        tln <<=8;
        tln |= *p++;

        if( tln >= ln )
            return p;

        tln = strlen(p) + 1;
        p += tln;
    }

    return NULL;
}

/**
 * \breif load a new line into the script space
 *
 * This keeps the script space sorted by line number. Inserting and replacing
 * as needed.
 */
int teash_load_line(uint16_t ln, char *newline, teash_state_t *teash)
{
    uint8_t *oldline = NULL;
    uint16_t tln;
    int oldlen=0;
    int newlen = strlen(newline) + 3;

    if(newlen == 3) /* oh, actually is deleting the whole line. */
        newlen = 0;

    /* set oldline to where we want to insert. */
    for(oldline = teash->mem.mem_start;
        oldline < teash->mem.script_end; ) {
        tln  = (*oldline++) << 8;
        tln |= *oldline++;
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
        *oldline++ = (ln >> 8)&0xff;
        *oldline++ = ln & 0xff;
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

    /* FIXME or verify: params passed must include command name as argv[0].
     * For nested commands, only the right most is passed in.
     * So for a cmd "spi flash dump 256 32" argv[0] is "dump"
     *
     * XXX Test nested commands.
     */
    for(; current->name != NULL; current++) {
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
                continue; /* Does this skip the current++ ??  it MUST */
            }
        }
    }

    /* No command to call at deepest find, backtrack up to see if we missed
     * one.
     *
     * This is to handle the following:
     * - Have tree with commands defined at the following points:
     *   A
     *   A B
     *   A B C D
     * - The command "A B C" is executed.
     * - So a depth search we find node C, but there are no commands there,
     *   so we need to backtrack to B.
     */
    for(ac--; ac > 0; ac--) {
        if( parents[ac]->cmd ) {
            return parents[ac]->cmd((argc-ac), &argv[ac], teash);
        }
    }

    /* No commands anywhere to run what was requested. */
    return -1;
}

char* teash_itoa(int i, char *b, unsigned max)
{
    char tb[12];
    char *t = tb;
    char sign = '+';

    /* check sign */
    if( i < 0 ) {
        sign = '-';
        i = -i;
    }

    /* ascii-fy (backwards.) */
    do {
        *t = (i%10) - '0';
        i /= 10;
    }while(i>0);

    if(sign == '-')
        *b++ = '-';
    for(; t > tb && max > 0; max--) {
        *b++ = *t--;
    }
    *b++ = '\0';
    return b;
}

/**
 * \brief Find the $vars and replace them
 *
 * FIXME work in place.
 */
int teash_subst(char *in, char *out, teash_state_t *teash)
{
    char *varname;
    int varlen;

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
                    for(varlen=0; !isalnum(*in) && *in != '\0'; in++, varlen++) {}
                }
                /* Now look up variable */
                if( varlen == 1 && isupper(*varname) ) {
                    /* Number variable. grab is and ascii-fy it */
                    /* FIXME needs work */
                    out = teash_itoa(teash->mem.vars['A' - *varname], out, 99);
#if 0
                } else if( dict ) {
                    /* Is it in the dictionary? */
#endif
                } else {
                    /* not found, copy as is ?or nothing? */
                    *out++ = '$';
                    for(; varlen > 0; varlen--) {
                        *out++ = *in++;
                    }
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
    /* FIXME change line to be in place modifiable */
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
        *p = '\0';
    }

    return teash_exec(argc, argv, teash);
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

    /* is first work a number? */
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
            teash->LP += strlen(teash->LP) + 3;
            if( teash->LP >= (char*)teash->mem.script_end)
                teash->LP = NULL;

            /* eval this line */
            teash_eval(line, teash);

        }
    }

    return 0;
}


#ifdef TEST_IT
uint8_t test_memory[4096];

int main(int argc, char **argv)
{
    teash_init_memory(test_memory, sizeof(test_memory), &teash_state.mem);
    teash_state.root = teash_root_commands;

    return teash_mloop(&teash_state);
}
#endif

/* vim: set ai cin et sw=4 ts=4 : */
