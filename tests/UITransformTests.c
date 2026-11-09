#include "TestSuites.h"
#include "TestUI.h"
#include "UIKit.h"
#include "raylib/raymath.h"
#include <math.h>


// Macros.
/** How close two positions in pixels must be to count as the same: float arithmetic over a few hundred pixels. */
#define PIXEL_TOLERANCE 0.01f
#define PI_F 3.14159265f


// Types.
/* A widget that draws nothing and writes down, in Render, the transform the context has: a probe of what rendering pushes. */
typedef struct ProbeWidgetStruct
{
    Widget Base;
    /* Set by the test: whether Render fails. */
    bool IsFailing;
    /* Whether the hit test hook answers, and how: only the box's left half, or everywhere. */
    bool HasHitTest;
    bool IsHitEverywhere;
} ProbeWidget;

/* What the probes saw in their last Render: a test's own record, read after UIScreen_Render. */
typedef struct ProbeRecordStruct
{
    size_t RenderCount;
    size_t Depths[8];
    Matrix Transforms[8];
    WidgetHitTestArgs LastHitArgs;
    size_t HitTestCount;
} ProbeRecord;


// Fields.
/* The probes' record. A test's only state outside its fixture: Render may not write the widget's own state. */
static ProbeRecord Record;


// Static functions: the probe.
static Error Probe_Render(void* self, UIRenderContext* context)
{
    ProbeWidget* Probe = self;
    if (Record.RenderCount < 8U)
    {
        Record.Depths[Record.RenderCount] = UIRenderContext_GetRenderContext(context)->_transformDepth;
        RenderContext_TryGetTransform(UIRenderContext_GetRenderContext(context), &Record.Transforms[Record.RenderCount]);
    }
    Record.RenderCount++;
    return Probe->IsFailing ? Error_Construct2(ErrorCode_InvalidOperation, "a probe that fails") : Error_CreateSuccess();
}

static bool Probe_HitTest(void* self, const WidgetHitTestArgs* args)
{
    ProbeWidget* Probe = self;
    Record.LastHitArgs = *args;
    Record.HitTestCount++;
    if (!Probe->HasHitTest)
    {
        return args->IsInsideBox;
    }
    return Probe->IsHitEverywhere || (args->IsInsideBox && (args->LocalPosition.x < 0.5f));
}

static const WidgetVTable ProbeVTable = { .Render = Probe_Render, .HitTest = Probe_HitTest };

static uint64_t RegisterProbe(TestRun* run, TestUIFixture* fixture)
{
    WidgetTypeDescriptor Descriptor = { .Name = u8"Probe", .StructSize = sizeof(ProbeWidget), .VTable = &ProbeVTable, .IsFocusable = false };
    uint64_t Type = WIDGET_TYPE_ID_INVALID;
    TEST_CHECK_SUCCESS(run, UIWidgetFactory_RegisterType(&fixture->Widgets, &Descriptor, &Type));
    return Type;
}

/* A probe placed in its parent (a root when the parent is WIDGET_ID_INVALID), by UI pixels from its parent's top-left. */
static uint64_t AddProbe(TestRun* run, TestUIFixture* fixture, uint64_t type, uint64_t parent, float x, float y, float width, float height)
{
    Widget* Created = NULL;
    if (!TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&fixture->Widgets, &fixture->Screen, type, NULL, &Created)))
    {
        return WIDGET_ID_INVALID;
    }
    uint64_t Id = Widget_GetId(Created);
    UIPlacement Placement = UIPlacement_FromTopLeft(x, y, width, height);
    TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Created, &Placement));
    TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
    Widget* Parent = NULL;
    if (parent == WIDGET_ID_INVALID)
    {
        TEST_CHECK_SUCCESS(run, UIScreen_AddWidget(&fixture->Screen, Created));
    }
    else if (TEST_CHECK(run, UIScreen_TryGetWidgetById(&fixture->Screen, parent, &Parent)))
    {
        TEST_CHECK_SUCCESS(run, Widget_AddSubWidget(Parent, Created));
    }
    return Id;
}

static Widget* Get(TestUIFixture* fixture, uint64_t id)
{
    Widget* Target = NULL;
    UIScreen_TryGetWidgetById(&fixture->Screen, id, &Target);
    return Target;
}

static UIRenderTransform Turned(float rotation, float pivotX, float pivotY)
{
    UIRenderTransform Turn = UIRenderTransform_Create();
    Turn.Rotation = rotation;
    Turn.Pivot = (Vector2){ .x = pivotX, .y = pivotY };
    return Turn;
}

static bool IsNear(Vector2 a, Vector2 b, float tolerance)
{
    return (fabsf(a.x - b.x) <= tolerance) && (fabsf(a.y - b.y) <= tolerance);
}

