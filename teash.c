
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
 * - teash_mem_start
 *   fixed
 * - teash_script_end
 *   moving
 * - teash_dict_start
 *   moving
 * - teash_dict_end
 *   fixed
 *   aka bottom of return stack
 * - teash_rs
 *   moving
 * - teash_rs_end
 *   fixed
 * - teash_num_vars
 *   fixed
 * - teash_mem_end
 *   fixed
 *
 * Free memory is (teash_dict_start - teash_sript_end)
 *
 */

#define TEASH_LINE_MAX      80
#define TEASH_PARAM_MAX     10
#define TEASH_CMD_DEPTH_MAX 10

struct teash_memory_s {
    uint8_t *mem_start;
    uint8_t *script_end;
    uint8_t *dict_start;
    uint8_t *dict_end;
    uint8_t *RS;
    uint32_t *vars;
    uint8_t *mem_end;
};

typedef int(*teash_f)(int,char**);

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
typedef struct teash_state_s teash_state_t;


teash_state_t teash_state;

/**
 * \breif How many bytes left for script or dict?
 */
#define teash_has_free(teash) ((teash)->mem.dict_start - (teash)->mem.script_end)

/*****************************************************************************/
int teash_clear_script(int argc, char **argv)
{
    /* where is teash_state_t??? Assuming single global for now. */
    teash_state.mem.script_end = teash_state.mem.mem_start;
    return 0;
}

int teash_run_script(int argc, char **argv)
{
    if( teash_state.LP != NULL ) return -1; /* already running */

    /* first line is three bytes in. */
    teash_state.LP = teash_state.mem.mem_start + 2;

    return 0;
}

/*****************************************************************************/
teash_cmd_t teash_root_commands[] = {
#if 0
    { "clear", teash_clear_script, NULL },
    { "run", teash_run_script, NULL },
    { "let", teash_let, NULL },
    { "goto", teash_goto_line, NULL },
    { "if", teash_if, NULL },
#endif

    { NULL, NULL, NULL }
};

/*****************************************************************************/

char* teash_find_line(uint16_t ln, teash_state_t *teash)
{
    uint8_t *p = teash->mem.mem_start;
    uint16_t tln;

    while(p < teash->mem.script_end) {
        tln = *p++;
        tln <<=8;
        tln |= *p++;

        if( tln == ln )
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

    /* set oldline to where we want to insert. */
    for(oldline = teash->mem.mem_start;
        oldline < teash->mem.script_end; ) {
        tln  = (*oldline++) << 8;
        tln |= *oldline++;
        if( tln < ln ) {
            /* inserting a new line */
            oldlen = 0;
            break;
        } else if( tln == ln ) {
            /* replacing an old line */
            oldlen = strlen(oldline) + 3;
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

    for(; current->name != NULL; current++) {
        if( strcmp(current->name, argv[ac]) == 0) {
            /* Matched name. */
            if( current->sub == NULL || (argc-ac) == 1) {
                /* Cannot go deeper, try to call */
                if( current->cmd ) {
                    return current->cmd((argc-ac), &argv[ac]);
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
     */
    for(ac--; ac > 0; ac--) {
        if( parents[ac]->cmd ) {
            return parents[ac]->cmd((argc-ac), &argv[ac]);
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
        *t = '0' - (i%10);
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
                        for(end=buf; *end != '\0'; end++) {}
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
        *p++ = '\0';
    }

    return teash_exec(argc, argv, teash);
}

/** 
 * \breif take a line from user input and figure out wht to do
 *
 * Lines are ether executed or loaded into script memory.
 */
int teash_do_line(char *line, teash_state_t *teash)
{
    char *p = line;
    int ln = 0;

    /* skip whitespace */
    for(; isspace(*p) && *p != '\0'; p++) {}
    if( *p == '\0' ) return 0;

    /* is first work a number? */
    for(; isdigit(*p) && *p != '\0'; p++) {
        ln *= 10;
        ln += '0' - *p;
    }
    if( isspace(*p) ) {
        return teash_load_line(ln, p, teash);
    }
    return teash_eval(line, teash);
}

int teash_mloop(teash_state_t *teash)
{
    char line[TEASH_LINE_MAX+1];

    while(1) {
        if( teash->LP == NULL ) {
            printf("> ");
            fflush(stdout);
            fgets(line, sizeof(line), stdin);
            teash_do_line(line, teash);
        } else {
            strncpy(line, teash->LP, TEASH_LINE_MAX);
            line[TEASH_LINE_MAX] = '\0';

            /* Set up LP for the next line after this one */
            teash->LP += strlen(teash->LP) + 3;
            if( teash->LP >= (char*)teash->mem.script_end)
                teash->LP == NULL;

            /* eval this line */
            teash_eval(line, teash);

        }
    }

    return 0;
}

