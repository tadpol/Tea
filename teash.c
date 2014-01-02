#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

/*
 * I keep getting lost in making a language, when much of what I want is a shell.
 * So maybe if I start making a shell first, I'll get what I want?
 *
 * - History: Couple of lines is probably enough. (many times seems like one would be)
 * - Line Editing: Not having to backspace all of it to change the first word.
 * - History recall for editing: Killer Feature.
 *
 */

#define TEASH_LINE_BUFFER_SIZE  81 /* 80 characters and a '\0' at the end */
#define TEASH_HISTORY_DEPTH     5
#define TEASH_PARAM_MAX         10

#define teash_status_unused_0   (1<<0)
#define teash_status_show_abi   (1<<1)
#define teash_status_in_hex     (1<<2)
#define teash_status_overwrite  (1<<3)
#define teash_status_unused_2   (1<<4)
#define teash_status_unused_3   (1<<5)
#define teash_status_unused_4   (1<<6)
#define teash_status_unused_5   (1<<7)
#define teash_status_vars_err   (1<<8)
#define teash_status_unused_6   (1<<9)
#define teash_status_unused_7   (1<<10)
#define teash_status_unused_8   (1<<11)
#define teash_status_event_0    (1<<12)
#define teash_status_event_1    (1<<13)
#define teash_status_event_2    (1<<14)
#define teash_status_event_3    (1<<15)

typedef int(*teash_f)(int,char**);
typedef struct teash_cmd_s teash_cmd_t;
struct teash_cmd_s {
    char *name;
    teash_f cmd;
    teash_cmd_t *sub;
};

#define TEASH_VARS_CNT  5
struct teash_state_s {
    int historyIdx;
    char history[TEASH_HISTORY_DEPTH][TEASH_LINE_BUFFER_SIZE];
    char line[TEASH_LINE_BUFFER_SIZE];
    uint8_t lineIdx;
    char esc_sbuf[25];
    uint8_t escIdx;

    uint8_t screen_height;

    int vars[TEASH_VARS_CNT];

    teash_cmd_t *root;
} teash_state = {
    .historyIdx = 0,
    .lineIdx = 0,
    .escIdx = 0,
    .screen_height = 24,
    .vars = {0,0,0,0,0},
    .root = NULL,
};

/*****************************************************************************/

static const char teash_statusBitMap[16] = "_axo____V___0123";

/*****************************************************************************/
/**
 * \brief Get the index offset from a variable name
 * \param[in] var Variable name
 * \returns int Array index to var or -1 for not found
 *
 * Variable 'S' is the status bits. The bits have meaning and get updated for
 * various things as the system runs.
 *
 * Variable 'R' is the return value of the last execed command.
 *
 * Remaining variables are just values.
 */
int teash_var_name_to_index(int var)
{
    const char varnames[TEASH_VARS_CNT+1] = "SRABI";
    int i;
    for(i=0; varnames[i] != '\0'; i++) {
        if(varnames[i] == var) {
            return i;
        }
    }
    return -1;
}

int teash_isvar(int var)
{
    return teash_var_name_to_index(var) >= 0;
}

void teash_var_status_toggle(int bits)
{
    /* Status is always index 0 */
    teash_state.vars[0] ^= bits;
}

void teash_var_status_set(int bits)
{
    /* Status is always index 0 */
    teash_state.vars[0] = bits;
}

int teash_var_status_get(void)
{
    /* Status is always index 0 */
    return teash_state.vars[0];
}

int teash_var_status_test(int bits)
{
    return (teash_state.vars[0] & bits) == bits;
}

int teash_var_get(int var)
{
    int idx = teash_var_name_to_index(var);
    if(idx < 0 || idx >= TEASH_VARS_CNT) {
        teash_var_status_set(teash_status_vars_err);
        return 0;
    }
    return teash_state.vars[idx];
}

int teash_var_set(int var, int value)
{
    int idx = teash_var_name_to_index(var);
    if(idx < 0 || idx >= TEASH_VARS_CNT) {
        teash_var_status_set(teash_status_vars_err);
        return 0;
    }
    return teash_state.vars[idx] = value;
}

/*****************************************************************************/

/**
 * \breif Search for a command, and call it when found.
 */
int teash_exec(int argc, char **argv)
{
    int ac = 0;
    teash_cmd_t *current = teash_state.root;

    /* params passed must include command name as argv[0].
     * For nested commands, only the right most is passed in.
     * So for a cmd "spi flash dump 256 32" argv[0] is "dump"
     */
    for(; current != NULL && current->name != NULL; ) {
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
                current = current->sub;
                continue;
            }
        }
        current++;
    }

    /* No commands anywhere to run what was requested. */
    return -1;
}

