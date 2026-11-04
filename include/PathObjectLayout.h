#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "PathCurve.h"
#include "PathObject.h"


/**
 * @file PathObjectLayout.h
 * @brief Where a path object's pieces lie along a path, as pure maths: the fit, the alignment, the walk along the curve. No
 *        world, no assets, no GL: lengths come in as numbers and distances along the path come out.
 *
 * RUNS AND SPANS. Consecutive sections that are not skipped form a run; a skipped section ends the run before it with its caps
 * and starts the next with its own. A closed path with no skipped section is one run without ends: no caps, and a joint where
 * it closes. While fits_each_section is on, each section of a run is a span that fits on its own; off, the whole run is one
 * span. Where two spans of a run meet lies a JOINT, a piece centred on their node: the corner slot's at a corner node, else the
 * separator's. It takes half its length from each span. A section of no length (two corners at one place) holds nothing.
 *
 * A SPAN'S SEQUENCE is its start cap (the run's first span), its middles with a separator after every separator_every of them,
 * and its end cap (the run's last). Pieces lie end to end by their lengths, each times the node's scale factor where it starts.
 *
 * THE FIT finds how many middles and what fills the rest. Every item of the sequence has a length A + B * x, and x is solved so
 * the sequence ends exactly at the span's end:
 * - stretch: x scales the slots that stretch (the middle's by default);
 * - resize_ends: x scales the first, the last or both middles, or says how much of them a cut leaves;
 * - fixed: x is the gap the alignment shares out, on top of the set gap between pieces.
 *
 * THE WALK. A rigid piece's two ends lie on the curve, so the next piece starts where a chord of its length ends, not where its
 * length along the curve does: chords are shorter than arcs, and x is solved over the real walk (a secant over the end's
 * distance). A bent piece and a gap are measured along the curve. The last piece ends exactly at the span's end, but where chords
 * outgrow a path's hairpins: a walk's end jumps there, and it may end short by less than an item's share of the span.
 *
 * WHEN ONE MIDDLE DOES NOT FIT (when_too_short): squeeze it down to minimum_stretch, the single slot, only the caps, or nothing.
 * A span holding exactly one middle draws the single slot instead, where it is in use: it replaces the span's caps too.
 *
 * LIMITS. A layout holds at most PathObjectLayoutInput.MaxPieces pieces, and walks at most PATH_OBJECT_LAYOUT_MAX_WALKED_ITEMS
 * items
 * in all its tries, so a hostile count cannot walk a hostile path for ever (WasLimited says it stopped at either). A middle of no
 * length (below PATH_OBJECT_LAYOUT_MIN_MIDDLE_LENGTH) fills nothing: HasZeroLengthMiddle says so. Nothing here gives a NaN.
 */


// Macros.
/** @brief A middle shorter than this, in metres, fills nothing. */
#define PATH_OBJECT_LAYOUT_MIN_MIDDLE_LENGTH 0.001f
/** @brief How far off the span's end the solve stops, in metres. */
#define PATH_OBJECT_LAYOUT_END_TOLERANCE 1.0e-4
/** @brief The smallest node scale factor a piece's length is measured with, so a scale of 0 still moves the walk on. */
#define PATH_OBJECT_LAYOUT_MIN_SCALE 0.01f
/** @brief The most items one build walks, over every span and every try: far past what its most pieces need. */
#define PATH_OBJECT_LAYOUT_MAX_WALKED_ITEMS ((size_t)16000000)


// Types.
/** @brief One model a slot can pick. */
typedef struct PathObjectLayoutModelStruct
{
    /** @brief Its piece's length in metres, at least 0 (PathObjectMeasure_GetLength). */
    float Length;
    /** @brief Its weight, above 0. */
    float Weight;
} PathObjectLayoutModel;

/** @brief What the layout needs of one slot. */
typedef struct PathObjectLayoutSlotStruct
{
    /** @brief Its models (borrowed for the build); none, and the slot counts as not in use. */
    const PathObjectLayoutModel* Models;
    uint32_t ModelCount;
    /** @brief Whether it is in use. The middle's flag says nothing: only its models do. */
    bool IsUsed;
    /** @brief How its pieces choose among its models. */
    PathObjectPick Pick;
    /** @brief Whether the stretch fit scales it. */
    bool Stretches;
    /** @brief Whether its pieces are straight, with their ends on the curve: measured by chords. */
    bool IsRigid;
} PathObjectLayoutSlot;

/** @brief What to lay out. */
typedef struct PathObjectLayoutInputStruct
{
    /** @brief The curve (borrowed for the build). */
    const PathCurve* Curve;
    /** @brief Its nodes, for which section starts are corners (borrowed for the build). */
    const PathNode* Nodes;
    size_t NodeCount;
    /** @brief The settings in effect (PathObject_ResolveSettings). */
    const PathObjectSettings* Settings;
    /** @brief The slots. */
    PathObjectLayoutSlot Slots[PathObjectSlot_Count];
    /** @brief The most pieces the layout may hold, above 0. */
    size_t MaxPieces;
} PathObjectLayoutInput;

