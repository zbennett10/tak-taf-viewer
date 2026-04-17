/*
 * taf.c -- TAF loader/decoder and mutable-tree model.
 *
 * TAF_Open reads a .taf file into a heap-allocated tree of
 * TAFFile → TAFEntry[] → TAFFrame[]; each frame owns its own
 * 16-bit pixel buffer. TAF_Save (taf_write.c) serializes it back.
 */

#include "taf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── internal helpers ─────────────────────────────────────────────── */

static int read_exact(const uint8_t *buf, size_t buf_size, size_t off, void *dst, size_t n) {
    if (off + n > buf_size) return -1;
    memcpy(dst, buf + off, n);
    return 0;
}

static uint32_t rd_u32(const uint8_t *buf, size_t off) {
    uint32_t v;
    memcpy(&v, buf + off, 4);
    return v;
}

static TAFFrame *parse_frame(const uint8_t *buf, size_t buf_size, uint32_t frame_off) {
    if (frame_off + TAF_FRAME_HEADER_SIZE > buf_size) return NULL;

    TAFFrame *f = (TAFFrame *)calloc(1, sizeof(TAFFrame));
    if (!f) return NULL;

    memcpy(&f->width,             buf + frame_off + 0,  2);
    memcpy(&f->height,            buf + frame_off + 2,  2);
    memcpy(&f->offset_x,          buf + frame_off + 4,  2);
    memcpy(&f->offset_y,          buf + frame_off + 6,  2);
    f->transparency = buf[frame_off + 8];
    f->format       = buf[frame_off + 9];
    memcpy(&f->subframes,         buf + frame_off + 10, 2);
    memcpy(&f->unknown,           buf + frame_off + 12, 4);

    uint32_t pixel_off = rd_u32(buf, frame_off + 16);
    size_t   total_px  = (size_t)f->width * f->height;
    size_t   bytes     = total_px * 2;

    if (total_px == 0 || pixel_off + bytes > buf_size ||
        (f->format != TAF_FORMAT_1555 && f->format != TAF_FORMAT_4444)) {
        free(f);
        return NULL;
    }

    f->pixels = (uint16_t *)malloc(bytes);
    if (!f->pixels) { free(f); return NULL; }
    memcpy(f->pixels, buf + pixel_off, bytes);
    return f;
}

static void free_frame(TAFFrame *f) {
    if (!f) return;
    free(f->pixels);
    free(f);
}

static void free_entry(TAFEntry *e) {
    if (!e) return;
    for (int i = 0; i < e->num_frames; i++) free_frame(e->frames[i]);
    free(e->frames);
    free(e);
}

static int ensure_capacity(void ***arr, int *cap, int needed, size_t elem_size) {
    if (*cap >= needed) return 0;
    int new_cap = *cap ? *cap : 4;
    while (new_cap < needed) new_cap *= 2;
    void **fresh = (void **)realloc(*arr, (size_t)new_cap * elem_size);
    if (!fresh) return -1;
    *arr = fresh;
    *cap = new_cap;
    return 0;
}

/* ── Open / Close ─────────────────────────────────────────────────── */

int TAF_Open(TAFFile **out, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "Cannot open: %s\n", path); return -1; }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < TAF_HEADER_SIZE) { fclose(fp); return -1; }

    uint8_t *buf = (uint8_t *)malloc((size_t)file_size);
    if (!buf) { fclose(fp); return -1; }
    if (fread(buf, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(buf); fclose(fp); return -1;
    }
    fclose(fp);

    if (rd_u32(buf, 0) != TAF_VERSION_MAGIC) {
        fprintf(stderr, "Invalid TAF version in: %s\n", path);
        free(buf);
        return -1;
    }

    uint32_t num_entries = rd_u32(buf, 4);

    TAFFile *taf = (TAFFile *)calloc(1, sizeof(TAFFile));
    if (!taf) { free(buf); return -1; }

    if (num_entries > 0) {
        taf->entries = (TAFEntry **)calloc(num_entries, sizeof(TAFEntry *));
        if (!taf->entries) { free(taf); free(buf); return -1; }
        taf->cap_entries = (int)num_entries;
    }

    for (uint32_t i = 0; i < num_entries; i++) {
        size_t entry_off_slot = TAF_HEADER_SIZE + i * 4;
        if (entry_off_slot + 4 > (size_t)file_size) goto fail;
        uint32_t entry_off = rd_u32(buf, entry_off_slot);

        if (entry_off + TAF_ENTRY_HEADER_SIZE > (size_t)file_size) goto fail;

        TAFEntry *e = (TAFEntry *)calloc(1, sizeof(TAFEntry));
        if (!e) goto fail;

        uint16_t num_frames;
        memcpy(&num_frames,    buf + entry_off + 0, 2);
        memcpy(&e->reserved1,  buf + entry_off + 2, 2);
        memcpy(&e->reserved2,  buf + entry_off + 4, 4);
        memcpy(e->name,        buf + entry_off + 8, 32);
        e->name[31] = '\0';

        if (num_frames > 0) {
            e->frames = (TAFFrame **)calloc(num_frames, sizeof(TAFFrame *));
            if (!e->frames) { free_entry(e); goto fail; }
            e->cap_frames = num_frames;
        }

        for (int j = 0; j < num_frames; j++) {
            size_t ptr_off = (size_t)entry_off + TAF_ENTRY_HEADER_SIZE +
                             (size_t)j * TAF_FRAME_PTR_SIZE;
            if (ptr_off + 4 > (size_t)file_size) { free_entry(e); goto fail; }
            uint32_t frame_off = rd_u32(buf, ptr_off);

            TAFFrame *f = parse_frame(buf, (size_t)file_size, frame_off);
            if (!f) { free_entry(e); goto fail; }
            e->frames[j] = f;
            e->num_frames++;
        }

        taf->entries[i] = e;
        taf->num_entries++;
    }

    free(buf);
    *out = taf;
    return 0;

fail:
    free(buf);
    TAF_Close(taf);
    return -1;
}

