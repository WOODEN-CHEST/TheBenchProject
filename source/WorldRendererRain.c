// The rain (the rain's model is WorldRain.h's): the rain map (what keeps the rain out around the camera: a depth map
// along the rain's mean fall, one layer of a shadow array through GLShadowArray.h), the rain's uniforms in every lit
// program (the map, the puddles and their mask, the sky mirrored in water, a lightning bolt's light, the debug view), and
// its draws in the scene pass: the lightning's bolt, the splashes and the streaks. Everything is in render space: the
// map's box sits around the camera with its centre snapped to its texels (WorldRenderSpace_StepCascade), the streaks
// and the splashes take anchors wrapped in double.

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "SpriteAnimation.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"
#include "wr/WRMemory.h"


// Macros.
/** Asset names of the streaks', the splashes' and the bolt's programs. */
#define STREAKS_SHADER_ASSET_NAME ((const unsigned char*)u8"world_rain_streaks")
#define SPLASHES_SHADER_ASSET_NAME ((const unsigned char*)u8"world_rain_splashes")
#define BOLT_SHADER_ASSET_NAME ((const unsigned char*)u8"world_lightning_bolt")
/** A quad's two triangles, unindexed (raylib's DrawMesh draws indexed only while the CPU index array is kept). */
#define QUAD_VERTEX_COUNT 6U
/** The salt of the streaks' places, classes and brightnesses: the same rain every time. */
#define STREAK_SALT 0x52A1FU
/** The dimmest streak's brightness, as a share of the brightest's. */
#define STREAK_DIMMEST 0.55f
/** The sphere the streaks' and the splashes' point lights are picked for: the streaks' box's half diagonal. */
#define RAIN_LIGHT_RADIUS (0.8660254f * (float)WORLD_RAIN_BOX_SIZE)
/** The bolt's colour (linear): a blue-white. */
#define BOLT_RED 0.82f
#define BOLT_GREEN 0.88f
#define BOLT_BLUE 1.0f
/** The overcast sky's radiance at the zenith per unit of its irradiance on a level surface (9 / (7 pi)), for a flash's
 *  even glow in the sky water mirrors. */
#define OVERCAST_ZENITH_PER_IRRADIANCE 0.40926163f
/** Room for a warning. */
#define WARNING_CAPACITY 512


