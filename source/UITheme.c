#include "UITheme.h"
#include "UIThemeInternal.h"
#include "wr/WRString.h"


// Macros.
/** A metric row of a part table: a vector with its default, or a number with its default, each within [0, UI_THEME_MAX_METRIC]; or a
 *  vector whose components may also be negative, within [-UI_THEME_MAX_METRIC, UI_THEME_MAX_METRIC]: an offset that points either way. */
#define VECTOR_PART(key, x, y) { .Key = u8##key, .Kind = UIThemePartKind_Vector, .DefaultVector = { (x), (y) }, .DefaultNumber = 0.0f, \
    .Minimum = 0.0f, .Maximum = UI_THEME_MAX_METRIC }
#define NUMBER_PART(key, value) { .Key = u8##key, .Kind = UIThemePartKind_Number, .DefaultVector = { 0.0f, 0.0f }, .DefaultNumber = (value), \
    .Minimum = 0.0f, .Maximum = UI_THEME_MAX_METRIC }
#define SIGNED_VECTOR_PART(key, x, y) { .Key = u8##key, .Kind = UIThemePartKind_Vector, .DefaultVector = { (x), (y) }, .DefaultNumber = 0.0f, \
    .Minimum = -UI_THEME_MAX_METRIC, .Maximum = UI_THEME_MAX_METRIC }
/** A part's row: a box or a colour per state (no bounds: they are no metric). */
#define BOX_PART(key) { .Key = u8##key, .Kind = UIThemePartKind_Box, .DefaultVector = { 0.0f, 0.0f }, .DefaultNumber = 0.0f, .Minimum = 0.0f, \
    .Maximum = 0.0f }
#define COLORS_PART(key) { .Key = u8##key, .Kind = UIThemePartKind_Colors, .DefaultVector = { 0.0f, 0.0f }, .DefaultNumber = 0.0f, \
    .Minimum = 0.0f, .Maximum = 0.0f }


// Fields.
/* Each kind's values, in its part enum's order. */
static const UIThemePartDescriptor PanelParts[UIPanelPart_Count] =
{
    [UIPanelPart_Padding] = VECTOR_PART("padding", 0.0f, 0.0f),
    [UIPanelPart_Spacing] = NUMBER_PART("spacing", 0.0f),
    [UIPanelPart_MinSize] = VECTOR_PART("min_size", 0.0f, 0.0f),
    [UIPanelPart_Background] = BOX_PART("background"),
};
static const UIThemePartDescriptor LabelParts[UILabelPart_Count] =
{
    [UILabelPart_Text] = COLORS_PART("text"),
};
static const UIThemePartDescriptor ButtonParts[UIButtonPart_Count] =
{
    [UIButtonPart_Padding] = VECTOR_PART("padding", 8.0f, 4.0f),
    [UIButtonPart_MinSize] = VECTOR_PART("min_size", 24.0f, 24.0f),
    [UIButtonPart_Background] = BOX_PART("background"),
    [UIButtonPart_Text] = COLORS_PART("text"),
};
static const UIThemePartDescriptor ToggleParts[UITogglePart_Count] =
{
    [UITogglePart_BoxSize] = VECTOR_PART("box_size", 16.0f, 16.0f),
    [UITogglePart_Spacing] = NUMBER_PART("spacing", 6.0f),
    [UITogglePart_MinSize] = VECTOR_PART("min_size", 16.0f, 24.0f),
    [UITogglePart_Box] = BOX_PART("box"),
    [UITogglePart_Check] = BOX_PART("check"),
    [UITogglePart_Text] = COLORS_PART("text"),
};
static const UIThemePartDescriptor SliderParts[UISliderPart_Count] =
{
    [UISliderPart_MinSize] = VECTOR_PART("min_size", 64.0f, 24.0f),
    [UISliderPart_ThumbSize] = VECTOR_PART("thumb_size", 12.0f, 20.0f),
    [UISliderPart_TrackThickness] = NUMBER_PART("track_thickness", 4.0f),
    [UISliderPart_Track] = BOX_PART("track"),
    [UISliderPart_Fill] = BOX_PART("fill"),
    [UISliderPart_Thumb] = BOX_PART("thumb"),
};
static const UIThemePartDescriptor TooltipParts[UITooltipPart_Count] =
{
    [UITooltipPart_Padding] = VECTOR_PART("padding", 6.0f, 3.0f),
    [UITooltipPart_Offset] = SIGNED_VECTOR_PART("offset", 12.0f, 18.0f),
    [UITooltipPart_MaxWidth] = NUMBER_PART("max_width", 320.0f),
    [UITooltipPart_Background] = BOX_PART("background"),
    [UITooltipPart_Text] = COLORS_PART("text"),
};