static Vector2 ApplyMatrix(const Matrix* matrix, Vector2 point)
{
    Vector3 Moved = Vector3Transform((Vector3){ .x = point.x, .y = point.y, .z = 0.0f }, *matrix);
    return (Vector2){ .x = Moved.x, .y = Moved.y };
}

/* A surface pixel as normalized screen coordinates of the fixture's surface. */
static Vector2 ToScreen(Vector2 pixels)
{
    return (Vector2){ .x = pixels.x / TEST_UI_SURFACE_WIDTH, .y = pixels.y / TEST_UI_SURFACE_HEIGHT };
}

/* What is top-most at a surface pixel. */
static uint64_t TopmostAt(TestUIFixture* fixture, Vector2 pixels)
{
    Widget* Top = NULL;
    return UIScreen_TryGetTopmostAt(&fixture->Screen, ToScreen(pixels), &Top) ? Widget_GetId(Top) : WIDGET_ID_INVALID;
}


// Tests: the arithmetic.
/* The matrix the renderer gets and the closed form the pointer goes through agree, and the inverse takes a point back. */
static void TheMatrixAndThePointsAgree(TestRun* run)
{
    const float Rotations[] = { 0.0f, 0.3f, PI_F * 0.5f, PI_F, -1.2f, 5.0f };
    const Vector2 Pivots[] = { { 0.5f, 0.5f }, { 0.0f, 0.0f }, { 1.0f, 0.25f }, { -0.5f, 2.0f } };
    const Vector2 Scales[] = { { 1.0f, 1.0f }, { 2.0f, 0.5f }, { -1.0f, 1.0f }, { 0.25f, 3.0f } };
    Rectangle Box = { .x = 120.0f, .y = 80.0f, .width = 200.0f, .height = 60.0f };
    for (size_t R = 0U; R < (sizeof(Rotations) / sizeof(Rotations[0])); R++)
    {
        for (size_t P = 0U; P < (sizeof(Pivots) / sizeof(Pivots[0])); P++)
        {
            for (size_t S = 0U; S < (sizeof(Scales) / sizeof(Scales[0])); S++)
            {
                UIRenderTransform Tried = Turned(Rotations[R], Pivots[P].x, Pivots[P].y);
                Tried.VisualScale = Scales[S];
                Tried.Offset = (Vector2){ .x = 7.0f, .y = -3.0f };
                TEST_CHECK(run, UIRenderTransform_IsValid(&Tried));
                Matrix Turning = UIRenderTransform_GetMatrix(&Tried, Box, 1.5f);
                Vector2 Points[] = { { 120.0f, 80.0f }, { 320.0f, 140.0f }, { 200.0f, 100.0f }, { -50.0f, 400.0f } };
                for (size_t Index = 0U; Index < (sizeof(Points) / sizeof(Points[0])); Index++)
                {
                    Vector2 Shown = UIRenderTransform_ApplyToPoint(&Tried, Box, 1.5f, Points[Index]);
                    TEST_CHECK(run, IsNear(Shown, ApplyMatrix(&Turning, Points[Index]), PIXEL_TOLERANCE));
                    Vector2 Back = { 0.0f, 0.0f };
                    TEST_CHECK(run, UIRenderTransform_TryInvertPoint(&Tried, Box, 1.5f, Shown, &Back));
                    TEST_CHECK(run, IsNear(Back, Points[Index], 0.05f));
                }
            }
        }
    }

    // By hand: a quarter turn about the centre takes the right middle to the bottom middle (clockwise on a surface whose y runs down),
    // and the offset counts in the scale it is given.
    UIRenderTransform Quarter = Turned(PI_F * 0.5f, 0.5f, 0.5f);
    Rectangle Square = { .x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 50.0f };
    TEST_CHECK(run, IsNear(UIRenderTransform_ApplyToPoint(&Quarter, Square, 1.0f, (Vector2){ 100.0f, 25.0f }), (Vector2){ 50.0f, 75.0f }, PIXEL_TOLERANCE));
    UIRenderTransform Moved = UIRenderTransform_Create();
    Moved.Offset = (Vector2){ .x = 10.0f, .y = 4.0f };
    TEST_CHECK(run, IsNear(UIRenderTransform_ApplyToPoint(&Moved, Square, 2.0f, (Vector2){ 1.0f, 1.0f }), (Vector2){ 21.0f, 9.0f }, PIXEL_TOLERANCE));
    TEST_CHECK(run, UIRenderTransform_IsIdentity(&(UIRenderTransform){ .Rotation = 0.0f, .Pivot = { 0.2f, 0.9f }, .Offset = { 0.0f, 0.0f },
        .VisualScale = { 1.0f, 1.0f } }));
    TEST_CHECK(run, !UIRenderTransform_IsIdentity(&Moved));

    // A visual scale of 0 holds no point; values that are not numbers or too large are not valid.
    UIRenderTransform Flat = UIRenderTransform_Create();
    Flat.VisualScale.y = 0.0f;
    Vector2 Ignored = { 0.0f, 0.0f };
    TEST_CHECK(run, !UIRenderTransform_TryInvertPoint(&Flat, Square, 1.0f, (Vector2){ 50.0f, 25.0f }, &Ignored));
    UIRenderTransform Bad = UIRenderTransform_Create();
    Bad.Rotation = NAN;
    TEST_CHECK(run, !UIRenderTransform_IsValid(&Bad));
    Bad = UIRenderTransform_Create();
    Bad.VisualScale.x = UI_RENDER_TRANSFORM_MAX_SCALE * 2.0f;
    TEST_CHECK(run, !UIRenderTransform_IsValid(&Bad));
    Bad = UIRenderTransform_Create();
    Bad.Offset.y = INFINITY;
    TEST_CHECK(run, !UIRenderTransform_IsValid(&Bad));
    Bad = UIRenderTransform_Create();
    Bad.Pivot.x = NAN;
    TEST_CHECK(run, !UIRenderTransform_IsValid(&Bad));
}

