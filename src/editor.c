/*
 * editor.c -- pixel painting tools + undo/redo.
 *
 * Undo stack holds two kinds of entries:
 *   UNDO_PIXELS: before/after snapshots of one frame's pixel buffer
 *                (fast, used for pencil/eraser/fill strokes)
 *   UNDO_TREE:   before/after full-tree snapshots
 *                (used for structural ops: add/delete/duplicate, PNG import)
 *
 * Cap = UNDO_CAP. When full, the oldest entry is evicted.
 */

#include "editor.h"
#include <stdlib.h>
#include <string.h>

static void free_undo_entry(UndoEntry *u) {
    if (!u) return;
    free(u->pixels_before);
    free(u->pixels_after);
    if (u->tree_before) TAF_Close(u->tree_before);
    if (u->tree_after)  TAF_Close(u->tree_after);
    free(u);
}

static void stack_clear(UndoEntry **stack, int *top) {
    for (int i = 0; i < *top; i++) { free_undo_entry(stack[i]); stack[i] = NULL; }
    *top = 0;
}

static void push_undo(EditorState *e, UndoEntry *u) {
    if (e->undo_top == UNDO_CAP) {
        free_undo_entry(e->undo[0]);
        memmove(&e->undo[0], &e->undo[1], sizeof(UndoEntry *) * (UNDO_CAP - 1));
        e->undo_top--;
    }
    e->undo[e->undo_top++] = u;
    /* Any new edit clears the redo stack. */
    stack_clear(e->redo, &e->redo_top);
}

/* ── Init / teardown ─────────────────────────────────────────────── */

void Editor_Init(EditorState *e) {
    memset(e, 0, sizeof(*e));
    e->tool  = TOOL_NONE;
    e->cur_r = 255; e->cur_g = 255; e->cur_b = 255; e->cur_a = 255;
}

void Editor_Free(EditorState *e) {
    if (!e) return;
    stack_clear(e->undo, &e->undo_top);
    stack_clear(e->redo, &e->redo_top);
    if (e->pending) { free_undo_entry(e->pending); e->pending = NULL; }
}

/* ── Encode with frame's native format ───────────────────────────── */

static uint16_t encode_for(const TAFFrame *f, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (f->format == TAF_FORMAT_1555)
        ? TAF_EncodePixel_1555(r, g, b, a)
        : TAF_EncodePixel_4444(r, g, b, a);
}

/* ── Pixel-scope workflow ────────────────────────────────────────── */

void Editor_BeginPixelStroke(EditorState *e, int entry_idx, int frame_idx, TAFFrame *frame) {
    if (!e || !frame) return;
    UndoEntry *u = (UndoEntry *)calloc(1, sizeof(UndoEntry));
    if (!u) return;
    u->kind = UNDO_PIXELS;
    u->entry_idx = entry_idx;
    u->frame_idx = frame_idx;
    u->w = frame->width;
    u->h = frame->height;
    size_t bytes = (size_t)frame->width * frame->height * 2;
    u->pixels_before = (uint16_t *)malloc(bytes);
    if (!u->pixels_before) { free(u); return; }
    memcpy(u->pixels_before, frame->pixels, bytes);
    /* pixels_after is captured in EndPixelStroke */
    /* stash on a side slot until End to finalize */
    /* Discard any prior pending (shouldn't happen, but guard against leaks). */
    if (e->pending) { free_undo_entry(e->pending); e->pending = NULL; }
    e->pending      = u;
    e->stroking     = 1;
    e->stroke_entry = entry_idx;
    e->stroke_frame = frame_idx;
}

void Editor_Paint(EditorState *e, TAFFrame *frame, int px, int py) {
    if (!e || !frame || !frame->pixels) return;
    if (px < 0 || py < 0 || px >= frame->width || py >= frame->height) return;
    size_t idx = (size_t)py * frame->width + px;
    if (e->tool == TOOL_PENCIL) {
        frame->pixels[idx] = encode_for(frame, e->cur_r, e->cur_g, e->cur_b, e->cur_a);
    } else if (e->tool == TOOL_ERASER) {
        frame->pixels[idx] = 0x0000;
    }
}

static void flood_fill(TAFFrame *frame, int sx, int sy, uint16_t target, uint16_t replacement) {
    if (target == replacement) return;

    /* stack-based scanline-ish flood fill -- avoids recursion depth issues
       on large regions. Uses a heap-allocated LIFO of (x,y) pairs. */
    size_t cap = 1024;
    int *stk = (int *)malloc(cap * 2 * sizeof(int));
    if (!stk) return;
    int top = 0;
    stk[top*2 + 0] = sx; stk[top*2 + 1] = sy; top++;

    while (top > 0) {
        top--;
        int x = stk[top*2 + 0];
        int y = stk[top*2 + 1];
        if (x < 0 || y < 0 || x >= frame->width || y >= frame->height) continue;
        size_t idx = (size_t)y * frame->width + x;
        if (frame->pixels[idx] != target) continue;
        frame->pixels[idx] = replacement;
        if ((size_t)top + 4 >= cap) {
            cap *= 2;
            int *grown = (int *)realloc(stk, cap * 2 * sizeof(int));
            if (!grown) { free(stk); return; }
            stk = grown;
        }
        stk[top*2 + 0] = x+1; stk[top*2 + 1] = y; top++;
        stk[top*2 + 0] = x-1; stk[top*2 + 1] = y; top++;
        stk[top*2 + 0] = x;   stk[top*2 + 1] = y+1; top++;
        stk[top*2 + 0] = x;   stk[top*2 + 1] = y-1; top++;
    }
    free(stk);
}

