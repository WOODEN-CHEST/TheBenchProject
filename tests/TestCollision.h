#pragma once
#include <stdint.h>
#include "TestFramework.h"
#include "CollisionShape.h"
#include "raylib/raylib.h"


/**
 * Helpers shared by the collision suites (the narrowphase and the characters): collision meshes shaped as the game's
 * generators make them.
 *
 * A HEDGE is what a path object makes of a box it bends along a curve (PathObjectPlacement's bend): slices across a
 * centreline TEST_HEDGE_RADIUS from a centre of curvature on +Z, each turned TEST_HEDGE_BEND_DEGREES further (so its -Z
 * side is convex and its +Z side concave, folded that much at every slice), TEST_HEDGE_WIDTH wide and TEST_HEDGE_HEIGHT
 * tall from y 0. Its top and bottom are each two source triangles meeting along a diagonal through a point of every
 * slice's line, cut at every slice and each part fanned, as the bend makes them: in every slab one of their triangles has
 * only inner edges (flat seams) and meets the outline at a single corner. Every triangle faces out; the middle slice
 * lies across the X axis at X = 0, its outward normal -Z.
 */


// Macros.
/** @brief The hedge's centreline's radius of curvature, metres. */
#define TEST_HEDGE_RADIUS 14.0f
/** @brief How far each slice turns from the last, degrees (a slab is about half a metre long). */
#define TEST_HEDGE_BEND_DEGREES 2.0f
/** @brief The hedge's width and height, metres. */
#define TEST_HEDGE_WIDTH 0.9f
#define TEST_HEDGE_HEIGHT 1.0f


// Functions.
/**
 * @brief Builds a hedge (see the file comment) of @p slabCount slabs.
 * @returns The mesh (CollisionMesh_Destroy releases it), or NULL after a failed check.
 */
CollisionMesh* TestCollision_CreateHedge(TestRun* run, uint32_t slabCount);

/**
 * @brief Where a hedge of @p slabCount slabs has its centreline at its bottom, @p along of its length from its first slice
 *        (0 to 1), and the outward normal of its convex side there.
 */
void TestCollision_GetHedgeFrame(uint32_t slabCount, float along, Vector3* outCenter, Vector3* outOutward);