/* The renderer's own stack composes in pixels, keeps one matrix for raylib, counts what it cannot store, and turns a clip into
 * the bounding box of where it shows. A measuring context records it all without a window. */
static void TheRendererComposesAndBalances(TestRun* run)
{
    RenderContext Context;
    RenderContext_CreateMeasuring(&Context, (Vector2){ .x = 800.0f, .y = 600.0f }, 4.0f / 3.0f, RenderTargetPosition_Centered());
    Matrix Outer = MatrixMultiply(MatrixRotateZ(0.5f), MatrixTranslate(100.0f, 0.0f, 0.0f));
    Matrix Inner = MatrixScale(2.0f, 3.0f, 1.0f);
    Matrix InEffect = MatrixIdentity();
    TEST_CHECK(run, !RenderContext_TryGetTransform(&Context, &InEffect));
    RenderContext_PushTransform(&Context, &Outer);
    RenderContext_PushTransform(&Context, &Inner);
    TEST_CHECK(run, RenderContext_TryGetTransform(&Context, &InEffect) && (Context._transformDepth == 2U));
    Vector2 Point = { .x = 5.0f, .y = 7.0f };
    // The inner one first, then the outer.
    TEST_CHECK(run, IsNear(ApplyMatrix(&InEffect, Point), ApplyMatrix(&Outer, ApplyMatrix(&Inner, Point)), PIXEL_TOLERANCE));
    RenderContext_PopTransform(&Context);
    TEST_CHECK(run, RenderContext_TryGetTransform(&Context, &InEffect));
    TEST_CHECK(run, IsNear(ApplyMatrix(&InEffect, Point), ApplyMatrix(&Outer, Point), PIXEL_TOLERANCE));
    RenderContext_PopTransform(&Context);
    TEST_CHECK(run, !RenderContext_TryGetTransform(&Context, &InEffect) && (Context._transformDepth == 0U));
    RenderContext_PopTransform(&Context); // Popping nothing does nothing.
    TEST_CHECK(run, Context._transformDepth == 0U);

    // Deeper than it stores (a widget tree's depth): counted, the deepest stored kept, and the pops balance.
    Matrix Step = MatrixTranslate(1.0f, 0.0f, 0.0f);
    for (size_t Index = 0U; Index < (RENDER_CONTEXT_MAX_TRANSFORM_DEPTH + 6U); Index++)
    {
        RenderContext_PushTransform(&Context, &Step);
    }
    TEST_CHECK(run, Context._transformDepth == (RENDER_CONTEXT_MAX_TRANSFORM_DEPTH + 6U));
    TEST_CHECK(run, RenderContext_TryGetTransform(&Context, &InEffect) && (fabsf(InEffect.m12 - (float)RENDER_CONTEXT_MAX_TRANSFORM_DEPTH) < 0.001f));
    for (size_t Index = 0U; Index < (RENDER_CONTEXT_MAX_TRANSFORM_DEPTH + 6U); Index++)
    {
        RenderContext_PopTransform(&Context);
        if (Index == 5U)
        {
            TEST_CHECK(run, RenderContext_TryGetTransform(&Context, &InEffect)
                && (fabsf(InEffect.m12 - (float)RENDER_CONTEXT_MAX_TRANSFORM_DEPTH) < 0.001f));
        }
    }
    TEST_CHECK(run, Context._transformDepth == 0U);

    // A clip under a turn is the bounding box of the turned rectangle: a square turned an eighth about its centre.
    Matrix Eighth = MatrixMultiply(MatrixMultiply(MatrixTranslate(-50.0f, -50.0f, 0.0f), MatrixRotateZ(PI_F * 0.25f)), MatrixTranslate(50.0f, 50.0f, 0.0f));
    RenderContext_PushTransform(&Context, &Eighth);
    RenderContext_PushClip(&Context, (Rectangle){ .x = 0.0f, .y = 0.0f, .width = 100.0f, .height = 100.0f });
    float HalfDiagonal = 50.0f * sqrtf(2.0f);
    Rectangle Clip = Context._clipStack[0];
    TEST_CHECK(run, (fabsf(Clip.x - (50.0f - HalfDiagonal)) < 0.01f) && (fabsf(Clip.width - (2.0f * HalfDiagonal)) < 0.01f));
    TEST_CHECK(run, (fabsf(Clip.y - (50.0f - HalfDiagonal)) < 0.01f) && (fabsf(Clip.height - (2.0f * HalfDiagonal)) < 0.01f));
    RenderContext_PopClip(&Context);
    RenderContext_PopTransform(&Context);
    TEST_CHECK(run, (Context._clipDepth == 0U) && (Context._transformDepth == 0U));
    RenderContext_Deconstruct(&Context);
}


