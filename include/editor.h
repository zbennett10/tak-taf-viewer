#ifndef EDITOR_H
#define EDITOR_H

#include "taf.h"

typedef enum {
    TOOL_NONE = 0,
    TOOL_PENCIL,
    TOOL_ERASER,
    TOOL_EYEDROPPER,
    TOOL_FILL,
} EditorTool;

#define UNDO_CAP 50

typedef enum { UNDO_PIXELS = 0, UNDO_TREE = 1 } UndoKind;

typedef struct UndoEntry {
    UndoKind kind;
    /* pixel-scope undo: */
    int       entry_idx, frame_idx;
    uint16_t  w, h;
    uint16_t *pixels_before;
    uint16_t *pixels_after;
    /* tree-scope undo (structural ops): */
    TAFFile  *tree_before;
    TAFFile  *tree_after;
} UndoEntry;

typedef struct {
    EditorTool tool;
    uint8_t cur_r, cur_g, cur_b, cur_a;
    int stroking;
    int stroke_entry, stroke_frame;

    UndoEntry *undo[UNDO_CAP];
    int        undo_top;
    UndoEntry *redo[UNDO_CAP];
    int        redo_top;

    /* In-progress stroke (between Begin/End) parks here, not on the stacks. */
    UndoEntry *pending;
} EditorState;

void Editor_Init(EditorState *e);
void Editor_Free(EditorState *e);

/* Pixel-stroke workflow:
 *   BeginPixelStroke  -- snapshot the frame's pixels before painting
 *   Paint/Fill        -- mutate pixels
 *   EndPixelStroke    -- snapshot 'after' state into the same undo entry
 */
void Editor_BeginPixelStroke(EditorState *e, int entry_idx, int frame_idx, TAFFrame *frame);
void Editor_Paint(EditorState *e, TAFFrame *frame, int px, int py);
void Editor_Fill(EditorState *e, TAFFrame *frame, int px, int py);
int  Editor_Sample(const TAFFrame *frame, int px, int py,
                   uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
void Editor_EndPixelStroke(EditorState *e, TAFFrame *frame);

/* Full-tree snapshot workflow (wraps any structural op: add/delete entry, dup, import PNG). */
void Editor_PushTreeSnapshot(EditorState *e, const TAFFile *before, const TAFFile *after);

/* Undo/redo. If they restore a tree snapshot, *taf is swapped and the caller
 * must refresh their derived state (entry_count, cursors, texture). */
int Editor_Undo(EditorState *e, TAFFile **taf);
int Editor_Redo(EditorState *e, TAFFile **taf);

#endif /* EDITOR_H */