/** @brief One stretch of a path that fits on its own. */
typedef struct PathObjectSpanStruct
{
    /** @brief The sections it covers, the first and the last (wrapping on a closed path). */
    uint32_t FirstSection;
    uint32_t LastSection;
    /** @brief Where it starts, as a distance along the path, and its length. Start + Length may pass the path's length on a
     *  closed path. */
    double Start;
    double Length;
    /** @brief Whether a section of its run meets it at its start, and which (a closed run's first span: its last section). */
    bool HasSectionBefore;
    uint32_t SectionBefore;
    /** @brief Whether a section of its run meets it at its end, and which. */
    bool HasSectionAfter;
    uint32_t SectionAfter;
} PathObjectSpan;

/** @brief One piece. */
typedef struct PathObjectPieceStruct
{
    /** @brief Its slot and the model it picked (an index among the slot's models). */
    PathObjectSlot Slot;
    uint32_t Model;
    /** @brief Its running number among its slot's pieces in its span: what alternating settings count. */
    uint32_t Index;
    /** @brief Its span (an index among PathObjectLayout_GetSpans). */
    uint32_t Span;
    /** @brief Where it starts and ends, as distances from its span's start. A joint's are both its node's. */
    double Start;
    double End;
    /** @brief Its length before anything resized it, in metres, without the node's scale. */
    float Nominal;
    /** @brief The share of its length a cut takes off at its start and at its end (resize_ends by cutting); 0 for none. */
    float CutStart;
    float CutEnd;
    /** @brief Whether it is a joint: centred on a node, its frame the curve's as it arrives there. */
    bool IsJoint;
    /** @brief Whether the fit resized it: its length follows the fit's x. */
    bool IsResized;
} PathObjectPiece;

/**
 * @brief A layout: construct, build, read, build again, deconstruct. A build reuses the buffers it has grown. Fields are
 *        internal to PathObjectLayout.c.
 */
typedef struct PathObjectLayoutStruct
{
    /** @brief PathObjectPiece entries, span by span, along the path. */
    GenericBuffer _pieces;
    /** @brief PathObjectSpan entries. */
    GenericBuffer _spans;
    /** @brief Scratch: the items of the span being fitted. */
    GenericBuffer _items;
    /** @brief How many items the last build walked. */
    size_t _walkedItems;
    /** @brief Whether the last build stopped at MaxPieces or at the most it may walk. */
    bool _wasLimited;
    /** @brief Whether the last build met a middle of no length. */
    bool _hasZeroLengthMiddle;
} PathObjectLayout;


// Functions.
/** @brief Initializes an empty layout. @param self The layout; must not be NULL. */
void PathObjectLayout_Construct(PathObjectLayout* self);

/** @brief Releases what it holds. @param self The layout, or NULL. */
void PathObjectLayout_Deconstruct(PathObjectLayout* self);

/**
 * @brief Lays the pieces out, replacing what the layout held.
 * @param self The layout; must not be NULL.
 * @param input What to lay out; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for NULL arguments; ErrorCode_BufferTooSmall when a buffer could not grow (the
 *          layout is then empty).
 */
Error PathObjectLayout_Build(PathObjectLayout* self, const PathObjectLayoutInput* input);

/**
 * @brief The pieces, span by span, along the path.
 * @param self The layout; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The pieces (borrowed until the next build); NULL when there are none.
 */
static inline const PathObjectPiece* PathObjectLayout_GetPieces(const PathObjectLayout* self, size_t* outCount)
{
    const void* Pieces = self->_pieces._data;
    *outCount = self->_pieces._count;
    return (self->_pieces._count > 0U) ? Pieces : NULL;
}

/**
 * @brief The spans.
 * @param self The layout; must not be NULL.
 * @param outCount [out] Receives how many. Must not be NULL.
 * @returns The spans (borrowed until the next build); NULL when there are none.
 */
static inline const PathObjectSpan* PathObjectLayout_GetSpans(const PathObjectLayout* self, size_t* outCount)
{
    const void* Spans = self->_spans._data;
    *outCount = self->_spans._count;
    return (self->_spans._count > 0U) ? Spans : NULL;
}

/** @brief Whether the last build stopped at its limit of pieces or of walks. @param self The layout; must not be NULL. @returns
 * true then. */
static inline bool PathObjectLayout_WasLimited(const PathObjectLayout* self)
{
    return self->_wasLimited;
}

/** @brief Whether the last build met a middle of no length. @param self The layout; must not be NULL. @returns true then. */
static inline bool PathObjectLayout_HasZeroLengthMiddle(const PathObjectLayout* self)
{
    return self->_hasZeroLengthMiddle;
}

/**
 * @brief The point at a distance along a span: inside its sections, so its start has the tangent its first section leaves with
 *        and its end the tangent its last arrives with, even at a corner; a point at a corner inside it has the one it leaves
 *        with.
 * @param curve The curve; must not be NULL.
 * @param span The span; must not be NULL.
 * @param distance The distance from the span's start, clamped to its length. Must be finite.
 * @param outPoint [out] Receives the point. Must not be NULL.
 * @returns true; false for a span the curve does not hold.
 */
bool PathObjectLayout_GetSpanPoint(const PathCurve* curve, const PathObjectSpan* span, double distance,
    PathCurvePoint* outPoint);

/**
 * @brief A stable random value: the same for the same inputs on every machine.
 * @param seed The seed.
 * @param slot The slot.
 * @param span The span's first section's first node, or anything that names the stretch.
 * @param index The piece's number.
 * @param salt What the value is for, so a piece's choices do not follow each other.
 * @returns A value in [0, 1).
 */
float PathObjectLayout_GetRandom(uint32_t seed, PathObjectSlot slot, uint32_t span, uint32_t index, uint32_t salt);
