#pragma once
#include <stdbool.h>
#include <stddef.h>
#include "TestAnimation.h"
#include "TestFramework.h"
#include "AnimationLibrary.h"
#include "AssetManager.h"
#include "raylib/raylib.h"


/**
 * Asset definitions built in code for headless tests. They load without files or a GL context, so tests can
 * exercise code that holds real assets (the world's sprite animations, the renderer's per-object cache):
 *  - a sprite animation with one texture-less frame, or with several frames and running playback;
 *  - a model without meshes whose mesh-space bounds are given (enough for anything that does not draw);
 *  - a skinned model with its skeleton, its mesh in CPU memory only (enough to pose it, T29);
 *  - an animation library read from a glTF file written in memory (TestGltf_Write), by the real reader;
 *  - a monospace font of the printable ASCII glyphs with metrics but no texture (enough to measure text);
 *  - a texture that names a made-up GPU id and was never uploaded (enough for anything that only copies it);
 *  - world models built from JSON by the real "world_model" type, over the code-built models and textures above;
 *  - world sprites built from JSON by the real "world_sprite" type, over the code-built sprite animations above.
 * Each definition can count how often its asset is loaded and unloaded, which shows when an asset user really
 * releases it.
 */


// Macros.
/** @brief Base (rasterization) size, in pixels, of the fonts TestAssets_AddFont registers. */
#define TEST_FONT_BASE_SIZE (10)
/** @brief First and last codepoint of the fonts TestAssets_AddFont registers (printable ASCII). */
#define TEST_FONT_FIRST_CODEPOINT (32)
#define TEST_FONT_LAST_CODEPOINT (126)


// Types.
/** @brief Load and unload counts of one test definition's asset; owned by the test (e.g. on its stack). */
typedef struct TestAssetCountersStruct
{
    /** @brief How many times the asset was built. */
    size_t LoadCount;
    /** @brief How many times the asset was destroyed. */
    size_t UnloadCount;
} TestAssetCounters;

/** @brief An asset manager with the standard asset types and no search roots; tests add definitions in code. */
typedef struct TestAssetFixtureStruct
{
    /** @brief The manager. */
    AssetManager* Manager;
    /** @brief The standard type ids. */
    StandardAssetTypes Types;
    /** @brief The JSON pool the standard types parse with. */
    JSONObjectPool* Pool;
} TestAssetFixture;


// Functions.
/**
 * @brief Creates the fixture's manager and standard types (no logger, no search roots).
 * @returns true when every step succeeded (failed checks are recorded on @p run).
 */
bool TestAssetFixture_Construct(TestRun* run, TestAssetFixture* self);

/** @brief Deconstructs the manager (unloading what is still loaded) and the JSON pool. */
void TestAssetFixture_Deconstruct(TestRun* run, TestAssetFixture* self);

/**
 * @brief Registers a sprite animation definition named @p name whose asset has one texture-less frame.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name,
    TestAssetCounters* counters);

/**
 * @brief Registers a sprite animation definition named @p name whose asset has @p frameCount texture-less frames and
 *        plays them running and looped at @p fps (frame step 1).
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddPlayingSpriteAnimation(TestRun* run, TestAssetFixture* fixture, const char* name, size_t frameCount,
    double fps, TestAssetCounters* counters);

/**
 * @brief Registers a font definition named @p name whose asset is a texture-less monospace font: every printable
 *        ASCII glyph (TEST_FONT_FIRST_CODEPOINT to TEST_FONT_LAST_CODEPOINT, '?' included) advances @p advancePixels at
 *        a base size of TEST_FONT_BASE_SIZE pixels. Its shadow offset is the default one.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddFont(TestRun* run, TestAssetFixture* fixture, const char* name, int advancePixels,
    TestAssetCounters* counters);

/**
 * @brief Like TestAssets_AddFont, but the font's shadow offset (GameFont_GetShadowOffset) is @p shadowOffset.
 * @returns true when it was registered.
 */
bool TestAssets_AddFontWithShadowOffset(TestRun* run, TestAssetFixture* fixture, const char* name, int advancePixels,
    Vector2 shadowOffset, TestAssetCounters* counters);

