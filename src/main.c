/*
 * main.c -- TAF Viewer
 *
 * A cross-platform viewer for TA: Kingdoms TAF (Truecolor Animation Format)
 * sprite files. TAF files are 16-bit truecolor -- no palette needed.
 *
 * Usage: taf-viewer [file.taf]
 *
 * Controls:
 *   Left/Right  - Step through frames (crosses entry boundaries)
 *   Up/Down     - Jump between entries
 *   Space       - Play/pause entry animation (game-style)
 *   A           - Play/pause all-frames animation
 *   F           - Toggle checkerboard transparency background
 *   +/-         - Zoom in/out
 *   [/]         - Slower/faster animation
 *   O           - Open TAF file
 *   Escape      - Quit
 *
 * Drag & drop .taf files onto the window.
 */

#include "taf.h"
#include "editor.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

/* ── Animation modes ────────────────────────────────────────────── */
enum { ANIM_STOPPED, ANIM_ENTRIES, ANIM_ALL_FRAMES };

/* ── UI button IDs ──────────────────────────────────────────────── */
enum {
    BTN_OPEN = 0,
    BTN_SAVE_AS,
    BTN_EXPORT_PNG,
    BTN_PREV,
    BTN_PLAY_ENTRIES,
    BTN_STOP,
    BTN_PLAY_ALL,
    BTN_NEXT,
    BTN_SLOWER,
    BTN_FASTER,
    BTN_ZOOM_OUT,
    BTN_ZOOM_IN,
    BTN_CHECKER,
    /* edit row */
    BTN_NEW_ENTRY,
    BTN_DEL_ENTRY,
    BTN_DUP_FRAME,
    BTN_DEL_FRAME,
    BTN_RENAME,
    BTN_IMPORT_PNG,
    BTN_PENCIL,
    BTN_ERASER,
    BTN_EYEDROPPER,
    BTN_FILL,
    BTN_UNDO,
    BTN_REDO,
    BTN_COUNT
};

/* ── UI Constants ───────────────────────────────────────────────── */
#define TOOLBAR_HEIGHT  96
#define TOOLBAR_ROW_H   44
#define FONT_SCALE      2
#define BTN_H          34
#define BTN_PAD         5
#define PANEL_TOP_H    28

typedef struct {
    SDL_Rect rect;
    const char *label;
    int hovered;
} UIButton;

typedef struct {
    TAFFile *taf;
    int has_taf;

    int current_entry;
    int current_frame;
    int entry_count;

    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *frame_texture;
    SDL_Texture *checker_cache;
    int checker_w, checker_h, checker_zoom;

    int anim_mode;
    float anim_timer;
    float frame_duration;

    int show_checkerboard;
    int zoom;

    UIButton buttons[BTN_COUNT];
    char taf_path[512];

    /* rename widget state */
    int  is_renaming;
    char rename_buffer[32];
    int  rename_len;

    /* editor (paint + undo/redo) */
    EditorState editor;

    /* last computed canvas rect, for mouse → pixel mapping */
    SDL_Rect canvas_rect;
    int      canvas_valid;

    /* color picker drag state */
    int picker_drag;   /* 0=none, 1=R, 2=G, 3=B, 4=A */
} ViewerState;

/* Forward declarations */
static void update_frame_texture(ViewerState *v);
static void clamp_cursor(ViewerState *v);

/* ── Tiny bitmap font (4x6 pixels per glyph) ─────────────────── */

static const uint8_t tiny_font[96][6] = {
    {0x0,0x0,0x0,0x0,0x0,0x0}, {0x4,0x4,0x4,0x4,0x0,0x4},
    {0xA,0xA,0x0,0x0,0x0,0x0}, {0xA,0xF,0xA,0xF,0xA,0x0},
    {0x4,0xE,0xC,0x6,0xE,0x4}, {0x9,0x2,0x4,0x2,0x9,0x0},
    {0x4,0xA,0x4,0xA,0x5,0x0}, {0x4,0x4,0x0,0x0,0x0,0x0},
    {0x2,0x4,0x4,0x4,0x2,0x0}, {0x4,0x2,0x2,0x2,0x4,0x0},
    {0x0,0xA,0x4,0xA,0x0,0x0}, {0x0,0x4,0xE,0x4,0x0,0x0},
    {0x0,0x0,0x0,0x4,0x4,0x8}, {0x0,0x0,0xE,0x0,0x0,0x0},
    {0x0,0x0,0x0,0x0,0x4,0x0}, {0x1,0x2,0x4,0x4,0x8,0x0},
    {0x6,0x9,0x9,0x9,0x6,0x0}, {0x4,0xC,0x4,0x4,0xE,0x0},
    {0x6,0x9,0x2,0x4,0xF,0x0}, {0xE,0x1,0x6,0x1,0xE,0x0},
    {0x2,0x6,0xA,0xF,0x2,0x0}, {0xF,0x8,0xE,0x1,0xE,0x0},
    {0x6,0x8,0xE,0x9,0x6,0x0}, {0xF,0x1,0x2,0x4,0x4,0x0},
    {0x6,0x9,0x6,0x9,0x6,0x0}, {0x6,0x9,0x7,0x1,0x6,0x0},
    {0x0,0x4,0x0,0x4,0x0,0x0}, {0x0,0x4,0x0,0x4,0x8,0x0},
    {0x2,0x4,0x8,0x4,0x2,0x0}, {0x0,0xE,0x0,0xE,0x0,0x0},
    {0x8,0x4,0x2,0x4,0x8,0x0}, {0x6,0x1,0x2,0x0,0x2,0x0},
    {0x6,0x9,0xB,0x8,0x6,0x0}, {0x6,0x9,0xF,0x9,0x9,0x0},
    {0xE,0x9,0xE,0x9,0xE,0x0}, {0x7,0x8,0x8,0x8,0x7,0x0},
    {0xE,0x9,0x9,0x9,0xE,0x0}, {0xF,0x8,0xE,0x8,0xF,0x0},
    {0xF,0x8,0xE,0x8,0x8,0x0}, {0x7,0x8,0xB,0x9,0x7,0x0},
    {0x9,0x9,0xF,0x9,0x9,0x0}, {0xE,0x4,0x4,0x4,0xE,0x0},
    {0x1,0x1,0x1,0x9,0x6,0x0}, {0x9,0xA,0xC,0xA,0x9,0x0},
    {0x8,0x8,0x8,0x8,0xF,0x0}, {0x9,0xF,0xF,0x9,0x9,0x0},
    {0x9,0xD,0xF,0xB,0x9,0x0}, {0x6,0x9,0x9,0x9,0x6,0x0},
    {0xE,0x9,0xE,0x8,0x8,0x0}, {0x6,0x9,0x9,0xA,0x5,0x0},
    {0xE,0x9,0xE,0xA,0x9,0x0}, {0x7,0x8,0x6,0x1,0xE,0x0},
    {0xE,0x4,0x4,0x4,0x4,0x0}, {0x9,0x9,0x9,0x9,0x6,0x0},
    {0x9,0x9,0x9,0x6,0x6,0x0}, {0x9,0x9,0xF,0xF,0x9,0x0},
    {0x9,0x9,0x6,0x9,0x9,0x0}, {0x9,0x9,0x6,0x4,0x4,0x0},
    {0xF,0x2,0x4,0x8,0xF,0x0}, {0x6,0x4,0x4,0x4,0x6,0x0},
    {0x8,0x4,0x4,0x2,0x1,0x0}, {0x6,0x2,0x2,0x2,0x6,0x0},
    {0x4,0xA,0x0,0x0,0x0,0x0}, {0x0,0x0,0x0,0x0,0xF,0x0},
    {0x8,0x4,0x0,0x0,0x0,0x0},
    {0x0,0x6,0x9,0x9,0x7,0x0}, {0x8,0xE,0x9,0x9,0xE,0x0},
    {0x0,0x7,0x8,0x8,0x7,0x0}, {0x1,0x7,0x9,0x9,0x7,0x0},
    {0x0,0x6,0xF,0x8,0x7,0x0}, {0x3,0x4,0xE,0x4,0x4,0x0},
    {0x0,0x7,0x9,0x7,0x1,0x6}, {0x8,0xE,0x9,0x9,0x9,0x0},
    {0x4,0x0,0x4,0x4,0x4,0x0}, {0x2,0x0,0x2,0x2,0xA,0x4},
    {0x8,0x9,0xE,0xA,0x9,0x0}, {0xC,0x4,0x4,0x4,0xE,0x0},
    {0x0,0xF,0xF,0x9,0x9,0x0}, {0x0,0xE,0x9,0x9,0x9,0x0},
    {0x0,0x6,0x9,0x9,0x6,0x0}, {0x0,0xE,0x9,0xE,0x8,0x8},
    {0x0,0x7,0x9,0x7,0x1,0x1}, {0x0,0x7,0x8,0x8,0x8,0x0},
    {0x0,0x7,0xC,0x3,0xE,0x0}, {0x4,0xE,0x4,0x4,0x3,0x0},
    {0x0,0x9,0x9,0x9,0x7,0x0}, {0x0,0x9,0x9,0x6,0x6,0x0},
    {0x0,0x9,0x9,0xF,0x6,0x0}, {0x0,0x9,0x6,0x6,0x9,0x0},
    {0x0,0x9,0x9,0x7,0x1,0x6}, {0x0,0xF,0x2,0x4,0xF,0x0},
    {0x2,0x4,0x8,0x4,0x2,0x0}, {0x4,0x4,0x4,0x4,0x4,0x0},
    {0x8,0x4,0x2,0x4,0x8,0x0}, {0x0,0x5,0xA,0x0,0x0,0x0},
};

