#include "jxr.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// jxrlib: Microsoft's JPEG XR library (third_party/jxrlib, BSD license).
#ifndef __ANSI__
#define __ANSI__
#endif
#include <JXRGlue.h>

// stb_image_write: a single-file JPG encoder (third_party/stb, public domain).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "stb_image_write.h"

#define JPG_QUALITY 92

// Where converted JPGs are kept, so each screenshot is only converted once.
#ifdef PS5
#define CACHE_DIR "/data/prospero-gallery/jxr-cache"
#else
#define CACHE_DIR "build/jxr-cache"
#endif

// --- Reading pixels ----------------------------------------------------------
// The decoder hands back pixels in whatever format the file was saved in.
// Each format below is turned into 3 floats per pixel:
//   HDR formats -> linear light where 1.0 = SDR white (80 nits), "scRGB"
//   SDR formats -> already display-ready values from 0 to 1

typedef enum { KIND_HDR_LINEAR, KIND_HDR_PQ, KIND_SDR } Kind;

typedef enum {
    F_RGBA_HALF, F_RGB_HALF_PADDED, F_RGB_HALF,        // 16-bit floats
    F_RGBA_FLOAT, F_RGB_FLOAT_PADDED, F_RGB_FLOAT,     // 32-bit floats
    F_RGBA_FIXED16, F_RGB_FIXED16_PADDED, F_RGB_FIXED16,
    F_RGBA_FIXED32, F_RGB_FIXED32_PADDED, F_RGB_FIXED32,
    F_RGB101010,                                       // 10 bits per channel, HDR10
    F_RGB24, F_BGR24, F_BGR32, F_BGRA32, F_RGBA32,     // plain 8-bit
    F_RGB48, F_RGBA64,                                 // plain 16-bit
} Format;

typedef struct {
    const GUID *guid;
    Format format;
    int bytes;   // per pixel
    Kind kind;
} FormatInfo;

static const FormatInfo FORMATS[] = {
    { &GUID_PKPixelFormat64bppRGBAHalf,         F_RGBA_HALF,          8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat64bppRGBHalf,          F_RGB_HALF_PADDED,    8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat48bppRGBHalf,          F_RGB_HALF,           6,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBAFloat,       F_RGBA_FLOAT,         16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBFloat,        F_RGB_FLOAT_PADDED,   16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat96bppRGBFloat,         F_RGB_FLOAT,          12, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat64bppRGBAFixedPoint,   F_RGBA_FIXED16,       8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat64bppRGBFixedPoint,    F_RGB_FIXED16_PADDED, 8,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat48bppRGBFixedPoint,    F_RGB_FIXED16,        6,  KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBAFixedPoint,  F_RGBA_FIXED32,       16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat128bppRGBFixedPoint,   F_RGB_FIXED32_PADDED, 16, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat96bppRGBFixedPoint,    F_RGB_FIXED32,        12, KIND_HDR_LINEAR },
    { &GUID_PKPixelFormat32bppRGB101010,        F_RGB101010,          4,  KIND_HDR_PQ },
    { &GUID_PKPixelFormat24bppRGB,              F_RGB24,              3,  KIND_SDR },
    { &GUID_PKPixelFormat24bppBGR,              F_BGR24,              3,  KIND_SDR },
    { &GUID_PKPixelFormat32bppBGR,              F_BGR32,              4,  KIND_SDR },
    { &GUID_PKPixelFormat32bppBGRA,             F_BGRA32,             4,  KIND_SDR },
    { &GUID_PKPixelFormat32bppRGBA,             F_RGBA32,             4,  KIND_SDR },
    { &GUID_PKPixelFormat48bppRGB,              F_RGB48,              6,  KIND_SDR },
    { &GUID_PKPixelFormat64bppRGBA,             F_RGBA64,             8,  KIND_SDR },
};

