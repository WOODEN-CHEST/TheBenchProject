#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "PropertyDescriptor.h"
#include "WorldModel.h"
#include "raylib/raylib.h"
#include "wr/WRError.h"
#include "wr/WRMemory.h"
#include "wr/WRVectorMath.h"

/*
 * Private header (source/): instanced drawing as pure maths, tested headless (tests/WorldInstancingTests.c). The
 * render cache builds a model instances component's cells with it, the renderer draws them, and the instanced vertex
 * stages (shaders/common/instancing.glsl) compute what WorldInstancing_Evaluate computes here: this file is their C
 * mirror, for the tests and for the A/B path that draws every copy on its own.
 *
 * CELLS. A component's copies are sorted into cells of WORLD_INSTANCE_CELL_SIZE metres by their position in the
 * object's own X and Z (a cell is a grid square, floor(x / size) and floor(z / size)), cell by cell in grid order and
 * within a cell in the list's order. A cell keeps its centre in double, in the object's space (the middle of its square
 * in X and Z, the middle of its copies' heights in Y), and each copy's record: the first three rows of its matrix
 * relative to that centre (scale, then the Euler XYZ rotation, then the position minus the centre, composed in double and
 * rounded once), its tint and a stable random value from the object's id and the copy's index. Its bounding sphere holds
 * every copy's sphere (the world model's under the copy's matrix) in the object's space. Records are what the GPU reads
 * (WorldInstanceRecord): a cell is drawn with one matrix, the object's world matrix times the translation to its centre in
 * render space, so the camera's movement never touches the records.
 *
 * A COPY'S FADE AND LOD (per copy, every frame, in the vertex stage). A copy is measured as an object is: its LOD by the
 * camera's distance to its bounding sphere's centre, its fade by the distance to the sphere's nearest point, both times
 * the zoom factor. The fade follows the objects' rule: the draw distance in effect (WorldVisibility_ScaleDrawDistance)
 * fades it over its last band (WorldVisibility_GetFadeBand), and 1 - its opacity (the object's times the copy's) asks
 * for a fade too; the larger counts. LOD changes are DISTANCE BANDS, not timed cross-fades (100 000 copies cannot each
 * keep an object's LOD state, WorldLod.h): around each switch distance s (times the lod_distance_scale), from s(1 - h)
 * to s(1 + h), h the world model's lod_hysteresis, the copy hands over from LOD i to LOD i + 1, the incoming LOD's
 * share rising linearly across the band, drawn with the LOD cross-fades' complementary dither. A band never reaches
 * past the middle between two switch distances, so bands never overlap. A standing camera never changes the picture;
 * a moving one moves the bands with it.
 *
 * THE PASSES' RULES, per copy (WorldInstancing_GetDraw), as for objects: the lit pass draws both
 * LODs of a band, each with its part of the dither, and the copy's fade; the G-buffer draws the LOD with the larger share,
 * solid, while the fade is below WORLD_VISIBILITY_GBUFFER_FADE_LIMIT; the shadow passes draw the LOD with the larger
 * share, solid but for the copy's own fade; the unlit fallback draws the LOD with the larger share, solid. A copy whose
 * fade is 1 is drawn by no pass. The CPU draws a cell only with the LODs its copies can show (WorldInstancing_GetCellLodMask);
 * the vertex stage of the LOD k draw drops every copy that does not show LOD k.
 */


// Macros.
/** @brief The side of a cell, in metres of the object's own X and Z. */
#define WORLD_INSTANCE_CELL_SIZE 32.0
/** @brief How many floats of a copy's matrix a record holds: its first three rows, four each (the last row is 0 0 0 1). */
#define WORLD_INSTANCE_RECORD_MATRIX_FLOATS 12U
/** @brief The most LOD bands: one between each two LODs. */
#define WORLD_INSTANCE_MAX_BANDS (WORLD_MODEL_MAX_LODS - 1U)
/** @brief How much the CPU widens a cell's distance range before it picks the LODs its copies show (a share, and
 *         metres), so the GPU's rounding of a copy's distance never leaves the range. */