// Tests: on a screen.
/* A turned widget is hit where it shows, at many turns and pivots, and so is a turned child in a turned parent. */
static void ATurnedWidgetIsHitWhereItShows(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Parent = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 300.0f, 150.0f, 240.0f, 120.0f);
        uint64_t Child = AddProbe(run, &Fixture, Type, Parent, 40.0f, 30.0f, 80.0f, 40.0f);
        TestUI_Tick(run, &Fixture);
        Rectangle ParentBox = { .x = 300.0f, .y = 150.0f, .width = 240.0f, .height = 120.0f };
        Rectangle ChildBox = { .x = 340.0f, .y = 180.0f, .width = 80.0f, .height = 40.0f };
        const float Rotations[] = { 0.4f, PI_F * 0.5f, 2.5f, -0.9f };
        const Vector2 Pivots[] = { { 0.5f, 0.5f }, { 0.0f, 0.0f }, { 1.0f, 1.0f } };
        for (size_t R = 0U; R < (sizeof(Rotations) / sizeof(Rotations[0])); R++)
        {
            for (size_t P = 0U; P < (sizeof(Pivots) / sizeof(Pivots[0])); P++)
            {
                UIRenderTransform Outer = Turned(Rotations[R], Pivots[P].x, Pivots[P].y);
                UIRenderTransform Inner = Turned(-Rotations[R] * 0.5f, 1.0f - Pivots[P].x, Pivots[P].y);
                TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Outer));
                TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Child), &Inner));
                // Where each shows: a point of the child's box through both, of the parent's own (away from the child) through its own.
                Vector2 ChildPoint = UIRenderTransform_ApplyToPoint(&Outer, ParentBox, 1.0f,
                    UIRenderTransform_ApplyToPoint(&Inner, ChildBox, 1.0f, (Vector2){ .x = 380.0f, .y = 200.0f }));
                Vector2 ParentPoint = UIRenderTransform_ApplyToPoint(&Outer, ParentBox, 1.0f, (Vector2){ .x = 500.0f, .y = 250.0f });
                Vector2 Outside = UIRenderTransform_ApplyToPoint(&Outer, ParentBox, 1.0f, (Vector2){ .x = 560.0f, .y = 210.0f });
                TEST_CHECK(run, TopmostAt(&Fixture, ChildPoint) == Child);
                TEST_CHECK(run, TopmostAt(&Fixture, ParentPoint) == Parent);
                TEST_CHECK(run, TopmostAt(&Fixture, Outside) == WIDGET_ID_INVALID);
                // The pointer is told where it is in the widget, the transforms undone.
                Vector2 Local = UIScreen_ScreenToWidget(&Fixture.Screen, Get(&Fixture, Child), ToScreen(ChildPoint));
                TEST_CHECK(run, IsNear(Local, (Vector2){ .x = 0.5f, .y = 0.5f }, 0.001f));
                Vector2 Corner = UIScreen_WidgetToScreen(&Fixture.Screen, Get(&Fixture, Child), (Vector2){ .x = 1.0f, .y = 0.0f });
                Vector2 Back = UIScreen_ScreenToWidget(&Fixture.Screen, Get(&Fixture, Child), Corner);
                TEST_CHECK(run, IsNear(Back, (Vector2){ .x = 1.0f, .y = 0.0f }, 0.001f));
            }
        }
        // Without them it is hit where layout put it again.
        TEST_CHECK_SUCCESS(run, Widget_ClearRenderTransform(Get(&Fixture, Parent)));
        TEST_CHECK_SUCCESS(run, Widget_ClearRenderTransform(Get(&Fixture, Child)));
        TEST_CHECK(run, !Widget_HasRenderTransform(Get(&Fixture, Parent)));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 380.0f, .y = 200.0f }) == Child);
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 530.0f, .y = 260.0f }) == Parent);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The visual offset and scale move what is hit with what shows; a visual scale of 0 holds no point, the subtree's neither. */
static void TheOffsetAndScaleMoveTheHitAndZeroHoldsNone(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Parent = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 50.0f, 50.0f, 100.0f, 50.0f); // 100, 100, 200 x 100 on the surface
        uint64_t Child = AddProbe(run, &Fixture, Type, Parent, 0.0f, 0.0f, 20.0f, 20.0f);
        TestUI_Tick(run, &Fixture);
        UIRenderTransform Slid = UIRenderTransform_Create();
        Slid.Offset = (Vector2){ .x = 100.0f, .y = 0.0f }; // UI pixels of the screen's scale: 200 surface pixels.
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Slid));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 150.0f, .y = 150.0f }) == WIDGET_ID_INVALID);
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 350.0f, .y = 150.0f }) == Parent);
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 310.0f, .y = 110.0f }) == Child);
        UIRenderTransform Down = UIRenderTransform_Create();
        Down.Offset = (Vector2){ .x = 0.0f, .y = 100.0f }; // Down alone moves it too.
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Down));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 150.0f, .y = 150.0f }) == WIDGET_ID_INVALID);
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 150.0f, .y = 350.0f }) == Parent);

        // A pointer at rest: what is under it follows the transform without the pointer moving.
        UIRenderTransform Still = UIRenderTransform_Create();
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Still));
        TestUI_MovePointer(&Fixture, 250.0f, 150.0f);
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Parent);
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Down));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == WIDGET_ID_INVALID);
        TEST_CHECK_SUCCESS(run, Widget_ClearRenderTransform(Get(&Fixture, Parent)));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Parent);

        UIRenderTransform Doubled = UIRenderTransform_Create();
        Doubled.Pivot = (Vector2){ .x = 0.0f, .y = 0.0f };
        Doubled.VisualScale = (Vector2){ .x = 2.0f, .y = 2.0f };
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Doubled));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 450.0f, .y = 250.0f }) == Parent);
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 170.0f, .y = 170.0f }) == Child);

        UIRenderTransform Flat = UIRenderTransform_Create();
        Flat.VisualScale = (Vector2){ .x = 1.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Flat));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 200.0f, .y = 150.0f }) == WIDGET_ID_INVALID);
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 110.0f, .y = 110.0f }) == WIDGET_ID_INVALID);
        // A drag still has somewhere to go: the level that holds no point is passed over.
        Vector2 Local = UIScreen_ScreenToWidget(&Fixture.Screen, Get(&Fixture, Child), ToScreen((Vector2){ .x = 120.0f, .y = 120.0f }));
        TEST_CHECK(run, isfinite(Local.x) && isfinite(Local.y));

        // Refused: a transform that is not valid changes nothing.
        UIRenderTransform Bad = UIRenderTransform_Create();
        Bad.Rotation = INFINITY;
        TEST_CHECK_ERROR(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Bad), ErrorCode_ArgumentOutOfRange);
        Bad = UIRenderTransform_Create();
        Bad.VisualScale = (Vector2){ .x = 1.0f, .y = UI_RENDER_TRANSFORM_MAX_SCALE * 2.0f };
        TEST_CHECK_ERROR(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Bad), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK(run, Widget_GetRenderTransform(Get(&Fixture, Parent)).VisualScale.y == 0.0f);
        TEST_CHECK_ERROR(run, Widget_SetRenderTransform(NULL, &Flat), ErrorCode_IllegalArgument);
        TEST_CHECK_ERROR(run, Widget_SetRenderTransform(Get(&Fixture, Parent), NULL), ErrorCode_IllegalArgument);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A scale change moves a visual offset with no box changing (anchors alone): what is under a pointer at rest follows it. */