// Fields.
/* A quad's corners as (side, along) or (x, y), counter-clockwise: two triangles. */
static const float QuadCorners[QUAD_VERTEX_COUNT][2] =
{
    { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f },
    { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
};


// Static functions.
/* A hash's top 24 bits as a number in [0, 1). */
static float UnitHash(uint32_t a, uint32_t b)
{
    return (float)(WorldRain_Hash(a, b, STREAK_SALT) >> 8) / 16777216.0f;
}

/* Frees a built mesh's CPU arrays (the GPU keeps its copy); the pointers are cleared, so UnloadMesh later frees only
 * what raylib allocated itself and the GPU objects. */
static void FreeMeshArrays(Mesh* mesh)
{
    Memory_Free(mesh->vertices);
    Memory_Free(mesh->texcoords);
    Memory_Free(mesh->normals);
    mesh->vertices = NULL;
    mesh->texcoords = NULL;
    mesh->normals = NULL;
}

/* Uploads a built mesh and frees its CPU arrays; false (nothing left allocated) when the GPU did not take it. */
static bool UploadBuiltMesh(Mesh* mesh)
{
    UploadMesh(mesh, false);
    FreeMeshArrays(mesh);
    if (mesh->vaoId == 0U)
    {
        UnloadMesh(*mesh);
        *mesh = (Mesh){ 0 };
        return false;
    }
    return true;
}

/* The streaks' mesh: every streak's quad, its vertices holding its seed (a place in the unit box), its corner (side,
 * along) and, in the normal, its speed class, its rank (ranks spread evenly, so a share of them is a share of the streaks
 * at random places) and its brightness. */
static bool BuildStreakMesh(Mesh* outMesh)
{
    size_t VertexCount = (size_t)WORLD_RAIN_STREAK_COUNT * QUAD_VERTEX_COUNT;
    Mesh Streaks = { 0 };
    Streaks.vertexCount = (int)VertexCount;
    Streaks.triangleCount = (int)(VertexCount / 3U);
    Streaks.vertices = Memory_Allocate(VertexCount * 3U * sizeof(float));
    Streaks.texcoords = Memory_Allocate(VertexCount * 2U * sizeof(float));
    Streaks.normals = Memory_Allocate(VertexCount * 3U * sizeof(float));
    for (uint32_t Streak = 0U; Streak < WORLD_RAIN_STREAK_COUNT; Streak++)
    {
        float Seed[3] = { UnitHash(Streak, 0U), UnitHash(Streak, 1U), UnitHash(Streak, 2U) };
        float SpeedClass = (float)(WorldRain_Hash(Streak, 3U, STREAK_SALT) % WORLD_RAIN_SPEED_CLASS_COUNT);
        float Rank = ((float)Streak + 0.5f) / (float)WORLD_RAIN_STREAK_COUNT;
        float Brightness = STREAK_DIMMEST + (1.0f - STREAK_DIMMEST) * UnitHash(Streak, 4U);
        for (size_t Corner = 0U; Corner < QUAD_VERTEX_COUNT; Corner++)
        {
            size_t Vertex = (size_t)Streak * QUAD_VERTEX_COUNT + Corner;
            Streaks.vertices[Vertex * 3U + 0U] = Seed[0];
            Streaks.vertices[Vertex * 3U + 1U] = Seed[1];
            Streaks.vertices[Vertex * 3U + 2U] = Seed[2];
            Streaks.texcoords[Vertex * 2U + 0U] = QuadCorners[Corner][0];
            Streaks.texcoords[Vertex * 2U + 1U] = QuadCorners[Corner][1];
            Streaks.normals[Vertex * 3U + 0U] = SpeedClass;
            Streaks.normals[Vertex * 3U + 1U] = Rank;
            Streaks.normals[Vertex * 3U + 2U] = Brightness;
        }
    }
    *outMesh = Streaks;
    return UploadBuiltMesh(outMesh);
}

/* The splashes' mesh: a quad per cell of the grid, its vertices holding the cell (x and z) and the quad's corner. */
static bool BuildSplashMesh(Mesh* outMesh)
{
    size_t CellCount = (size_t)WORLD_RAIN_SPLASH_GRID * (size_t)WORLD_RAIN_SPLASH_GRID;
    size_t VertexCount = CellCount * QUAD_VERTEX_COUNT;
    Mesh Splashes = { 0 };
    Splashes.vertexCount = (int)VertexCount;
    Splashes.triangleCount = (int)(VertexCount / 3U);
    Splashes.vertices = Memory_Allocate(VertexCount * 3U * sizeof(float));
    Splashes.texcoords = Memory_Allocate(VertexCount * 2U * sizeof(float));
    for (size_t Cell = 0U; Cell < CellCount; Cell++)
    {
        float CellX = (float)(Cell % (size_t)WORLD_RAIN_SPLASH_GRID);
        float CellZ = (float)(Cell / (size_t)WORLD_RAIN_SPLASH_GRID);
        for (size_t Corner = 0U; Corner < QUAD_VERTEX_COUNT; Corner++)
        {
            size_t Vertex = Cell * QUAD_VERTEX_COUNT + Corner;
            Splashes.vertices[Vertex * 3U + 0U] = CellX;
            Splashes.vertices[Vertex * 3U + 1U] = 0.0f;
            Splashes.vertices[Vertex * 3U + 2U] = CellZ;
            Splashes.texcoords[Vertex * 2U + 0U] = QuadCorners[Corner][0];
            Splashes.texcoords[Vertex * 2U + 1U] = QuadCorners[Corner][1];
        }
    }
    *outMesh = Splashes;
    return UploadBuiltMesh(outMesh);
}

/* Loads one of the rain's programs and caches its locations (a lit program, and its own uniforms). */
static void LoadProgram(WorldRenderer* self, WorldRendererRainProgram* program, const unsigned char* assetName)
{
    program->_shader = WorldRendererCommon_LoadShader(self, assetName);
    if (program->_shader == NULL)
    {
        return;
    }
    Shader RayShader = GameShader_GetRaylibShader(program->_shader);
    WorldRendererLighting_PrepareLitShader(self, RayShader, assetName, &program->_lit);
    program->_locViewProjection = GetShaderLocation(RayShader, "rainViewProjection");
    program->_locResolution = GetShaderLocation(RayShader, "rainResolution");
    program->_locColor = GetShaderLocation(RayShader, "rainColor");
    program->_locAnchor = GetShaderLocation(RayShader, "rainAnchor");
    program->_locFall = GetShaderLocation(RayShader, "rainFall");
    program->_locWind = GetShaderLocation(RayShader, "rainWind");
    program->_locFallSpeed = GetShaderLocation(RayShader, "rainFallSpeed");
    program->_locExposure = GetShaderLocation(RayShader, "rainExposure");
    program->_locStreakShare = GetShaderLocation(RayShader, "rainStreakShare");
    program->_locSplashCell = GetShaderLocation(RayShader, "rainSplashCell");
    program->_locSplashCorner = GetShaderLocation(RayShader, "rainSplashCorner");
    program->_locSplashCycles = GetShaderLocation(RayShader, "rainSplashCycles");
    program->_locSplashShare = GetShaderLocation(RayShader, "rainSplashShare");
    program->_locSplashPeriod = GetShaderLocation(RayShader, "rainSplashPeriod");
    program->_locSlant = GetShaderLocation(RayShader, "rainSlant");
    program->_locSplashLife = GetShaderLocation(RayShader, "rainSplashLife");
    program->_locFrame = GetShaderLocation(RayShader, "rainFrame");
    program->_locFrameCount = GetShaderLocation(RayShader, "rainFrameCount");
    program->_locFrameArea = GetShaderLocation(RayShader, "rainFrameArea");
    program->_locFrameAspect = GetShaderLocation(RayShader, "rainFrameAspect");
    program->_locCameraRight = GetShaderLocation(RayShader, "rainCameraRight");
    program->_locCameraUp = GetShaderLocation(RayShader, "rainCameraUp");
    program->_locBoltFoot = GetShaderLocation(RayShader, "boltFoot");
    program->_locBoltTop = GetShaderLocation(RayShader, "boltTop");
    program->_locBoltWidth = GetShaderLocation(RayShader, "boltWidth");
    program->_locBoltArea = GetShaderLocation(RayShader, "boltArea");
    program->_locBoltRadiance = GetShaderLocation(RayShader, "boltRadiance");
}

/* Whether the rain map can be drawn at all: the shadow levels' depth program and the map's GL functions. */
static bool IsMapAvailable(const WorldRenderer* self)
{
    return (self->_shadowLevels._depthShader != NULL) && self->_rain._api._isAvailable;
}

/* Makes the rain map when there is none (a failure is logged once and not retried). Returns whether there is one. */
static bool EnsureMap(WorldRenderer* self)
{
    WorldRendererRainState* State = &self->_rain;
    if (State->_hasMap)
    {
        return true;
    }
    if (State->_isMapFailureReported)
    {
        return false;
    }
    Error Result = GLShadowArray_Create(&State->_api, WORLD_RAIN_MAP_TEXELS, 1, &State->_map);
    if (Result.Code != ErrorCode_Success)
    {
        char Message[WARNING_CAPACITY];
        snprintf(Message, sizeof(Message), "WorldRenderer: could not make the rain map (%d texels); the rain falls "
            "everywhere and every surface is wet alike (%s).", WORLD_RAIN_MAP_TEXELS,
            (Result.Message != NULL) ? (const char*)Result.Message : "no reason given");
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
        Error_Deconstruct(&Result);
        State->_isMapFailureReported = true;
        return false;
    }
    State->_hasMap = true;
    Memory_Zero(&State->_mapKeep, sizeof(State->_mapKeep)); // a new map holds nothing yet
    return true;
}

/* Draws what keeps the rain out among the models (the rain map's visibility list): each at the LOD the shadows draw solid,
 * dithered by its fade, through the depth program (skinned meshes posed through its twin). */
static void DrawModelOccluders(WorldRenderer* self, const WorldRendererMeshShaders* shaders)
{
    WorldRendererShadowLevelState* Shadows = &self->_shadowLevels;
    size_t ModelCount = 0U;
    const WorldRenderModel* Models = WorldRenderCache_GetModels(&self->_cache, &ModelCount);
    size_t OccluderCount = 0U;
    const size_t* Occluders = WorldVisibility_GetModels(&self->_visibility, &self->_cache, WorldVisibilityPass_Rain,
        &OccluderCount);
    WorldRendererDitherRun Run = { 0 };
    WorldRendererDitherRun SkinnedRun = { 0 };
    for (size_t Position = 0U; Position < OccluderCount; Position++)
    {
        size_t Index = Occluders[Position];
        const WorldRenderModel* Entry = &Models[Index];
        float Fade = WorldVisibility_GetModelFade(&self->_visibility, Index).Combined;
        WorldRendererCommon_SetRunDither(*shaders->Rigid, &Shadows->_dither, &Run, Fade);
        if ((shaders->Skinned != NULL) && (Entry->Pose != NULL))
        {
            WorldRendererCommon_SetRunDither(*shaders->Skinned, &Shadows->_skinnedDither, &SkinnedRun, Fade);
        }
        WorldRendererGeometry_DrawModelDepth(Entry, WorldLodState_GetShadowLod(&Entry->Lod), shaders);
    }
    WorldRendererCommon_EndDepthMeshes();
}

/* Uploads what every rain draw takes from the frame: its lighting (a lit program's), the point lights around the camera,
 * and its place (the air's own haze, no ambient zone of its own, not in the ambient pre-pass). */
static void PrepareProgram(WorldRenderer* self, const WorldRenderFrame* frame, const WorldRendererRainProgram* program,
    Shader shader)
{
    WorldRendererLighting_UploadLitProgram(self, frame, shader, &program->_lit, true, false);
    Vector3 CameraPlace = { 0.0f, 0.0f, 0.0f };
    WorldRendererLighting_UploadObjectLights(self, shader, &program->_lit, CameraPlace, RAIN_LIGHT_RADIUS);
    WorldRendererLighting_SetDrawPlace(self, shader, &program->_lit, CameraPlace, RAIN_LIGHT_RADIUS, 1.0f, false,
        false);
    SetShaderValueMatrix(shader, program->_locViewProjection, frame->ViewProjection);
}

/* Draws a mesh of the rain with a material built on the stack: @p texture on its albedo map (texture0). */
static void DrawRainMesh(Mesh mesh, Shader shader, Texture2D texture)
{
    MaterialMap Maps[WORLD_RENDERER_MATERIAL_MAP_COUNT];
    Memory_Zero(Maps, sizeof(Maps));
    Maps[MATERIAL_MAP_ALBEDO].texture = texture;
    Maps[MATERIAL_MAP_ALBEDO].color = WHITE;
    Material RainMaterial = { .shader = shader, .maps = Maps };
    DrawMesh(mesh, RainMaterial, MatrixIdentity());
}

/* A sprite animation frame's area in its texture as texture coordinates (x and y from the texture's top left, then the
 * width and the height) and its width over its height; false for a frame without a texture or an area. */
static bool GetFrameArea(const SpriteAnimationFrame* frame, float outArea[4], float* outAspect)
{
    Rectangle Area = frame->_areaInTexture;
    if ((frame->_texture.id == 0U) || (frame->_texture.width <= 0) || (frame->_texture.height <= 0)
        || !(Area.width > 0.0f) || !(Area.height > 0.0f))
    {
        return false;
    }
    float Width = (float)frame->_texture.width;
    float Height = (float)frame->_texture.height;
    outArea[0] = Area.x / Width;
    outArea[1] = Area.y / Height;
    outArea[2] = Area.width / Width;
    outArea[3] = Area.height / Height;
    *outAspect = Area.width / Area.height;
    return true;
}

/* The frames of the splash animation a splash plays (at most WORLD_RAIN_SPLASH_MAX_FRAMES), and its life: 0 without a
 * usable animation. */
static uint32_t GetSplashFrames(const WorldRendererRainState* state, float* outLife)
{
    SpriteAnimation* Animation = state->_splashAnimation._animation;
    size_t Count = (Animation != NULL) ? SpriteAnimation_GetFrameCount(Animation) : 0U;
    *outLife = (Animation != NULL) ? WorldRain_GetSplashLife(Count, SpriteAnimation_GetDefaultFPS(Animation)) : 0.0f;
    return (*outLife > 0.0f) ? (uint32_t)((Count < WORLD_RAIN_SPLASH_MAX_FRAMES) ? Count : WORLD_RAIN_SPLASH_MAX_FRAMES)
        : 0U;
}

/* Checks a newly loaded splash animation once: without an fps it plays nothing, beyond WORLD_RAIN_SPLASH_MAX_FRAMES frames
 * a splash plays only the first ones (logged). */
static void CheckSplashAnimation(WorldRenderer* self)
{
    WorldRendererRainState* State = &self->_rain;
    SpriteAnimation* Animation = State->_splashAnimation._animation;
    if ((Animation == NULL) || (Animation == State->_checkedSplashAnimation))
    {
        return;
    }
    State->_checkedSplashAnimation = Animation;
    const char* Name = (const char*)State->_splashAnimation._name._data;
    char Message[WARNING_CAPACITY];
    if (!(SpriteAnimation_GetDefaultFPS(Animation) > 0.0) || (SpriteAnimation_GetFrameCount(Animation) == 0U))
    {
        snprintf(Message, sizeof(Message), "WorldRenderer: the rain's splash animation \"%s\" has no frames or no fps, so "
            "no rain splashes are drawn (a splash plays its animation once at its fps).", (Name != NULL) ? Name : "");
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
    }
    else if (SpriteAnimation_GetFrameCount(Animation) > WORLD_RAIN_SPLASH_MAX_FRAMES)
    {
        snprintf(Message, sizeof(Message), "WorldRenderer: the rain's splash animation \"%s\" has %zu frames; a splash "
            "plays its first %u.", (Name != NULL) ? Name : "", SpriteAnimation_GetFrameCount(Animation),
            (unsigned)WORLD_RAIN_SPLASH_MAX_FRAMES);
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
    }
}

/* The rain's colour times its brightness (linear), for the streaks and the splashes. */
static void UploadColor(const WorldRenderFrame* frame, Shader shader, int location)
{
    float RainColor[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        RainColor[Channel] = frame->Rain.Color[Channel] * frame->Rain.StreakBrightness;
    }
    SetShaderValue(shader, location, RainColor, SHADER_UNIFORM_VEC3);
}

/* The streaks: their anchor and fall for the frame's origin, the wind, the exposure, the share drawn. */
static void DrawStreaks(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererRainState* State = &self->_rain;
    const WorldRendererRainProgram* Program = &State->_streaks;
    Shader RayShader = GameShader_GetRaylibShader(Program->_shader);
    PrepareProgram(self, frame, Program, RayShader);
    float Resolution[2] = { (float)frame->SceneWidth, (float)frame->SceneHeight };
    float Anchor[3];
    float Fall = 0.0f;
    WorldRain_GetStreakAnchor(&State->_drift, frame->Origin, Anchor, &Fall);
    float Wind[3] = { frame->Rain.Wind.x, frame->Rain.Wind.y, frame->Rain.Wind.z };
    SetShaderValue(RayShader, Program->_locResolution, Resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(RayShader, Program->_locAnchor, Anchor, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, Program->_locFall, &Fall, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locWind, Wind, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, Program->_locFallSpeed, &frame->Rain.FallSpeed, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locExposure, &frame->Rain.StreakExposure, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locStreakShare, &frame->Rain.StreakShare, SHADER_UNIFORM_FLOAT);
    UploadColor(frame, RayShader, Program->_locColor);
    DrawRainMesh(State->_streakMesh, RayShader, GameTexture_GetRaylibTexture(State->_streakTexture._texture));
    State->_wereStreaksDrawn = true;
}

/* The splashes: the grid for the frame's origin, the cycles run, the share and the cycle's length, the slant of the rain
 * map they land by, the camera's axes (the quads face it: the view's rows); then one draw per frame of the animation, with
 * that frame's texture and area (each draw shows the splashes at that frame). */
static void DrawSplashes(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererRainState* State = &self->_rain;
    const WorldRendererRainProgram* Program = &State->_splashes;
    Shader RayShader = GameShader_GetRaylibShader(Program->_shader);
    PrepareProgram(self, frame, Program, RayShader);
    float Cell[2];
    float Corner[2];
    WorldRain_GetSplashGrid(frame->Origin, Cell, Corner);
    float Right[3] = { frame->View.m0, frame->View.m4, frame->View.m8 };
    float Up[3] = { frame->View.m1, frame->View.m5, frame->View.m9 };
    SetShaderValue(RayShader, Program->_locSplashCell, Cell, SHADER_UNIFORM_VEC2);
    SetShaderValue(RayShader, Program->_locSplashCorner, Corner, SHADER_UNIFORM_VEC2);
    float Cycles = (float)State->_drift.SplashCycles;
    Vector2 Slant = WorldRain_GetSlant(frame->RainMapTowardRain);
    float SlantValue[2] = { Slant.x, Slant.y };
    SetShaderValue(RayShader, Program->_locSplashCycles, &Cycles, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locSplashShare, &frame->Rain.SplashShare, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locSplashPeriod, &frame->Rain.SplashPeriod, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locSlant, SlantValue, SHADER_UNIFORM_VEC2);
    SetShaderValue(RayShader, Program->_locCameraRight, Right, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, Program->_locCameraUp, Up, SHADER_UNIFORM_VEC3);
    UploadColor(frame, RayShader, Program->_locColor);
    float Life = 0.0f;
    uint32_t FrameCount = GetSplashFrames(State, &Life);
    int Count = (int)FrameCount;
    SetShaderValue(RayShader, Program->_locSplashLife, &Life, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locFrameCount, &Count, SHADER_UNIFORM_INT);
    for (uint32_t FrameIndex = 0U; FrameIndex < FrameCount; FrameIndex++)
    {
        SpriteAnimationFrame Frame;
        float Area[4];
        float Aspect = 1.0f;
        Error FrameResult = SpriteAnimation_GetFrameAt(State->_splashAnimation._animation, FrameIndex, &Frame);
        bool HasFrame = (FrameResult.Code == ErrorCode_Success) && GetFrameArea(&Frame, Area, &Aspect);
        Error_Deconstruct(&FrameResult);
        if (!HasFrame)
        {
            continue; // a frame without a texture shows nothing
        }
        int Index = (int)FrameIndex;
        SetShaderValue(RayShader, Program->_locFrame, &Index, SHADER_UNIFORM_INT);
        SetShaderValue(RayShader, Program->_locFrameArea, Area, SHADER_UNIFORM_VEC4);
        SetShaderValue(RayShader, Program->_locFrameAspect, &Aspect, SHADER_UNIFORM_FLOAT);
        DrawRainMesh(State->_splashMesh, RayShader, Frame._texture);
    }
    State->_wereSplashesDrawn = true;
}

/* The lightning's bolt: its channel, its frame of the bolt animation (its area, mirrored or not; as wide as the frame's
 * shape makes it), its radiance against the eye's exposure. */
static void DrawBolt(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererRainState* State = &self->_rain;
    const WorldRainBolt* Bolt = &frame->Bolt;
    SpriteAnimationFrame Frame;
    float Area[4];
    float Aspect = 1.0f;
    Error FrameResult = SpriteAnimation_GetFrameAt(State->_boltAnimation._animation, Bolt->Variant, &Frame);
    bool HasFrame = (FrameResult.Code == ErrorCode_Success) && GetFrameArea(&Frame, Area, &Aspect);
    Error_Deconstruct(&FrameResult);
    if (!HasFrame)
    {
        return; // a frame without a texture shows nothing; the bolt still lights the world
    }
    const WorldRendererRainProgram* Program = &State->_bolt;
    Shader RayShader = GameShader_GetRaylibShader(Program->_shader);
    PrepareProgram(self, frame, Program, RayShader);
    float Foot[3] = { Bolt->Foot.x, Bolt->Foot.y, Bolt->Foot.z };
    float Top[3] = { Bolt->Top.x, Bolt->Top.y, Bolt->Top.z };
    float Width = (Bolt->Top.y - Bolt->Foot.y) * Aspect;
    if (Bolt->IsMirrored)
    {
        Area[0] += Area[2];
        Area[2] = -Area[2];
    }
    float Exposure = (frame->LastExposure > 0.0f) ? frame->LastExposure : 1.0f;
    float Radiance = fminf(Bolt->Radiance * WORLD_RAIN_BOLT_SCREEN_EXPOSURE / Exposure, WORLD_SKY_LIGHT_MAX_LIGHT);
    float BoltRadiance[3] = { BOLT_RED * Radiance, BOLT_GREEN * Radiance, BOLT_BLUE * Radiance };
    SetShaderValue(RayShader, Program->_locBoltFoot, Foot, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, Program->_locBoltTop, Top, SHADER_UNIFORM_VEC3);
    SetShaderValue(RayShader, Program->_locBoltWidth, &Width, SHADER_UNIFORM_FLOAT);
    SetShaderValue(RayShader, Program->_locBoltArea, Area, SHADER_UNIFORM_VEC4);
    SetShaderValue(RayShader, Program->_locBoltRadiance, BoltRadiance, SHADER_UNIFORM_VEC3);
    DrawRainMesh(self->_sprites._quad, RayShader, Frame._texture);
    State->_wasBoltDrawn = true;
}

/* The bolt animation's frames (each one bolt); 0 without an animation. */
static uint32_t GetBoltFrames(const WorldRendererRainState* state)
{
    SpriteAnimation* Animation = state->_boltAnimation._animation;
    size_t Count = (Animation != NULL) ? SpriteAnimation_GetFrameCount(Animation) : 0U;
    return (Count < UINT32_MAX) ? (uint32_t)Count : UINT32_MAX;
}

/* Whether the frame's bolt can be drawn (its program, an animation with frames and the sprites' quad), not only light
 * the world. */
static bool IsBoltDrawable(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    return frame->IsBoltActive && (self->_rain._bolt._shader != NULL) && (GetBoltFrames(&self->_rain) > 0U)
        && self->_sprites._hasQuad;
}

/* Syncs the assets the world names, each only while a program would draw with it. */
static void SyncTextures(WorldRenderer* self, const WorldEnvironment* environment)
{
    WorldRendererRainState* State = &self->_rain;
    if (State->_streaks._shader != NULL)
    {
        WorldRendererCommon_SyncTextureSlot(self, &State->_streakTexture, environment->RainStreakTexture,
            (const unsigned char*)u8"no rain streaks are drawn");
    }
    if (State->_splashes._shader != NULL)
    {
        WorldRendererCommon_SyncAnimationSlot(self, &State->_splashAnimation, environment->RainSplashAnimation,
            (const unsigned char*)u8"no rain splashes are drawn");
        CheckSplashAnimation(self);
    }
    if (State->_bolt._shader != NULL)
    {
        WorldRendererCommon_SyncAnimationSlot(self, &State->_boltAnimation, environment->LightningBoltAnimation,
            (const unsigned char*)u8"no lightning bolts are drawn (their light stays)");
    }
    WorldRendererCommon_SyncTextureSlot(self, &State->_puddleMask, environment->PuddleMaskTexture,
        (const unsigned char*)u8"no puddles form");
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererRain_Construct(WorldRenderer* self)
{
    WorldRendererRainState* State = &self->_rain;
    Memory_Zero(State, sizeof(*State));
    WorldRendererCommon_ConstructAssetSlot(&State->_streakTexture);
    WorldRendererCommon_ConstructAssetSlot(&State->_splashAnimation);
    WorldRendererCommon_ConstructAssetSlot(&State->_puddleMask);
    WorldRendererCommon_ConstructAssetSlot(&State->_boltAnimation);
    State->_isEnabled = true;
    State->_view = WorldRendererRainView_Off;
}

void WorldRendererRain_Deconstruct(WorldRenderer* self)
{
    WorldRendererRainState* State = &self->_rain;
    WorldRendererCommon_DeconstructAssetSlot(self, &State->_streakTexture);
    WorldRendererCommon_DeconstructAssetSlot(self, &State->_splashAnimation);
    WorldRendererCommon_DeconstructAssetSlot(self, &State->_puddleMask);
    WorldRendererCommon_DeconstructAssetSlot(self, &State->_boltAnimation);
    State->_checkedSplashAnimation = NULL;
}

void WorldRendererRain_Load(WorldRenderer* self)
{
    WorldRendererRainState* State = &self->_rain;
    LoadProgram(self, &State->_streaks, STREAKS_SHADER_ASSET_NAME);
    LoadProgram(self, &State->_splashes, SPLASHES_SHADER_ASSET_NAME);
    LoadProgram(self, &State->_bolt, BOLT_SHADER_ASSET_NAME);
    if ((State->_streaks._shader != NULL) && !BuildStreakMesh(&State->_streakMesh))
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: could not upload the rain's streaks; no "
            u8"rain streaks are drawn.");
    }
    State->_hasStreakMesh = (State->_streakMesh.vaoId != 0U);
    if ((State->_splashes._shader != NULL) && !BuildSplashMesh(&State->_splashMesh))
    {
        WorldRendererCommon_Warn(self, (const unsigned char*)u8"WorldRenderer: could not upload the rain's splashes; no "
            u8"rain splashes are drawn.");
    }
    State->_hasSplashMesh = (State->_splashMesh.vaoId != 0U);
    if (!GLShadowArray_LoadApi(&State->_api))
    {
        char Message[WARNING_CAPACITY];
        snprintf(Message, sizeof(Message), "WorldRenderer: the GPU lacks %s; the rain falls everywhere and every surface "
            "is wet alike.", (State->_api._missing != NULL) ? State->_api._missing : "a function the rain map needs");
        WorldRendererCommon_Warn(self, (const unsigned char*)Message);
    }
}

void WorldRendererRain_Unload(WorldRenderer* self)
{
    WorldRendererRainState* State = &self->_rain;
    if (State->_hasMap)
    {
        GLShadowArray_Destroy(&State->_api, &State->_map);
        State->_hasMap = false;
    }
    if (State->_hasStreakMesh)
    {
        UnloadMesh(State->_streakMesh);
        State->_streakMesh = (Mesh){ 0 };
        State->_hasStreakMesh = false;
    }
    if (State->_hasSplashMesh)
    {
        UnloadMesh(State->_splashMesh);
        State->_splashMesh = (Mesh){ 0 };
        State->_hasSplashMesh = false;
    }
}

void WorldRendererRain_PrepareLitShader(Shader shader, WorldRendererRainLocations* outLocations)
{
    outLocations->_locMapActive = GetShaderLocation(shader, "rainMapActive");
    outLocations->_locMap = GetShaderLocation(shader, "rainMap");
    outLocations->_locMapMatrix = GetShaderLocation(shader, "rainMapMatrix");
    outLocations->_locMapOffsets = GetShaderLocation(shader, "rainMapOffsets");
    outLocations->_locPuddleActive = GetShaderLocation(shader, "rainPuddleActive");
    outLocations->_locPuddleMask = GetShaderLocation(shader, "puddleMask");
    outLocations->_locPuddleFrame = GetShaderLocation(shader, "rainPuddleFrame");
    outLocations->_locRippleFrame = GetShaderLocation(shader, "rainRippleFrame");
    outLocations->_locActive = GetShaderLocation(shader, "rainActive");
    outLocations->_locTop = GetShaderLocation(shader, "rainTop");
    outLocations->_locReflectionClearShare = GetShaderLocation(shader, "rainReflectionClearShare");
    outLocations->_locReflectionDeck = GetShaderLocation(shader, "rainReflectionDeck");
    outLocations->_locReflectionMoon = GetShaderLocation(shader, "rainReflectionMoon");
    outLocations->_locBoltLight = GetShaderLocation(shader, "boltLight");
    outLocations->_locBoltDirection = GetShaderLocation(shader, "boltDirection");
    outLocations->_locView = GetShaderLocation(shader, "rainView");
    // The samplers on their fixed units, once (sampler values persist in the program).
    int MapUnit = (int)WorldRendererTextureUnit_RainMap;
    SetShaderValue(shader, outLocations->_locMap, &MapUnit, SHADER_UNIFORM_INT);
    int MaskUnit = (int)WorldRendererTextureUnit_PuddleMask;
    SetShaderValue(shader, outLocations->_locPuddleMask, &MaskUnit, SHADER_UNIFORM_INT);
}

void WorldRendererRain_PrepareFrame(WorldRenderer* self, WorldRenderFrame* frame, double deltaSeconds)
{
    WorldRendererRainState* State = &self->_rain;
    WorldRain_GetFrame(frame->Environment, frame->Origin.Y, &frame->Rain);
    WorldRain_AdvanceDrift(&State->_drift, &frame->Rain, deltaSeconds);
    frame->IsRainActive = State->_isEnabled;
    if (!frame->IsRainActive)
    {
        Memory_Zero(&State->_mapKeep, sizeof(State->_mapKeep));
        return; // no streaks, splashes, bolt or map: every surface wet alike
    }
    SyncTextures(self, frame->Environment);
    WorldRain_GetBolt(frame->Environment, frame->Weather.FlashIrradiance, frame->Origin.Y, GetBoltFrames(State),
        &frame->Bolt);
    frame->IsBoltActive = frame->Bolt.IsVisible;
    frame->AreRainStreaksActive = frame->Rain.AreStreaksVisible && (State->_streaks._shader != NULL)
        && State->_hasStreakMesh && (State->_streakTexture._texture != NULL);

    // The rain map, while it rains or surfaces are wet: its box around the camera along the rain's mean fall, its centre
    // on its texels; what lies nearer the rain than the box is drawn onto its near plane, so its near plane culls nothing.
    // Drawn every few frames and read in between through the box it was drawn with (WorldRain.h, "THE RAIN MAP").
    frame->IsRainMapActive = frame->Rain.IsMapNeeded && IsMapAvailable(self) && EnsureMap(self);
    if (!frame->IsRainMapActive)
    {
        Memory_Zero(&State->_mapKeep, sizeof(State->_mapKeep));
    }
    else
    {
        WorldRenderCascadeShape Shape = WorldRain_GetMapShape();
        WorldRenderCascade Cascade = WorldRenderSpace_StepCascade(&State->_mapGrid, frame->Origin,
            frame->Rain.TowardRain, &Shape);
        frame->IsRainMapDrawn = WorldRain_IsMapDue(&State->_mapKeep, frame->Origin, frame->Rain.TowardRain);
        if (frame->IsRainMapDrawn)
        {
            WorldRain_KeepMap(&State->_mapKeep, Cascade.Anchor, frame->Rain.TowardRain);
            frame->RainMapTowardRain = frame->Rain.TowardRain;
            frame->RainMapBox = WorldVisibility_ExtractFrustum(Cascade.ViewProjection);
            frame->RainMapBox.Planes[WorldFrustumPlane_Near] = (WorldPlane){ .Normal = { 0.0f, 0.0f, 0.0f },
                .Distance = 0.0f };
        }
        else
        {
            Cascade = WorldRenderSpace_GetCascadeAt(frame->Origin, State->_mapKeep.Center, State->_mapKeep.TowardRain,
                &Shape);
            frame->RainMapTowardRain = State->_mapKeep.TowardRain;
        }
        frame->RainMapView = Cascade.View;
        frame->RainMapProjection = Cascade.Projection;
        frame->RainMapViewProjection = Cascade.ViewProjection;
    }
    // A splash lands where the map finds a surface (none without it) and plays the splash animation (none without one
    // that has frames and an fps).
    float SplashLife = 0.0f;
    frame->AreRainSplashesActive = frame->Rain.AreSplashesVisible && frame->IsRainMapActive
        && (State->_splashes._shader != NULL) && State->_hasSplashMesh && (GetSplashFrames(State, &SplashLife) > 0U);
}

void WorldRendererRain_RenderMap(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererRainState* State = &self->_rain;
    State->_wasMapRead = frame->IsRainMapActive;
    if (!frame->IsRainMapDrawn)
    {
        return;
    }
    WorldRendererShadowLevelState* Shadows = &self->_shadowLevels;
    Shader DepthShader = GameShader_GetRaylibShader(Shadows->_depthShader);
    bool HasSkinned = (Shadows->_skinnedDepthShader != NULL);
    Shader SkinnedShader = HasSkinned ? GameShader_GetRaylibShader(Shadows->_skinnedDepthShader) : DepthShader;
    WorldRendererMeshShaders Shaders = { .Rigid = &DepthShader, .Skinned = HasSkinned ? &SkinnedShader : NULL,
        .Palette = &Shadows->_skinnedPalette };

    GLShadowArray_AttachLayer(&State->_api, &State->_map, 0);
    RenderTexture2D Target = { 0 };
    Target.id = GLShadowArray_GetFramebuffer(&State->_map);
    Target.texture.width = State->_map._size;
    Target.texture.height = State->_map._size;
    BeginTextureMode(Target);
    ClearBackground(WHITE); // clears depth to the far plane (the framebuffer has no colour): open everywhere
    rlEnableDepthTest();
    rlSetMatrixProjection(frame->RainMapProjection);
    rlSetMatrixModelview(frame->RainMapView);
    GLShadowArray_BeginDepthClamp(&State->_api);
    // Both faces: the map holds the first surface along the rain, whichever way it faces (an open roof, the ground's one
    // side, the top of a closed thing).
    rlDisableBackfaceCulling();
    DrawModelOccluders(self, &Shaders);
    WorldRendererInstances_DrawCasters(self, frame, DepthShader, &Shadows->_dither, WorldVisibilityPass_Rain, NULL);
    WorldRendererTerrain_DrawCasters(self, DepthShader, &Shadows->_dither, WorldVisibilityPass_Rain);
    rlEnableBackfaceCulling();
    GLShadowArray_EndDepthClamp(&State->_api);
    rlDisableDepthTest();
    EndTextureMode();
}

void WorldRendererRain_UploadLitFrame(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererRainLocations* locations)
{
    const WorldRendererRainState* State = &self->_rain;
    int MapActive = frame->IsRainMapActive ? 1 : 0;
    SetShaderValue(shader, locations->_locMapActive, &MapActive, SHADER_UNIFORM_INT);
    if (frame->IsRainMapActive)
    {
        SetShaderValueMatrix(shader, locations->_locMapMatrix, frame->RainMapViewProjection);
        float Offsets[2];
        WorldRain_GetMapOffsets(&Offsets[0], &Offsets[1]);
        SetShaderValue(shader, locations->_locMapOffsets, Offsets, SHADER_UNIFORM_VEC2);
    }

    // The puddles: their mask's place (the origin wrapped where its two reads repeat together) and their coverage.
    bool IsPuddleActive = frame->IsRainActive && (frame->Rain.PuddleCoverage > 0.0f)
        && (State->_puddleMask._texture != NULL);
    int PuddleActive = IsPuddleActive ? 1 : 0;
    SetShaderValue(shader, locations->_locPuddleActive, &PuddleActive, SHADER_UNIFORM_INT);
    if (IsPuddleActive)
    {
        float Origin[2];
        WorldRain_GetPatternOrigin(frame->Origin, WORLD_RAIN_PATTERN_TILES * (double)frame->Rain.PuddleTile, Origin);
        float PuddleFrame[4] = { Origin[0], Origin[1], 1.0f / frame->Rain.PuddleTile, frame->Rain.PuddleCoverage };
        SetShaderValue(shader, locations->_locPuddleFrame, PuddleFrame, SHADER_UNIFORM_VEC4);
    }
    // The rain's rings on still water (the puddles', and every water's that has ripples) while it rains: none while the
    // rain is off.
    float RippleOrigin[2];
    WorldRain_GetPatternOrigin(frame->Origin, WORLD_RAIN_RIPPLE_PERIOD, RippleOrigin);
    float RippleFrame[4] = { RippleOrigin[0], RippleOrigin[1], (float)State->_drift.RippleCycles,
        frame->IsRainActive ? frame->Rain.RippleStrength : 0.0f };
    SetShaderValue(shader, locations->_locRippleFrame, RippleFrame, SHADER_UNIFORM_VEC4);

    // While the rain is on, nothing above its top is rained on, and water mirrors the clear sky the sky view holds (the
    // gaps' share of it while the clouds are drawn: the table then holds the sky without them) and the clouds' deck with a
    // flash's even glow, as the sky shows them (tinted). Switched off, neither: wet surfaces as without the rain.
    int Active = frame->IsRainActive ? 1 : 0;
    SetShaderValue(shader, locations->_locActive, &Active, SHADER_UNIFORM_INT);
    float Top = (float)(frame->Rain.TopY - frame->Origin.Y);
    SetShaderValue(shader, locations->_locTop, &Top, SHADER_UNIFORM_FLOAT);
    float ClearShare = frame->AreCloudsActive ? frame->Weather.DirectShare : 1.0f;
    SetShaderValue(shader, locations->_locReflectionClearShare, &ClearShare, SHADER_UNIFORM_FLOAT);
    float Tint[3];
    WorldRendererCommon_ColorToLinear(frame->Environment->SkyTint, Tint);
    float Deck[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        Deck[Channel] = (frame->Weather.ZenithRadiance[Channel]
            + frame->Weather.FlashIrradiance[Channel] * OVERCAST_ZENITH_PER_IRRADIANCE) * Tint[Channel];
    }
    SetShaderValue(shader, locations->_locReflectionDeck, Deck, SHADER_UNIFORM_VEC3);
    int Moon = (frame->MoonSkyIntensity > 0.0f) ? 1 : 0;
    SetShaderValue(shader, locations->_locReflectionMoon, &Moon, SHADER_UNIFORM_INT);

    // A bolt's channel lights every surface while it stands.
    float BoltLight[3] = { 0.0f, 0.0f, 0.0f };
    float BoltDirection[3] = { 0.0f, 1.0f, 0.0f };
    if (frame->IsBoltActive)
    {
        for (int Channel = 0; Channel < 3; Channel++)
        {
            BoltLight[Channel] = frame->Bolt.Irradiance[Channel];
        }
        BoltDirection[0] = frame->Bolt.Direction.x;
        BoltDirection[1] = frame->Bolt.Direction.y;
        BoltDirection[2] = frame->Bolt.Direction.z;
    }
    SetShaderValue(shader, locations->_locBoltLight, BoltLight, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locations->_locBoltDirection, BoltDirection, SHADER_UNIFORM_VEC3);
    int View = (int)State->_view;
    SetShaderValue(shader, locations->_locView, &View, SHADER_UNIFORM_INT);
}

void WorldRendererRain_BindFrameTextures(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    const WorldRendererRainState* State = &self->_rain;
    if (frame->IsRainMapActive)
    {
        GLShadowArray_Bind(&State->_api, &State->_map, (int)WorldRendererTextureUnit_RainMap);
    }
    if (State->_puddleMask._texture != NULL)
    {
        rlActiveTextureSlot(WorldRendererTextureUnit_PuddleMask);
        rlEnableTexture(GameTexture_GetRaylibTexture(State->_puddleMask._texture).id);
    }
    if (frame->AreAtmosphereTablesActive && self->_atmosphere._hasSkyView)
    {
        rlActiveTextureSlot(WorldRendererTextureUnit_RainSkyView);
        rlEnableTexture(self->_atmosphere._skyView.texture.id);
    }
    rlActiveTextureSlot(0);
}

void WorldRendererRain_DrawScene(WorldRenderer* self, const WorldRenderFrame* frame)
{
    WorldRendererRainState* State = &self->_rain;
    State->_wereStreaksDrawn = false;
    State->_wereSplashesDrawn = false;
    State->_wasBoltDrawn = false;
    bool IsBoltDrawn = IsBoltDrawable(self, frame);
    if (!frame->AreRainStreaksActive && !frame->AreRainSplashesActive && !IsBoltDrawn)
    {
        return;
    }
    // Draw what the batch holds under the state it was queued with; the rain is two-sided, blended, depth tested and
    // writes no depth.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    if (IsBoltDrawn)
    {
        BeginBlendMode(BLEND_ADD_COLORS);
        DrawBolt(self, frame);
    }
    BeginBlendMode(BLEND_ALPHA_PREMULTIPLY);
    if (frame->AreRainSplashesActive)
    {
        DrawSplashes(self, frame);
    }
    if (frame->AreRainStreaksActive)
    {
        DrawStreaks(self, frame);
    }
    EndBlendMode();
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

void WorldRendererRain_UpdateStatus(WorldRenderer* self, const WorldRenderFrame* frame)
{
    const WorldRendererRainState* State = &self->_rain;
    WorldRendererStatus* Status = &self->_status;
    bool IsAvailable = (State->_streaks._shader != NULL);
    bool IsActive = State->_wereStreaksDrawn || State->_wereSplashesDrawn || State->_wasBoltDrawn;
    Status->Features[WorldRendererFeature_Rain] = !IsAvailable ? WorldRendererFeatureState_Unavailable
        : (IsActive ? WorldRendererFeatureState_Active : WorldRendererFeatureState_Inactive);
    bool IsMapPossible = IsMapAvailable(self) && !State->_isMapFailureReported;
    Status->Features[WorldRendererFeature_RainMap] = !IsMapPossible ? WorldRendererFeatureState_Unavailable
        : (State->_wasMapRead ? WorldRendererFeatureState_Active : WorldRendererFeatureState_Inactive);
    Status->IsRainEnabled = State->_isEnabled;
    Status->RainPrecipitation = frame->Rain.Precipitation;
    Status->RainRate = frame->Rain.RainRate;
    Status->RainStreakCount = State->_wereStreaksDrawn
        ? (uint32_t)lroundf(frame->Rain.StreakShare * (float)WORLD_RAIN_STREAK_COUNT) : 0U;
    Status->RainSplashShare = State->_wereSplashesDrawn ? frame->Rain.SplashShare : 0.0f;
    Status->PuddleCoverage = (frame->IsRainActive && (State->_puddleMask._texture != NULL))
        ? frame->Rain.PuddleCoverage : 0.0f;
    Status->IsRainMapActive = State->_wasMapRead;
    Status->IsBoltVisible = State->_wasBoltDrawn;
    Status->RainView = State->_view;
}

void WorldRendererRain_LogStatus(const WorldRenderer* self)
{
    if (self->_logger == NULL)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: rain %s: precipitation %.3g (%.3g mm/h), %" PRIu32 u8" streaks, %.3g of "
        u8"the splash cells, puddles up to %.3g of a surface, the rain map %s, a bolt %s; debug view %s.",
        Status->IsRainEnabled ? "on" : "off", (double)Status->RainPrecipitation, (double)Status->RainRate,
        Status->RainStreakCount, (double)Status->RainSplashShare, (double)Status->PuddleCoverage,
        Status->IsRainMapActive ? "in use" : "not in use", Status->IsBoltVisible ? "stands" : "none",
        (const char*)WorldRenderer_GetRainViewName(Status->RainView));
    Error_Deconstruct(&LogResult);
}
