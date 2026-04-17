/*
 * png_io.c -- PNG import/export bridge between TAF frames and disk.
 * Uses stb_image / stb_image_write (vcpkg `stb` port).
 */

#include "taf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int TAF_ExportFrameToPNG(const TAFFrame *frame, const char *path) {
    if (!frame || !path) return -1;
    uint32_t *rgba = TAF_DecodeFrameRGBA(frame);
    if (!rgba) return -1;
    int ok = stbi_write_png(path, frame->width, frame->height, 4,
                            rgba, frame->width * 4);
    free(rgba);
    return ok ? 0 : -1;
}

int TAF_ImportPNGToFrame(TAFFrame *frame, const char *path) {
    if (!frame || !path) return -1;

    int w = 0, h = 0, ch = 0;
    uint8_t *rgba8 = stbi_load(path, &w, &h, &ch, 4);
    if (!rgba8 || w <= 0 || h <= 0) {
        if (rgba8) stbi_image_free(rgba8);
        return -1;
    }

    /* Resize the frame's pixel buffer to match the imported PNG. */
    size_t new_total = (size_t)w * h;
    uint16_t *new_pixels = (uint16_t *)malloc(new_total * sizeof(uint16_t));
    if (!new_pixels) { stbi_image_free(rgba8); return -1; }

    for (size_t i = 0; i < new_total; i++) {
        uint8_t r = rgba8[i * 4 + 0];
        uint8_t g = rgba8[i * 4 + 1];
        uint8_t b = rgba8[i * 4 + 2];
        uint8_t a = rgba8[i * 4 + 3];
        new_pixels[i] = (frame->format == TAF_FORMAT_1555)
            ? TAF_EncodePixel_1555(r, g, b, a)
            : TAF_EncodePixel_4444(r, g, b, a);
    }

    stbi_image_free(rgba8);
    free(frame->pixels);
    frame->pixels = new_pixels;
    frame->width  = (uint16_t)w;
    frame->height = (uint16_t)h;
    return 0;
}
