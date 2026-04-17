/*
 * taf.c -- TAF (Truecolor Animation Format) loader and decoder
 *
 * TAF files use the same container as GAF (header, entry/frame tables)
 * but store 16-bit truecolor pixels instead of 8-bit palette indices.
 * No palette needed.
 *
 * Pixel formats:
 *   ARGB 1555 (format=5): bit15=alpha, bits14-10=R, bits9-5=G, bits4-0=B
 *   ARGB 4444 (format=4): bits15-12=A, bits11-8=R, bits7-4=G, bits3-0=B
 *
 * Pixel data is raw uncompressed: width * height * 2 bytes.
 * 0x0000 = fully transparent.
 */

#include "taf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAF_HEADER_SIZE       12
#define TAF_ENTRY_HEADER_SIZE 40

int TAF_Open(TAFFile **out, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "Cannot open: %s\n", path);
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size < TAF_HEADER_SIZE) {
        fclose(fp);
        return -1;
    }

    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) { fclose(fp); return -1; }

    fread(buffer, file_size, 1, fp);
    fclose(fp);

    if (*(uint32_t *)buffer != TAF_VERSION_MAGIC) {
        fprintf(stderr, "Invalid TAF version in: %s\n", path);
        free(buffer);
        return -1;
    }

    TAFFile *taf = (TAFFile *)malloc(sizeof(TAFFile));
    if (!taf) { free(buffer); return -1; }

    taf->data = buffer;
    taf->data_size = (uint32_t)file_size;
    taf->num_entries = *(uint32_t *)(buffer + 4);

    *out = taf;
    return 0;
}

void TAF_Close(TAFFile *taf) {
    if (taf) {
        free(taf->data);
        free(taf);
    }
}

int TAF_GetEntryCount(TAFFile *taf) {
    return taf ? (int)taf->num_entries : 0;
}

static uint32_t taf_entry_offset(TAFFile *taf, int entry_index) {
    return *(uint32_t *)(taf->data + TAF_HEADER_SIZE + entry_index * 4);
}

int TAF_GetEntryInfo(TAFFile *taf, int entry_index, TAFEntryHeader **out) {
    if (!taf || entry_index < 0 || entry_index >= (int)taf->num_entries)
        return -1;
    *out = (TAFEntryHeader *)(taf->data + taf_entry_offset(taf, entry_index));
    return 0;
}

int TAF_GetFrameInfo(TAFFile *taf, int entry_index, int frame_index, TAFFrameHeader **out) {
    if (!taf || entry_index < 0 || entry_index >= (int)taf->num_entries)
        return -1;

    uint32_t entry_off = taf_entry_offset(taf, entry_index);
    TAFEntryHeader *entry = (TAFEntryHeader *)(taf->data + entry_off);

    if (frame_index < 0 || frame_index >= entry->num_frames)
        return -1;

    uint8_t *frame_ptr_table = taf->data + entry_off + TAF_ENTRY_HEADER_SIZE;
    uint32_t frame_off = *(uint32_t *)(frame_ptr_table + frame_index * 8);
    *out = (TAFFrameHeader *)(taf->data + frame_off);
    return 0;
}

uint32_t *TAF_DecodeFrameRGBA(TAFFile *taf, const TAFFrameHeader *frame) {
    if (!taf || !frame) return NULL;

    size_t total_pixels = (size_t)frame->width * frame->height;
    size_t data_size = total_pixels * 2;

    if (frame->pixel_data_offset + data_size > taf->data_size) return NULL;

    uint16_t *src = (uint16_t *)(taf->data + frame->pixel_data_offset);
    uint32_t *rgba = (uint32_t *)malloc(total_pixels * sizeof(uint32_t));
    if (!rgba) return NULL;

    if (frame->format == TAF_FORMAT_1555) {
        for (size_t i = 0; i < total_pixels; i++) {
            uint16_t px = src[i];
            if (px == 0) {
                rgba[i] = 0x00000000;
            } else {
                uint8_t a = (px & 0x8000) ? 255 : 0;
                uint8_t r = (uint8_t)(((px >> 10) & 0x1F) * 255 / 31);
                uint8_t g = (uint8_t)(((px >> 5) & 0x1F) * 255 / 31);
                uint8_t b = (uint8_t)((px & 0x1F) * 255 / 31);
                rgba[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                          ((uint32_t)g << 8) | (uint32_t)r;
            }
        }
    } else if (frame->format == TAF_FORMAT_4444) {
        for (size_t i = 0; i < total_pixels; i++) {
            uint16_t px = src[i];
            if (px == 0) {
                rgba[i] = 0x00000000;
            } else {
                uint8_t a = (uint8_t)(((px >> 12) & 0xF) * 17);
                uint8_t r = (uint8_t)(((px >> 8) & 0xF) * 17);
                uint8_t g = (uint8_t)(((px >> 4) & 0xF) * 17);
                uint8_t b = (uint8_t)((px & 0xF) * 17);
                rgba[i] = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                          ((uint32_t)g << 8) | (uint32_t)r;
            }
        }
    } else {
        free(rgba);
        return NULL;
    }

    return rgba;
}