#define WORLD_INSTANCE_CELL_RANGE_SHARE 1.0e-4f
#define WORLD_INSTANCE_CELL_RANGE_MARGIN 1.0e-3f


// Types.
/**
 * @brief One copy as the GPU reads it: the instance buffer's layout, 56 bytes, read at the instanced vertex stages'
 *        attribute locations 9 to 13 (agents/WorldRuntime.md, "Vertex attributes").
 */
typedef struct WorldInstanceRecordStruct
{
    /** @brief The first three rows of the copy's matrix relative to its cell's centre: row r is Rows[4r .. 4r + 3], so
     *  a point p goes to (dot(row0, p), dot(row1, p), dot(row2, p)) for p = (x, y, z, 1). In the object's own space. */
    float Rows[WORLD_INSTANCE_RECORD_MATRIX_FLOATS];
    /** @brief The copy's tint: red, green and blue multiply its colour, alpha is its opacity. */
    Color Tint;
    /** @brief A stable random value in [0, 1) (WorldInstancing_GetRandom): the same for the same object and copy. */
    float Random;
} WorldInstanceRecord;

/** @brief One cell of a component's copies, in the object's own space. A plain value type. */
typedef struct WorldInstanceCellStruct
{
    /** @brief The cell's origin, the point its records are relative to: the middle of its grid square in X and Z, the
     *  middle of its copies' heights in Y, in double. */
    Vector3Double Center;
    /** @brief A sphere holding every copy's bounding sphere: its centre, in double, and its radius. */
    Vector3Double SphereCenter;
    double SphereRadius;
    /** @brief Its records: RecordCount of them from FirstRecord. */
    size_t FirstRecord;
    size_t RecordCount;
} WorldInstanceCell;

/** @brief The world model's bounding sphere in one copy's own space (a copy stands where an object would): its centre
 *         (WorldModel_GetBoundsTransform applied to WorldModel_GetBoundsCenter) and its radius times that transform's
 *         stretch. A plain value type. */
typedef struct WorldInstanceBoundsStruct
{
    /** @brief The centre. */
    Vector3 Center;
    /** @brief The radius (>= 0). */
    float Radius;
} WorldInstanceBounds;

/**
 * @brief A component's cells and their records, built by WorldInstanceCells_Build.
 *
 * Construct with WorldInstanceCells_Construct and release with WorldInstanceCells_Deconstruct. Fields are internal to
 * WorldInstancing.c: read through the accessors. A build reuses the buffers it has grown.
 */
typedef struct WorldInstanceCellsStruct
{
    /** @brief WorldInstanceCell entries, in grid order (X, then Z). */
    GenericBuffer _cells;
    /** @brief WorldInstanceRecord entries, cell by cell. */
    GenericBuffer _records;
    /** @brief Per record, the copy's index in the instance list it was built from (size_t). */
    GenericBuffer _sources;
    /** @brief Scratch: each copy's grid square and index while sorting, and the sort's own scratch. */
    GenericBuffer _keys;
    GenericBuffer _sortScratch;
} WorldInstanceCells;

/** @brief A component's LOD bands for a frame: which LODs its copies show at which distance. A plain value type. */
typedef struct WorldInstanceLodBandsStruct
{
    /** @brief How many LODs (1 to WORLD_MODEL_MAX_LODS). */
    size_t LodCount;
    /** @brief Whether every copy shows ForcedLod (below LodCount) whatever its distance. */
    bool IsForced;
    size_t ForcedLod;
    /** @brief Band i hands a copy from LOD i to LOD i + 1 between Start[i] and End[i] (zoomed distances in metres,
     *  Start[i] <= End[i] <= Start[i + 1]); LodCount - 1 bands. An empty band (Start equal to End) switches at once. */
    float Start[WORLD_INSTANCE_MAX_BANDS];
    float End[WORLD_INSTANCE_MAX_BANDS];
} WorldInstanceLodBands;

