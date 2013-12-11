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

#define TEASH_HISTORY_DEPTH 5
#define teash_status_run        (1<<0)

struct teash_state_s {
    int historyIdx;
    char history[TEASH_HISTORY_DEPTH][TEASH_LINE_BUFFER_SIZE];
    char line[TEASH_LINE_BUFFER_SIZE];
    uint8_t lineIdx;
    char esc_sbuf[25];
    uint8_t escIdx;

    uint16_t status;
    uint8_t screen_height;

    int vars[10];
    char script[1024];
    char *script_end;
} teash_state;

/*****************************************************************************/
#define teash_is_var(x)
#define teash_var_get(x)
#define teash_var_set(x,v)

/*****************************************************************************/
char *teash_math(char *p)
{
    long int st[10];
    long int *sp = st;
    long int a, b, pushback=1, adjust=-1;

    while(*p != '\0' && *p != ']') {
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
        } else if(*p >= 'A' && *p <= 'Z') { /* TODO think about this more. */
            /* variable lookup */
            adjust = 1;
            a = teash_state.vars[*p - '?'];
        } else if(*p >= 'a' && *p <= 'z') { /* TODO think about this more. */
            /* variable reference lookup */
            a = (int)&teash_state.vars[*p - '?'];
        } else if(*p == 'x') {
            pushback = 0;
        } else if(*p == '+') {
            a = b + a;
        } else if(*p == '-') {
            a = b - a;
        } else if(*p == '*') {
            a = b * a;
        } else if(*p == '/') {
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

void teash_eval(char *line)
{
    if(*line == '[') {
        /* line is prefixed with a math test. */
        line = teash_math(line);
        if(line == NULL || *line == '\0') return;
    }

    /* - do substitutions
     * - break into parameters
     *
     */
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
        teash_eval(teash_state.line);
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
            putchar('\b');
            putchar(' ');
            putchar('\b');
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



    return 0;
}
#endif


/* vim: set ai cin et sw=4 ts=4 : */