void Editor_Fill(EditorState *e, TAFFrame *frame, int px, int py) {
    if (!e || !frame || !frame->pixels) return;
    if (px < 0 || py < 0 || px >= frame->width || py >= frame->height) return;
    size_t idx = (size_t)py * frame->width + px;
    uint16_t target = frame->pixels[idx];
    uint16_t repl = encode_for(frame, e->cur_r, e->cur_g, e->cur_b, e->cur_a);
    flood_fill(frame, px, py, target, repl);
}

int Editor_Sample(const TAFFrame *frame, int px, int py,
                  uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (!frame || !frame->pixels) return -1;
    if (px < 0 || py < 0 || px >= frame->width || py >= frame->height) return -1;
    uint16_t px16 = frame->pixels[(size_t)py * frame->width + px];
    if (px16 == 0) { *r=*g=*b=*a=0; return 0; }
    if (frame->format == TAF_FORMAT_1555) {
        *a = (px16 & 0x8000) ? 255 : 0;
        *r = (uint8_t)(((px16 >> 10) & 0x1F) * 255 / 31);
        *g = (uint8_t)(((px16 >>  5) & 0x1F) * 255 / 31);
        *b = (uint8_t)(( px16        & 0x1F) * 255 / 31);
    } else {
        *a = (uint8_t)(((px16 >> 12) & 0xF) * 17);
        *r = (uint8_t)(((px16 >>  8) & 0xF) * 17);
        *g = (uint8_t)(((px16 >>  4) & 0xF) * 17);
        *b = (uint8_t)(( px16        & 0xF) * 17);
    }
    return 0;
}

void Editor_EndPixelStroke(EditorState *e, TAFFrame *frame) {
    if (!e || !frame) return;
    if (!e->stroking || !e->pending) { e->stroking = 0; return; }

    UndoEntry *u = e->pending;
    e->pending = NULL;

    size_t bytes = (size_t)frame->width * frame->height * 2;
    u->pixels_after = (uint16_t *)malloc(bytes);
    if (!u->pixels_after) { free_undo_entry(u); e->stroking = 0; return; }
    memcpy(u->pixels_after, frame->pixels, bytes);

    /* Discard a zero-delta stroke to keep undo history tidy. */
    if (memcmp(u->pixels_before, u->pixels_after, bytes) == 0) {
        free_undo_entry(u);
    } else {
        push_undo(e, u);
    }
    e->stroking = 0;
}

/* ── Tree-scope snapshots ────────────────────────────────────────── */

void Editor_PushTreeSnapshot(EditorState *e, const TAFFile *before, const TAFFile *after) {
    if (!e || !before || !after) return;
    UndoEntry *u = (UndoEntry *)calloc(1, sizeof(UndoEntry));
    if (!u) return;
    u->kind = UNDO_TREE;
    u->tree_before = TAF_Clone(before);
    u->tree_after  = TAF_Clone(after);
    if (!u->tree_before || !u->tree_after) { free_undo_entry(u); return; }
    push_undo(e, u);
}

/* ── Undo / Redo ─────────────────────────────────────────────────── */

static void apply_pixels(TAFFile *taf, int entry_idx, int frame_idx,
                         uint16_t w, uint16_t h, const uint16_t *pixels) {
    TAFFrame *f = NULL;
    if (TAF_GetFrameInfo(taf, entry_idx, frame_idx, &f) != 0) return;
    size_t bytes = (size_t)w * h * 2;
    if (f->width != w || f->height != h) {
        uint16_t *buf = (uint16_t *)malloc(bytes);
        if (!buf) return;
        free(f->pixels);
        f->pixels = buf;
        f->width = w; f->height = h;
    }
    memcpy(f->pixels, pixels, bytes);
}

int Editor_Undo(EditorState *e, TAFFile **taf) {
    if (!e || !taf || !*taf || e->undo_top == 0) return -1;
    UndoEntry *u = e->undo[--e->undo_top];

    if (u->kind == UNDO_PIXELS) {
        apply_pixels(*taf, u->entry_idx, u->frame_idx, u->w, u->h, u->pixels_before);
    } else {
        TAFFile *restored = TAF_Clone(u->tree_before);
        if (!restored) { e->undo_top++; return -1; }  /* abort */
        TAF_Close(*taf);
        *taf = restored;
    }

    /* Move to redo stack so Redo can reverse it. */
    if (e->redo_top < UNDO_CAP) e->redo[e->redo_top++] = u;
    else { free_undo_entry(u); }   /* shouldn't happen in practice */
    (*taf)->dirty = 1;
    return 0;
}

int Editor_Redo(EditorState *e, TAFFile **taf) {
    if (!e || !taf || !*taf || e->redo_top == 0) return -1;
    UndoEntry *u = e->redo[--e->redo_top];

    if (u->kind == UNDO_PIXELS) {
        apply_pixels(*taf, u->entry_idx, u->frame_idx, u->w, u->h, u->pixels_after);
    } else {
        TAFFile *restored = TAF_Clone(u->tree_after);
        if (!restored) { e->redo_top++; return -1; }
        TAF_Close(*taf);
        *taf = restored;
    }

    if (e->undo_top < UNDO_CAP) e->undo[e->undo_top++] = u;
    else { free_undo_entry(u); }
    (*taf)->dirty = 1;
    return 0;
}
