#include "buffer.h"

#include <stdlib.h>
#include <string.h>

void buf_init(Buffer *b)
{
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

bool buf_append(Buffer *b, const char *s)
{
    size_t add = strlen(s);
    size_t needed = b->len + add + 1;

    if (needed > b->cap) {
        size_t new_cap = (b->cap == 0) ? 256 : b->cap;
        while (new_cap < needed) {
            new_cap *= 2;
        }

        // realloc may move the block, so only trust the pointer it returns.
        char *new_data = realloc(b->data, new_cap);
        if (new_data == NULL) {
            return false;                       // old buffer is still valid
        }
        b->data = new_data;
        b->cap = new_cap;
    }

    // Copy the new text onto the end, then re-terminate.
    memcpy(b->data + b->len, s, add);
    b->len += add;
    b->data[b->len] = '\0';
    return true;
}

void buf_free(Buffer *b)
{
    free(b->data);                           
    buf_init(b);
}