static void AScaleChangeMovesTheHoverWithTheOffset(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Ids[2] = { WIDGET_ID_INVALID, WIDGET_ID_INVALID };
        for (size_t Index = 0U; Index < 2U; Index++)
        {
            Widget* Created = NULL;
            if (TEST_CHECK_SUCCESS(run, UIWidgetFactory_CreateWidget(&Fixture.Widgets, &Fixture.Screen, Type, NULL, &Created)))
            {
                Ids[Index] = Widget_GetId(Created);
                UIPlacement Whole = UIPlacement_Stretch(UIInsets_All(0.0f));
                TEST_CHECK_SUCCESS(run, Widget_SetPlacement(Created, &Whole));
                TEST_CHECK_SUCCESS(run, Widget_InitializeWidget(Created));
                TEST_CHECK_SUCCESS(run, (Index == 0U) ? UIScreen_AddWidget(&Fixture.Screen, Created)
                    : Widget_AddSubWidget(Get(&Fixture, Ids[0]), Created));
            }
        }
        UIRenderTransform Moved = UIRenderTransform_Create();
        Moved.Offset = (Vector2){ .x = 100.0f, .y = 0.0f };
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Ids[1]), &Moved));
        TestUI_MovePointer(&Fixture, 150.0f, 250.0f);
        TestUI_Ticks(run, &Fixture, 2U);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Ids[1]); // The child shows from 100.
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 2.0f));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Ids[0]); // From 200: the parent is under the pointer.
        TEST_CHECK_SUCCESS(run, UIScreen_SetScale(&Fixture.Screen, 1.0f));
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Ids[1]);
        TEST_CHECK_SUCCESS(run, Widget_SetScale(Get(&Fixture, Ids[0]), 2.0f)); // The child's offset counts in its parent's scale.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK(run, UIScreen_GetHoveredWidgetId(&Fixture.Screen) == Ids[0]);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A turned slider is dragged along its own track. */
