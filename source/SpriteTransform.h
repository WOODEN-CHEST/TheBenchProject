#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "WorldSprite.h"
#include "raylib/raylib.h"
#include "wr/WRMemory.h"

/*
 * Private header (source/): the maths of a world sprite's quad, pure and tested headless (tests/SpriteTransformTests.c).
 *
 * Every sprite is drawn as one unit quad: x and y in [0, 1] (the image from left to right and from bottom to top), z 0,
 * its normal +Z. A sprite's model matrix maps that quad into the world:
 *   - the pivot (a point of the image, in the same [0, 1] units) lands on the object's position;
 *   - the image's width and height become the sprite's world size (SpriteTransform_GetWorldSize), mirrored about the
 *     pivot by the flips;
 *   - the quad's X, Y and Z axes become a basis that depends on the orientation and on who looks at the sprite (the
 *     viewer):
 *       billboard       the normal points at the viewer (the camera, a point light) or along a directional light's
 *                       direction, and the image's up leans toward the viewer's up;
 *       axis billboard  the image's up is the object's local Y axis, and the normal is the direction to the viewer turned
 *                       around that axis;
 *       fixed           the object's rotation (the viewer does not matter).
 * So in the camera's passes a billboard faces the camera, and in a light's shadow pass it faces the light: its shadow is
 * the sprite as the light sees it, and does not turn with the camera.
 *
 * The matrix's third column is the quad's normal (unit: it carries no scale), so a shader gets the normal without an
 * inverse transpose. Its determinant is negative when exactly one of the image's axes is mirrored (draw sprites with
 * culling off).
 */


// Types.
/**
 * @brief Who a sprite turns toward: a point (the camera, a point light) or a directional light.
 *
 * Build with SpriteViewer_FromPoint or SpriteViewer_FromDirection. A plain value type.
 */
typedef struct SpriteViewerStruct
{
    /** @brief true: a directional light, Direction is used; false: a point viewer, Position is used. */
    bool IsDirectional;
    /** @brief The point viewer's world position. */
    Vector3 Position;
    /** @brief Unit direction TOWARD the directional light. */
    Vector3 Direction;
    /** @brief The viewer's up (unit): a billboard's image leans its top toward it. The camera's up for the camera, world
     *  up for lights. */
    Vector3 Up;
} SpriteViewer;

/**
 * @brief What a sprite's quad is made of, before a viewer turns it. A plain value type.
 */
typedef struct SpriteShapeStruct
{
    /** @brief How the quad is turned. */
    WorldSpriteOrientation Orientation;
    /** @brief World position of the pivot (the object's position). */
    Vector3 Position;
    /** @brief The object's rotation as its local X, Y and Z axes in world space (unit, right-handed): a fixed quad's
     *  basis, an axis billboard's axis (Y), and the facing a billboard falls back to when its viewer gives none (Z). */
    Vector3 AxisX;
    Vector3 AxisY;
    Vector3 AxisZ;
    /** @brief World width and height (SpriteTransform_GetWorldSize); a negative one mirrors like a flip. */
    Vector2 Size;
    /** @brief The point of the image at Position: (0, 0) bottom left, (1, 1) top right. */
    Vector2 Pivot;
    /** @brief Whether the image is mirrored left to right, and top to bottom, about the pivot. */
    bool IsFlippedX;
    bool IsFlippedY;
} SpriteShape;

/**
 * @brief One entry of a list sorted for drawing: a sprite's index in its list and its depth along the view.
 */
typedef struct SpriteDrawOrderStruct
{
    /** @brief The sprite's index in the list the caller draws from. */
    size_t Index;
    /** @brief Its distance in front of the camera, along the view direction (larger = farther). */
    float Depth;
} SpriteDrawOrder;


// Functions.
/**
 * @brief Makes a point viewer: the camera, or a point light.
 * @param position The viewer's world position.
 * @param up The viewer's up; need not be unit or perpendicular to anything, only not zero (world up for lights).
 * @returns The viewer.
 */
SpriteViewer SpriteViewer_FromPoint(Vector3 position, Vector3 up);

/**
 * @brief Makes a directional viewer: a directional light (the sun, the moon), all of whose rays are parallel.
 * @param towardLight Direction TOWARD the light; need not be unit, only not zero.
 * @param up The viewer's up (world up for the sky's lights).
 * @returns The viewer.
 */
SpriteViewer SpriteViewer_FromDirection(Vector3 towardLight, Vector3 up);

/**
 * @brief A sprite's world width and height: the frame's size in pixels divided by the pixels per unit, times the
 *        object's X and Y scale.
 * @param frameArea The animation frame's area in its texture, in pixels.
 * @param pixelsPerUnit Pixels per world unit; anything not finite and above 0 gives a zero size.
 * @param objectScale The object's scale; X scales the width, Y the height, Z nothing (a negative one mirrors).
 * @returns The size; (0, 0) for an unusable pixels-per-unit.
 */
Vector2 SpriteTransform_GetWorldSize(Rectangle frameArea, float pixelsPerUnit, Vector3 objectScale);

/**
 * @brief The radius of a sphere around the pivot's position that holds the quad in every orientation: the distance from
 *        the pivot to the image's farthest corner.
 * @param size The world size (its sign does not matter).
 * @param pivot The pivot, in image units.
 * @returns The radius (>= 0).
 */
float SpriteTransform_GetBoundingRadius(Vector2 size, Vector2 pivot);

/**
 * @brief Builds the sprite's model matrix for one viewer: the unit quad (x, y in [0, 1], normal +Z) into the world.
 *
 * A billboard's normal points at the viewer (or along a directional viewer's direction) with the image's up toward the
 * viewer's up; an axis billboard's up is AxisY and its normal the direction to the viewer turned around it; a fixed
 * sprite uses AxisX/AxisY/AxisZ. Where the viewer gives no direction (a point viewer at the pivot, or looking straight
 * along an axis billboard's axis) the sprite falls back to facing AxisZ. Constant time; no allocation.
 * @param shape The sprite; must not be NULL.
 * @param viewer Who it turns toward; must not be NULL (ignored for fixed sprites).
 * @returns The matrix (raylib's layout: m0..m2 the image's X axis times its width, m4..m6 its Y axis times its height,
 *          m8..m10 the unit normal, m12..m14 the translation).
 */
Matrix SpriteTransform_GetMatrix(const SpriteShape* shape, const SpriteViewer* viewer);

/**
 * @brief The quad's unit normal under a matrix from SpriteTransform_GetMatrix (its third column).
 * @param matrix The sprite's matrix.
 * @returns The normal.
 */
Vector3 SpriteTransform_GetNormal(Matrix matrix);

/**
 * @brief The centre of the quad under a matrix from SpriteTransform_GetMatrix (the image's middle in the world).
 * @param matrix The sprite's matrix.
 * @returns The centre.
 */
Vector3 SpriteTransform_GetCenter(Matrix matrix);

/**
 * @brief Sorts a list for drawing back to front: the farthest first; equal depths keep their index order (so the draw
 *        order is deterministic).
 *
 * In place, through the framework's sort with a two-element scratch on the stack: no allocation.
 * @param drawOrder A buffer of SpriteDrawOrder; must not be NULL.
 * @returns true when sorted (an empty or one-entry list is sorted already); false for a read-only buffer or the wrong
 *          element size.
 */
bool SpriteTransform_SortBackToFront(GenericBuffer* drawOrder);