static const struct
{
    const unsigned char* Name;
    const UIThemePartDescriptor* Parts;
    size_t PartCount;
}
Kinds[UIElementKind_Count] =
{
    [UIElementKind_Panel] = { u8"panel", PanelParts, UIPanelPart_Count },
    [UIElementKind_Label] = { u8"label", LabelParts, UILabelPart_Count },
    [UIElementKind_Button] = { u8"button", ButtonParts, UIButtonPart_Count },
    [UIElementKind_Toggle] = { u8"toggle", ToggleParts, UITogglePart_Count },
    [UIElementKind_Slider] = { u8"slider", SliderParts, UISliderPart_Count },
    [UIElementKind_Tooltip] = { u8"tooltip", TooltipParts, UITooltipPart_Count },
};


// Static functions.
static bool IsKindValid(UIElementKind kind)
{
    return ((int32_t)kind >= 0) && ((int32_t)kind < (int32_t)UIElementKind_Count);
}

/* The value at @p part, when a theme gave it and it is of that kind. */
static const UIThemeValue* GetSetValue(const UIThemeStyle* self, int32_t part, UIThemePartKind partKind)
{
    if ((self == NULL) || (part < 0) || ((size_t)part >= UI_THEME_MAX_PARTS) || !self->Values[part].IsSet
        || (self->Values[part].Kind != partKind))
    {
        return NULL;
    }
    return &self->Values[part];
}

static bool IsPartOfKind(UIElementKind kind, int32_t part, UIThemePartKind partKind)
{
    const UIThemePartDescriptor* Descriptor = UIElementKind_GetPart(kind, part);
    return (Descriptor != NULL) && (Descriptor->Kind == partKind);
}

/* The custom value under @p key, when the style has one and it is of that kind. */
static const UIThemeCustomValue* FindCustomValue(const UIThemeStyle* self, const unsigned char* key, UIThemeCustomKind kind)
{
    if ((self == NULL) || (key == NULL))
    {
        return NULL;
    }
    for (size_t Index = 0U; Index < self->CustomValueCount; Index++)
    {
        const UIThemeCustomValue* Value = &self->CustomValues[Index];
        bool IsSame = false;
        Error Compare = StringUTF8_EqualsExact(Value->Key, key, &IsSame);
        Error_Deconstruct(&Compare);
        if (IsSame)
        {
            return (Value->Kind == kind) ? Value : NULL;
        }
    }
    return NULL;
}


// Public functions: the element kinds.
const unsigned char* UIElementKind_GetName(UIElementKind kind)
{
    return IsKindValid(kind) ? Kinds[kind].Name : NULL;
}

size_t UIElementKind_GetPartCount(UIElementKind kind)
{
    return IsKindValid(kind) ? Kinds[kind].PartCount : 0U;
}

const UIThemePartDescriptor* UIElementKind_GetPart(UIElementKind kind, int32_t part)
{
    if (!IsKindValid(kind) || (part < 0) || ((size_t)part >= Kinds[kind].PartCount))
    {
        return NULL;
    }
    return &Kinds[kind].Parts[part];
}


// Public functions: the theme.
const unsigned char* UITheme_GetName(const UITheme* self)
{
    return self->Name;
}

const unsigned char* UITheme_GetFontName(const UITheme* self)
{
    return (self != NULL) ? self->FontName : NULL;
}

float UITheme_GetTextSize(const UITheme* self)
{
    return (self != NULL) ? self->TextSize : UI_THEME_DEFAULT_TEXT_SIZE;
}

float UITheme_GetTransitionSeconds(const UITheme* self)
{
    return (self != NULL) ? self->TransitionSeconds : 0.0f;
}