void TAF_Close(TAFFile *taf) {
    if (!taf) return;
    for (int i = 0; i < taf->num_entries; i++) free_entry(taf->entries[i]);
    free(taf->entries);
    free(taf);
}

/* ── Navigation ───────────────────────────────────────────────────── */

int TAF_GetEntryCount(const TAFFile *taf) {
    return taf ? taf->num_entries : 0;
}

int TAF_GetEntryInfo(const TAFFile *taf, int entry_index, TAFEntry **out) {
    if (!taf || entry_index < 0 || entry_index >= taf->num_entries) return -1;
    *out = taf->entries[entry_index];
    return 0;
}

int TAF_GetFrameInfo(const TAFFile *taf, int entry_index, int frame_index, TAFFrame **out) {
    TAFEntry *e = NULL;
    if (TAF_GetEntryInfo(taf, entry_index, &e) != 0) return -1;
    if (frame_index < 0 || frame_index >= e->num_frames) return -1;
    *out = e->frames[frame_index];
    return 0;
}

/* ── Decode to 32-bit RGBA ────────────────────────────────────────── */

uint32_t *TAF_DecodeFrameRGBA(const TAFFrame *frame) {
    if (!frame || !frame->pixels) return NULL;

    size_t total = (size_t)frame->width * frame->height;
    uint32_t *rgba = (uint32_t *)malloc(total * sizeof(uint32_t));
    if (!rgba) return NULL;

    const uint16_t *src = frame->pixels;

    if (frame->format == TAF_FORMAT_1555) {
        for (size_t i = 0; i < total; i++) {
            uint16_t px = src[i];
            if (px == 0) { rgba[i] = 0; continue; }
            uint8_t a = (px & 0x8000) ? 255 : 0;
            uint8_t r = (uint8_t)(((px >> 10) & 0x1F) * 255 / 31);
            uint8_t g = (uint8_t)(((px >> 5)  & 0x1F) * 255 / 31);
            uint8_t b = (uint8_t)(( px        & 0x1F) * 255 / 31);
            rgba[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                      ((uint32_t)g <<  8) | (uint32_t)r;
        }
    } else if (frame->format == TAF_FORMAT_4444) {
        for (size_t i = 0; i < total; i++) {
            uint16_t px = src[i];
            if (px == 0) { rgba[i] = 0; continue; }
            uint8_t a = (uint8_t)(((px >> 12) & 0xF) * 17);
            uint8_t r = (uint8_t)(((px >>  8) & 0xF) * 17);
            uint8_t g = (uint8_t)(((px >>  4) & 0xF) * 17);
            uint8_t b = (uint8_t)(( px        & 0xF) * 17);
            rgba[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                      ((uint32_t)g <<  8) | (uint32_t)r;
        }
    } else {
        free(rgba);
        return NULL;
    }
    return rgba;
}

/* ── Encode RGBA → 16-bit ────────────────────────────────────────── */

uint16_t TAF_EncodePixel_1555(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (a < 128) return 0x0000;  /* fully transparent */
    uint16_t r5 = (uint16_t)((r * 31 + 127) / 255) & 0x1F;
    uint16_t g5 = (uint16_t)((g * 31 + 127) / 255) & 0x1F;
    uint16_t b5 = (uint16_t)((b * 31 + 127) / 255) & 0x1F;
    uint16_t px = (uint16_t)(0x8000 | (r5 << 10) | (g5 << 5) | b5);
    /* Avoid colliding with the "fully transparent" sentinel 0x0000
       when alpha is opaque but RGB are all zero. */
    if (px == 0) px = 0x8000;
    return px;
}

uint16_t TAF_EncodePixel_4444(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    uint16_t a4 = (uint16_t)((a + 8) / 17) & 0xF;
    uint16_t r4 = (uint16_t)((r + 8) / 17) & 0xF;
    uint16_t g4 = (uint16_t)((g + 8) / 17) & 0xF;
    uint16_t b4 = (uint16_t)((b + 8) / 17) & 0xF;
    if (a4 == 0) return 0x0000;
    uint16_t px = (uint16_t)((a4 << 12) | (r4 << 8) | (g4 << 4) | b4);
    if (px == 0) px = (uint16_t)(a4 << 12);
    return px;
}

void TAF_EncodeFrameFromRGBA(TAFFrame *frame, const uint32_t *rgba) {
    if (!frame || !frame->pixels || !rgba) return;
    size_t total = (size_t)frame->width * frame->height;
    for (size_t i = 0; i < total; i++) {
        uint32_t c = rgba[i];
        uint8_t r = (uint8_t)( c        & 0xFF);
        uint8_t g = (uint8_t)((c >>  8) & 0xFF);
        uint8_t b = (uint8_t)((c >> 16) & 0xFF);
        uint8_t a = (uint8_t)((c >> 24) & 0xFF);
        frame->pixels[i] = (frame->format == TAF_FORMAT_1555)
            ? TAF_EncodePixel_1555(r, g, b, a)
            : TAF_EncodePixel_4444(r, g, b, a);
    }
}

/* ── Mutation API ────────────────────────────────────────────────── */

TAFEntry *TAF_NewEntry(TAFFile *taf, const char *name) {
    if (!taf) return NULL;
    if (ensure_capacity((void ***)&taf->entries, &taf->cap_entries,
                        taf->num_entries + 1, sizeof(TAFEntry *)) != 0) return NULL;
    TAFEntry *e = (TAFEntry *)calloc(1, sizeof(TAFEntry));
    if (!e) return NULL;
    if (name) { strncpy(e->name, name, sizeof(e->name) - 1); }
    else      { strncpy(e->name, "new_entry", sizeof(e->name) - 1); }
    taf->entries[taf->num_entries++] = e;
    taf->dirty = 1;
    return e;
}

int TAF_DeleteEntry(TAFFile *taf, int idx) {
    if (!taf || idx < 0 || idx >= taf->num_entries) return -1;
    free_entry(taf->entries[idx]);
    for (int i = idx; i < taf->num_entries - 1; i++) taf->entries[i] = taf->entries[i+1];
    taf->num_entries--;
    taf->dirty = 1;
    return 0;
}

static TAFFrame *clone_frame(const TAFFrame *src) {
    TAFFrame *f = (TAFFrame *)calloc(1, sizeof(TAFFrame));
    if (!f) return NULL;
    *f = *src;
    size_t bytes = (size_t)src->width * src->height * 2;
    f->pixels = (uint16_t *)malloc(bytes);
    if (!f->pixels) { free(f); return NULL; }
    memcpy(f->pixels, src->pixels, bytes);
    return f;
}

int TAF_DuplicateEntry(TAFFile *taf, int idx) {
    TAFEntry *src = NULL;
    if (TAF_GetEntryInfo(taf, idx, &src) != 0) return -1;
    if (ensure_capacity((void ***)&taf->entries, &taf->cap_entries,
                        taf->num_entries + 1, sizeof(TAFEntry *)) != 0) return -1;
    TAFEntry *dup = (TAFEntry *)calloc(1, sizeof(TAFEntry));
    if (!dup) return -1;
    memcpy(dup->name, src->name, sizeof(dup->name));
    dup->reserved1 = src->reserved1;
    dup->reserved2 = src->reserved2;
    if (src->num_frames > 0) {
        dup->frames = (TAFFrame **)calloc(src->num_frames, sizeof(TAFFrame *));
        if (!dup->frames) { free(dup); return -1; }
        dup->cap_frames = src->num_frames;
        for (int j = 0; j < src->num_frames; j++) {
            dup->frames[j] = clone_frame(src->frames[j]);
            if (!dup->frames[j]) { free_entry(dup); return -1; }
            dup->num_frames++;
        }
    }
    for (int i = taf->num_entries; i > idx + 1; i--) taf->entries[i] = taf->entries[i-1];
    taf->entries[idx + 1] = dup;
    taf->num_entries++;
    taf->dirty = 1;
    return 0;
}

int TAF_MoveEntry(TAFFile *taf, int from, int to) {
    if (!taf || from < 0 || from >= taf->num_entries ||
        to   < 0 || to   >= taf->num_entries || from == to) return -1;
    TAFEntry *moving = taf->entries[from];
    if (from < to) {
        for (int i = from; i < to; i++) taf->entries[i] = taf->entries[i+1];
    } else {
        for (int i = from; i > to; i--) taf->entries[i] = taf->entries[i-1];
    }
    taf->entries[to] = moving;
    taf->dirty = 1;
    return 0;
}

int TAF_RenameEntry(TAFFile *taf, int idx, const char *name) {
    TAFEntry *e = NULL;
    if (TAF_GetEntryInfo(taf, idx, &e) != 0 || !name) return -1;
    memset(e->name, 0, sizeof(e->name));
    strncpy(e->name, name, sizeof(e->name) - 1);
    taf->dirty = 1;
    return 0;
}

TAFFrame *TAF_NewFrame(TAFEntry *entry, uint16_t w, uint16_t h, uint8_t format) {
    if (!entry || w == 0 || h == 0) return NULL;
    if (format != TAF_FORMAT_1555 && format != TAF_FORMAT_4444) return NULL;
    if (ensure_capacity((void ***)&entry->frames, &entry->cap_frames,
                        entry->num_frames + 1, sizeof(TAFFrame *)) != 0) return NULL;
    TAFFrame *f = (TAFFrame *)calloc(1, sizeof(TAFFrame));
    if (!f) return NULL;
    f->width = w; f->height = h; f->format = format;
    size_t total = (size_t)w * h;
    f->pixels = (uint16_t *)calloc(total, sizeof(uint16_t)); /* all transparent */
    if (!f->pixels) { free(f); return NULL; }
    entry->frames[entry->num_frames++] = f;
    return f;
}

int TAF_DeleteFrame(TAFEntry *entry, int idx) {
    if (!entry || idx < 0 || idx >= entry->num_frames) return -1;
    free_frame(entry->frames[idx]);
    for (int i = idx; i < entry->num_frames - 1; i++) entry->frames[i] = entry->frames[i+1];
    entry->num_frames--;
    return 0;
}

int TAF_DuplicateFrame(TAFEntry *entry, int idx) {
    if (!entry || idx < 0 || idx >= entry->num_frames) return -1;
    if (ensure_capacity((void ***)&entry->frames, &entry->cap_frames,
                        entry->num_frames + 1, sizeof(TAFFrame *)) != 0) return -1;
    TAFFrame *dup = clone_frame(entry->frames[idx]);
    if (!dup) return -1;
    for (int i = entry->num_frames; i > idx + 1; i--) entry->frames[i] = entry->frames[i-1];
    entry->frames[idx + 1] = dup;
    entry->num_frames++;
    return 0;
}

TAFFile *TAF_Clone(const TAFFile *src) {
    if (!src) return NULL;
    TAFFile *dst = (TAFFile *)calloc(1, sizeof(TAFFile));
    if (!dst) return NULL;
    if (src->num_entries > 0) {
        dst->entries = (TAFEntry **)calloc(src->num_entries, sizeof(TAFEntry *));
        if (!dst->entries) { free(dst); return NULL; }
        dst->cap_entries = src->num_entries;
    }
    for (int i = 0; i < src->num_entries; i++) {
        const TAFEntry *se = src->entries[i];
        TAFEntry *de = (TAFEntry *)calloc(1, sizeof(TAFEntry));
        if (!de) { TAF_Close(dst); return NULL; }
        memcpy(de->name, se->name, sizeof(de->name));
        de->reserved1 = se->reserved1;
        de->reserved2 = se->reserved2;
        if (se->num_frames > 0) {
            de->frames = (TAFFrame **)calloc(se->num_frames, sizeof(TAFFrame *));
            if (!de->frames) { free(de); TAF_Close(dst); return NULL; }
            de->cap_frames = se->num_frames;
        }
        for (int j = 0; j < se->num_frames; j++) {
            de->frames[j] = clone_frame(se->frames[j]);
            if (!de->frames[j]) { free_entry(de); TAF_Close(dst); return NULL; }
            de->num_frames++;
        }
        dst->entries[i] = de;
        dst->num_entries++;
    }
    dst->dirty = src->dirty;
    return dst;
}

int TAF_MoveFrame(TAFEntry *entry, int from, int to) {
    if (!entry || from < 0 || from >= entry->num_frames ||
        to < 0 || to >= entry->num_frames || from == to) return -1;
    TAFFrame *moving = entry->frames[from];
    if (from < to) {
        for (int i = from; i < to; i++) entry->frames[i] = entry->frames[i+1];
    } else {
        for (int i = from; i > to; i--) entry->frames[i] = entry->frames[i-1];
    }
    entry->frames[to] = moving;
    return 0;
}