static void ATurnedSliderIsDraggedAlongItsTrack(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        UISliderOptions Options = UISliderOptions_Create();
        Options.Value.Settings.Maximum = 10.0;
        Options.Element.HasPlacement = true;
        Options.Element.Placement = UIPlacement_FromTopLeft(400.0f, 200.0f, 200.0f, 24.0f);
        uint64_t Id = WIDGET_ID_INVALID;
        TEST_CHECK_SUCCESS(run, UIKit_CreateSlider(&Fixture.Kit, &Fixture.Screen, WIDGET_ID_INVALID, &Options, &Id));
        TestUI_Tick(run, &Fixture);
        UIRenderTransform Quarter = Turned(PI_F * 0.5f, 0.5f, 0.5f);
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Id), &Quarter));
        TestUI_Tick(run, &Fixture);
        // Turned a quarter about its centre (500, 212): its start shows at the top, its end at the bottom.
        TestUI_MovePointer(&Fixture, 500.0f, 212.0f - 60.0f);
        TestUI_Press(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
        TestUI_MovePointer(&Fixture, 500.0f, 212.0f + 200.0f);
        TestUI_Tick(run, &Fixture);
        double Value = -1.0;
        TEST_CHECK_SUCCESS(run, UIValue_Get(&Fixture.Screen, Id, &Value));
        TEST_CHECK(run, Value == 10.0); // Past its end along its own track: exactly the limit.
        TestUI_MovePointer(&Fixture, 900.0f, 212.0f); // Far to the side, across its track: its share stays where it was.
        TestUI_Tick(run, &Fixture);
        TEST_CHECK_SUCCESS(run, UIValue_Get(&Fixture.Screen, Id, &Value));
        TEST_CHECK(run, fabs(Value - 5.0) < 0.6);
        TestUI_Release(&Fixture, GameKey_MouseLeft);
        TestUI_Tick(run, &Fixture);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* Rendering pushes a transform only for a widget whose transform changes something, around its whole subtree, and pops it on every
 * path: an error in a Render included. */
static void RenderingPushesOnlyWhatChangesAndBalances(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Parent = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 100.0f, 100.0f, 200.0f, 100.0f);
        uint64_t Child = AddProbe(run, &Fixture, Type, Parent, 10.0f, 10.0f, 50.0f, 50.0f);
        TestUI_Tick(run, &Fixture);
        RenderContext Context;
        RenderContext_CreateMeasuring(&Context, (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT },
            TEST_UI_SURFACE_WIDTH / TEST_UI_SURFACE_HEIGHT, RenderTargetPosition_Centered());

        // An identity transform costs nothing: no push.
        UIRenderTransform Identity = UIRenderTransform_Create();
        Identity.Pivot = (Vector2){ .x = 0.1f, .y = 0.9f };
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Identity));
        Record = (ProbeRecord){ .RenderCount = 0U };
        TEST_CHECK_SUCCESS(run, UIScreen_Render(&Fixture.Screen, &Context));
        TEST_CHECK(run, (Record.RenderCount == 2U) && (Record.Depths[0] == 0U) && (Record.Depths[1] == 0U));

        // A turned parent turns its child with it; a turned child adds its own.
        UIRenderTransform Half = Turned(0.5f, 0.5f, 0.5f);
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Half));
        Record = (ProbeRecord){ .RenderCount = 0U };
        TEST_CHECK_SUCCESS(run, UIScreen_Render(&Fixture.Screen, &Context));
        TEST_CHECK(run, (Record.Depths[0] == 1U) && (Record.Depths[1] == 1U));
        Matrix Expected = UIRenderTransform_GetMatrix(&Half, (Rectangle){ .x = 100.0f, .y = 100.0f, .width = 200.0f, .height = 100.0f }, 1.0f);
        TEST_CHECK(run, IsNear(ApplyMatrix(&Record.Transforms[1], (Vector2){ 7.0f, 9.0f }), ApplyMatrix(&Expected, (Vector2){ 7.0f, 9.0f }), PIXEL_TOLERANCE));
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Child), &Half));
        Record = (ProbeRecord){ .RenderCount = 0U };
        TEST_CHECK_SUCCESS(run, UIScreen_Render(&Fixture.Screen, &Context));
        TEST_CHECK(run, (Record.Depths[0] == 1U) && (Record.Depths[1] == 2U));
        TEST_CHECK(run, (Context._transformDepth == 0U) && (Context._clipDepth == 0U));

        // A Render that fails leaves nothing pushed, and a cut subtree's clip is popped too.
        TEST_CHECK_SUCCESS(run, Widget_SetAreSubWidgetsCut(Get(&Fixture, Parent), true));
        ((ProbeWidget*)Get(&Fixture, Child))->IsFailing = true;
        TEST_CHECK_ERROR(run, UIScreen_Render(&Fixture.Screen, &Context), ErrorCode_InvalidOperation);
        TEST_CHECK(run, (Context._transformDepth == 0U) && (Context._clipDepth == 0U));
        ((ProbeWidget*)Get(&Fixture, Child))->IsFailing = false;
        ((ProbeWidget*)Get(&Fixture, Parent))->IsFailing = true;
        TEST_CHECK_ERROR(run, UIScreen_Render(&Fixture.Screen, &Context), ErrorCode_InvalidOperation);
        TEST_CHECK(run, (Context._transformDepth == 0U) && (Context._clipDepth == 0U));
        ((ProbeWidget*)Get(&Fixture, Parent))->IsFailing = false;
        RenderContext_Deconstruct(&Context);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* A tree as deep as a tree goes, every level turned: drawing composes on the context's own stack and still balances. */
