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

#define TEASH_LINE_BUFFER_SIZE  80
#define TEASH_HISTORY_DEPTH     5
#define TEASH_PARAM_MAX         10

#define teash_status_run        (1<<0)
#define teash_status_math_err   (1<<1)
#define teash_status_vars_err   (1<<2)

typedef int(*teash_f)(int,char**);
typedef struct teash_cmd_s teash_cmd_t;
struct teash_cmd_s {
    char *name;
    teash_f cmd;
    teash_cmd_t *sub;
};

struct teash_state_s {
    int historyIdx;
    char history[TEASH_HISTORY_DEPTH][TEASH_LINE_BUFFER_SIZE];
    char line[TEASH_LINE_BUFFER_SIZE];
    uint8_t lineIdx;
    char esc_sbuf[25];
    uint8_t escIdx;

    uint16_t status;
    uint8_t screen_height;

    int vars[8];
    char script[1024];
    char *script_end;
    char *LP;

    teash_cmd_t *root;
} teash_state = {
    .historyIdx = 0,
    .lineIdx = 0,
    .escIdx = 0,
    .status = 0,
    .screen_height = 24,
    .script_end = NULL,
    .LP = NULL,
    .root = NULL,
};

/*****************************************************************************/


/*****************************************************************************/
int teash_var_name_to_index(int var)
{
    const char varnames[] = "ABCDRXYZ";
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

int teash_var_get(int var)
{
    int idx = teash_var_name_to_index(var);
    if(idx < 0) {
        teash_state.status |= teash_status_vars_err;
        return 0;
    }
    return teash_state.vars[idx];
}

int teash_var_set(int var, int value)
{
    int idx = teash_var_name_to_index(var);
    if(idx < 0) {
        teash_state.status |= teash_status_vars_err;
        return 0;
    }
    return teash_state.vars[idx] = value;
}

/*****************************************************************************/
char *teash_math(char *p)
{
    long int st[10];
    long int *sp = st;
    long int a, b, pushback=1, adjust=-1;

    for(;*p != '\0' && *p != ']';p++) {
        a = *sp;
        b = *(sp-1);

        if(*p >= '0' && *p <= '9') {
            uint8_t base = 10;
            if(*(p+1) == 'x') {
                base = 16;
                p += 2;
            } else if(*(p+1) == 'b') {
                base = 2;
                p += 2;
            }
            a=0;
            for(; *p != '\0'; p++) {
                b = *p;
                if( b >= '0' && b <= '9' ) b -= '0';
                else if( b >= 'a' && b <= 'z') b = (b - 'a') + 10;
                else if( b >= 'A' && b <= 'Z') b = (b - 'A') + 10;
                else break;
                if( b >= base ) break;
                a *= base;
                a += b;
            }
            adjust = 1;
        } else if(teash_isvar(*p)) {
            /* variable lookup */
            adjust = 1;
            a = (int)&teash_state.vars[teash_var_name_to_index(*p)];
        } else if(*p == 'S') { /* Read status */
            a = teash_state.status;
            adjust = 1;
        } else if(*p == 'x') {
            pushback = 0;
        } else if(*p == '+') {
            a = b + a;
        } else if(*p == '-') {
            a = b - a;
        } else if(*p == '*') {
            a = b * a;
        } else if(*p == '/') {
            if(a == 0) {
                teash_state.status |= teash_status_math_err;
                return NULL;
            }
            a = b / a;
        } else if(*p == '%') {
            a = b % a;
        } else if(*p == '|') {
            a = b | a;
        } else if(*p == '&') {
            a = b & a;
        } else if(*p == '^') {
            a = b ^ a;
        } else if(*p == '~') {
            a = ~ a;
            adjust = 0;

        } else if( *p == '=' ) { // Test equal to ( a b -- a==b )
            a = a == b;
        } else if( *p == '>' ) {
            p++;
            if( *p == '>' ) { // Bit shift right ( a b -- b>>a )
                a = b >> a;
            } else if( *p == '=' ) { // Test Greater than equalto ( a b -- b>=a )
                a = b >= a;
            } else { // Test Greater than ( a b -- b>a )
                a = b > a;
                p--;
            }
        } else if( *p == '<' ) {
            p++;
            if( *p == '<' ) { // Bit shift left ( a b -- b<<a )
                a = b << a;
            } else if( *p == '=' ) { // Test Less Than equalto ( a b -- b<=a )
                a = b <= a;
            } else if( *p == '>' ) { // Test not equal to ( a b -- a<>b )
                a = a != b;
            } else { // Test Less Than ( a b -- b<a )
                a = b < a;
                p--;
            }

        } else if(*p == '@') {
            p++;
            adjust = 0;
            if(*p == 'c') {
                a = *((uint8_t*)a);
            } else if(*p == 's') {
                a = *((uint16_t*)a);
            } else if(*p == 'i') {
                a = *((uint32_t*)a);
            } else {
                a = *((int*)a);
                --p;
            }
        } else if(*p == '!') {
            p++;
            adjust = -2;
            if(*p == 'c') {
                *((uint8_t*)a) = b;
            } else if(*p == 's') {
                *((uint16_t*)a) = b;
            } else if(*p == 'i') {
                *((uint32_t*)a) = b;
            } else if(*p == '+') {
                (*((int*)a)) ++;
            } else if(*p == '-') {
                (*((int*)a)) --;
            } else {
                *((int*)a) = b;
                --p;
            }
        } else {
            adjust = 0;
            pushback = 0;
        }
        if(((sp+adjust) < (st-1)) || ((sp+adjust) >= (st + 10))) {
            teash_state.status |= teash_status_math_err;
            return NULL;
        }
        sp += adjust;
        switch(pushback) {
            case 2: *(sp-1) = b;
            case 1: *sp = a;
            default:
                break;
        }
    }

    if(*sp == 0) return NULL; /* math result is FALSE, do not eval rest of p. */

    if(*p == ']') p++;
    for(; isspace(*p) && *p != '\0'; p++) {} /* skip whitespace */
    return p;
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

/**
 * \brief take a line, do subs, and break it into params.
 * \param[in,out] line The string to parse. This is modified heavily.
 */
void teash_eval(char *line)
{
    char *argv[TEASH_PARAM_MAX+1];
    char *end=NULL;
    int argc;

    if(*line == '[') {
        /* line is prefixed with a math test. */
        line = teash_math(line);
        if(line == NULL || *line == '\0') return;
    }

    /* do substitutions */
    teash_subst(line, line+TEASH_LINE_BUFFER_SIZE); // FIXME End is in wrong place.

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
    idx = (teash_state.historyIdx + idx) % TEASH_HISTORY_DEPTH; // FIXME this is likely wrong
    strcpy(teash_state.line, teash_state.history[idx]);
    teash_state.lineIdx = strlen(teash_state.line);
}

/*****************************************************************************/
/**
 * \brief Goto a line (or the next one if not exact)
 *
 * Finds a line or the next following.  If looking for line 22, but only
 * lines 20 and 25 exist, will return line 25.
 *
 */
void teash_goto_line(uint16_t ln)
{
    char *p = teash_state.script;
    uint16_t tln;

    while(p < teash_state.script_end) {
        tln = *(uint8_t*)p++;
        tln <<=8;
        tln |= *(uint8_t*)p++;

        if( tln >= ln ) {
            teash_state.LP = p;
            return;
        }

        tln = strlen(p) + 1;
        p += tln;
    }

    teash_state.LP = NULL;
}

/**
 * \brief Jump to the next line in the script.
 */
void teash_next_line(void)
{
    teash_state.LP += strlen(teash_state.LP) + 3;
    if( teash_state.LP >= teash_state.script_end)
        teash_state.LP = NULL;
}

/**
 * \brief How much free script space is there?
 */
int teash_has_free(void)
{
    return (teash_state.script + sizeof(teash_state.script)) - teash_state.script_end;
}

/**
 * \breif load a new line into the script space
 *
 * This keeps the script space sorted by line number. Inserting and replacing
 * as needed.
 */
int teash_load_line(uint16_t ln, char *newline)
{
    char *oldline = NULL;
    uint16_t tln;
    int oldlen=0;
    int newlen = strlen(newline) + 3;

    if(newlen == 3) /* oh, actually is deleting the whole line. */
        newlen = 0;

    /* set oldline to where we want to insert. */
    for(oldline = teash_state.script; oldline < teash_state.script_end; ) {
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
    if( oldline >= teash_state.script_end ) {
        /* at the end, so just append. */
        oldline = teash_state.script_end;
        if( teash_has_free() < newlen) 
            return -2;
        teash_state.script_end += newlen;
    } else if( oldlen < newlen ) {
        /* growing */
        if( teash_has_free() < newlen-oldlen) 
            return -2;

        memmove(oldline+newlen, oldline+oldlen, teash_state.script_end - oldline+oldlen);

        teash_state.script_end += newlen-oldlen; /* grew by this much */
    } else if( oldlen > newlen ) {
        /* shrinking */
        memmove(oldline+newlen, oldline+oldlen, teash_state.script_end - oldline+oldlen);

        teash_state.script_end -= oldlen-newlen; /*shrunk by this much */
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

/** 
 * \breif take a line from user input and figure out what to do
 *
 * Lines are ether executed or loaded into script memory.
 */
void teash_load_or_eval(void)
{
    char *p;
    int ln;

    /* trim whitespace on tail */
    for(p=teash_state.line; *p != '\0'; p++) {} /* goto end */
    for(p--; isspace(*p); p--) {} /* back up over whitespaces */
    *(++p) = '\0'; /* end the line */

    /* skip whitespace */
    for(p=teash_state.line; isspace(*p) && *p != '\0'; p++) {}
    if( *p == '\0' ) return;

    /* is first word a number? */
    for(ln=0; isdigit(*p) && *p != '\0'; p++) {
        ln *= 10;
        ln += *p - '0';
    }
    if( isspace(*p) || *p == '\0' ) {
        for(; isspace(*p) && *p != '\0'; p++) {} /* skip whitespace */
        teash_load_line(ln, p);
    }
    teash_history_push();
    teash_eval(p);
    while(teash_state.status & teash_status_run) {
        /* TODO copy line from script to line. */
        strcpy(teash_state.line, teash_state.LP); /* FIXME more thought here. */
        teash_eval(teash_state.line);
        teash_next_line();
    }
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
        printf("\x1b[%u;0f", teash_state.screen_height); /* Put cursor at edit line */
        printf("%s", teash_state.line);
        printf("\x1b[s"); /* Save cursor */
        printf("\x1b[K"); /* Erase to end of line */

    } else if(strcmp(teash_state.esc_sbuf, "[B")==0) { /* Cursor down */
        /* Replace editing line with Next line. */
        teash_history_load(1);
        printf("\x1b[%u;0f", teash_state.screen_height); /* Put cursor at edit line */
        printf("%s", teash_state.line);
        printf("\x1b[s"); /* Save cursor */
        printf("\x1b[K"); /* Erase to end of line */

    } else if(strcmp(teash_state.esc_sbuf, "[C")==0) { /* Cursor forward */
        if(teash_state.line[teash_state.lineIdx] != '\0') {
            teash_state.lineIdx++;
        }
        printf("\x1b[C"); /* Move right */
        printf("\x1b[s"); /* Save cursor */

    } else if(strcmp(teash_state.esc_sbuf, "[D")==0) { /* Cursor backward */
        if(teash_state.lineIdx > 0) {
            teash_state.lineIdx--;
        }
        printf("\x1b[D"); /* Move left */
        printf("\x1b[s"); /* Save cursor */

    } else if(sscanf(teash_state.esc_sbuf, "[%u;%uR", &a, &b) == 2) {
        teash_state.screen_height = a;
        printf("\x1b[0;%ur", teash_state.screen_height - 2);/* Scrolling region is top lines */
        printf("\x1b[%u;%uf", teash_state.screen_height, teash_state.lineIdx); /* Put cursor at edit line */
        printf("\x1b[s"); /* Save cursor */
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
            teash_state.line[--teash_state.lineIdx] = '\0';
            printf("\b \b");
        } else if(c == '\n') {
            teash_load_or_eval();
            teash_state.lineIdx = 0;
        } else {
            teash_state.line[teash_state.lineIdx++] = c;
            teash_state.line[teash_state.lineIdx] = '\0';
            putchar(c);
        }
    } else { /* In esc sequence */
        teash_state.esc_sbuf[teash_state.escIdx++] = c;
        if(isalnum(c)) {
            teash_state.esc_sbuf[teash_state.escIdx++] = '\0';
            inesc = false;
            teash_esc_eval();
        }
    }
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
