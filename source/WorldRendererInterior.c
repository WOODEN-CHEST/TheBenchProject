// Interior lighting's parts beside the sky's visibility map (WorldRendererSkyVisibility.c): the ambient zones each lit
// draw takes (shaders/common/interior.glsl lays them over each other per fragment), the lit shaders' interior uniforms,
// the camera's interior for the eye (its openness and the zones at it, eased) and the zones' debug wireframes. The maths
// is WorldInterior.h's.

#include <inttypes.h>
#include <math.h>
#include "WorldRendererInternal.h"
#include "Logger.h"
#include "raylib/raymath.h"
#include "raylib/rlgl.h"


// Macros.
/** The sRGB gamma a zone's wireframe colour is shown with (its light is linear). */
#define ZONE_WIRE_GAMMA 2.2f
/** A sphere zone's wireframe: its rings and slices. */
#define ZONE_WIRE_RINGS 8
#define ZONE_WIRE_SLICES 12
/** The floats one vec4 of a zone's uniform arrays takes. */
#define ZONE_VEC4 4


// Static functions.
/* The colour a zone's wireframe is drawn in: its own light's hue at full brightness (light grey for a zone without
 * light of its own). */
static Color GetZoneWireColor(const WorldInteriorZone* zone)
{
    float Brightest = fmaxf(zone->Ambient[0], fmaxf(zone->Ambient[1], zone->Ambient[2]));
    if (!(Brightest > 0.0f))
    {
        return LIGHTGRAY;
    }
    unsigned char Channels[3];
    for (int Channel = 0; Channel < 3; Channel++)
    {
        float Shown = powf(fmaxf(zone->Ambient[Channel] / Brightest, 0.0f), 1.0f / ZONE_WIRE_GAMMA);
        Channels[Channel] = (unsigned char)lroundf(fminf(Shown, 1.0f) * 255.0f);
    }
    return (Color){ Channels[0], Channels[1], Channels[2], 255 };
}

/* The matrix placing a zone's unit frame: its axes as columns, its centre as the translation (render space). */
static Matrix GetZoneFrame(const WorldInteriorZone* zone)
{
    Matrix Frame = { 0 };
    Frame.m0 = zone->Axes[0].x;
    Frame.m1 = zone->Axes[0].y;
    Frame.m2 = zone->Axes[0].z;
    Frame.m4 = zone->Axes[1].x;
    Frame.m5 = zone->Axes[1].y;
    Frame.m6 = zone->Axes[1].z;
    Frame.m8 = zone->Axes[2].x;
    Frame.m9 = zone->Axes[2].y;
    Frame.m10 = zone->Axes[2].z;
    Frame.m12 = zone->Center.x;
    Frame.m13 = zone->Center.y;
    Frame.m14 = zone->Center.z;
    Frame.m15 = 1.0f;
    return Frame;
}


// Internal functions (declared in WorldRendererInternal.h).
void WorldRendererInterior_Construct(WorldRenderer* self)
{
    WorldRendererInteriorState* State = &self->_interior;
    State->_isEnabled = true;
    State->_isActive = false;
    State->_cameraOpenness = 1.0f;
    WorldInteriorCamera_Construct(&State->_camera);
    State->_view = WorldRendererInteriorView_Off;
}

void WorldRendererInterior_PrepareShader(Shader shader, WorldRendererInteriorLocations* outLocations)
{
    outLocations->_locActive = GetShaderLocation(shader, "interiorActive");
    outLocations->_locSkyVisibilityActive = GetShaderLocation(shader, "skyVisibilityActive");
    outLocations->_locSkyVisibilityMap = GetShaderLocation(shader, "skyVisibilityMap");
    outLocations->_locSkyVisibilityViewProjection = GetShaderLocation(shader, "skyVisibilityViewProjection");
    outLocations->_locZoneCount = GetShaderLocation(shader, "ambientZoneCount");
    outLocations->_locZoneCenters = GetShaderLocation(shader, "ambientZoneCenters");
    outLocations->_locZoneAxisX = GetShaderLocation(shader, "ambientZoneAxisX");
    outLocations->_locZoneAxisY = GetShaderLocation(shader, "ambientZoneAxisY");
    outLocations->_locZoneAxisZ = GetShaderLocation(shader, "ambientZoneAxisZ");
    outLocations->_locZoneLights = GetShaderLocation(shader, "ambientZoneLights");
    outLocations->_locZoneSettings = GetShaderLocation(shader, "ambientZoneSettings");
    outLocations->_locView = GetShaderLocation(shader, "interiorView");
    outLocations->_locViewScale = GetShaderLocation(shader, "interiorViewScale");
    // The map's sampler on its fixed unit, once (see WorldRendererTextureUnit).
    int Unit = (int)WorldRendererTextureUnit_SkyVisibility;
    SetShaderValue(shader, outLocations->_locSkyVisibilityMap, &Unit, SHADER_UNIFORM_INT);
}

