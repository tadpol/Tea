

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


struct teash_state_s {
    int history_idx;
    char history_buf[TEASH_HISTORY_DEPTH][TEASH_LINE_BUFFER_SIZE];
    char line[TEASH_LINE_BUFFER_SIZE];
    char lineIdx;

    char esc_sbuf[25];
    char escIdx;

    uint8_t screen_height;

    int vars[10];
    char script[1024];
    char *script_end;
} teash_state;


/*****************************************************************************/
char *teash_math(char *line)
{
    int st[10];
    int *sp = st;
    int a, b, pushback=1, adjust=-1;

    while(*line != '\0' && *line != ']') {
        a = *sp;
        b = *(sp-1);

        if(*line >= '0' && *line <= '9') {
            uint8 base = 10;
            if(*(line+1) == 'x') {
                base = 16;
                p += 2;
            } else if(*(line+1) == 'b') {
                base = 2;
                p += 2;
            }
            a=0;
            for(; *line != '\0'; line++) {
                b = *line;
                if( b >= '0' && b <= '9' ) b -= '0';
                else if( b >= 'a' && b <= 'z') b = (b - 'a') + 10;
                else if( b >= 'A' && b <= 'Z') b = (b - 'A') + 10;
                else break;
                if( b >= base ) break;
                a *= base;
                a += b;
            }
            adjust = 1;
        } else if(*line >= 'A' && *line <= 'Z') { /* TODO think about this more. */
            /* variable lookup */
            adjust = 1;
            a = teash_state.vars[*line - '?'];
        } else if(*line >= 'a' && *line <= 'z') { /* TODO think about this more. */
            /* variable reference lookup */
            a = &teash_state.vars[*line - '?'];
        } else if(*line == 'x') {
            pushback = 0;
        } else if(*line == '+') {
            a = b + a;
        } else if(*line == '-') {
            a = b - a;
        } else if(*line == '*') {
            a = b * a;
        } else if(*line == '/') {
            a = b / a;
        } else if(*line == '%') {
            a = b % a;
        } else if(*line == '|') {
            a = b | a;
        } else if(*line == '&') {
            a = b & a;
        } else if(*line == '^') {
            a = b ^ a;
        } else if(*line == '~') {
            a = ~ a;
            adjust = 0;

        } else if(*line == '@') {
            line++;
            adjust = 0;
            if(*line == 'c') {
                a = *((uint8_t*)a);
            } else if(*line == 's') {
                a = *((uint16_t*)a);
            } else if(*line == 'i') {
                a = *((uint32_t*)a);
            } else {
                a = *((uint32_t*)a);
                --line;
            }
        } else if(*line == '!') {
            line++;
            adjust = -2;
            if(*line == 'c') {
                *((uint8_t*)a) = b;
            } else if(*line == 's') {
                *((uint16_t*)a) = b;
            } else if(*line == 'i') {
                *((uint32_t*)a) = b;
            } else {
                *((uint32_t*)a) = b;
                --line;
            }
        }
        sp += adjust;
        switch(pushback) {
            case 2: *(sp-1) = b;
            case 1: *sp = a;
            default:
                break;
        }
    }

    if(*sp == 0) return NULL; /* math result is FALSE, do not eval rest of line. */

    if(*line == ']') line++;
    for(; isspace(*line) && *line != '\0'; line++) {} /* skip whitespace */
    return line;
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
        if( teash_has_free(teash) < newlen) 
            return -2;
        teash_state.script_end += newlen;
    } else if( oldlen < newlen ) {
        /* growing */
        if( teash_has_free(teash) < newlen-oldlen) 
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
    for(p=line; isspace(*p) && *p != '\0'; p++) {}
    if( *p == '\0' ) return 0;

    /* is first word a number? */
    for(ln=0; isdigit(*p) && *p != '\0'; p++) {
        ln *= 10;
        ln += *p - '0';
    }
    if( isspace(*p) || *p == '\0' ) {
        for(; isspace(*p) && *p != '\0'; p++) {} /* skip whitespace */
        teash_load_line(ln, p);
    }
    teash_eval(p);
}

/**
 * \brief Evaluate a VT100 escape sequence.
 */
void teash_esc_eval(void)
{
    int a, b;
    if(strcmp(teash_state.esc_sbuf, "[A", 3)==0) { /* Cursor up */
        /* Replace editing line with prev line. */
    } else if(strcmp(teash_state.esc_sbuf, "[B", 3)==0) { /* Cursor down */
        /* Replace editing line with Next line. */
    } else if(strcmp(teash_state.esc_sbuf, "[C", 3)==0) { /* Cursor forward */
        if(teash_state.line[teash_state.lineIdx] != '\0') {
            teash_state.lineIdx++;
        }
        printf("\x1b[C");
    } else if(strcmp(teash_state.esc_sbuf, "[D", 3)==0) { /* Cursor backward */
        if(teash_state.lineIdx > 0) {
            teash_state.lineIdx--;
        }
        printf("\x1b[D");
    } else if(sscanf(teash_state.esc_sbuf, "[%u;%uR", &a, &b) == 2) {
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
            teash_outchar('\b');
            teash_outchar(' ');
            teash_outchar('\b');
        } else if(c == '\n') {
            teash_state.line[teash_state.lineIdx++] = '\0';
            teash_load_or_eval();
            teash_state.lineIdx = 0;
        } else {
            teash_state.line[teash_state.lineIdx++] = c;
            teash_state.line[teash_state.lineIdx] = '\0';
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




/* vim: set ai cin et sw=4 ts=4 : */