static void draw_char(SDL_Renderer *r, int x, int y, char ch, int scale) {
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= 96) return;
    const uint8_t *glyph = tiny_font[idx];
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 4; col++)
            if (glyph[row] & (8 >> col)) {
                SDL_Rect px = { x + col*scale, y + row*scale, scale, scale };
                SDL_RenderFillRect(r, &px);
            }
}

static void draw_text(SDL_Renderer *r, int x, int y, const char *text, int scale) {
    for (int i = 0; text[i]; i++)
        draw_char(r, x + i * 5 * scale, y, text[i], scale);
}

static int text_width(const char *text, int scale) {
    return (int)strlen(text) * 5 * scale;
}

/* ── File dialog ────────────────────────────────────────────────── */

#ifdef _WIN32
static int open_file_dialog_filter(char *out, int out_size, const char *title, const char *filter) {
    OPENFILENAMEA ofn = {0};
    char file[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) { strncpy(out, file, out_size - 1); return 0; }
    return -1;
}
static int open_file_dialog(char *out, int out_size) {
    return open_file_dialog_filter(out, out_size, "Open TAF Sprite File",
                                   "TAF Files (*.taf)\0*.taf\0All Files\0*.*\0");
}
static int save_file_dialog(char *out, int out_size, const char *title,
                            const char *filter, const char *default_ext) {
    OPENFILENAMEA ofn = {0};
    char file[MAX_PATH] = {0};
    if (out[0]) strncpy(file, out, MAX_PATH - 1);
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = default_ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn)) { strncpy(out, file, out_size - 1); return 0; }
    return -1;
}
#else
static int open_file_dialog_filter(char *out, int out_size, const char *title, const char *filter) {
    (void)out; (void)out_size; (void)title; (void)filter;
    fprintf(stderr, "Open dialog not available. Use command line or drag & drop.\n");
    return -1;
}
static int open_file_dialog(char *out, int out_size) {
    return open_file_dialog_filter(out, out_size, "Open TAF Sprite File",
                                   "TAF Files (*.taf)\0*.taf\0All Files\0*.*\0");
}
static int save_file_dialog(char *out, int out_size, const char *title,
                            const char *filter, const char *default_ext) {
    (void)out; (void)out_size; (void)title; (void)filter; (void)default_ext;
    fprintf(stderr, "Save dialog not available on this platform.\n");
    return -1;
}
#endif

/* ── Frame navigation ───────────────────────────────────────────── */

static void advance_frame(ViewerState *v) {
    if (!v->has_taf) return;
    TAFEntry *entry = NULL;
    TAF_GetEntryInfo(v->taf, v->current_entry, &entry);
    if (!entry) return;
    v->current_frame++;
    if (v->current_frame >= entry->num_frames) {
        v->current_frame = 0;
        v->current_entry++;
        if (v->current_entry >= v->entry_count) v->current_entry = 0;
    }
    update_frame_texture(v);
}

static void retreat_frame(ViewerState *v) {
    if (!v->has_taf) return;
    v->current_frame--;
    if (v->current_frame < 0) {
        v->current_entry--;
        if (v->current_entry < 0) v->current_entry = v->entry_count - 1;
        TAFEntry *entry = NULL;
        TAF_GetEntryInfo(v->taf, v->current_entry, &entry);
        v->current_frame = entry ? entry->num_frames - 1 : 0;
    }
    update_frame_texture(v);
}

static void advance_entry_anim(ViewerState *v) {
    if (!v->has_taf) return;
    v->current_entry++;
    if (v->current_entry >= v->entry_count) v->current_entry = 0;
    v->current_frame = 0;
    update_frame_texture(v);
}

static int total_frame_count(ViewerState *v) {
    int total = 0;
    for (int i = 0; i < v->entry_count; i++) {
        TAFEntry *e = NULL;
        TAF_GetEntryInfo(v->taf, i, &e);
        if (e) total += e->num_frames;
    }
    return total;
}

static int global_frame_index(ViewerState *v) {
    int idx = 0;
    for (int i = 0; i < v->current_entry; i++) {
        TAFEntry *e = NULL;
        TAF_GetEntryInfo(v->taf, i, &e);
        if (e) idx += e->num_frames;
    }
    return idx + v->current_frame;
}

/* ── Update texture ─────────────────────────────────────────────── */

