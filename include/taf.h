#ifndef TAF_H
#define TAF_H

#include <stdint.h>
#include <stddef.h>

/*
 * TAF (Truecolor Animation Format) -- TA: Kingdoms only.
 *
 * Same container structure as GAF (header, entry table, frame headers)
 * but pixel data is 16-bit truecolor instead of 8-bit palette indices.
 * No palette needed -- each pixel stores its own color directly.
 *
 * TAF files come in pairs:
 *   *_1555.taf -- ARGB 1555 (1 bit alpha, 5 bits each for R, G, B)
 *   *_4444.taf -- ARGB 4444 (4 bits each for A, R, G, B)
 *
 * Frame header 'format' field:
 *   4 = ARGB 4444
 *   5 = ARGB 1555
 *
 * Pixel data is raw uncompressed: width * height * sizeof(uint16_t) bytes.
 * 0x0000 = fully transparent pixel.
 */

#define TAF_VERSION_MAGIC 0x00010100
#define TAF_FORMAT_4444   4
#define TAF_FORMAT_1555   5

typedef struct {
    uint16_t num_frames;
    uint16_t reserved1;
    uint32_t reserved2;
    char name[32];
} TAFEntryHeader;

typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t  offset_x;
    int16_t  offset_y;
    uint8_t  transparency;
    uint8_t  format;       /* 4 = ARGB4444, 5 = ARGB1555 */
    uint16_t subframes;
    uint32_t unknown;
    uint32_t pixel_data_offset;
} TAFFrameHeader;

typedef struct {
    uint8_t *data;
    uint32_t data_size;
    uint32_t num_entries;
} TAFFile;

/* File operations */
int TAF_Open(TAFFile **out, const char *path);
void TAF_Close(TAFFile *taf);

/* Navigation */
int TAF_GetEntryCount(TAFFile *taf);
int TAF_GetEntryInfo(TAFFile *taf, int entry_index, TAFEntryHeader **out);
int TAF_GetFrameInfo(TAFFile *taf, int entry_index, int frame_index, TAFFrameHeader **out);

/* Decode to 32-bit RGBA (no palette needed) */
uint32_t *TAF_DecodeFrameRGBA(TAFFile *taf, const TAFFrameHeader *frame);

#endif /* TAF_H */