float UITheme_GetTooltipDelaySeconds(const UITheme* self)
{
    return (self != NULL) ? self->TooltipDelaySeconds : UI_THEME_DEFAULT_TOOLTIP_DELAY_SECONDS;
}

const unsigned char* UITheme_GetLookName(const UITheme* self, UIElementKind kind)
{
    if ((self == NULL) || !IsKindValid(kind) || !self->Elements[kind].IsPresent || !self->Elements[kind].HasLook)
    {
        return NULL;
    }
    return self->Elements[kind].LookName;
}

const UIThemeStyle* UITheme_FindStyle(const UITheme* self, UIElementKind kind, const unsigned char* variant)
{
    if ((self == NULL) || !IsKindValid(kind) || !self->Elements[kind].IsPresent)
    {
        return NULL;
    }
    const UIThemeElement* Element = &self->Elements[kind];
    if ((variant != NULL) && (variant[0] != 0U))
    {
        for (size_t Index = 0U; Index < Element->VariantCount; Index++)
        {
            bool IsSame = false;
            Error Compare = StringUTF8_EqualsExact(Element->Variants[Index].Name, variant, &IsSame);
            Error_Deconstruct(&Compare);
            if (IsSame)
            {
                return &Element->Variants[Index].Style;
            }
        }
    }
    return &Element->Base;
}

const UIBoxStyle* UIThemeStyle_GetBox(const UIThemeStyle* self, int32_t part)
{
    const UIThemeValue* Value = GetSetValue(self, part, UIThemePartKind_Box);
    return (Value != NULL) ? &Value->Data.Box : NULL;
}

const UIStateColors* UIThemeStyle_GetColors(const UIThemeStyle* self, int32_t part)
{
    const UIThemeValue* Value = GetSetValue(self, part, UIThemePartKind_Colors);
    return (Value != NULL) ? &Value->Data.Colors : NULL;
}

Vector2 UIThemeStyle_GetVector(const UIThemeStyle* self, UIElementKind kind, int32_t part)
{
    if (!IsPartOfKind(kind, part, UIThemePartKind_Vector))
    {
        return (Vector2){ .x = 0.0f, .y = 0.0f };
    }
    const UIThemeValue* Value = GetSetValue(self, part, UIThemePartKind_Vector);
    return (Value != NULL) ? Value->Data.Vector : Kinds[kind].Parts[part].DefaultVector;
}

float UIThemeStyle_GetNumber(const UIThemeStyle* self, UIElementKind kind, int32_t part)
{
    if (!IsPartOfKind(kind, part, UIThemePartKind_Number))
    {
        return 0.0f;
    }
    const UIThemeValue* Value = GetSetValue(self, part, UIThemePartKind_Number);
    return (Value != NULL) ? Value->Data.Number : Kinds[kind].Parts[part].DefaultNumber;
}

float UIThemeStyle_FindCustomNumber(const UIThemeStyle* self, const unsigned char* key, float defaultValue)
{
    const UIThemeCustomValue* Value = FindCustomValue(self, key, UIThemeCustomKind_Number);
    return (Value != NULL) ? Value->Data.Number : defaultValue;
}

Vector2 UIThemeStyle_FindCustomVector(const UIThemeStyle* self, const unsigned char* key, Vector2 defaultValue)
{
    const UIThemeCustomValue* Value = FindCustomValue(self, key, UIThemeCustomKind_Vector);
    return (Value != NULL) ? Value->Data.Vector : defaultValue;
}

Color UIThemeStyle_FindCustomColor(const UIThemeStyle* self, const unsigned char* key, Color defaultValue)
{
    const UIThemeCustomValue* Value = FindCustomValue(self, key, UIThemeCustomKind_Color);
    return (Value != NULL) ? Value->Data.Color : defaultValue;
}

const UIBoxStyle* UIThemeStyle_FindCustomBox(const UIThemeStyle* self, const unsigned char* key)
{
    const UIThemeCustomValue* Value = FindCustomValue(self, key, UIThemeCustomKind_Box);
    return (Value != NULL) ? &Value->Data.Box : NULL;
}
