#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "raylib/raylib.h"
#include "wr/WRMemory.h"
#include "WorldCollision.h"

/*
 * Private header (source/) of the character system: the capsule's motion against the collision world
 * (WorldCharacterMotion.c), used by the system's update (WorldCharacters.c). Positions are the capsule's FEET (the
 * bottom of its bottom cap); the capsule stands upright above them.
 *
 * SUPPORT. What a walking capsule stands on: a contact whose normal is walkable (within the slope limit), or, while it
 * climbs a step, the step's edge under its round bottom: the contact's own normal is then steep, so a short ray down
 * just inside the contact looks at the face the edge belongs to. Standing on such an edge is how the round bottom
 * rolls over a step's edge after a step up lands on it. Only a climb does: going off a ledge or down a stair, the round
 * bottom rolls over the edge only as far as the contact stays walkable (a human's 0.3 m radius and 45 degree slope
 * limit sink it 8.8 cm), and then the walker falls (T13's first tuning: rolling on down the edge kept a walker on the
 * ground while it sank most of its radius, and the ground snap then dropped it the rest in a tick).
 *
 * STEPS. A walker's ground is also the plane its moves are measured from: an edge (a contact that is not on a face)
 * higher than the step height above that plane, measured straight up, is a wall to the slide and no support to the
 * ground follower, whatever its normal. Otherwise the round bottom would roll over any edge up to radius x
 * (1 - cos slope_limit) high, a step height of 0 included. A slope's or a crest's edge lies on the plane, and an edge a
 * walker rolls down off is level with it, so neither counts.
 */


// Macros.
/** A contact on the bottom cap counts as an edge to stand on only while its normal points at least this far up (the
 *  contact is not on the cap's rim). */
#define CHARACTER_EDGE_MIN_NORMAL_Y 0.01f
/** The edge probe's ray starts this far inside the contact (away from the capsule), metres... */
#define CHARACTER_EDGE_PROBE_INSET 0.01f
/** ...this far above it... */
#define CHARACTER_EDGE_PROBE_LIFT 0.02f
/** ...and looks this far below it for the face. */
#define CHARACTER_EDGE_PROBE_DEPTH 0.03f
/** A move stops this far beyond the skin from what it hits, along its normal, metres (it rests the skin and this from
 *  the ground). Room for a move's direction, off by rounding, to run along a wall or the floor without running into
 *  it: with 0.2 mm a run along a yawed wall at 30 m/s hit it within the tick and lost speed. And well over the
 *  collision casts' touch tolerance (0.1 mm), so the next sweep starts clear: a sweep starting within it takes any move
 *  into the contact as a hit, and a tiny move's direction, rounded where its ends are, can point a hair into a curved
 *  contact it only rolls along (a stair's edge), stalling a slow walker. */
#define CHARACTER_REST_GAP 1e-3f
/** A move backs off along its direction at most the rest gap over this (grazing hits). */
#define CHARACTER_MIN_APPROACH 0.1f
/** A grazing hit leaves less than the rest gap; a capsule that drifted closer than this is backed out to it, metres:
 *  five times the casts' touch tolerance, which a run along a wall at 30 m/s does not drift across in a tick. */
#define CHARACTER_MIN_REST_GAP 5e-4f
/** Moves shorter than this are not swept, metres. */
#define CHARACTER_MIN_MOVE 1e-5f
/** A step up must climb at least this much before it moves forward, metres (otherwise a ceiling is in the way). */
#define CHARACTER_MIN_STEP_CLIMB 1e-3f
/** A step up must land at least this much higher than it started, metres: only what lands back on the level it started
 *  from fails it (CHARACTER_STEP_ENGAGE_DISTANCE makes a real step rise millimetres at once). */
#define CHARACTER_MIN_STEP_RISE 1e-4f
/** A step up's move forward goes at least this far past the gap the slide kept to the riser, metres, so the round bottom
 *  always reaches over a step's edge, however little of the tick's move was left (a slow walk, a riser met at the end of
 *  a move). */
#define CHARACTER_STEP_ENGAGE_DISTANCE 1e-4f
/** A contact's normal and the normal of the face under it differ by more than this (the cosine of about 2 degrees):
 *  the contact is on an edge, not on the face. */
#define CHARACTER_SAME_FACE_COSINE 0.9994f
/** How far down an airborne character looks for ground to land on, metres. */
#define CHARACTER_LANDING_PROBE_DISTANCE 0.004f
/** How far below a character "touching the ground" may have it (a flying bird about to land), metres. */
#define CHARACTER_TOUCH_PROBE_DISTANCE 0.02f
/** The most planes one move remembers (its slides, plus the planes of steps it tried). */
#define CHARACTER_MAX_PLANES 6U
/** How many times the ground follower slides off a rim before it gives up. */
#define CHARACTER_MAX_GROUND_ITERATIONS 3U


// Types.
/**
 * The capsule a move uses and what it collides with: a plain value built for each character's update.
 */
typedef struct CharacterBodyStruct
{
    /** The collision world (borrowed). */
    WorldCollision* Collision;
    /** What moves collide with: static and kinematic colliders on the character's mask (pushables are the collision
     *  step's), ignoring the character and what it carries. */
    WorldCollisionFilter MoveFilter;
    /** What it stands on: the same with pushables (crates). */
    WorldCollisionFilter GroundFilter;
    /** The capsule's radius and its whole height now (the stance's), metres. */
    float Radius;
    float Height;
    /** The cosine of the slope limit: a normal at least this far up is walkable. */
    float WalkableNormalY;
    /** The highest step it climbs, metres. */
    float StepHeight;
} CharacterBody;