void WorldRendererInterior_PrepareCamera(WorldRenderer* self, WorldRenderFrame* frame, float deltaSeconds)
{
    WorldRendererInteriorState* State = &self->_interior;
    State->_isActive = State->_isEnabled;
    frame->IsInteriorActive = State->_isActive;
    if (!State->_isActive)
    {
        // Off: the eye takes the sky's light as in the open, and starts afresh once switched on again.
        WorldInteriorCamera_Construct(&State->_camera);
        frame->CameraInterior = State->_camera;
        frame->ExposureBias = 0.0f;
        return;
    }
    // The zones at the camera: render space's origin.
    size_t ZoneCount = 0U;
    const WorldInteriorZone* Zones = WorldRenderCache_GetAmbientZones(&self->_cache, &ZoneCount);
    size_t Selected[WORLD_INTERIOR_MAX_DRAW_ZONES];
    Vector3 CameraPosition = { 0.0f, 0.0f, 0.0f };
    size_t Count = WorldInterior_SelectZones(Zones, ZoneCount, CameraPosition, 0.0f, Selected);
    WorldInteriorZoneLight AtCamera = WorldInterior_ComposeZones(Zones, Selected, Count, CameraPosition,
        (Vector3){ 0.0f, 0.0f, 0.0f });
    WorldInteriorCamera_Update(&State->_camera, State->_cameraOpenness, &AtCamera, deltaSeconds);
    frame->CameraInterior = State->_camera;
    frame->ExposureBias = State->_camera.Zones.ExposureBias;
}

void WorldRendererInterior_UploadFrame(const WorldRenderer* self, const WorldRenderFrame* frame, Shader shader,
    const WorldRendererInteriorLocations* locations)
{
    int Active = frame->IsInteriorActive ? 1 : 0;
    SetShaderValue(shader, locations->_locActive, &Active, SHADER_UNIFORM_INT);
    int MapActive = frame->IsSkyVisibilityActive ? 1 : 0;
    SetShaderValue(shader, locations->_locSkyVisibilityActive, &MapActive, SHADER_UNIFORM_INT);
    if (frame->IsSkyVisibilityActive)
    {
        SetShaderValueMatrix(shader, locations->_locSkyVisibilityViewProjection, frame->SkyMap.ViewProjection);
    }
    // No zones until a draw sets its own.
    int NoZones = 0;
    SetShaderValue(shader, locations->_locZoneCount, &NoZones, SHADER_UNIFORM_INT);
    // The sky share's view shows its value on screen whatever the exposure: divided by the exposure the eye is at.
    int View = (frame->IsInteriorActive && (self->_interior._view == WorldRendererInteriorView_Sky)) ? 1 : 0;
    SetShaderValue(shader, locations->_locView, &View, SHADER_UNIFORM_INT);
    float ViewScale = (frame->LastExposure > 0.0f) ? (1.0f / frame->LastExposure) : 1.0f;
    SetShaderValue(shader, locations->_locViewScale, &ViewScale, SHADER_UNIFORM_FLOAT);
}

void WorldRendererInterior_UploadZones(const WorldRenderer* self, Shader shader,
    const WorldRendererInteriorLocations* locations, Vector3 center, float radius)
{
    if (locations->_locZoneCount < 0)
    {
        return;
    }
    size_t Count = 0U;
    if (self->_interior._isActive)
    {
        size_t ZoneCount = 0U;
        const WorldInteriorZone* Zones = WorldRenderCache_GetAmbientZones(&self->_cache, &ZoneCount);
        size_t Selected[WORLD_INTERIOR_MAX_DRAW_ZONES];
        Count = WorldInterior_SelectZones(Zones, ZoneCount, center, radius, Selected);
        float Centers[WORLD_INTERIOR_MAX_DRAW_ZONES * ZONE_VEC4];
        float AxisX[WORLD_INTERIOR_MAX_DRAW_ZONES * ZONE_VEC4];
        float AxisY[WORLD_INTERIOR_MAX_DRAW_ZONES * ZONE_VEC4];
        float AxisZ[WORLD_INTERIOR_MAX_DRAW_ZONES * ZONE_VEC4];
        float Lights[WORLD_INTERIOR_MAX_DRAW_ZONES * ZONE_VEC4];
        float Settings[WORLD_INTERIOR_MAX_DRAW_ZONES * ZONE_VEC4];
        float Group = 0.0f;
        for (size_t Position = 0U; Position < Count; Position++)
        {
            const WorldInteriorZone* Zone = &Zones[Selected[Position]];
            bool IsSphere = (Zone->Shape == WorldAmbientZoneShape_Sphere);
            float* Center = &Centers[Position * ZONE_VEC4];
            Center[0] = Zone->Center.x;
            Center[1] = Zone->Center.y;
            Center[2] = Zone->Center.z;
            Center[3] = IsSphere ? 1.0f : 0.0f;
            float Halves[3] = { IsSphere ? Zone->Radius : Zone->HalfExtents.x, Zone->HalfExtents.y,
                Zone->HalfExtents.z };
            float* Axes[3] = { &AxisX[Position * ZONE_VEC4], &AxisY[Position * ZONE_VEC4], &AxisZ[Position * ZONE_VEC4] };
            for (int Axis = 0; Axis < 3; Axis++)
            {
                Axes[Axis][0] = Zone->Axes[Axis].x;
                Axes[Axis][1] = Zone->Axes[Axis].y;
                Axes[Axis][2] = Zone->Axes[Axis].z;
                Axes[Axis][3] = Halves[Axis];
            }
            float* Light = &Lights[Position * ZONE_VEC4];
            Light[0] = Zone->Ambient[0];
            Light[1] = Zone->Ambient[1];
            Light[2] = Zone->Ambient[2];
            Light[3] = Zone->BlendDistance;
            // Equal priorities share a group (the shader blends a group and lays the groups over each other in order):
            // the group's index, not the priority itself, so no two priorities meet in a float.
            if ((Position > 0U) && (Zone->Priority != Zones[Selected[Position - 1U]].Priority))
            {
                Group += 1.0f;
            }
            float* Setting = &Settings[Position * ZONE_VEC4];
            Setting[0] = Zone->SkyAmbientScale;
            Setting[1] = Zone->Haze;
            Setting[2] = Group;
            Setting[3] = 0.0f;
        }
        if (Count > 0U)
        {
            int Uploaded = (int)Count;
            SetShaderValueV(shader, locations->_locZoneCenters, Centers, SHADER_UNIFORM_VEC4, Uploaded);
            SetShaderValueV(shader, locations->_locZoneAxisX, AxisX, SHADER_UNIFORM_VEC4, Uploaded);
            SetShaderValueV(shader, locations->_locZoneAxisY, AxisY, SHADER_UNIFORM_VEC4, Uploaded);
            SetShaderValueV(shader, locations->_locZoneAxisZ, AxisZ, SHADER_UNIFORM_VEC4, Uploaded);
            SetShaderValueV(shader, locations->_locZoneLights, Lights, SHADER_UNIFORM_VEC4, Uploaded);
            SetShaderValueV(shader, locations->_locZoneSettings, Settings, SHADER_UNIFORM_VEC4, Uploaded);
        }
    }
    int CountValue = (int)Count;
    SetShaderValue(shader, locations->_locZoneCount, &CountValue, SHADER_UNIFORM_INT);
}