/**
 * @brief Registers a model definition named @p name whose asset has no meshes, an identity import transform and
 *        the mesh-space bounds @p bounds.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddModel(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    TestAssetCounters* counters);

/**
 * @brief Registers a model definition like TestAssets_AddModel whose asset also has @p materialCount materials
 *        (without maps or textures) and the import transform @p transform.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddModelWithMaterials(TestRun* run, TestAssetFixture* fixture, const char* name, BoundingBox bounds,
    int materialCount, Matrix transform, TestAssetCounters* counters);

/**
 * @brief Registers a model definition named @p name whose asset has one CPU-side mesh (copies of @p vertices, three
 *        floats each, and of @p indices, three per triangle, or none: every three vertices are a triangle), never
 *        uploaded, one material, the import transform @p transform and the vertices' box as its mesh-space bounds: what
 *        collision hulls and meshes read from a real model.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddMeshModel(TestRun* run, TestAssetFixture* fixture, const char* name, const float* vertices,
    int vertexCount, const unsigned short* indices, int triangleCount, Matrix transform, TestAssetCounters* counters);

/**
 * @brief Registers a model definition named @p name whose asset is a skinned model built in code
 *        (TestSkeletonModel_Create: the bones' skeleton and one mesh with a box of @p boxSize per bone, never uploaded)
 *        with its skeleton (GameSkeleton_Create, as the model type makes it), the import transform @p transform and the
 *        boxes' box as its mesh-space bounds: what the animation system and the render cache read from a real one.
 * @param bones The bones (copied, names too).
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddSkinnedModel(TestRun* run, TestAssetFixture* fixture, const char* name, const TestSkeletonBone* bones,
    int boneCount, float boxSize, Matrix transform, TestAssetCounters* counters);

/**
 * @brief Registers an animation library definition named @p name, of the standard "animation_library" type, whose asset
 *        is read when it loads from the glTF file text @p gltf with the clip settings @p clips
 *        (AnimationLibrary_CreateFromGltf), as the real definition reads its file.
 * @param gltf The file's text (copied; TestGltf_Write writes one).
 * @param clips The clip settings (copied, strings too); may be NULL when @p clipCount is 0.
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddAnimationLibrary(TestRun* run, TestAssetFixture* fixture, const char* name, const char* gltf,
    const AnimationClipSettings* clips, size_t clipCount, TestAssetCounters* counters);

/**
 * @brief Registers a texture definition named @p name whose asset names the GPU texture id @p textureId without
 *        uploading anything (16 x 16, sRGB).
 * @param counters Counts its loads and unloads; may be NULL. Must outlive the fixture's manager.
 * @returns true when it was registered.
 */
bool TestAssets_AddTexture(TestRun* run, TestAssetFixture* fixture, const char* name, unsigned int textureId,
    TestAssetCounters* counters);

/**
 * @brief Builds a world model definition from @p json with the real "world_model" type (AssetManager_BuildDefinition)
 *        and registers it. Its LOD models and textures must be code-built (TestAssets_AddModel, TestAssets_AddTexture)
 *        for it to load without a GL context.
 * @returns true when it was built and registered.
 */
bool TestAssets_AddWorldModel(TestRun* run, TestAssetFixture* fixture, const char* json);

/**
 * @brief Registers a world model named @p name with one LOD, the model @p modelName, used up to 600 m (the draw
 *        distance models had before world models), and the default settings.
 * @returns true when it was built and registered.
 */
bool TestAssets_AddWorldModelOf(TestRun* run, TestAssetFixture* fixture, const char* name, const char* modelName);

/**
 * @brief Builds a world sprite definition from @p json with the real "world_sprite" type (AssetManager_BuildDefinition)
 *        and registers it. Its sprite animation must be code-built (TestAssets_AddSpriteAnimation) for it to load without
 *        a GL context.
 * @returns true when it was built and registered.
 */
bool TestAssets_AddWorldSprite(TestRun* run, TestAssetFixture* fixture, const char* json);

/**
 * @brief Registers a world sprite named @p name that plays the sprite animation @p animationName with the default
 *        settings.
 * @returns true when it was built and registered.
 */
bool TestAssets_AddWorldSpriteOf(TestRun* run, TestAssetFixture* fixture, const char* name, const char* animationName);

/**
 * @brief Builds a character definition from @p json with the real "character" type (AssetManager_BuildDefinition) and
 *        registers it (a character has no dependencies: it loads without a GL context).
 * @returns true when it was built and registered.
 */
bool TestAssets_AddCharacter(TestRun* run, TestAssetFixture* fixture, const char* json);

/**
 * @brief Builds an animation graph definition from @p json with the real "animation_graph" type
 *        (AssetManager_BuildDefinition) and registers it (a graph has no dependencies: it loads without a GL context).
 * @returns true when it was built and registered.
 */
bool TestAssets_AddAnimationGraph(TestRun* run, TestAssetFixture* fixture, const char* json);