/**
 * @brief What every copy of a component needs for its fade and its LOD this frame: the uniforms of the instanced vertex
 *        stages (shaders/common/instancing.glsl), with the same names. A plain value type: fill it with
 *        WorldInstancing_SetDrawDistance and the fields below.
 */
typedef struct WorldInstanceViewStruct
{
    /** @brief The camera's zoom factor (WorldVisibility_GetZoomFactor; the caller gives 1 for anything not finite or not
     *  above 0): distances are multiplied by it. */
    float ZoomFactor;
    /** @brief The draw distance in effect (FLT_MAX for one that never fades, 0 for none), where the fade band starts and
     *  how long it is (always above 0): WorldInstancing_SetDrawDistance fills them. */
    float DrawDistance;
    float FadeStart;
    float FadeBand;
    /** @brief The object's opacity (its final tint's alpha over 255): every copy's opacity is multiplied by it. */
    float ObjectOpacity;
    /** @brief The world model's sphere in a copy's space (WorldInstanceBounds), its radius also times the object's
     *  world matrix's stretch (WorldTransform_GetMaxScale), so a copy's radius is it times the copy's own stretch. */
    Vector3 BoundsCenter;
    float BoundsRadius;
    /** @brief The LOD bands. */
    WorldInstanceLodBands Bands;
} WorldInstanceView;

/** @brief Where a copy stands this frame (WorldInstancing_Evaluate). A plain value type. */
typedef struct WorldInstanceStateStruct
{
    /** @brief Its fade: the larger of its draw distance's fade and 1 - its opacity, 0 solid .. 1 gone. */
    float Fade;
    /** @brief The LOD it shows; in a band, the outgoing one. */
    size_t Lod;
    /** @brief In a band, the incoming LOD (Lod + 1); Lod otherwise. */
    size_t IncomingLod;
    /** @brief In a band, the incoming LOD's share of the pixels (0 at the band's start .. 1 at its end); 0 otherwise. */
    float Progress;
    /** @brief Whether it is in a band (two LODs share it). */
    bool IsInBand;
} WorldInstanceState;

/** @brief The passes, by how they draw a copy (WorldInstancing_GetDraw). Values shared with common/instancing's
 *         instancePass. */
typedef enum WorldInstancePassEnum
{
    /** @brief The lit pass: both LODs of a band, each dithered by its part, and the copy's fade. */
    WorldInstancePass_Lit = 0,
    /** @brief The G-buffer: the LOD with the larger share, solid, while the fade is below
     *  WORLD_VISIBILITY_GBUFFER_FADE_LIMIT. */
    WorldInstancePass_GBuffer = 1,
    /** @brief The shadow passes: the LOD with the larger share, dithered by the copy's own fade alone. */
    WorldInstancePass_Shadow = 2,
    /** @brief The unlit fallback (raylib's default shader, no dither): the LOD with the larger share, solid. */
    WorldInstancePass_Unlit = 3
} WorldInstancePass;

/** @brief How one pass draws one LOD of one copy. A plain value type. */
typedef struct WorldInstanceDrawStruct
{
    /** @brief Whether the pass draws the copy with this LOD (otherwise the vertex stage drops it). */
    bool IsDrawn;
    /** @brief Its dither: the fade, the band's progress and whether this LOD is the incoming one (WorldVisibility's
     *  dither formula). */
    float Fade;
    float Progress;
    bool IsIncoming;
} WorldInstanceDraw;


// Functions: cells.
/**
 * @brief Initializes empty cells.
 * @param self The cells; must not be NULL.
 */
void WorldInstanceCells_Construct(WorldInstanceCells* self);

/**
 * @brief Releases the cells' buffers.
 * @param self The cells, or NULL.
 */
void WorldInstanceCells_Deconstruct(WorldInstanceCells* self);

