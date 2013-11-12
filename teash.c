

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
} teash_state;


/*
 * Get characters, put them into buffer, until newline.
 * Modifying with control/escape characters.
 */
char *teash_gets(char *buffer, int size)
{
    int c;
    int i=0;


    while( (c = fgetc(STDIN)) >= 0 ) {

        if(c == '\r' || c == '\n') {
            buffer[i] = '\0';
            return buffer;
        } else if(c == '\b') {
            buffer[--i] = '\0';
            fputc('\b', STDIN);
            fputc(' ', STDIN);
        } else {
            buffer[i++] = c;
        }

        if(i == size) {
            buffer[i] = '\0';
            return buffer;
        }
    }


    return NULL;
}


/* vim: set ai cin et sw=4 ts=4 : */
