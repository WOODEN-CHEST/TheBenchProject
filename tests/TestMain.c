#include "TestSuites.h"
#include <stdio.h>


// Program entry.
int main(void)
{
    TestRun Run;
    Error ConstructResult = TestRun_Construct(&Run);
    if (ConstructResult.Code != ErrorCode_Success)
    {
        printf("Could not start the test run: %s\n",
            (ConstructResult.Message != NULL) ? (const char*)ConstructResult.Message : "(no message)");
        Error_Deconstruct(&ConstructResult);
        TestRun_Deconstruct(&Run);
        return 2;
    }

    GameJSONTests_Run(&Run);
    WRFrameworkTests_Run(&Run);
    WRVectorMathTests_Run(&Run);
    GameGHDFTests_Run(&Run);
    ConfigTests_Run(&Run);
    AssetManagerTests_Run(&Run);
    ShaderSourceTests_Run(&Run);
    TextureTests_Run(&Run);
    ModelOrmMapTests_Run(&Run);
    ModelGltfMaterialsTests_Run(&Run);
    CollisionGeometryTests_Run(&Run);
    TerrainMeshTests_Run(&Run);
    TerrainGltfTests_Run(&Run);
    TerrainAssetTests_Run(&Run);
    TerrainMaterialTests_Run(&Run);
    TerrainBlendTests_Run(&Run);
    WorldLightCullingTests_Run(&Run);
    WorldLightTests_Run(&Run);
    WorldLocalShadowsTests_Run(&Run);
    NamespacedKeyTests_Run(&Run);
    RegistryTests_Run(&Run);
    DataValueTests_Run(&Run);
    WorldTests_Run(&Run);
    WorldModelTests_Run(&Run);
    WorldColliderTests_Run(&Run);
    WorldScreenTests_Run(&Run);
    CrtScreenTests_Run(&Run);
    WorldCollisionTests_Run(&Run);
    WorldCollisionMotionTests_Run(&Run);
    WorldTerrainCollisionTests_Run(&Run);
    PathCurveTests_Run(&Run);
    WorldPathTests_Run(&Run);
    PathObjectLayoutTests_Run(&Run);
    PathObjectTests_Run(&Run);
    WorldPathObjectTests_Run(&Run);
    PrefabTests_Run(&Run);
    WorldPrefabTests_Run(&Run);
    WorldCharacterTests_Run(&Run);
    WorldSpriteTests_Run(&Run);
    WorldHierarchyTests_Run(&Run);
    WorldTransformTests_Run(&Run);
    ObjectDataTests_Run(&Run);
    WorldFileTests_Run(&Run);
    PropertyDescriptorTests_Run(&Run);
    GameCalendarTests_Run(&Run);
    GameClockTests_Run(&Run);
    WorldEnvironmentTests_Run(&Run);
    ClimateTests_Run(&Run);
    WeatherSystemTests_Run(&Run);
    WorldWeatherLightTests_Run(&Run);
    CloudNoiseTests_Run(&Run);
    WorldCloudsTests_Run(&Run);
    WorldRainTests_Run(&Run);
    WorldReflectionsTests_Run(&Run);
    WaterTests_Run(&Run);
    NightSkyTests_Run(&Run);
    NightVisionTests_Run(&Run);
    EyeAdaptationTests_Run(&Run);
    WorldRenderCacheTests_Run(&Run);
    SpriteTransformTests_Run(&Run);
    WorldVisibilityTests_Run(&Run);
    WorldRenderSpaceTests_Run(&Run);
    WorldLodTests_Run(&Run);
    WorldShadowLevelsTests_Run(&Run);
    WorldFarShadowsTests_Run(&Run);
    WorldInstancingTests_Run(&Run);
    WorldAtmosphereTests_Run(&Run);
    WorldSkyLightTests_Run(&Run);
    WorldInteriorTests_Run(&Run);
    AnimationLibraryTests_Run(&Run);
    AnimationGraphTests_Run(&Run);
    AnimationGraphPlayerTests_Run(&Run);
    SkeletalAnimationTests_Run(&Run);
    WorldAnimationTests_Run(&Run);
    WorldAnimationGraphTests_Run(&Run);
    LoggerTests_Run(&Run);
    TextComponentTests_Run(&Run);
    TextComponentCodecTests_Run(&Run);
    TextDocumentTests_Run(&Run);
    TextLayoutTests_Run(&Run);
    UITextEditTests_Run(&Run);
    FontTests_Run(&Run);
    LabelWidgetTests_Run(&Run);
    GameInputTests_Run(&Run);
    InputActionsTests_Run(&Run);
    UIAnimationTests_Run(&Run);
    UIScreenTests_Run(&Run);
    UILayoutTests_Run(&Run);
    UIThemeTests_Run(&Run);
    UIElementTests_Run(&Run);
    UIScalePolicyTests_Run(&Run);
    UIWindowHostTests_Run(&Run);
    UIRoutingTests_Run(&Run);
    UIEnteringTests_Run(&Run);
    UITransformTests_Run(&Run);
    UIScaleTests_Run(&Run);
    UILookTests_Run(&Run);
    UITooltipTests_Run(&Run);
    UIScrollTests_Run(&Run);
    UIPopupTests_Run(&Run);
    UIDropdownTests_Run(&Run);
    UIMenuTests_Run(&Run);
    UITextBoxTests_Run(&Run);
    UIValueTests_Run(&Run);
    UISliderTests_Run(&Run);
    UIPropertyBoxTests_Run(&Run);
    UICompoundTests_Run(&Run);
    UIListTests_Run(&Run);
    UITreeTests_Run(&Run);
    UIDialogTests_Run(&Run);
    UIColorPickerTests_Run(&Run);
    UIDragDropTests_Run(&Run);
    UIToolbarTests_Run(&Run);
    UIWindowTests_Run(&Run);
    UIWindowTypesTests_Run(&Run);
    GameWindowStateTests_Run(&Run);
    GameWindowTests_Run(&Run);
    UITestFrameTests_Run(&Run);
    GameFrameManagerTests_Run(&Run);

    int ExitCode = TestRun_Finish(&Run);
    TestRun_Deconstruct(&Run);
    return ExitCode;
}
