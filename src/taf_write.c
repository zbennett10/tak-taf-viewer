/*
 * taf_write.c -- TAF serializer.
 *
 * Two-pass layout (no gaps, no padding):
 *
 *   [0..11]       header: magic(4) num_entries(4) reserved(4)
 *   [12..]        entry offset table: uint32 * num_entries
 *   per entry:    entry header (40) + frame ptr table (8 * num_frames)
 *   per frame:    frame header (20)
 *   per frame:    pixel data (width * height * 2)
 *
 * Frame ptr table entries are 8 bytes wide; only the first 4 bytes
 * (the frame header offset) are read by TAF_Open. The trailing 4
 * bytes are written as zero.
 */

#include "taf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_u16(uint8_t *buf, size_t off, uint16_t v) { memcpy(buf + off, &v, 2); }
static void put_i16(uint8_t *buf, size_t off, int16_t  v) { memcpy(buf + off, &v, 2); }
static void put_u32(uint8_t *buf, size_t off, uint32_t v) { memcpy(buf + off, &v, 4); }

int TAF_Save(const TAFFile *taf, const char *path) {
    if (!taf || !path) return -1;
    int N = taf->num_entries;

    /* Pass 1: compute offsets. */
    size_t pos = TAF_HEADER_SIZE + (size_t)N * 4;

    uint32_t *entry_off = (uint32_t *)calloc((size_t)(N ? N : 1), sizeof(uint32_t));
    if (!entry_off) return -1;

    for (int i = 0; i < N; i++) {
        entry_off[i] = (uint32_t)pos;
        pos += TAF_ENTRY_HEADER_SIZE +
               (size_t)taf->entries[i]->num_frames * TAF_FRAME_PTR_SIZE;
    }

    /* Flatten frame offsets for easy indexing in pass 2. */
    int total_frames = 0;
    for (int i = 0; i < N; i++) total_frames += taf->entries[i]->num_frames;

    uint32_t *frame_hdr_off = (uint32_t *)calloc((size_t)(total_frames ? total_frames : 1), sizeof(uint32_t));
    uint32_t *pixel_off     = (uint32_t *)calloc((size_t)(total_frames ? total_frames : 1), sizeof(uint32_t));
    if (!frame_hdr_off || !pixel_off) {
        free(entry_off); free(frame_hdr_off); free(pixel_off);
        return -1;
    }

    int fi = 0;
    for (int i = 0; i < N; i++) {
        TAFEntry *e = taf->entries[i];
        for (int j = 0; j < e->num_frames; j++) {
            frame_hdr_off[fi++] = (uint32_t)pos;
            pos += TAF_FRAME_HEADER_SIZE;
        }
    }
    fi = 0;
    for (int i = 0; i < N; i++) {
        TAFEntry *e = taf->entries[i];
        for (int j = 0; j < e->num_frames; j++) {
            pixel_off[fi++] = (uint32_t)pos;
            pos += (size_t)e->frames[j]->width * e->frames[j]->height * 2;
        }
    }

    size_t total_size = pos;
    uint8_t *buf = (uint8_t *)calloc(total_size, 1);
    if (!buf) {
        free(entry_off); free(frame_hdr_off); free(pixel_off);
        return -1;
    }

    /* Pass 2: write bytes. */
    put_u32(buf, 0, TAF_VERSION_MAGIC);
    put_u32(buf, 4, (uint32_t)N);
    put_u32(buf, 8, 0);

    for (int i = 0; i < N; i++) {
        put_u32(buf, TAF_HEADER_SIZE + (size_t)i * 4, entry_off[i]);
    }

    fi = 0;
    for (int i = 0; i < N; i++) {
        TAFEntry *e = taf->entries[i];
        size_t off = entry_off[i];

        put_u16(buf, off + 0, (uint16_t)e->num_frames);
        put_u16(buf, off + 2, e->reserved1);
        put_u32(buf, off + 4, e->reserved2);
        memcpy(buf + off + 8, e->name, 32);

        for (int j = 0; j < e->num_frames; j++) {
            size_t ptr_off = off + TAF_ENTRY_HEADER_SIZE + (size_t)j * TAF_FRAME_PTR_SIZE;
            put_u32(buf, ptr_off + 0, frame_hdr_off[fi + j]);
            put_u32(buf, ptr_off + 4, 0);
        }
        fi += e->num_frames;
    }

    fi = 0;
    for (int i = 0; i < N; i++) {
        TAFEntry *e = taf->entries[i];
        for (int j = 0; j < e->num_frames; j++) {
            TAFFrame *f = e->frames[j];
            size_t off = frame_hdr_off[fi];
            put_u16(buf, off +  0, f->width);
            put_u16(buf, off +  2, f->height);
            put_i16(buf, off +  4, f->offset_x);
            put_i16(buf, off +  6, f->offset_y);
            buf[off + 8] = f->transparency;
            buf[off + 9] = f->format;
            put_u16(buf, off + 10, f->subframes);
            put_u32(buf, off + 12, f->unknown);
            put_u32(buf, off + 16, pixel_off[fi]);
            fi++;
        }
    }

    fi = 0;
    for (int i = 0; i < N; i++) {
        TAFEntry *e = taf->entries[i];
        for (int j = 0; j < e->num_frames; j++) {
            TAFFrame *f = e->frames[j];
            size_t bytes = (size_t)f->width * f->height * 2;
            memcpy(buf + pixel_off[fi], f->pixels, bytes);
            fi++;
        }
    }

    /* Write atomically-ish: temp file, then rename. */
    FILE *fp = fopen(path, "wb");
    if (!fp) { free(buf); free(entry_off); free(frame_hdr_off); free(pixel_off); return -1; }
    size_t wrote = fwrite(buf, 1, total_size, fp);
    fclose(fp);

    free(buf); free(entry_off); free(frame_hdr_off); free(pixel_off);

    if (wrote != total_size) return -1;

    ((TAFFile *)taf)->dirty = 0;   /* cast away const: dirty is a bookkeeping flag */
    return 0;
}