static void ADeepTurnedTreeBalances(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Parent = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 10.0f, 10.0f, 900.0f, 450.0f);
        UIRenderTransform Small = Turned(0.01f, 0.5f, 0.5f);
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Small));
        for (size_t Level = 1U; Level < WIDGET_MAX_TREE_DEPTH; Level++)
        {
            Parent = AddProbe(run, &Fixture, Type, Parent, 1.0f, 1.0f, 800.0f, 400.0f);
            TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Parent), &Small));
        }
        TestUI_Tick(run, &Fixture);
        RenderContext Context;
        RenderContext_CreateMeasuring(&Context, (Vector2){ .x = TEST_UI_SURFACE_WIDTH, .y = TEST_UI_SURFACE_HEIGHT },
            TEST_UI_SURFACE_WIDTH / TEST_UI_SURFACE_HEIGHT, RenderTargetPosition_Centered());
        Record = (ProbeRecord){ .RenderCount = 0U };
        TEST_CHECK_SUCCESS(run, UIScreen_Render(&Fixture.Screen, &Context));
        TEST_CHECK(run, (Record.RenderCount == WIDGET_MAX_TREE_DEPTH) && (Context._transformDepth == 0U));
        Widget* Top = NULL;
        TEST_CHECK(run, UIScreen_TryGetTopmostAt(&Fixture.Screen, (Vector2){ .x = 0.5f, .y = 0.5f }, &Top));
        RenderContext_Deconstruct(&Context);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The hit test hook stands in for the box for its widget alone, with the point on the screen and in the box. */
static void TheHitTestHookStandsInForTheBox(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Behind = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 0.0f, 0.0f, 1000.0f, 500.0f);
        uint64_t Front = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 100.0f, 100.0f, 200.0f, 100.0f);
        TestUI_Tick(run, &Fixture);
        ((ProbeWidget*)Get(&Fixture, Front))->HasHitTest = true;
        Record = (ProbeRecord){ .HitTestCount = 0U };
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 150.0f, .y = 150.0f }) == Front);
        TEST_CHECK(run, Record.LastHitArgs.IsInsideBox && IsNear(Record.LastHitArgs.LocalPosition, (Vector2){ 0.25f, 0.5f }, 0.001f));
        TEST_CHECK(run, IsNear(Record.LastHitArgs.ScreenPosition, ToScreen((Vector2){ 150.0f, 150.0f }), 0.0001f));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 250.0f, .y = 150.0f }) == Behind); // Its right half is not it.
        ((ProbeWidget*)Get(&Fixture, Front))->IsHitEverywhere = true;
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 900.0f, .y = 400.0f }) == Front); // A world object answers past its box.
        TEST_CHECK(run, !Record.LastHitArgs.IsInsideBox);
        // Through a turn the box's point is the turned one.
        UIRenderTransform Half = Turned(PI_F, 0.5f, 0.5f);
        ((ProbeWidget*)Get(&Fixture, Front))->IsHitEverywhere = false;
        TEST_CHECK_SUCCESS(run, Widget_SetRenderTransform(Get(&Fixture, Front), &Half));
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 250.0f, .y = 150.0f }) == Front); // Turned half way, its left half is at the right.
        TEST_CHECK(run, TopmostAt(&Fixture, (Vector2){ .x = 150.0f, .y = 150.0f }) == Behind);
    }
    TestUI_Deconstruct(run, &Fixture);
}

