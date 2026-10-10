#ifndef GALLERY_H
#define GALLERY_H
#include "buffer.h"

// Writes {"titles":{"ID":"Game name",...},"captures":["photo/...jpg",...]}
void scan_captures(const char* root, Buffer* out);

#endif
