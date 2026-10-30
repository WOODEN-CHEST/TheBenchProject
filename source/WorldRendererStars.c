// The star field: the seeded star catalogue (NightSky.c) as one mesh of one-pixel quads, drawn right after the sky.

#include <math.h>
#include "WorldRendererInternal.h"
#include "NightSky.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset name of the star shader (vertex + fragment). */
#define STARS_SHADER_ASSET_NAME ((const unsigned char*)u8"world_stars")
/** Vertices per star: two triangles, unindexed. */
#define STAR_VERTICES_PER_STAR 6
/** Cosine of an angle no disc reaches: with it, no star counts as behind the moon. */
#define NO_MOON_COS_RADIUS 2.0f
/** Linear radiance of a magnitude-0 star at StarBrightness 1. At the night exposure (the eye adaptation's maximum, 24)
 *  a magnitude 1 star then reads as a clearly bright pixel and a magnitude 6 star, on a dark site, as a just visible
 *  one. Raise it for brighter stars overall; StarBrightness scales it per world. */
#define STAR_RADIANCE_SCALE 0.2f


// Fields.
/* The corners of a star's two triangles, in pixels around its centre (counter-clockwise on screen). */
static const float StarCorners[STAR_VERTICES_PER_STAR][2] =
{
    { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f },
    { -0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f },
};


// Static functions.
static float ClampUnit(float value)
{
    return fminf(fmaxf(value, 0.0f), 1.0f); // fmaxf drops a NaN, so NaN becomes 0
}

/* Frees the CPU arrays a star mesh was built from; the GPU keeps its copy. The pointers are cleared, so UnloadMesh
 * later frees only what raylib allocated itself (the buffer ids) and the GPU objects. */
static void FreeStarMeshArrays(Mesh* mesh)
{
    Memory_Free(mesh->vertices);
    Memory_Free(mesh->texcoords);
    Memory_Free(mesh->texcoords2);
    Memory_Free(mesh->colors);
    mesh->vertices = NULL;
    mesh->texcoords = NULL;
    mesh->texcoords2 = NULL;
    mesh->colors = NULL;
}

/* Builds the mesh of @p count stars and uploads it: per vertex the star's direction, its corner offset (texcoords),
 * its magnitude and twinkle seed (texcoords2) and its colour. false when the sizes overflow or the upload fails (then
 * nothing stays allocated). */
static bool BuildStarMesh(const NightSkyStar* stars, size_t count, Mesh* outMesh)
{
    size_t VertexCount = 0U;
    size_t PositionBytes = 0U;
    size_t CoordinateBytes = 0U;
    size_t ColorBytes = 0U;
    if (!Memory_TryMultiplySize(count, STAR_VERTICES_PER_STAR, &VertexCount)
        || !Memory_TryMultiplySize(VertexCount, 3U * sizeof(float), &PositionBytes)
        || !Memory_TryMultiplySize(VertexCount, 2U * sizeof(float), &CoordinateBytes)
        || !Memory_TryMultiplySize(VertexCount, 4U, &ColorBytes)
        || (VertexCount > (size_t)INT32_MAX))
    {
        return false;
    }

    Mesh StarMesh = { 0 };
    StarMesh.vertexCount = (int)VertexCount;
    StarMesh.triangleCount = (int)(VertexCount / 3U);
    StarMesh.vertices = Memory_Allocate(PositionBytes);
    StarMesh.texcoords = Memory_Allocate(CoordinateBytes);
    StarMesh.texcoords2 = Memory_Allocate(CoordinateBytes);
    StarMesh.colors = Memory_Allocate(ColorBytes);

    size_t Vertex = 0U;
    for (size_t StarIndex = 0U; StarIndex < count; StarIndex++)
    {
        const NightSkyStar* Star = &stars[StarIndex];
        for (size_t Corner = 0U; Corner < STAR_VERTICES_PER_STAR; Corner++)
        {
            StarMesh.vertices[Vertex * 3U + 0U] = Star->Direction.x;
            StarMesh.vertices[Vertex * 3U + 1U] = Star->Direction.y;
            StarMesh.vertices[Vertex * 3U + 2U] = Star->Direction.z;
            StarMesh.texcoords[Vertex * 2U + 0U] = StarCorners[Corner][0];
            StarMesh.texcoords[Vertex * 2U + 1U] = StarCorners[Corner][1];
            StarMesh.texcoords2[Vertex * 2U + 0U] = Star->Magnitude;
            StarMesh.texcoords2[Vertex * 2U + 1U] = Star->TwinkleSeed;
            StarMesh.colors[Vertex * 4U + 0U] = Star->Color.r;
            StarMesh.colors[Vertex * 4U + 1U] = Star->Color.g;
            StarMesh.colors[Vertex * 4U + 2U] = Star->Color.b;
            StarMesh.colors[Vertex * 4U + 3U] = Star->Color.a;
            Vertex++;
        }
    }

    UploadMesh(&StarMesh, false);
    FreeStarMeshArrays(&StarMesh);
    if (StarMesh.vaoId == 0U)
    {
        UnloadMesh(StarMesh);
        return false;
    }
    *outMesh = StarMesh;
    return true;
}