/**
 * @brief Sorts @p count copies into cells (replacing what @p self held) and builds their records and bounds.
 *
 * One pass to key them, one sort (O(n log n), copies with the same square kept in the list's order), one pass to build
 * the records (each copy's matrix composed in double relative to its cell's centre, rounded once; the random value from
 * @p objectId and the copy's index) and the cells' spheres (each copy's sphere is @p bounds under its matrix, in double).
 * Allocates only to grow the buffers. Main thread only.
 * @param self The cells; must not be NULL.
 * @param instances The copies (positions relative to the object, in its space); may be NULL when @p count is 0.
 * @param count How many (at most PROPERTY_INSTANCE_LIST_MAX_COUNT).
 * @param objectId The object's id, for the random values.
 * @param bounds The world model's sphere in a copy's space; must not be NULL.
 * @returns Success; ErrorCode_IllegalArgument for a NULL argument (or NULL copies with a count above 0);
 *          ErrorCode_InvalidOperation when a buffer could not grow (the cells are then empty).
 */
Error WorldInstanceCells_Build(WorldInstanceCells* self, const PropertyInstanceEntry* instances, size_t count,
    uint64_t objectId, const WorldInstanceBounds* bounds);

/**
 * @brief Returns the cells.
 * @param self The cells; must not be NULL.
 * @param outCount [out] Receives their count. Must not be NULL.
 * @returns The cells (borrowed until the next build); NULL when there are none.
 */
static inline const WorldInstanceCell* WorldInstanceCells_GetCells(const WorldInstanceCells* self, size_t* outCount)
{
    const void* Cells = self->_cells._data;
    *outCount = self->_cells._count;
    return (self->_cells._count > 0U) ? Cells : NULL;
}

/**
 * @brief Returns every record, cell by cell (a cell's are RecordCount from FirstRecord).
 * @param self The cells; must not be NULL.
 * @param outCount [out] Receives their count. Must not be NULL.
 * @returns The records (borrowed until the next build); NULL when there are none.
 */
static inline const WorldInstanceRecord* WorldInstanceCells_GetRecords(const WorldInstanceCells* self, size_t* outCount)
{
    const void* Records = self->_records._data;
    *outCount = self->_records._count;
    return (self->_records._count > 0U) ? Records : NULL;
}

/**
 * @brief Returns each record's copy: its index in the instance list the cells were built from.
 * @param self The cells; must not be NULL.
 * @param outCount [out] Receives their count (the records'). Must not be NULL.
 * @returns The indices (borrowed until the next build); NULL when there are none.
 */
static inline const size_t* WorldInstanceCells_GetSources(const WorldInstanceCells* self, size_t* outCount)
{
    const void* Sources = self->_sources._data;
    *outCount = self->_sources._count;
    return (self->_sources._count > 0U) ? Sources : NULL;
}

/**
 * @brief A copy's stable random value: a hash of the object's id and the copy's index (wind phases and variation
 *        later), the same on every machine.
 * @param objectId The object's id.
 * @param index The copy's index in its list.
 * @returns A value in [0, 1) with 24 bits of randomness.
 */
float WorldInstancing_GetRandom(uint64_t objectId, size_t index);

/**
 * @brief A record's matrix (its three rows and 0 0 0 1) as a raylib matrix, relative to its cell's centre.
 * @param record The record; must not be NULL.
 * @returns The matrix.
 */
Matrix WorldInstanceRecord_GetMatrix(const WorldInstanceRecord* record);


// Functions: fades and bands (the C mirror of common/instancing).
/**
 * @brief A component's LOD bands for a frame, from its LOD settings in effect and the player's lod_distance_scale.
 *
 * Band i lies around switch distance i times the scale, s, from s(1 - h) to s(1 + h), h the settings' hysteresis (a
 * share in [0, 1); anything else counts as 0); each band stops at the middle between its switch distance and its
 * neighbours', so no two overlap. A forced LOD is clamped to the last. A scale of 0 shows the last LOD everywhere, as
 * WorldLod_Select does.
 * @param settings The LOD settings in effect (WorldModelInstancesComponent_ResolveLodSettings); must not be NULL. A LOD
 *        count of 0 counts as 1.
 * @param distanceScale The lod_distance_scale; anything not finite or below 0 counts as 1.
 * @param outBands [out] Receives the bands. Must not be NULL.
 */