static void update_frame_texture(ViewerState *v) {
    if (v->frame_texture) { SDL_DestroyTexture(v->frame_texture); v->frame_texture = NULL; }
    if (!v->has_taf) return;

    TAFEntry *entry = NULL;
    if (TAF_GetEntryInfo(v->taf, v->current_entry, &entry) != 0) return;
    TAFFrame *frame = NULL;
    if (TAF_GetFrameInfo(v->taf, v->current_entry, v->current_frame, &frame) != 0) return;

    uint32_t *rgba = TAF_DecodeFrameRGBA(frame);
    if (!rgba) return;

    v->frame_texture = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC,
                                          frame->width, frame->height);
    if (v->frame_texture) {
        SDL_SetTextureBlendMode(v->frame_texture, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(v->frame_texture, NULL, rgba, frame->width * 4);
    }

    const char *fmt = (frame->format == TAF_FORMAT_1555) ? "ARGB1555" : "ARGB4444";
    const char *dirty = v->taf->dirty ? "* " : "";
    char title[512];
    snprintf(title, sizeof(title), "TAF Viewer - %s%s  |  %s [%d/%d]  Frame %d/%d  (%dx%d)  [%s]",
             dirty, v->taf_path, entry->name,
             v->current_entry + 1, v->entry_count,
             v->current_frame + 1, entry->num_frames,
             frame->width, frame->height, fmt);
    SDL_SetWindowTitle(v->window, title);

    free(rgba);
}

/* ── Load TAF ───────────────────────────────────────────────────── */

static int confirm_discard_dirty(ViewerState *v, const char *title) {
    if (!v->has_taf || !v->taf->dirty) return 1;
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Discard" },
    };
    const SDL_MessageBoxData mbd = {
        SDL_MESSAGEBOX_WARNING, v->window, title,
        "This file has unsaved changes. Discard them?",
        SDL_arraysize(buttons), buttons, NULL,
    };
    int choice = 0;
    if (SDL_ShowMessageBox(&mbd, &choice) < 0) return 1;  /* fall open on failure */
    return choice == 1;
}

static void load_taf_file(ViewerState *v, const char *path) {
    if (!confirm_discard_dirty(v, "Open another TAF")) return;
    TAFFile *new_taf = NULL;
    if (TAF_Open(&new_taf, path) != 0) return;
    if (v->has_taf) TAF_Close(v->taf);
    /* Drop any undo/redo history from the previously-loaded file. */
    Editor_Free(&v->editor);
    Editor_Init(&v->editor);
    v->taf = new_taf;
    v->has_taf = 1;
    v->entry_count = TAF_GetEntryCount(v->taf);
    v->current_entry = 0;
    v->current_frame = 0;
    v->anim_mode = ANIM_STOPPED;
    strncpy(v->taf_path, path, sizeof(v->taf_path) - 1);
    if (v->checker_cache) { SDL_DestroyTexture(v->checker_cache); v->checker_cache = NULL; }
    update_frame_texture(v);
}

static void save_taf_as(ViewerState *v) {
    if (!v->has_taf) return;
    char path[512] = {0};
    strncpy(path, v->taf_path, sizeof(path) - 1);
    if (save_file_dialog(path, sizeof(path), "Save TAF As",
            "TAF Files (*.taf)\0*.taf\0All Files\0*.*\0", "taf") != 0) return;
    if (TAF_Save(v->taf, path) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Save failed",
                                 "Could not write TAF file.", v->window);
        return;
    }
    strncpy(v->taf_path, path, sizeof(v->taf_path) - 1);
    update_frame_texture(v);
}

/* ── Editor glue ─────────────────────────────────────────────────── */

static void wrap_tree_op(ViewerState *v, void (*op)(ViewerState *)) {
    if (!v->has_taf) return;
    TAFFile *before = TAF_Clone(v->taf);
    op(v);
    if (before) {
        Editor_PushTreeSnapshot(&v->editor, before, v->taf);
        TAF_Close(before);
    }
}

static int canvas_mouse_to_pixel(ViewerState *v, int mx, int my, int *px, int *py) {
    if (!v->canvas_valid) return 0;
    if (mx < v->canvas_rect.x || my < v->canvas_rect.y ||
        mx >= v->canvas_rect.x + v->canvas_rect.w ||
        my >= v->canvas_rect.y + v->canvas_rect.h) return 0;
    *px = (mx - v->canvas_rect.x) / v->zoom;
    *py = (my - v->canvas_rect.y) / v->zoom;
    return 1;
}

static TAFFrame *current_frame(ViewerState *v) {
    TAFFrame *f = NULL;
    if (!v->has_taf) return NULL;
    TAF_GetFrameInfo(v->taf, v->current_entry, v->current_frame, &f);
    return f;
}

static void apply_paint(ViewerState *v, int mx, int my) {
    TAFFrame *f = current_frame(v);
    if (!f) return;
    int px, py;
    if (!canvas_mouse_to_pixel(v, mx, my, &px, &py)) return;

    if (v->editor.tool == TOOL_PENCIL || v->editor.tool == TOOL_ERASER) {
        Editor_Paint(&v->editor, f, px, py);
    } else if (v->editor.tool == TOOL_FILL) {
        Editor_Fill(&v->editor, f, px, py);
    } else if (v->editor.tool == TOOL_EYEDROPPER) {
        uint8_t r, g, b, a;
        if (Editor_Sample(f, px, py, &r, &g, &b, &a) == 0 && a > 0) {
            v->editor.cur_r = r;
            v->editor.cur_g = g;
            v->editor.cur_b = b;
            v->editor.cur_a = a;
        }
    }
    v->taf->dirty = 1;
    update_frame_texture(v);
}

static void do_undo(ViewerState *v) {
    if (!v->has_taf) return;
    if (Editor_Undo(&v->editor, &v->taf) != 0) return;
    v->entry_count = TAF_GetEntryCount(v->taf);
    clamp_cursor(v);
    if (v->checker_cache) { SDL_DestroyTexture(v->checker_cache); v->checker_cache = NULL; }
    update_frame_texture(v);
}

static void do_redo(ViewerState *v) {
    if (!v->has_taf) return;
    if (Editor_Redo(&v->editor, &v->taf) != 0) return;
    v->entry_count = TAF_GetEntryCount(v->taf);
    clamp_cursor(v);
    if (v->checker_cache) { SDL_DestroyTexture(v->checker_cache); v->checker_cache = NULL; }
    update_frame_texture(v);
}

static void clamp_cursor(ViewerState *v) {
    if (v->current_entry >= v->entry_count) v->current_entry = v->entry_count - 1;
    if (v->current_entry < 0) v->current_entry = 0;
    TAFEntry *e = NULL;
    TAF_GetEntryInfo(v->taf, v->current_entry, &e);
    if (e && v->current_frame >= e->num_frames) v->current_frame = e->num_frames - 1;
    if (v->current_frame < 0) v->current_frame = 0;
}

