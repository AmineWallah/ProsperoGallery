#ifndef JXR_H
#define JXR_H

#include <stdbool.h>
#include <stddef.h>

// HDR screenshots are saved as JPEG XR (.jxr), which no browser can show.
// These turn them into ordinary JPGs, tone-mapping the HDR brightness range
// down to what a normal screen can display.

// Converts the .jxr at src into a JPG at dst. Returns false if the file
// can't be decoded or uses a pixel format this code doesn't handle.
bool jxr_to_jpg(const char *src, const char *dst);

// Returns (in out) the path of a JPG version of the .jxr at src, converting
// it on first use and reusing the result afterwards. Conversions run one at
// a time, since a 4K HDR image needs ~70 MB while it's being converted.
bool jxr_cached_jpg(const char *src, char *out, size_t out_size);

#endif
