#include "qr.h"
#include "qrcodegen.h"

#include <stdint.h>
#include <stdio.h>

#define QR_BORDER 4   // the blank margin scanners need, in modules

bool qr_svg(const char *text, Buffer *out)
{
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    if (!qrcodegen_encodeText(text, temp, qrcode, qrcodegen_Ecc_MEDIUM,
                              qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX,
                              qrcodegen_Mask_AUTO, true))
        return false;

    int size = qrcodegen_getSize(qrcode);
    int full = size + QR_BORDER * 2;

    char tmp[256];
    snprintf(tmp, sizeof(tmp),
             "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %d %d\" "
             "shape-rendering=\"crispEdges\">"
             "<rect width=\"100%%\" height=\"100%%\" fill=\"#fff\"/><path fill=\"#000\" d=\"",
             full, full);
    buf_append(out, tmp);

    // One 1x1 square per dark module.
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qrcodegen_getModule(qrcode, x, y)) {
                snprintf(tmp, sizeof(tmp), "M%d,%dh1v1h-1z", x + QR_BORDER, y + QR_BORDER);
                buf_append(out, tmp);
            }
        }
    }
    return buf_append(out, "\"/></svg>");
}