static void new_entry_op(ViewerState *v) {
    char name[32];
    snprintf(name, sizeof(name), "entry_%d", v->entry_count);
    TAFEntry *e = TAF_NewEntry(v->taf, name);
    if (!e) return;
    uint8_t fmt = TAF_FORMAT_1555;
    if (v->entry_count > 0) {
        TAFFrame *ref = NULL;
        if (TAF_GetFrameInfo(v->taf, 0, 0, &ref) == 0) fmt = ref->format;
    }
    TAF_NewFrame(e, 32, 32, fmt);
    v->entry_count = TAF_GetEntryCount(v->taf);
    v->current_entry = v->entry_count - 1;
    v->current_frame = 0;
    update_frame_texture(v);
}
static void do_new_entry(ViewerState *v) { wrap_tree_op(v, new_entry_op); }

static void delete_entry_op(ViewerState *v) {
    if (v->entry_count <= 1) return;
    TAF_DeleteEntry(v->taf, v->current_entry);
    v->entry_count = TAF_GetEntryCount(v->taf);
    clamp_cursor(v);
    update_frame_texture(v);
}
static void do_delete_entry(ViewerState *v) {
    if (!v->has_taf || v->entry_count <= 1) return;
    wrap_tree_op(v, delete_entry_op);
}

static void dup_frame_op(ViewerState *v) {
    TAFEntry *e = NULL;
    if (TAF_GetEntryInfo(v->taf, v->current_entry, &e) != 0) return;
    if (TAF_DuplicateFrame(e, v->current_frame) != 0) return;
    v->taf->dirty = 1;
    v->current_frame++;
    update_frame_texture(v);
}
static void do_dup_frame(ViewerState *v) { wrap_tree_op(v, dup_frame_op); }

static void delete_frame_op(ViewerState *v) {
    TAFEntry *e = NULL;
    if (TAF_GetEntryInfo(v->taf, v->current_entry, &e) != 0) return;
    if (e->num_frames <= 1) return;
    TAF_DeleteFrame(e, v->current_frame);
    v->taf->dirty = 1;
    clamp_cursor(v);
    update_frame_texture(v);
}
static void do_delete_frame(ViewerState *v) {
    if (!v->has_taf) return;
    TAFEntry *e = NULL;
    if (TAF_GetEntryInfo(v->taf, v->current_entry, &e) != 0) return;
    if (e->num_frames <= 1) return;
    wrap_tree_op(v, delete_frame_op);
}

static void start_rename(ViewerState *v) {
    if (!v->has_taf) return;
    TAFEntry *e = NULL;
    if (TAF_GetEntryInfo(v->taf, v->current_entry, &e) != 0) return;
    memset(v->rename_buffer, 0, sizeof(v->rename_buffer));
    strncpy(v->rename_buffer, e->name, sizeof(v->rename_buffer) - 1);
    v->rename_len = (int)strlen(v->rename_buffer);
    v->is_renaming = 1;
    SDL_StartTextInput();
}

static void commit_rename(ViewerState *v) {
    if (!v->is_renaming) return;
    TAFFile *before = TAF_Clone(v->taf);
    TAF_RenameEntry(v->taf, v->current_entry, v->rename_buffer);
    if (before) {
        Editor_PushTreeSnapshot(&v->editor, before, v->taf);
        TAF_Close(before);
    }
    v->is_renaming = 0;
    SDL_StopTextInput();
    update_frame_texture(v);
}

static void cancel_rename(ViewerState *v) {
    v->is_renaming = 0;
    SDL_StopTextInput();
}

static void do_import_png(ViewerState *v) {
    if (!v->has_taf) return;
    TAFFrame *frame = NULL;
    if (TAF_GetFrameInfo(v->taf, v->current_entry, v->current_frame, &frame) != 0) return;

    char path[512] = {0};
    if (open_file_dialog_filter(path, sizeof(path), "Import PNG into Frame",
            "PNG Images (*.png)\0*.png\0All Files\0*.*\0") != 0) return;

    TAFFile *before = TAF_Clone(v->taf);
    if (TAF_ImportPNGToFrame(frame, path) != 0) {
        if (before) TAF_Close(before);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Import failed",
                                 "Could not load PNG (decode error or out of memory).", v->window);
        return;
    }
    if (before) {
        Editor_PushTreeSnapshot(&v->editor, before, v->taf);
        TAF_Close(before);
    }
    v->taf->dirty = 1;
    update_frame_texture(v);
}

static void export_frame_png(ViewerState *v) {
    if (!v->has_taf) return;
    TAFFrame *frame = NULL;
    if (TAF_GetFrameInfo(v->taf, v->current_entry, v->current_frame, &frame) != 0) return;

    TAFEntry *entry = NULL;
    TAF_GetEntryInfo(v->taf, v->current_entry, &entry);

    char path[512] = {0};
    if (entry) {
        snprintf(path, sizeof(path), "%s_%d.png", entry->name, v->current_frame);
    }
    if (save_file_dialog(path, sizeof(path), "Export Frame as PNG",
            "PNG Images (*.png)\0*.png\0All Files\0*.*\0", "png") != 0) return;
    if (TAF_ExportFrameToPNG(frame, path) != 0) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Export failed",
                                 "Could not write PNG file.", v->window);
    }
}

/* ── Toolbar ────────────────────────────────────────────────────── */

