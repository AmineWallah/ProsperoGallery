#ifndef BUFFER_H
#define BUFFER_H
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    char *data;    // the text so far, always '\0'-terminated
    size_t len;    // characters used, not counting the terminator
    size_t cap;    // bytes allocated
} Buffer;

void buf_init(Buffer *b);                    // data = NULL, len = 0, cap = 0
bool buf_append(Buffer *b, const char *s);   // false if memory ran out
void buf_free(Buffer *b);


#endif