static void ReportBuildFailure(WorldRenderer* self, const Error* error)
{
    if (self->_stars._isBuildFailureReported || (self->_logger == NULL))
    {
        return;
    }
    self->_stars._isBuildFailureReported = true;
    Error LogResult = Logger_LogWarningFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: the star catalogue could not be built, so no stars are drawn (%s).",
        (error->Message != NULL) ? (const char*)error->Message : "no details");
    Error_Deconstruct(&LogResult);
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererStars_LoadShader(WorldRenderer* self)
{
    self->_stars._shader = WorldRendererCommon_LoadShader(self, STARS_SHADER_ASSET_NAME);
    if (self->_stars._shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(self->_stars._shader);
    self->_stars._locStarsToWorld = GetShaderLocation(RayShader, "starsToWorld");
    self->_stars._locViewProjection = GetShaderLocation(RayShader, "viewProjection");
    self->_stars._locResolution = GetShaderLocation(RayShader, "resolution");
    self->_stars._locLimitingMagnitude = GetShaderLocation(RayShader, "limitingMagnitude");
    self->_stars._locRadianceScale = GetShaderLocation(RayShader, "radianceScale");
    self->_stars._locTwinkleStrength = GetShaderLocation(RayShader, "twinkleStrength");
    self->_stars._locTimeSeconds = GetShaderLocation(RayShader, "timeSeconds");
    self->_stars._locMoonDirection = GetShaderLocation(RayShader, "moonDirection");
    self->_stars._locMoonCosRadius = GetShaderLocation(RayShader, "moonCosRadius");
    self->_stars._locDepthReversed = GetShaderLocation(RayShader, "depthReversed");
    WorldRendererAtmosphere_PrepareShader(RayShader, &self->_stars._atmosphere);
    WorldRendererAtmosphere_PrepareHazeShader(RayShader, false, &self->_stars._haze);
}

void WorldRendererStars_Unload(WorldRenderer* self)
{
    if (self->_stars._hasMesh)
    {
        UnloadMesh(self->_stars._mesh);
        self->_stars._hasMesh = false;
    }
    self->_stars._starCount = 0U;
}

void WorldRendererStars_SyncCatalogue(WorldRenderer* self, const WorldEnvironment* environment)
{
    if (self->_stars._shader == NULL)
    {
        return;
    }
    size_t Count = NightSky_GetStarCount(environment->StarDensity);
    if (self->_stars._isBuilt && (self->_stars._builtSeed == environment->StarSeed) && (self->_stars._builtCount == Count))
    {
        return;
    }

    WorldRendererStars_Unload(self);
    self->_stars._isBuilt = true;
    self->_stars._builtSeed = environment->StarSeed;
    self->_stars._builtCount = Count;
    if (Count == 0U)
    {
        return;
    }

    GenericBuffer Stars;
    GenericBuffer_AllocateVariable(&Stars, Count, sizeof(NightSkyStar));
    Error Result = NightSky_GenerateStars(environment->StarSeed, environment->StarDensity, &Stars);
    if (Result.Code == ErrorCode_Success)
    {
        const void* StarData = Stars._data;
        if (BuildStarMesh(StarData, Stars._count, &self->_stars._mesh))
        {
            self->_stars._hasMesh = true;
            self->_stars._starCount = Stars._count;
        }
        else
        {
            Result = Error_Construct2(ErrorCode_InvalidState, "the GPU did not take the star mesh");
        }
    }
    if (Result.Code != ErrorCode_Success)
    {
        ReportBuildFailure(self, &Result);
    }
    Error_Deconstruct(&Result);
    Memory_Free(Stars._data);
}

void WorldRendererStars_Draw(WorldRenderer* self, const WorldRenderFrame* frame)
{
    Shader RayShader = GameShader_GetRaylibShader(self->_stars._shader);
    const WorldEnvironment* Environment = frame->Environment;

    SetShaderValueMatrix(RayShader, self->_stars._locStarsToWorld, frame->StarsToWorld);
    SetShaderValueMatrix(RayShader, self->_stars._locViewProjection, frame->ViewProjection);
    float Resolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    SetShaderValue(RayShader, self->_stars._locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(RayShader, self->_stars._locLimitingMagnitude, &frame->LimitingMagnitude, SHADER_UNIFORM_FLOAT);
    // The weather's clouds hide their share of the stars.
    float RadianceScale = STAR_RADIANCE_SCALE * fmaxf(Environment->StarBrightness, 0.0f) * frame->Weather.DirectShare;
    SetShaderValue(RayShader, self->_stars._locRadianceScale, &RadianceScale, SHADER_UNIFORM_FLOAT);
    float TwinkleStrength = ClampUnit(Environment->StarTwinkle);
    SetShaderValue(RayShader, self->_stars._locTwinkleStrength, &TwinkleStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, self->_stars._locTimeSeconds, &frame->TimeSeconds, SHADER_UNIFORM_FLOAT);
    // The moon's disc hides the stars behind it.
    float MoonDir[3] = { frame->MoonDirection.x, frame->MoonDirection.y, frame->MoonDirection.z };
    SetShaderValue(RayShader, self->_stars._locMoonDirection, MoonDir, SHADER_UNIFORM_VEC3);
    float MoonCosRadius = frame->IsMoonActive ? frame->MoonDisc.CosRadius : NO_MOON_COS_RADIUS;
    SetShaderValue(RayShader, self->_stars._locMoonCosRadius, &MoonCosRadius, SHADER_UNIFORM_FLOAT);
    WorldRendererCommon_SetDepthConvention(RayShader, self->_stars._locDepthReversed, frame);
    // The air dims and reddens the stars near the horizon through the transmittance table, or the air mass; the
    // ground haze dims them too.
    WorldRendererAtmosphere_UploadFrame(RayShader, &self->_stars._atmosphere, frame);
    WorldRendererAtmosphere_UploadHaze(RayShader, &self->_stars._haze, frame);
    WorldRendererAtmosphere_BindTables(self, frame);

    // Added onto the sky with (ONE, ONE) blending, so the fragment's alpha of 0 leaves the scene's alpha alone.
    // Depth-tested but never written: the depth buffer must stay clear, which the bloom prefilter and the sun shafts
    // read as sky. Culling off: a star's quad faces the camera by construction, whatever the winding.
    rlDrawRenderBatchActive();
    BeginBlendMode(BLEND_ADD_COLORS);
    rlEnableDepthTest();
    rlDisableDepthMask();
    rlDisableBackfaceCulling();

    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Zero(Maps, sizeof(Maps));
    Material StarMaterial = { .shader = RayShader, .maps = Maps };
    DrawMesh(self->_stars._mesh, StarMaterial, MatrixIdentity());

    rlEnableBackfaceCulling();
    rlEnableDepthMask();
    rlDisableDepthTest();
    EndBlendMode();
}
