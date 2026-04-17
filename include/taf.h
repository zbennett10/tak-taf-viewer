#ifndef TAF_H
#define TAF_H

#include <stdint.h>
#include <stddef.h>

/*
 * TAF (Truecolor Animation Format) -- TA: Kingdoms only.
 *
 * Same container structure as GAF (header, entry table, frame headers)
 * but pixel data is 16-bit truecolor instead of 8-bit palette indices.
 *
 * Pixel formats:
 *   ARGB 1555 (format=5): bit15=alpha, bits14-10=R, bits9-5=G, bits4-0=B
 *   ARGB 4444 (format=4): bits15-12=A, bits11-8=R, bits7-4=G, bits3-0=B
 * 0x0000 = fully transparent pixel.
 *
 * In-memory model:
 *   TAFFile owns an array of TAFEntry*; each TAFEntry owns an array of
 *   TAFFrame*; each TAFFrame owns its own pixel buffer. This is a mutable
 *   tree: add/delete/duplicate/reorder entries and frames freely, then
 *   call TAF_Save to serialize.
 */

#define TAF_VERSION_MAGIC      0x00010100
#define TAF_FORMAT_4444        4
#define TAF_FORMAT_1555        5

#define TAF_HEADER_SIZE        12
#define TAF_ENTRY_HEADER_SIZE  40
#define TAF_FRAME_PTR_SIZE     8    /* frame-ptr-table stride in bytes */
#define TAF_FRAME_HEADER_SIZE  20   /* on-disk frame header size */

typedef struct {
    uint16_t width;
    uint16_t height;
    int16_t  offset_x;
    int16_t  offset_y;
    uint8_t  transparency;
    uint8_t  format;       /* 4 = ARGB4444, 5 = ARGB1555 */
    uint16_t subframes;
    uint32_t unknown;
    uint16_t *pixels;      /* owned; width*height raw 16-bit pixels in `format` */
} TAFFrame;

typedef struct {
    char      name[32];    /* nul-terminated in practice */
    uint16_t  reserved1;
    uint32_t  reserved2;
    int       num_frames;
    int       cap_frames;
    TAFFrame **frames;     /* owned array, capacity cap_frames */
} TAFEntry;

typedef struct {
    int        num_entries;
    int        cap_entries;
    TAFEntry **entries;    /* owned array, capacity cap_entries */
    int        dirty;      /* unsaved edits? */
} TAFFile;

/* ── File operations ─────────────────────────────────────────────── */
int  TAF_Open(TAFFile **out, const char *path);
void TAF_Close(TAFFile *taf);
int  TAF_Save(const TAFFile *taf, const char *path);   /* src/taf_write.c */

/* ── Navigation ──────────────────────────────────────────────────── */
int TAF_GetEntryCount(const TAFFile *taf);
int TAF_GetEntryInfo(const TAFFile *taf, int entry_index, TAFEntry **out);
int TAF_GetFrameInfo(const TAFFile *taf, int entry_index, int frame_index, TAFFrame **out);

/* ── Decode / encode ─────────────────────────────────────────────── */
uint32_t *TAF_DecodeFrameRGBA(const TAFFrame *frame);
uint16_t  TAF_EncodePixel_1555(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
uint16_t  TAF_EncodePixel_4444(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void      TAF_EncodeFrameFromRGBA(TAFFrame *frame, const uint32_t *rgba);

/* ── Mutation (Phase 2) ──────────────────────────────────────────── */
TAFEntry *TAF_NewEntry(TAFFile *taf, const char *name);
int       TAF_DeleteEntry(TAFFile *taf, int idx);
int       TAF_DuplicateEntry(TAFFile *taf, int idx);
int       TAF_MoveEntry(TAFFile *taf, int from, int to);
int       TAF_RenameEntry(TAFFile *taf, int idx, const char *name);

TAFFrame *TAF_NewFrame(TAFEntry *entry, uint16_t w, uint16_t h, uint8_t format);
int       TAF_DeleteFrame(TAFEntry *entry, int idx);
int       TAF_DuplicateFrame(TAFEntry *entry, int idx);
int       TAF_MoveFrame(TAFEntry *entry, int from, int to);

/* Deep-copy an entire TAFFile tree (all entries + frames + pixel buffers). */
TAFFile  *TAF_Clone(const TAFFile *src);

/* ── PNG bridge (Phase 1/2) ──────────────────────────────────────── */
int TAF_ExportFrameToPNG(const TAFFrame *frame, const char *path);   /* src/png_io.c */
int TAF_ImportPNGToFrame(TAFFrame *frame, const char *path);         /* src/png_io.c */

#endif /* TAF_H */
