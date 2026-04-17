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
    BTN_COUNT
};

/* ── UI Constants ───────────────────────────────────────────────── */
#define TOOLBAR_HEIGHT  52
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
} ViewerState;

/* Forward declarations */
static void update_frame_texture(ViewerState *v);

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
static int open_file_dialog(char *out, int out_size) {
    OPENFILENAMEA ofn = {0};
    char file[MAX_PATH] = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "TAF Files (*.taf)\0*.taf\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Open TAF Sprite File";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) { strncpy(out, file, out_size - 1); return 0; }
    return -1;
}
#else
static int open_file_dialog(char *out, int out_size) {
    (void)out; (void)out_size;
    fprintf(stderr, "File dialog not available. Use command line or drag & drop.\n");
    return -1;
}
#endif

/* ── Frame navigation ───────────────────────────────────────────── */

static void advance_frame(ViewerState *v) {
    if (!v->has_taf) return;
    TAFEntryHeader *entry = NULL;
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
        TAFEntryHeader *entry = NULL;
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
        TAFEntryHeader *e = NULL;
        TAF_GetEntryInfo(v->taf, i, &e);
        if (e) total += e->num_frames;
    }
    return total;
}

static int global_frame_index(ViewerState *v) {
    int idx = 0;
    for (int i = 0; i < v->current_entry; i++) {
        TAFEntryHeader *e = NULL;
        TAF_GetEntryInfo(v->taf, i, &e);
        if (e) idx += e->num_frames;
    }
    return idx + v->current_frame;
}

/* ── Update texture ─────────────────────────────────────────────── */

static void update_frame_texture(ViewerState *v) {
    if (v->frame_texture) { SDL_DestroyTexture(v->frame_texture); v->frame_texture = NULL; }
    if (!v->has_taf) return;

    TAFEntryHeader *entry = NULL;
    if (TAF_GetEntryInfo(v->taf, v->current_entry, &entry) != 0) return;
    TAFFrameHeader *frame = NULL;
    if (TAF_GetFrameInfo(v->taf, v->current_entry, v->current_frame, &frame) != 0) return;

    uint32_t *rgba = TAF_DecodeFrameRGBA(v->taf, frame);
    if (!rgba) return;

    v->frame_texture = SDL_CreateTexture(v->renderer, SDL_PIXELFORMAT_RGBA32,
                                          SDL_TEXTUREACCESS_STATIC,
                                          frame->width, frame->height);
    if (v->frame_texture) {
        SDL_SetTextureBlendMode(v->frame_texture, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(v->frame_texture, NULL, rgba, frame->width * 4);
    }

    const char *fmt = (frame->format == TAF_FORMAT_1555) ? "ARGB1555" : "ARGB4444";
    char title[512];
    snprintf(title, sizeof(title), "TAF Viewer - %s  |  %s [%d/%d]  Frame %d/%d  (%dx%d)  [%s]",
             v->taf_path, entry->name,
             v->current_entry + 1, v->entry_count,
             v->current_frame + 1, entry->num_frames,
             frame->width, frame->height, fmt);
    SDL_SetWindowTitle(v->window, title);

    free(rgba);
}

/* ── Load TAF ───────────────────────────────────────────────────── */

static void load_taf_file(ViewerState *v, const char *path) {
    TAFFile *new_taf = NULL;
    if (TAF_Open(&new_taf, path) != 0) return;
    if (v->has_taf) TAF_Close(v->taf);
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

/* ── Toolbar ────────────────────────────────────────────────────── */

static int point_in_rect(int px, int py, SDL_Rect *r) {
    return px >= r->x && px < r->x + r->w && py >= r->y && py < r->y + r->h;
}

static void layout_toolbar(ViewerState *v) {
    int win_w, win_h;
    SDL_GetWindowSize(v->window, &win_w, &win_h);
    int y = win_h - TOOLBAR_HEIGHT + (TOOLBAR_HEIGHT - BTN_H) / 2;
    int bw_sm = 44, bw_md = 70, bw_lg = 90;
    int x = BTN_PAD + 4;

    v->buttons[BTN_OPEN] = (UIButton){{x, y, bw_lg, BTN_H}, "Open TAF", 0};
    x += bw_lg + BTN_PAD + 14;
    v->buttons[BTN_PREV] = (UIButton){{x, y, bw_md, BTN_H}, "< Prev", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_PLAY_ENTRIES] = (UIButton){{x, y, bw_md, BTN_H}, "Anim", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_STOP] = (UIButton){{x, y, bw_md-10, BTN_H}, "Stop", 0};
    x += bw_md - 10 + BTN_PAD;
    v->buttons[BTN_PLAY_ALL] = (UIButton){{x, y, bw_md, BTN_H}, "All", 0};
    x += bw_md + BTN_PAD;
    v->buttons[BTN_NEXT] = (UIButton){{x, y, bw_md, BTN_H}, "Next >", 0};
    x += bw_md + BTN_PAD + 14;
    v->buttons[BTN_SLOWER] = (UIButton){{x, y, bw_sm, BTN_H}, "Slow", 0};
    x += bw_sm + BTN_PAD;
    v->buttons[BTN_FASTER] = (UIButton){{x, y, bw_sm, BTN_H}, "Fast", 0};
    x += bw_sm + BTN_PAD + 14;
    v->buttons[BTN_ZOOM_OUT] = (UIButton){{x, y, bw_sm, BTN_H}, "Z-", 0};
    x += bw_sm + BTN_PAD;
    v->buttons[BTN_ZOOM_IN] = (UIButton){{x, y, bw_sm, BTN_H}, "Z+", 0};
    x += bw_sm + BTN_PAD;
    v->buttons[BTN_CHECKER] = (UIButton){{x, y, bw_sm+6, BTN_H}, "BG", 0};
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
                     (i == BTN_CHECKER && v->show_checkerboard);

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

    TAFEntryHeader *entry = NULL;
    TAF_GetEntryInfo(v->taf, v->current_entry, &entry);
    TAFFrameHeader *frame = NULL;
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
    }
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    ViewerState v = {0};
    v.anim_mode = ANIM_STOPPED;
    v.frame_duration = 1.0f / 10.0f;
    v.show_checkerboard = 1;
    v.zoom = 3;

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
            switch (event.type) {
            case SDL_QUIT: running = 0; break;
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
                if (event.button.button == SDL_BUTTON_LEFT)
                    for (int i = 0; i < BTN_COUNT; i++)
                        if (point_in_rect(event.button.x, event.button.y, &v.buttons[i].rect))
                            { handle_button_click(&v, i); break; }
                break;
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_o: handle_button_click(&v, BTN_OPEN); break;
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
        } else if (!v.has_taf) {
            draw_welcome(&v);
        }

        draw_info_panel(&v);
        draw_toolbar(&v);
        SDL_RenderPresent(v.renderer);
    }

    if (v.frame_texture) SDL_DestroyTexture(v.frame_texture);
    if (v.checker_cache) SDL_DestroyTexture(v.checker_cache);
    SDL_DestroyRenderer(v.renderer);
    SDL_DestroyWindow(v.window);
    SDL_Quit();
    if (v.has_taf) TAF_Close(v.taf);
    return 0;
}
