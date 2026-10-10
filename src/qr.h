#ifndef QR_H
#define QR_H
#include "buffer.h"
#include <stdbool.h>

// Encodes text as a QR code and writes it to out as an SVG image.
bool qr_svg(const char *text, Buffer *out);

#endif