/* The transform's values and the scale are typed properties: animated, read back, refused when not numbers. */
static void TheTransformAndTheScaleAreProperties(TestRun* run)
{
    TestUIFixture Fixture;
    if (TestUI_Construct(run, &Fixture))
    {
        uint64_t Type = RegisterProbe(run, &Fixture);
        uint64_t Id = AddProbe(run, &Fixture, Type, WIDGET_ID_INVALID, 100.0f, 100.0f, 200.0f, 100.0f);
        TestUI_Tick(run, &Fixture);
        Widget* Target = Get(&Fixture, Id);
        UIPropertyValue Value;
        TEST_CHECK_SUCCESS(run, Widget_GetProperty(Target, WidgetBaseProperty_Rotation, &Value));
        TEST_CHECK(run, (Value.Type == UIPropertyType_Float) && (Value.Data.FloatValue == 0.0f) && !Widget_HasRenderTransform(Target));
        UIPropertyValue Offset = UIPropertyValue_FromVector2((Vector2){ .x = 5.0f, .y = 6.0f });
        TEST_CHECK_SUCCESS(run, Widget_SetProperty(Target, WidgetBaseProperty_VisualOffset, &Offset));
        TEST_CHECK(run, Widget_HasRenderTransform(Target) && (Widget_GetRenderTransform(Target).Offset.y == 6.0f));
        UIPropertyValue NotANumber = UIPropertyValue_FromFloat(NAN);
        TEST_CHECK_ERROR(run, Widget_SetProperty(Target, WidgetBaseProperty_Rotation, &NotANumber), ErrorCode_ArgumentOutOfRange);
        TEST_CHECK_ERROR(run, Widget_SetProperty(Target, WidgetBaseProperty_Scale, &NotANumber), ErrorCode_ArgumentOutOfRange);
        UIPropertyValue Huge = UIPropertyValue_FromVector2((Vector2){ .x = 1e9f, .y = 1.0f });
        TEST_CHECK_ERROR(run, Widget_SetProperty(Target, WidgetBaseProperty_VisualScale, &Huge), ErrorCode_ArgumentOutOfRange);
        UIPropertyValue Pivot = UIPropertyValue_FromVector2((Vector2){ .x = 0.0f, .y = 1.0f });
        TEST_CHECK_SUCCESS(run, Widget_SetProperty(Target, WidgetBaseProperty_Pivot, &Pivot));
        TEST_CHECK_SUCCESS(run, Widget_GetProperty(Target, WidgetBaseProperty_Pivot, &Value));
        TEST_CHECK(run, (Value.Type == UIPropertyType_Vector2) && (Value.Data.Vector2Value.y == 1.0f));

        // A turn animated from 0 to a half over a second of ticks.
        UIKeyframe Frames[2] =
        {
            { .Time = 0.0, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .FloatValue = 0.0f } },
            { .Time = 1.0, .Interpolation = UIInterpolation_Linear, .CustomEasing = NULL, .Value = { .FloatValue = PI_F } }
        };
        uint64_t Animation = 0U;
        TEST_CHECK_SUCCESS(run, UIScreen_StartAnimation(&Fixture.Screen, Id, WidgetBaseProperty_Rotation, UIPropertyType_Float, Frames, 2U,
            UIAnimationOptions_CreateDefault(), &Animation));
        TestUI_Ticks(run, &Fixture, 240U);
        float Rotation = Widget_GetRenderTransform(Get(&Fixture, Id)).Rotation;
        TEST_CHECK(run, (Rotation > 1.4f) && (Rotation < 1.75f));
    }
    TestUI_Deconstruct(run, &Fixture);
}


// Public functions.
void UITransformTests_Run(TestRun* run)
{
    static const TestCase Cases[] =
    {
        { "the_matrix_and_the_points_agree", TheMatrixAndThePointsAgree },
        { "the_renderer_composes_and_balances", TheRendererComposesAndBalances },
        { "a_turned_widget_is_hit_where_it_shows", ATurnedWidgetIsHitWhereItShows },
        { "the_offset_and_scale_move_the_hit_and_zero_holds_none", TheOffsetAndScaleMoveTheHitAndZeroHoldsNone },
        { "a_turned_slider_is_dragged_along_its_track", ATurnedSliderIsDraggedAlongItsTrack },
        { "a_scale_change_moves_the_hover_with_the_offset", AScaleChangeMovesTheHoverWithTheOffset },
        { "rendering_pushes_only_what_changes_and_balances", RenderingPushesOnlyWhatChangesAndBalances },
        { "a_deep_turned_tree_balances", ADeepTurnedTreeBalances },
        { "the_hit_test_hook_stands_in_for_the_box", TheHitTestHookStandsInForTheBox },
        { "the_transform_and_the_scale_are_properties", TheTransformAndTheScaleAreProperties },
    };
    TestRun_RunSuite(run, "UITransform", Cases, sizeof(Cases) / sizeof(Cases[0]));
}