//#define INLINE_SUBST
#ifdef INLINE_SUBST
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
int teash_subst(char *b, char *be)
{
    /* First slide buffer to back */
    unsigned l = be-(b+strlen(b));
    memmove(b+l, b, l);

    char *in = b+l;

    /* Now work over as if two buffers. */
    for(; *in != '\0' && b < in; in++, b++) {
        if( *in != '$' ) {
            *b = *in;
        } else {
            in++;
            if( *in == '$' ) {
                *b = '$';
            } else if( teash_isvar(*in) ) {
                /* Number variable. grab it and ascii-fy it */
                b = teash_itoa(teash_var_get(*in), b, in-b);
                b--;
            }
        }
    }

    *b = '\0';
    return 0;
}
#endif
/**
 * \brief take a line, do subs, and break it into params.
 * \param[in,out] line The string to parse. This is modified heavily.
 */
void teash_eval(char *line)
{
    char *argv[TEASH_PARAM_MAX+1];
    char *end=NULL;
    int argc;

#ifdef INLINE_SUBST
    /* do substitutions */
    teash_subst(line, line+TEASH_LINE_BUFFER_SIZE); // FIXME Line End is in wrong place.
#endif

    /* Break up into parameters */
    for(argc=0; *line != '\0'; line++) {
        /* skip white space */
        for(; isspace(*line) && *line != '\0'; line++) {}
        if( *line == '\0' ) break;

        if( *line == '"' ) {
            line++; /* skip over the quote */
            argv[argc++] = line;
            for(; *line != '"' && *line != '\0'; line++) {
                if( *line == '\\' ) {
                    if( end == NULL ) {
                        /* We only get the end if we need it.
                         * But once we have it, we don't need to get it
                         * again.
                         */
                        for(end=line; *end != '\0'; end++) {}
                    }
                    /* slide all on down */
                    memmove(line, line+1, end-line);
                    end --;
                }
            }
        } else {
            argv[argc++] = line;
            /* find end */
            for(; !isspace(*line) && *line != '\0'; line++) {}
        }
        if( *line == '\0' ) break;
        *line = '\0';
    }

    teash_var_set('R', teash_exec(argc, argv));
}

/*****************************************************************************/
void teash_history_push(void)
{
    strcpy(teash_state.history[teash_state.historyIdx], teash_state.line);
    teash_state.historyIdx++;
    if(teash_state.historyIdx >= TEASH_HISTORY_DEPTH) {
        teash_state.historyIdx = 0;
    }
}

void teash_history_load(int idx)
{
    idx = (teash_state.historyIdx + idx) % TEASH_HISTORY_DEPTH;
    if(idx < 0) idx += TEASH_HISTORY_DEPTH;
    strcpy(teash_state.line, teash_state.history[idx]);
    teash_state.lineIdx = strlen(teash_state.line);
}

/*****************************************************************************/
/**
 * \brief update status line
 */
void teash_update_status(void)
{
    int i, s = teash_var_status_get();;
    char *numfmt = "%10d";
    if(s & teash_status_in_hex) {
        numfmt = "0x%08x";
    }

    printf("\x1b[%u;0f", teash_state.screen_height-1); /* move to status line */

    /* Disaply status bits */
    putchar('[');
    for(i=0; i < 16; i++) {
        if(s & (1<<i)) {
            putchar(teash_statusBitMap[i]);
        } else {
            putchar('-');
        }
    }
    putchar(']');

    /* Display result code */
    printf(" R:");
    printf(numfmt, teash_var_get('R'));

    /* Display Other vars */
    if(s & teash_status_show_abi) {
        /* Display A */
        printf(" A:");
        printf(numfmt, teash_var_get('A'));

        /* Display B */
        printf(" B:");
        printf(numfmt, teash_var_get('B'));

        /* Display I */
        printf(" I:");
        printf(numfmt, teash_var_get('I'));
    } else {
        printf("\x1b[K"); /* erase to end of line */
    }

    printf("\x1b[u"); /* put cursor back */
}

/**
 * \brief Redraw the entire current line.
 */
void teash_update_cmd(void)
{
    printf("\x1b[%u;0f", teash_state.screen_height); /* move to cmd line */
    printf("%s", teash_state.line);
    printf("\x1b[s"); /* Save cursor */
    printf("\x1b[K"); /* erase to end of line */
}

/**
 * \brief Evaluate a VT100 escape sequence.
 */