void WorldInstancing_GetLodBands(const WorldModelLodSettings* settings, float distanceScale,
    WorldInstanceLodBands* outBands);

/**
 * @brief Fills a view's draw distance, where its fade band starts and the band's length, from the draw distance in
 *        effect (already scaled by the quality setting): the objects' rule (WorldVisibility_GetFadeBand), as the
 *        vertex stage receives it (an infinite distance as FLT_MAX, which nothing reaches; none above 0 fades
 *        everything).
 * @param self The view; must not be NULL.
 * @param drawDistance The draw distance in effect, in metres.
 */
void WorldInstancing_SetDrawDistance(WorldInstanceView* self, float drawDistance);

/**
 * @brief A copy's draw-distance fade at a (zoomed) distance to its sphere's nearest point: 0 before the band, rising
 *        linearly to 1 at the draw distance, 1 beyond (and for a NaN). The same as WorldVisibility_GetDistanceFade for
 *        the view's draw distance.
 * @param view The view; must not be NULL.
 * @param distance The distance.
 * @returns The fade, in [0, 1].
 */
float WorldInstancing_GetDistanceFade(const WorldInstanceView* view, float distance);

/**
 * @brief Where a copy stands this frame: the vertex stage's per-copy maths in C.
 *
 * The copy's sphere is the view's bounds under the record's matrix (its centre moved, its radius times the matrix's
 * longest axis), placed in render space by the cell's matrix; its LOD distance is the camera's (at the origin) to the
 * centre times the zoom, its fade distance to the nearest point times the zoom; the fade and the LOD follow (see the
 * file comment).
 * @param view The component's view this frame; must not be NULL.
 * @param cellRenderMatrix The cell's matrix in render space (the object's world matrix times the translation to the
 *        cell's centre, relative to the frame's origin); must not be NULL.
 * @param record The copy; must not be NULL.
 * @returns The state.
 */
WorldInstanceState WorldInstancing_Evaluate(const WorldInstanceView* view, const Matrix* cellRenderMatrix,
    const WorldInstanceRecord* record);

/**
 * @brief The LOD a pass that draws one LOD of a copy draws (the G-buffer, the shadows, the unlit fallback): the one with
 *        the larger share (the incoming one from WORLD_LOD_SOLID_SHARE of its band on).
 * @param state The copy's state; must not be NULL.
 * @returns The LOD.
 */
size_t WorldInstancing_GetSolidLod(const WorldInstanceState* state);

/**
 * @brief How @p pass draws LOD @p lod of a copy (see the file comment for the rules).
 * @param state The copy's state; must not be NULL.
 * @param pass The pass.
 * @param lod The LOD of the draw.
 * @returns The draw: not drawn, or drawn with its dither.
 */
WorldInstanceDraw WorldInstancing_GetDraw(const WorldInstanceState* state, WorldInstancePass pass, size_t lod);

/**
 * @brief The LODs the copies of a cell can show this frame: bit k for LOD k, from the cell's range of (zoomed) LOD
 *        distances, widened by WORLD_INSTANCE_CELL_RANGE_SHARE and WORLD_INSTANCE_CELL_RANGE_MARGIN for the GPU's
 *        rounding. LOD k shows from the start of the band before it to the end of the band after it.
 * @param bands The component's bands; must not be NULL.
 * @param nearest The smallest LOD distance a copy of the cell can have (the camera's distance to the cell's sphere's
 *        nearest point, times the zoom); a NaN gives every LOD.
 * @param farthest The largest (the distance to its farthest point, times the zoom).
 * @returns The mask (the forced LOD's bit alone while one is forced).
 */
uint32_t WorldInstancing_GetCellLodMask(const WorldInstanceLodBands* bands, float nearest, float farthest);