static int point_in_rect(int px, int py, SDL_Rect *r) {
    return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

static void layout_toolbar(ViewerState *v) {
    int win_w, win_h;
    SDL_GetWindowSize(v->window, &win_w, &win_h);
    int bw_sm = 44, bw_md = 70, bw_lg = 90;

    /* Row 1: file / playback / view */
    int y1 = win_h - TOOLBAR_HEIGHT + (TOOLBAR_ROW_H - BTN_H) / 2;
    int x = BTN_PAD + 4;

    v->buttons[BTN_OPEN] = (UIButton){{x, y1, bw_lg, BTN_H}, "Open TAF", 0};
    x += bw_lg + BTN_PAD;
    v->buttons[BTN_SAVE_AS] = (UIButton){{x, y1, bw_md, BTN_H}, "Save As", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_EXPORT_PNG] = (UIButton){{x, y1, bw_md, BTN_H}, "PNG", 0};
    x += bw_md + BTN_PAD + 14;
    v->buttons[BTN_PREV] = (UIButton){{x, y1, bw_md, BTN_H}, "< Prev", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_PLAY_ENTRIES] = (UIButton){{x, y1, bw_md, BTN_H}, "Anim", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_STOP] = (UIButton){{x, y1, bw_md-10, BTN_H}, "Stop", 0};
    x += bw_md - 10 + BTN_PAD;
    v->buttons[BTN_PLAY_ALL] = (UIButton){{x, y1, bw_md, BTN_H}, "All", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_NEXT] = (UIButton){{x, y1, bw_md, BTN_H}, "Next >", 0};
    x += bw_md + BTN_PAD + 14;
    v->buttons[BTN_SLOWER] = (UIButton){{x, y1, bw_sm, BTN_H}, "Slow", 0};
    x += bw_sm + BTN_PAD;
    v->buttons[BTN_FASTER] = (UIButton){{x, y1, bw_sm, BTN_H}, "Fast", 0};
    x += bw_sm + BTN_PAD + 14;
    v->buttons[BTN_ZOOM_OUT] = (UIButton){{x, y1, bw_sm, BTN_H}, "Z-", 0};
    x += bw_sm + BTN_PAD;
    v->buttons[BTN_ZOOM_IN] = (UIButton){{x, y1, bw_sm, BTN_H}, "Z+", 0};
    x += bw_sm + BTN_PAD;
    v->buttons[BTN_CHECKER] = (UIButton){{x, y1, bw_sm+6, BTN_H}, "BG", 0};

    /* Row 2: edit actions, paint tools, undo/redo */
    int y2 = y1 + TOOLBAR_ROW_H;
    int bw_e = 66, bw_t = 54;
    x = BTN_PAD + 4;
    v->buttons[BTN_NEW_ENTRY]  = (UIButton){{x, y2, bw_e, BTN_H}, "+Entry",  0}; x += bw_e + BTN_PAD;
    v->buttons[BTN_DEL_ENTRY]  = (UIButton){{x, y2, bw_e, BTN_H}, "-Entry",  0}; x += bw_e + BTN_PAD;
    v->buttons[BTN_RENAME]     = (UIButton){{x, y2, bw_e, BTN_H}, "Rename",  0}; x += bw_e + BTN_PAD + 10;
    v->buttons[BTN_DUP_FRAME]  = (UIButton){{x, y2, bw_e, BTN_H}, "+Frame",  0}; x += bw_e + BTN_PAD;
    v->buttons[BTN_DEL_FRAME]  = (UIButton){{x, y2, bw_e, BTN_H}, "-Frame",  0}; x += bw_e + BTN_PAD + 10;
    v->buttons[BTN_IMPORT_PNG] = (UIButton){{x, y2, bw_e+44, BTN_H}, "Import PNG", 0}; x += bw_e + 44 + BTN_PAD + 14;
    v->buttons[BTN_PENCIL]     = (UIButton){{x, y2, bw_t, BTN_H}, "Pen",     0}; x += bw_t + BTN_PAD;
    v->buttons[BTN_ERASER]     = (UIButton){{x, y2, bw_t, BTN_H}, "Erase",   0}; x += bw_t + BTN_PAD;
    v->buttons[BTN_EYEDROPPER] = (UIButton){{x, y2, bw_t, BTN_H}, "Pick",    0}; x += bw_t + BTN_PAD;
    v->buttons[BTN_FILL]       = (UIButton){{x, y2, bw_t, BTN_H}, "Fill",    0}; x += bw_t + BTN_PAD + 14;
    v->buttons[BTN_UNDO]       = (UIButton){{x, y2, bw_t, BTN_H}, "Undo",    0}; x += bw_t + BTN_PAD;
    v->buttons[BTN_REDO]       = (UIButton){{x, y2, bw_t, BTN_H}, "Redo",    0};
}

static void draw_toolbar(ViewerState *v) {
    int win_w, win_h;
    SDL_GetWindowSize(v->window, &win_w, &win_h);

    SDL_SetRenderDrawColor(v->renderer, 22, 22, 28, 245);
    SDL_Rect bar = {0, win_h - TOOLBAR_HEIGHT, win_w, TOOLBAR_HEIGHT};
    SDL_RenderFillRect(v->renderer, &bar);
    SDL_SetRenderDrawColor(v->renderer, 80, 80, 95, 255);
    SDL_RenderDrawLine(v->renderer, 0, win_h - TOOLBAR_HEIGHT, win_w, win_h - TOOLBAR_HEIGHT);

    for (int i = 0; i < BTN_COUNT; i++) {
        UIButton *btn = &v->buttons[i];
        int active = (i == BTN_PLAY_ENTRIES && v->anim_mode == ANIM_ENTRIES) ||
                     (i == BTN_PLAY_ALL && v->anim_mode == ANIM_ALL_FRAMES) ||
                     (i == BTN_CHECKER && v->show_checkerboard) ||
                     (i == BTN_PENCIL     && v->editor.tool == TOOL_PENCIL) ||
                     (i == BTN_ERASER     && v->editor.tool == TOOL_ERASER) ||
                     (i == BTN_EYEDROPPER && v->editor.tool == TOOL_EYEDROPPER) ||
                     (i == BTN_FILL       && v->editor.tool == TOOL_FILL);

        if (active) SDL_SetRenderDrawColor(v->renderer, 45, 110, 50, 255);
        else if (btn->hovered) SDL_SetRenderDrawColor(v->renderer, 70, 70, 85, 255);
        else SDL_SetRenderDrawColor(v->renderer, 50, 50, 60, 255);
        SDL_RenderFillRect(v->renderer, &btn->rect);

        SDL_SetRenderDrawColor(v->renderer, 80, 80, 95, 255);
        SDL_RenderDrawRect(v->renderer, &btn->rect);

        SDL_SetRenderDrawColor(v->renderer, 210, 210, 220, 255);
        int tw = text_width(btn->label, FONT_SCALE);
        draw_text(v->renderer, btn->rect.x + (btn->rect.w - tw)/2,
                  btn->rect.y + (btn->rect.h - 6*FONT_SCALE)/2, btn->label, FONT_SCALE);
    }
}

static void draw_info_panel(ViewerState *v) {
    int win_w, win_h;
    SDL_GetWindowSize(v->window, &win_w, &win_h);

    SDL_SetRenderDrawColor(v->renderer, 22, 22, 28, 220);
    SDL_Rect bar = {0, 0, win_w, PANEL_TOP_H};
    SDL_RenderFillRect(v->renderer, &bar);
    SDL_SetRenderDrawColor(v->renderer, 80, 80, 95, 255);
    SDL_RenderDrawLine(v->renderer, 0, PANEL_TOP_H, win_w, PANEL_TOP_H);

    if (!v->has_taf) {
        SDL_SetRenderDrawColor(v->renderer, 120, 120, 135, 255);
        draw_text(v->renderer, 8, 6, "No file loaded. Click 'Open TAF' or drag & drop a .taf file.", FONT_SCALE);
        return;
    }

    TAFEntry *entry = NULL;
    TAF_GetEntryInfo(v->taf, v->current_entry, &entry);
    TAFFrame *frame = NULL;
    TAF_GetFrameInfo(v->taf, v->current_entry, v->current_frame, &frame);
    if (!entry || !frame) return;

    int fps = (int)(1.0f / v->frame_duration + 0.5f);
    const char *fmt = (frame->format == TAF_FORMAT_1555) ? "ARGB1555" : "ARGB4444";
    const char *anim = v->anim_mode == ANIM_ENTRIES ? "Entry Anim" :
                       v->anim_mode == ANIM_ALL_FRAMES ? "All Frames" : "Stopped";

    char info[512];
    snprintf(info, sizeof(info), "%s  [%d/%d]  Frame %d/%d  |  %dx%d  |  %s  %d FPS  |  %s  |  Zoom %dx  |  Global %d/%d",
             entry->name, v->current_entry+1, v->entry_count,
             v->current_frame+1, entry->num_frames,
             frame->width, frame->height, anim, fps, fmt, v->zoom,
             global_frame_index(v)+1, total_frame_count(v));

    SDL_SetRenderDrawColor(v->renderer, 210, 210, 220, 255);
    draw_text(v->renderer, 8, 6, info, FONT_SCALE);
}

static void draw_welcome(ViewerState *v) {
    int win_w, win_h;
    SDL_GetWindowSize(v->window, &win_w, &win_h);
    int cy = win_h / 2 - 60;

    SDL_SetRenderDrawColor(v->renderer, 100, 180, 255, 255);
    const char *t1 = "TAF Viewer";
    draw_text(v->renderer, (win_w - text_width(t1, 3))/2, cy, t1, 3);

    SDL_SetRenderDrawColor(v->renderer, 210, 210, 220, 255);
    const char *t2 = "TA: Kingdoms Truecolor Animation Format";
    draw_text(v->renderer, (win_w - text_width(t2, FONT_SCALE))/2, cy+40, t2, FONT_SCALE);

    SDL_SetRenderDrawColor(v->renderer, 120, 120, 135, 255);
    const char *t3 = "Click 'Open TAF' or drag a .taf file here";
    draw_text(v->renderer, (win_w - text_width(t3, FONT_SCALE))/2, cy+80, t3, FONT_SCALE);
    const char *t4 = "No palette needed - TAF files contain direct 16-bit color";
    draw_text(v->renderer, (win_w - text_width(t4, FONT_SCALE))/2, cy+105, t4, FONT_SCALE);
}

/* ── Color picker panel ──────────────────────────────────────────── */

#define PICKER_W     220
#define PICKER_ROW_H 22
#define PICKER_TOP   (PANEL_TOP_H + 8)

static SDL_Rect picker_track_rect(ViewerState *v, int row) {
    (void)v;
    int win_w; SDL_GetWindowSize(v->window, &win_w, NULL);
    int x = win_w - PICKER_W - 8;
    SDL_Rect r = { x + 22, PICKER_TOP + row * PICKER_ROW_H, PICKER_W - 30, 14 };
    return r;
}

static int picker_visible(const ViewerState *v) {
    return v->has_taf && v->editor.tool != TOOL_NONE;
}

static void draw_color_picker(ViewerState *v) {
    if (!picker_visible(v)) return;

    int win_w; SDL_GetWindowSize(v->window, &win_w, NULL);
    int px = win_w - PICKER_W - 8;
    int py = PICKER_TOP - 4;
    int ph = PICKER_ROW_H * 4 + 44;

    /* Panel background */
    SDL_SetRenderDrawColor(v->renderer, 22, 22, 28, 220);
    SDL_Rect bg = { px, py, PICKER_W, ph };
    SDL_RenderFillRect(v->renderer, &bg);
    SDL_SetRenderDrawColor(v->renderer, 80, 80, 95, 255);
    SDL_RenderDrawRect(v->renderer, &bg);

    const char *labels[4] = { "R", "G", "B", "A" };
    uint8_t *vals[4] = { &v->editor.cur_r, &v->editor.cur_g, &v->editor.cur_b, &v->editor.cur_a };

    for (int i = 0; i < 4; i++) {
        SDL_Rect trk = picker_track_rect(v, i);
        /* gradient (256 thin rects) */
        for (int k = 0; k < 256; k++) {
            int x0 = trk.x + (trk.w * k) / 256;
            int x1 = trk.x + (trk.w * (k + 1)) / 256;
            uint8_t r_ = (i == 0) ? (uint8_t)k : v->editor.cur_r;
            uint8_t g_ = (i == 1) ? (uint8_t)k : v->editor.cur_g;
            uint8_t b_ = (i == 2) ? (uint8_t)k : v->editor.cur_b;
            uint8_t a_ = (i == 3) ? (uint8_t)k : 255;
            SDL_SetRenderDrawColor(v->renderer, r_, g_, b_, a_);
            SDL_Rect seg = { x0, trk.y, (x1 - x0) > 0 ? (x1 - x0) : 1, trk.h };
            SDL_RenderFillRect(v->renderer, &seg);
        }
        /* border */
        SDL_SetRenderDrawColor(v->renderer, 80, 80, 95, 255);
        SDL_RenderDrawRect(v->renderer, &trk);
        /* marker */
        int mx = trk.x + (trk.w * (*vals[i])) / 255;
        SDL_SetRenderDrawColor(v->renderer, 240, 240, 240, 255);
        SDL_Rect mark = { mx - 1, trk.y - 2, 3, trk.h + 4 };
        SDL_RenderFillRect(v->renderer, &mark);
        /* label + numeric value */
        SDL_SetRenderDrawColor(v->renderer, 210, 210, 220, 255);
        draw_text(v->renderer, px + 8, trk.y + 1, labels[i], FONT_SCALE);
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", *vals[i]);
        draw_text(v->renderer, trk.x + trk.w + 6, trk.y + 1, buf, FONT_SCALE);
    }

    /* swatch */
    int sw_y = PICKER_TOP + 4 * PICKER_ROW_H + 6;
    SDL_Rect swatch = { px + 8, sw_y, 40, 24 };
    SDL_SetRenderDrawColor(v->renderer, v->editor.cur_r, v->editor.cur_g, v->editor.cur_b, 255);
    SDL_RenderFillRect(v->renderer, &swatch);
    SDL_SetRenderDrawColor(v->renderer, 80, 80, 95, 255);
    SDL_RenderDrawRect(v->renderer, &swatch);
    SDL_SetRenderDrawColor(v->renderer, 210, 210, 220, 255);
    char hexbuf[32];
    snprintf(hexbuf, sizeof(hexbuf), "#%02X%02X%02X a%d",
             v->editor.cur_r, v->editor.cur_g, v->editor.cur_b, v->editor.cur_a);
    draw_text(v->renderer, px + 56, sw_y + 8, hexbuf, FONT_SCALE);
}

/* Returns row (1..4 for R/G/B/A) if click is inside a slider track, 0 otherwise. */
static int picker_hit(ViewerState *v, int mx, int my) {
    if (!picker_visible(v)) return 0;
    for (int i = 0; i < 4; i++) {
        SDL_Rect trk = picker_track_rect(v, i);
        if (point_in_rect(mx, my, &trk)) return i + 1;
    }
    return 0;
}

static void picker_update_from_mouse(ViewerState *v, int mx) {
    if (v->picker_drag < 1 || v->picker_drag > 4) return;
    SDL_Rect trk = picker_track_rect(v, v->picker_drag - 1);
    int rel = mx - trk.x;
    if (rel < 0) rel = 0;
    if (rel >= trk.w) rel = trk.w - 1;
    uint8_t val = (uint8_t)((rel * 255) / (trk.w - 1));
    switch (v->picker_drag) {
    case 1: v->editor.cur_r = val; break;
    case 2: v->editor.cur_g = val; break;
    case 3: v->editor.cur_b = val; break;
    case 4: v->editor.cur_a = val; break;
    }
}

static SDL_Texture *get_checkerboard(ViewerState *v, int w, int h) {
    if (v->checker_cache && v->checker_w == w && v->checker_h == h && v->checker_zoom == v->zoom)
        return v->checker_cache;
    if (v->checker_cache) SDL_DestroyTexture(v->checker_cache);
    int cell = 8 * v->zoom;
    v->checker_cache = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!v->checker_cache) return NULL;
    uint32_t *pixels; int pitch;
    SDL_LockTexture(v->checker_cache, NULL, (void**)&pixels, &pitch);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            pixels[y*(pitch/4)+x] = (((x/cell)+(y/cell))%2) ? 0xFF999999 : 0xFFCCCCCC;
    SDL_UnlockTexture(v->checker_cache);
    v->checker_w = w; v->checker_h = h; v->checker_zoom = v->zoom;
    return v->checker_cache;
}