/**
 * What a capsule stands on. A walker's support is also the plane its steps are measured from (the file comment).
 */
typedef struct CharacterSupportStruct
{
    /** Whether it stands on walkable ground (a walkable face, or the edge of one). */
    bool IsSupported;
    /** The object (0: none). */
    uint64_t ObjectId;
    /** The walkable ground's normal (the contact's, or the face's under an edge with a steep contact; up while not
     *  supported). */
    Vector3 Normal;
    /** The contact point. */
    Vector3 Point;
    /** Whether it holds the capsule by an edge whose contact is steeper than the slope limit (the face under the edge
     *  is walkable): a step a step up landed on, until the walker stands on walkable ground (the file comment). */
    bool IsSteepEdge;
} CharacterSupport;

/**
 * What a look for the ground found.
 */
typedef enum CharacterGroundEnum
{
    /** Nothing supports the capsule within reach. */
    CharacterGround_Unsupported = 0,
    /** Something supports it (the support says what). */
    CharacterGround_Supported = 1,
    /** A pushable overlapping it from the side (another character it walks into) hid what is below: it cannot tell.
     *  The collision step will push them apart; the caller keeps what it knew. */
    CharacterGround_Unsure = 2
} CharacterGround;

/**
 * The end of a move and slide.
 */
typedef struct CharacterMoveStruct
{
    /** Where the feet ended. */
    Vector3 Feet;
    /** Whether the move climbed a step (then StepSupport is what it landed on). */
    bool HasStepped;
    CharacterSupport StepSupport;
    /** Whether anything stopped or deflected it. */
    bool HasHit;
} CharacterMove;


// Functions (WorldCharacterMotion.c).
/** A support that holds nothing: not supported, straight up. */
CharacterSupport CharacterSupport_CreateNone(void);

/** Sweeps the body's capsule, a skin wider all round, from feet @p from to feet @p to with @p filter (see
 *  WorldCollision_SweepCapsule): where it touches something the body is the skin from it along the contact's normal,
 *  so a move that stops short of the sweep's distance keeps the collision step's skin to everything it passes, not only
 *  to what it stops at (a curb's edge beside the floor a walker settles on). Within the skin at the start is a start
 *  penetration whose depth is how far short of the skin it is. */
bool CharacterMotion_Sweep(const CharacterBody* body, Vector3 from, Vector3 to, const WorldCollisionFilter* filter,
    WorldCastHit* outHit);

/** Whether a cast's contact is something to stand on (see the file comment): a walkable contact, or, with
 *  @p isSteepEdgeAllowed (a step being climbed), an edge whose face below is walkable. */
CharacterSupport CharacterMotion_EvaluateSupport(const CharacterBody* body, const WorldCastHit* hit,
    bool isSteepEdgeAllowed);

/** The normal of the face a support's contact belongs to: the walkable face just inside the contact when it holds by
 *  an edge (the top of a step whose rim the round bottom rolls over), its own normal on a face. */
Vector3 CharacterMotion_GetFaceNormal(const CharacterBody* body, const CharacterSupport* support);

/** Whether walkable ground lies straight below @p feet within @p distance (a ray down the capsule's axis). */
bool CharacterMotion_HasGroundBelow(const CharacterBody* body, Vector3 feet, float distance);

/** @p vector without the parts that go into any of @p planes (unit normals): clipped against one plane when that is
 *  enough, along the crease of two, or zero in a corner of three. */
Vector3 CharacterMotion_ClipToPlanes(Vector3 vector, const Vector3* planes, size_t planeCount);

/** A horizontal move made to run along walkable ground of @p normal: the same horizontal part, the height the ground
 *  rises or falls by under it. */
Vector3 CharacterMotion_ProjectOnGround(Vector3 move, Vector3 normal);

/**
 * Moves the capsule from @p feet by @p displacement: at most CHARACTER_MAX_SLIDE_ITERATIONS sweeps, each stopping the
 * skin and CHARACTER_REST_GAP before what it hits (backing out to that gap when it started closer) and sliding the rest
 * along it. @p velocity loses its parts into what was hit. With a @p ground (a walker on it; NULL in the air and
 * flying), steep ground and an edge above the step height (the file comment) count as an upright wall, which a walker
 * backs away from level, and a blocked move tries to climb a step once. Pushables do not stop it (the body's move
 * filter): the collision step pushes them apart, and a character falling into one comes out on top of it in its ground
 * follow (CharacterMotion_FollowGround).
 */
CharacterMove CharacterMotion_MoveAndSlide(const CharacterBody* body, Vector3 feet, Vector3 displacement,
    Vector3* velocity, const CharacterSupport* ground);

/**
 * Follows the ground down from @p feet by at most @p distance, sliding off rims, until something supports the capsule.
 * With the @p ground a walker walked on (NULL: placing, landing, a flyer's probe), an edge above the step height is a
 * rim (the file comment), and so is a steep edge unless that ground is one (a climb goes on). Supported: the new feet
 * and the support are written. Otherwise the outputs are untouched: Unsupported (nothing within reach) or Unsure (a
 * pushable overlapping it from the side hid the ground).
 */
CharacterGround CharacterMotion_FollowGround(const CharacterBody* body, Vector3 feet, float distance,
    const CharacterSupport* ground, Vector3* outFeet, CharacterSupport* outSupport);

/** Whether a capsule of @p height fits at @p feet: the capsule a skin narrower, its bottom two skins above the feet (so
 *  resting on the ground does not count), touching nothing. @p scratchIds holds uint64_t ids; it is cleared and reused. */
bool CharacterMotion_DoesFit(const CharacterBody* body, Vector3 feet, float height, GenericBuffer* scratchIds);
