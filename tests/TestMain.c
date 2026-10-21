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
    GameGHDFTests_Run(&Run);
    ConfigTests_Run(&Run);
    AssetManagerTests_Run(&Run);
    ShaderSourceTests_Run(&Run);
    TextureTests_Run(&Run);
    CollisionGeometryTests_Run(&Run);
    WorldLightCullingTests_Run(&Run);
    NamespacedKeyTests_Run(&Run);
    DataValueTests_Run(&Run);
    WorldTests_Run(&Run);
    WorldModelTests_Run(&Run);
    WorldColliderTests_Run(&Run);
    WorldCollisionTests_Run(&Run);
    WorldSpriteTests_Run(&Run);
    WorldHierarchyTests_Run(&Run);
    WorldTransformTests_Run(&Run);
    ObjectDataTests_Run(&Run);
    WorldFileTests_Run(&Run);
    PropertyDescriptorTests_Run(&Run);
    GameCalendarTests_Run(&Run);
    GameClockTests_Run(&Run);
    WorldEnvironmentTests_Run(&Run);
    NightSkyTests_Run(&Run);
    NightVisionTests_Run(&Run);
    WorldRenderCacheTests_Run(&Run);
    SpriteTransformTests_Run(&Run);
    WorldVisibilityTests_Run(&Run);
    WorldLodTests_Run(&Run);
    LoggerTests_Run(&Run);
    TextComponentTests_Run(&Run);
    TextComponentCodecTests_Run(&Run);
    TextDocumentTests_Run(&Run);
    TextLayoutTests_Run(&Run);
    FontTests_Run(&Run);
    LabelWidgetTests_Run(&Run);
    GameInputTests_Run(&Run);
    UIAnimationTests_Run(&Run);
    UIScreenTests_Run(&Run);
    UITestFrameTests_Run(&Run);

    int ExitCode = TestRun_Finish(&Run);
    TestRun_Deconstruct(&Run);
    return ExitCode;
}