static void handle_button_click(ViewerState *v, int id) {
    switch (id) {
    case BTN_OPEN: { char p[512]={0}; if (open_file_dialog(p,sizeof(p))==0) load_taf_file(v,p); break; }
    case BTN_SAVE_AS: save_taf_as(v); break;
    case BTN_EXPORT_PNG: export_frame_png(v); break;
    case BTN_PREV: retreat_frame(v); break;
    case BTN_NEXT: advance_frame(v); break;
    case BTN_PLAY_ENTRIES: v->anim_mode = (v->anim_mode==ANIM_ENTRIES)?ANIM_STOPPED:ANIM_ENTRIES; v->anim_timer=0; break;
    case BTN_PLAY_ALL: v->anim_mode = (v->anim_mode==ANIM_ALL_FRAMES)?ANIM_STOPPED:ANIM_ALL_FRAMES; v->anim_timer=0; break;
    case BTN_STOP: v->anim_mode = ANIM_STOPPED; break;
    case BTN_SLOWER: if (v->frame_duration < 1.0f) v->frame_duration *= 1.5f; update_frame_texture(v); break;
    case BTN_FASTER: if (v->frame_duration > 0.01f) v->frame_duration /= 1.5f; update_frame_texture(v); break;
    case BTN_ZOOM_IN: if (v->zoom < 8) v->zoom++; break;
    case BTN_ZOOM_OUT: if (v->zoom > 1) v->zoom--; break;
    case BTN_CHECKER: v->show_checkerboard = !v->show_checkerboard; break;
    case BTN_NEW_ENTRY:  do_new_entry(v);    break;
    case BTN_DEL_ENTRY:  do_delete_entry(v); break;
    case BTN_DUP_FRAME:  do_dup_frame(v);    break;
    case BTN_DEL_FRAME:  do_delete_frame(v); break;
    case BTN_RENAME:     start_rename(v);    break;
    case BTN_IMPORT_PNG: do_import_png(v);   break;
    case BTN_PENCIL:     v->editor.tool = (v->editor.tool == TOOL_PENCIL)     ? TOOL_NONE : TOOL_PENCIL;     break;
    case BTN_ERASER:     v->editor.tool = (v->editor.tool == TOOL_ERASER)     ? TOOL_NONE : TOOL_ERASER;     break;
    case BTN_EYEDROPPER: v->editor.tool = (v->editor.tool == TOOL_EYEDROPPER) ? TOOL_NONE : TOOL_EYEDROPPER; break;
    case BTN_FILL:       v->editor.tool = (v->editor.tool == TOOL_FILL)       ? TOOL_NONE : TOOL_FILL;       break;
    case BTN_UNDO:       do_undo(v);  break;
    case BTN_REDO:       do_redo(v);  break;
    }
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    ViewerState v = {0};
    v.anim_mode = ANIM_STOPPED;
    v.frame_duration = 1.0f / 10.0f;
    v.show_checkerboard = 1;
    v.zoom = 3;
    Editor_Init(&v.editor);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    v.window = SDL_CreateWindow("TAF Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                1024, 700, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    v.renderer = SDL_CreateRenderer(v.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_SetRenderDrawBlendMode(v.renderer, SDL_BLENDMODE_BLEND);
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    layout_toolbar(&v);

    if (argc >= 2) load_taf_file(&v, argv[1]);

    int running = 1;
    uint64_t last_time = SDL_GetPerformanceCounter();
    uint64_t freq = SDL_GetPerformanceFrequency();

    while (running) {
        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last_time) / (float)freq;
        last_time = now;

        int mx, my; SDL_GetMouseState(&mx, &my);
        for (int i = 0; i < BTN_COUNT; i++)
            v.buttons[i].hovered = point_in_rect(mx, my, &v.buttons[i].rect);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            /* While renaming, swallow text/key events and route them into the buffer. */
            if (v.is_renaming) {
                if (event.type == SDL_TEXTINPUT) {
                    for (int i = 0; event.text.text[i]; i++) {
                        char c = event.text.text[i];
                        if (v.rename_len < (int)sizeof(v.rename_buffer) - 1 && (unsigned char)c >= 0x20) {
                            v.rename_buffer[v.rename_len++] = c;
                            v.rename_buffer[v.rename_len] = '\0';
                        }
                    }
                    continue;
                }
                if (event.type == SDL_KEYDOWN) {
                    if (event.key.keysym.sym == SDLK_BACKSPACE) {
                        if (v.rename_len > 0) v.rename_buffer[--v.rename_len] = '\0';
                    } else if (event.key.keysym.sym == SDLK_RETURN ||
                               event.key.keysym.sym == SDLK_KP_ENTER) {
                        commit_rename(&v);
                    } else if (event.key.keysym.sym == SDLK_ESCAPE) {
                        cancel_rename(&v);
                    }
                    continue;
                }
                if (event.type == SDL_QUIT) { /* allow quit */ }
                else continue;
            }
            switch (event.type) {
            case SDL_QUIT:
                if (confirm_discard_dirty(&v, "Quit without saving")) running = 0;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    layout_toolbar(&v);
                    if (v.checker_cache) { SDL_DestroyTexture(v.checker_cache); v.checker_cache = NULL; }
                }
                break;
            case SDL_DROPFILE:
                load_taf_file(&v, event.drop.file);
                SDL_free(event.drop.file);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    int handled = 0;
                    for (int i = 0; i < BTN_COUNT; i++) {
                        if (point_in_rect(event.button.x, event.button.y, &v.buttons[i].rect))
                            { handle_button_click(&v, i); handled = 1; break; }
                    }
                    if (!handled) {
                        int row = picker_hit(&v, event.button.x, event.button.y);
                        if (row) {
                            v.picker_drag = row;
                            picker_update_from_mouse(&v, event.button.x);
                            handled = 1;
                        }
                    }
                    if (!handled && v.has_taf && v.editor.tool != TOOL_NONE) {
                        int px, py;
                        if (canvas_mouse_to_pixel(&v, event.button.x, event.button.y, &px, &py)) {
                            if (v.editor.tool == TOOL_PENCIL || v.editor.tool == TOOL_ERASER) {
                                TAFFrame *f = current_frame(&v);
                                if (f) {
                                    Editor_BeginPixelStroke(&v.editor, v.current_entry, v.current_frame, f);
                                    apply_paint(&v, event.button.x, event.button.y);
                                }
                            } else if (v.editor.tool == TOOL_FILL) {
                                TAFFrame *f = current_frame(&v);
                                if (f) {
                                    Editor_BeginPixelStroke(&v.editor, v.current_entry, v.current_frame, f);
                                    apply_paint(&v, event.button.x, event.button.y);
                                    Editor_EndPixelStroke(&v.editor, f);
                                }
                            } else if (v.editor.tool == TOOL_EYEDROPPER) {
                                apply_paint(&v, event.button.x, event.button.y);
                            }
                        }
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                if (v.picker_drag) {
                    picker_update_from_mouse(&v, event.motion.x);
                } else if (v.editor.stroking &&
                           (v.editor.tool == TOOL_PENCIL || v.editor.tool == TOOL_ERASER)) {
                    apply_paint(&v, event.motion.x, event.motion.y);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    if (v.picker_drag) v.picker_drag = 0;
                    if (v.editor.stroking) {
                        TAFFrame *f = current_frame(&v);
                        if (f) Editor_EndPixelStroke(&v.editor, f);
                    }
                }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (confirm_discard_dirty(&v, "Quit without saving")) running = 0;
                    break;
                case SDLK_o: handle_button_click(&v, BTN_OPEN); break;
                case SDLK_s:
                    if (event.key.keysym.mod & KMOD_CTRL) handle_button_click(&v, BTN_SAVE_AS);
                    break;
                case SDLK_e:
                    if (event.key.keysym.mod & KMOD_CTRL) handle_button_click(&v, BTN_EXPORT_PNG);
                    else handle_button_click(&v, BTN_ERASER);
                    break;
                case SDLK_b: handle_button_click(&v, BTN_PENCIL); break;
                case SDLK_i: handle_button_click(&v, BTN_EYEDROPPER); break;
                case SDLK_g: handle_button_click(&v, BTN_FILL); break;
                case SDLK_z:
                    if (event.key.keysym.mod & KMOD_CTRL) do_undo(&v);
                    break;
                case SDLK_y:
                    if (event.key.keysym.mod & KMOD_CTRL) do_redo(&v);
                    break;
                case SDLK_UP: if (v.has_taf && v.current_entry > 0) { v.current_entry--; v.current_frame=0; update_frame_texture(&v); } break;
                case SDLK_DOWN: if (v.has_taf && v.current_entry < v.entry_count-1) { v.current_entry++; v.current_frame=0; update_frame_texture(&v); } break;
                case SDLK_LEFT: retreat_frame(&v); break;
                case SDLK_RIGHT: advance_frame(&v); break;
                case SDLK_SPACE: v.anim_mode = (v.anim_mode==ANIM_ENTRIES)?ANIM_STOPPED:ANIM_ENTRIES; v.anim_timer=0; break;
                case SDLK_a: v.anim_mode = (v.anim_mode==ANIM_ALL_FRAMES)?ANIM_STOPPED:ANIM_ALL_FRAMES; v.anim_timer=0; break;
                case SDLK_f: v.show_checkerboard = !v.show_checkerboard; break;
                case SDLK_EQUALS: case SDLK_PLUS: if (v.zoom < 8) v.zoom++; break;
                case SDLK_MINUS: if (v.zoom > 1) v.zoom--; break;
                case SDLK_RIGHTBRACKET: if (v.frame_duration > 0.01f) { v.frame_duration /= 1.5f; update_frame_texture(&v); } break;
                case SDLK_LEFTBRACKET: if (v.frame_duration < 1.0f) { v.frame_duration *= 1.5f; update_frame_texture(&v); } break;
                }
                break;
            }
        }

        if (v.anim_mode != ANIM_STOPPED && v.has_taf) {
            v.anim_timer += dt;
            if (v.anim_timer >= v.frame_duration) {
                v.anim_timer -= v.frame_duration;
                if (v.anim_mode == ANIM_ENTRIES) advance_entry_anim(&v);
                else advance_frame(&v);
            }
        }

        SDL_SetRenderDrawColor(v.renderer, 30, 30, 35, 255);
        SDL_RenderClear(v.renderer);

        if (v.has_taf && v.frame_texture) {
            int tw, th; SDL_QueryTexture(v.frame_texture, NULL, NULL, &tw, &th);
            int dw = tw*v.zoom, dh = th*v.zoom;
            int ww, wh; SDL_GetWindowSize(v.window, &ww, &wh);
            int dx = (ww-dw)/2, dy = PANEL_TOP_H + (wh-TOOLBAR_HEIGHT-PANEL_TOP_H-dh)/2;
            SDL_Rect dst = {dx, dy, dw, dh};
            if (v.show_checkerboard) { SDL_Texture *c = get_checkerboard(&v,dw,dh); if (c) SDL_RenderCopy(v.renderer,c,NULL,&dst); }
            SDL_RenderCopy(v.renderer, v.frame_texture, NULL, &dst);
            v.canvas_rect = dst;
            v.canvas_valid = 1;
        } else {
            v.canvas_valid = 0;
            if (!v.has_taf) draw_welcome(&v);
        }

        draw_info_panel(&v);
        draw_toolbar(&v);
        draw_color_picker(&v);

        if (v.is_renaming) {
            int ww, wh; SDL_GetWindowSize(v.window, &ww, &wh);
            int box_w = 400, box_h = 80;
            int bx = (ww - box_w) / 2, by = (wh - box_h) / 2;
            SDL_SetRenderDrawColor(v.renderer, 18, 18, 22, 240);
            SDL_Rect bg = {bx - 2, by - 2, box_w + 4, box_h + 4};
            SDL_RenderFillRect(v.renderer, &bg);
            SDL_SetRenderDrawColor(v.renderer, 45, 110, 50, 255);
            SDL_Rect frame = {bx, by, box_w, box_h};
            SDL_RenderDrawRect(v.renderer, &frame);
            SDL_SetRenderDrawColor(v.renderer, 210, 210, 220, 255);
            draw_text(v.renderer, bx + 12, by + 10,
                      "Rename entry (Enter=commit, Esc=cancel):", FONT_SCALE);
            SDL_SetRenderDrawColor(v.renderer, 100, 180, 255, 255);
            draw_text(v.renderer, bx + 12, by + 40, v.rename_buffer, FONT_SCALE);
            /* blinking cursor */
            if ((SDL_GetTicks() / 500) % 2 == 0) {
                int cx = bx + 12 + text_width(v.rename_buffer, FONT_SCALE);
                SDL_Rect cur = {cx, by + 40, FONT_SCALE, 6 * FONT_SCALE};
                SDL_RenderFillRect(v.renderer, &cur);
            }
        }
        SDL_RenderPresent(v.renderer);
    }

    if (v.frame_texture) SDL_DestroyTexture(v.frame_texture);
    if (v.checker_cache) SDL_DestroyTexture(v.checker_cache);
    SDL_DestroyRenderer(v.renderer);
    SDL_DestroyWindow(v.window);
    SDL_Quit();
    Editor_Free(&v.editor);
    if (v.has_taf) TAF_Close(v.taf);
    return 0;
}