void WorldRendererInterior_DrawZones(const WorldRenderer* self, const WorldRenderFrame* frame)
{
    if (!frame->IsInteriorActive || (self->_interior._view == WorldRendererInteriorView_Off))
    {
        return;
    }
    size_t ZoneCount = 0U;
    const WorldInteriorZone* Zones = WorldRenderCache_GetAmbientZones(&self->_cache, &ZoneCount);
    for (size_t Index = 0U; Index < ZoneCount; Index++)
    {
        const WorldInteriorZone* Zone = &Zones[Index];
        Color Wire = GetZoneWireColor(Zone);
        if (Zone->Shape == WorldAmbientZoneShape_Sphere)
        {
            DrawSphereWires(Zone->Center, Zone->Radius, ZONE_WIRE_RINGS, ZONE_WIRE_SLICES, Wire);
            continue;
        }
        rlPushMatrix();
        rlMultMatrixf(MatrixToFloat(GetZoneFrame(Zone)));
        DrawCubeWires((Vector3){ 0.0f, 0.0f, 0.0f }, 2.0f * Zone->HalfExtents.x, 2.0f * Zone->HalfExtents.y,
            2.0f * Zone->HalfExtents.z, Wire);
        rlPopMatrix();
    }
}

void WorldRendererInterior_LogStatus(const WorldRenderer* self)
{
    if ((self->_logger == NULL) || !self->_isStatusReported)
    {
        return;
    }
    const WorldRendererStatus* Status = &self->_status;
    if (!Status->IsInteriorLightingEnabled)
    {
        Error LogResult = Logger_LogInfo(self->_logger, (const unsigned char*)u8"WorldRenderer: interior lighting off "
            u8"(every surface takes the whole sky's light, no ambient zone applies, the eye takes the sky's light at the "
            u8"camera).");
        Error_Deconstruct(&LogResult);
        return;
    }
    Error LogResult = Logger_LogInfoFormatted(self->_logger,
        (const unsigned char*)u8"WorldRenderer: interior lighting on: the sky's visibility map %s (%" PRIu64 u8" things "
        u8"keep the sky out around the camera), %" PRIu64 u8" ambient zones; the camera %.0f %% open and %.0f %% in "
        u8"zones (exposure bias %+.2f stops); the eye's light %.4g (the sky's at the camera %.4g); view: %s.",
        (Status->Features[WorldRendererFeature_SkyVisibility] == WorldRendererFeatureState_Active) ? "drawn"
            : ((Status->Features[WorldRendererFeature_SkyVisibility] == WorldRendererFeatureState_Unavailable)
                ? "unavailable" : "not drawn"),
        (uint64_t)Status->SkyOccluderCount, (uint64_t)Status->AmbientZoneCount, (double)(Status->CameraOpenness * 100.0f),
        (double)(Status->CameraZoneWeight * 100.0f), (double)Status->ExposureBias, (double)Status->CameraSceneLuminance,
        (double)Status->SceneLuminance, (const char*)WorldRenderer_GetInteriorViewName(Status->InteriorView));
    Error_Deconstruct(&LogResult);
}