void teash_esc_eval(void)
{
    int a, b;
    if(strcmp(teash_state.esc_sbuf, "[A")==0) { /* Cursor up */
        /* Replace editing line with prev line. */
        teash_history_load(-1);
        teash_update_cmd();

    } else if(strcmp(teash_state.esc_sbuf, "[B")==0) { /* Cursor down */
        /* Replace editing line with Next line. */
        teash_history_load(1);
        teash_update_cmd();

    } else if(strcmp(teash_state.esc_sbuf, "[C")==0) { /* Cursor forward */
        if(teash_state.line[teash_state.lineIdx] != '\0') {
            teash_state.lineIdx++;
            printf("\x1b[C"); /* Move right */
            printf("\x1b[s"); /* Save cursor */
        }

    } else if(strcmp(teash_state.esc_sbuf, "[D")==0) { /* Cursor backward */
        if(teash_state.lineIdx > 0) {
            teash_state.lineIdx--;
            printf("\x1b[D"); /* Move left */
            printf("\x1b[s"); /* Save cursor */
        }

    } else if(strcmp(teash_state.esc_sbuf, "[2~")==0) { /* Insert key */
        teash_var_status_toggle(teash_status_overwrite);
        teash_update_status();

    } else if(sscanf(teash_state.esc_sbuf, "[%u;%uR", &a, &b) == 2) {
        /* To make this work, at start send:
         *  \x1b[999,999f
         *  \x1b[6n
         * Then we'll get the response with the size of the screen.
         */
        teash_state.screen_height = a;
        printf("\x1b[0;%ur", teash_state.screen_height - 2);/* Scrolling region is top lines */
        printf("\x1b[%u;%uf", teash_state.screen_height, teash_state.lineIdx); /* Put cursor at edit line */
        printf("\x1b[s"); /* Save cursor */

        /* redraw status, since it might have moved. */
        teash_update_status();
    }
}

/* A new character input.
 */
void teash_inchar(int c)
{
    static bool inesc = false;

    if(!inesc) {
        if(c == 0x1b) {
            inesc = true;
            teash_state.escIdx = 0;
        } else if(c == '\b') {
            printf("\b \b");
            printf("\x1b[s"); /* Save cursor */

            if(teash_state.line[teash_state.lineIdx--] == '\0') {
                /* at the end */
                teash_state.line[teash_state.lineIdx] = '\0';
            } else {
                /* In middle */
                int left = (TEASH_LINE_BUFFER_SIZE - teash_state.lineIdx) - 1;
                memmove(&teash_state.line[teash_state.lineIdx], &teash_state.line[teash_state.lineIdx+1], left);
                printf("%s", &teash_state.line[teash_state.lineIdx]); /* rewrite the moved chars */
                printf("\x1b[K"); /* erase to end of line */
                printf("\x1b[u"); /* put cursor back */
            }

        } else if(c == '\n' || c == '\r') {
            teash_history_push();
            teash_eval(teash_state.line);
            teash_state.lineIdx = 0;
            teash_state.line[0] = '\0';
            teash_update_cmd();
            teash_update_status();
        } else if(teash_state.lineIdx >= TEASH_LINE_BUFFER_SIZE) {
            /* do nothing */
        } else {
            /* Insert or overwrite */
            if(teash_state.line[teash_state.lineIdx] == '\0') {
                /* Append to the end */
                teash_state.line[teash_state.lineIdx++] = c;
                teash_state.line[teash_state.lineIdx] = '\0';
                putchar(c);
                printf("\x1b[s"); /* Save cursor */
            } else if((teash_var_status_get() & teash_status_overwrite)) {
                /* Overwriting in the middle */
                teash_state.line[teash_state.lineIdx++] = c;
                putchar(c);
                printf("\x1b[s"); /* Save cursor */

            } else {
                /* Inserting in the middle of a line */
                /* So make room */
                int left = (TEASH_LINE_BUFFER_SIZE - teash_state.lineIdx) - 1;
                memmove(&teash_state.line[teash_state.lineIdx+1], &teash_state.line[teash_state.lineIdx], left);
                teash_state.line[TEASH_LINE_BUFFER_SIZE-1] = '\0'; /* jic */
                teash_state.line[teash_state.lineIdx++] = c;
                putchar(c);
                printf("\x1b[s"); /* Save cursor */
                printf("%s", &teash_state.line[teash_state.lineIdx]); /* rewrite the moved chars */
                printf("\x1b[u"); /* put cursor back */
            }
        }
    } else { /* In esc sequence */
        teash_state.esc_sbuf[teash_state.escIdx++] = c;
        if(isalpha(c) || c == '~') {
            teash_state.esc_sbuf[teash_state.escIdx++] = '\0';
            inesc = false;
            teash_esc_eval();
        }
    }
}

/*****************************************************************************/

void teash_init(teash_cmd_t *commands)
{
    teash_state.root = commands;

    printf("\x1b[2J"); /* Clear Screen */
    printf("\x1b[7l"); /* Disable line wrapping */
    printf("\x1b[999;999f"); /* Put cursor somewhere hopefully off the screen */
    printf("\x1b[6n"); /* ask terminal where cursor ended up */
}

/*****************************************************************************/
#ifdef TEST_IT
int main(int argc, char **argv)
{
    int c;

    while((c=getchar()) != EOF) {
        teash_inchar(c);
    }


    return 0;
}
#endif


/* vim: set ai cin et sw=4 ts=4 : */