static float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t bits;

    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {                       // subnormal: normalize it
            exp = 113;
            while (!(mant & 0x400)) { mant <<= 1; exp--; }
            bits = sign | (exp << 23) | ((mant & 0x3ff) << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7f800000u | (mant << 13);
    } else {
        bits = sign | ((exp + 112) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// HDR10 "PQ" curve: turns a 0..1 signal into brightness in nits.
static float pq_to_nits(float e)
{
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
    float p = powf(e, 1.0f / m2);
    float num = p - c1 > 0 ? p - c1 : 0;
    return 10000.0f * powf(num / (c2 - c3 * p), 1.0f / m1);
}

static void read_pixel(const uint8_t *p, Format f, float rgb[3])
{
    const uint16_t *u16 = (const uint16_t *)p;
    const int16_t *i16 = (const int16_t *)p;
    const int32_t *i32 = (const int32_t *)p;
    const float *f32 = (const float *)p;

    switch (f) {
    case F_RGBA_HALF: case F_RGB_HALF_PADDED: case F_RGB_HALF:
        for (int c = 0; c < 3; c++) rgb[c] = half_to_float(u16[c]);
        break;
    case F_RGBA_FLOAT: case F_RGB_FLOAT_PADDED: case F_RGB_FLOAT:
        for (int c = 0; c < 3; c++) rgb[c] = f32[c];
        break;
    case F_RGBA_FIXED16: case F_RGB_FIXED16_PADDED: case F_RGB_FIXED16:
        for (int c = 0; c < 3; c++) rgb[c] = i16[c] / 8192.0f;        // s2.13
        break;
    case F_RGBA_FIXED32: case F_RGB_FIXED32_PADDED: case F_RGB_FIXED32:
        for (int c = 0; c < 3; c++) rgb[c] = i32[c] / 16777216.0f;    // s7.24
        break;
    case F_RGB101010: {
        uint32_t v;
        memcpy(&v, p, 4);
        // PQ-encoded BT.2020 -> linear BT.709 (the colors normal screens use).
        float r = pq_to_nits(((v >> 20) & 0x3ff) / 1023.0f) / 80.0f;
        float g = pq_to_nits(((v >> 10) & 0x3ff) / 1023.0f) / 80.0f;
        float b = pq_to_nits((v & 0x3ff) / 1023.0f) / 80.0f;
        rgb[0] =  1.6605f * r - 0.5876f * g - 0.0728f * b;
        rgb[1] = -0.1246f * r + 1.1329f * g - 0.0083f * b;
        rgb[2] = -0.0182f * r - 0.1006f * g + 1.1187f * b;
        break;
    }
    case F_RGB24: case F_RGBA32:
        for (int c = 0; c < 3; c++) rgb[c] = p[c] / 255.0f;
        break;
    case F_BGR24: case F_BGR32: case F_BGRA32:
        for (int c = 0; c < 3; c++) rgb[c] = p[2 - c] / 255.0f;
        break;
    case F_RGB48: case F_RGBA64:
        for (int c = 0; c < 3; c++) rgb[c] = u16[c] / 65535.0f;
        break;
    }
}

// --- Tone mapping ------------------------------------------------------------

static float luminance(const float rgb[3])
{
    return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
}

// Linear light -> the sRGB curve screens expect, as a 0..255 byte.
static uint8_t to_srgb_byte(float v)
{
    if (!(v > 0)) return 0;          // also catches NaN
    if (v >= 1) return 255;
    float s = v <= 0.0031308f ? v * 12.92f : 1.055f * powf(v, 1.0f / 2.4f) - 0.055f;
    return (uint8_t)(s * 255.0f + 0.5f);
}

static uint8_t to_byte(float v)
{
    if (!(v > 0)) return 0;
    if (v >= 1) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

#define HIST_BINS 1024
#define HIST_MIN_LOG2 (-12.0f)
#define HIST_MAX_LOG2 (8.0f)

// Brightness of the brightest 0.1% of the image (ignoring stray hot pixels),
// used as the point that becomes pure white.
static float bright_point(const uint8_t *pixels, size_t count, const FormatInfo *fi)
{
    static const float range = HIST_MAX_LOG2 - HIST_MIN_LOG2;
    uint32_t *hist = calloc(HIST_BINS, sizeof(uint32_t));
    if (hist == NULL) return 1.0f;

    size_t counted = 0;
    for (size_t i = 0; i < count; i++) {
        float rgb[3];
        read_pixel(pixels + i * fi->bytes, fi->format, rgb);
        float l = luminance(rgb);
        if (!(l > 0)) continue;
        int bin = (int)((log2f(l) - HIST_MIN_LOG2) / range * HIST_BINS);
        if (bin < 0) bin = 0;
        if (bin >= HIST_BINS) bin = HIST_BINS - 1;
        hist[bin]++;
        counted++;
    }

    float result = 1.0f;
    size_t target = counted - counted / 1000, seen = 0;
    for (int bin = 0; bin < HIST_BINS && counted > 0; bin++) {
        seen += hist[bin];
        if (seen >= target) {
            result = exp2f(HIST_MIN_LOG2 + (bin + 1) * range / HIST_BINS);
            break;
        }
    }
    free(hist);
    return result < 1.0f ? 1.0f : result;   // never brighten an image that fits already
}

// --- Decoding ----------------------------------------------------------------

bool jxr_to_jpg(const char *src, const char *dst)
{
    PKCodecFactory *factory = NULL;
    PKImageDecode *decoder = NULL;
    uint8_t *pixels = NULL;
    bool ok = false;

    if (PKCreateCodecFactory(&factory, WMP_SDK_VERSION) != WMP_errSuccess) return false;
    if (factory->CreateDecoderFromFile(src, &decoder) != WMP_errSuccess) goto done;

    const FormatInfo *fi = NULL;
    for (size_t i = 0; i < sizeof(FORMATS) / sizeof(FORMATS[0]); i++) {
        if (IsEqualGUID(FORMATS[i].guid, &decoder->guidPixFormat)) {
            fi = &FORMATS[i];
            break;
        }
    }
    if (fi == NULL) {
        fprintf(stderr, "jxr: %s uses an unsupported pixel format\n", src);
        goto done;
    }

    // Decoder setup, as in jxrlib's own JxrDecApp: full image, no alpha.
    PKPixelInfo info;
    info.pGUIDPixFmt = &decoder->guidPixFormat;
    if (PixelFormatLookup(&info, LOOKUP_FORWARD) != WMP_errSuccess) goto done;
    decoder->WMP.wmiSCP.uAlphaMode = 0;
    decoder->WMP.wmiI.cfColorFormat = info.cfColorFormat;
    decoder->WMP.wmiI.bdBitDepth = info.bdBitDepth;
    decoder->WMP.wmiI.cBitsPerUnit = info.cbitUnit;
    decoder->WMP.wmiI.cThumbnailWidth = decoder->WMP.wmiI.cWidth;
    decoder->WMP.wmiI.cThumbnailHeight = decoder->WMP.wmiI.cHeight;
    decoder->WMP.wmiI.bSkipFlexbits = FALSE;
    decoder->WMP.wmiI.cROILeftX = 0;
    decoder->WMP.wmiI.cROITopY = 0;
    decoder->WMP.wmiI.cROIWidth = decoder->WMP.wmiI.cWidth;
    decoder->WMP.wmiI.cROIHeight = decoder->WMP.wmiI.cHeight;
    decoder->WMP.wmiI.oOrientation = O_NONE;
    decoder->WMP.wmiI.cPostProcStrength = 0;

    I32 width, height;
    decoder->GetSize(decoder, &width, &height);
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) goto done;

    size_t count = (size_t)width * (size_t)height;
    pixels = malloc(count * fi->bytes);
    if (pixels == NULL) goto done;

    PKRect rect = { 0, 0, width, height };
    if (decoder->Copy(decoder, &rect, pixels, (U32)(width * fi->bytes)) != WMP_errSuccess) goto done;

    // Brightest point -> white. Extended Reinhard: keeps dark and mid tones
    // close to how they were and rolls the highlights off smoothly.
    float white = fi->kind == KIND_SDR ? 1.0f : bright_point(pixels, count, fi);
    float white_sq = white * white;

    // Write the 8-bit result over the decoded pixels: every format is at
    // least 3 bytes per pixel, so the output never overtakes the input.
    for (size_t i = 0; i < count; i++) {
        float rgb[3];
        read_pixel(pixels + i * fi->bytes, fi->format, rgb);
        uint8_t *out = pixels + i * 3;

        if (fi->kind == KIND_SDR) {
            for (int c = 0; c < 3; c++) out[c] = to_byte(rgb[c]);
            continue;
        }
        for (int c = 0; c < 3; c++) if (!(rgb[c] > 0)) rgb[c] = 0;  // out-of-gamut / NaN
        float l = luminance(rgb);
        float scale = l > 0 ? (1.0f + l / white_sq) / (1.0f + l) : 0;
        for (int c = 0; c < 3; c++) out[c] = to_srgb_byte(rgb[c] * scale);
    }

    ok = stbi_write_jpg(dst, width, height, 3, pixels, JPG_QUALITY) != 0;

done:
    free(pixels);
    if (decoder) decoder->Release(&decoder);
    if (factory) factory->Release(&factory);
    return ok;
}

// --- Cache -------------------------------------------------------------------

static pthread_mutex_t convert_lock = PTHREAD_MUTEX_INITIALIZER;

static void make_dirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

bool jxr_cached_jpg(const char *src, char *out, size_t out_size)
{
    struct stat src_st, cache_st;
    if (stat(src, &src_st) != 0) return false;

    // "/user/av_contents/photo/.../x.jxr" -> "<cache>/user_av_contents_photo_..._x.jpg"
    char name[400];
    const char *s = src;
    while (*s == '/' || *s == '.') s++;
    snprintf(name, sizeof(name), "%s", s);
    for (char *p = name; *p; p++) if (*p == '/') *p = '_';
    char *dot = strrchr(name, '.');
    if (dot != NULL) *dot = '\0';
    snprintf(out, out_size, "%s/%s.jpg", CACHE_DIR, name);

    // Converted already, and the screenshot hasn't changed since.
    if (stat(out, &cache_st) == 0 && cache_st.st_mtime >= src_st.st_mtime)
        return true;

    pthread_mutex_lock(&convert_lock);
    bool ok;
    if (stat(out, &cache_st) == 0 && cache_st.st_mtime >= src_st.st_mtime) {
        ok = true;   // another request finished it while this one waited
    } else {
        make_dirs(CACHE_DIR);
        // Write to a temporary name and rename at the end, so nobody is ever
        // sent a half-written JPG.
        char tmp[600];
        snprintf(tmp, sizeof(tmp), "%s.part", out);
        ok = jxr_to_jpg(src, tmp) && rename(tmp, out) == 0;
        if (!ok) remove(tmp);
    }
    pthread_mutex_unlock(&convert_lock);
    return ok;
}
