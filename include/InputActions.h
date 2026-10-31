#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wr/WRError.h"
#include "wr/WRJSON.h"
#include "AssetManager.h"
#include "GameInput.h"
#include "Logger.h"


/**
 * @file InputActions.h
 * @brief The input_actions asset: one action category with its actions, and overrides of registered actions and
 *        categories, as data a pack writes; and the startup step that registers every definition with the input.
 *
 * An input_actions definition (JSON in a resource pack's input_actions/ folder, references/asset_structure.md "Input
 * actions") is a category: its name is the category's key, and each action's key is the category's namespace, a colon
 * and the action's name ("mymod:journal" declares "mymod:open_journal"). Its overrides change actions and categories
 * that others registered, the engine's included (GameInput_OverrideAction). Every rule is checked when the definition
 * is read, so a broken file is skipped at startup with a message naming the path to the bad value; unknown keys are
 * refused everywhere.
 *
 * REGISTRATION (InputActions_RegisterDefinitions, once at startup, after the engine's actions and before the controls
 * file). Every definition registers under its own key as the source, so withdrawing one definition is one call
 * (GameInput_WithdrawSource) and two definitions that override one action are two sources (the second is refused).
 * Two passes, so an override does not depend on the order of the names: first each definition's category and actions
 * register, all or nothing; then each registered definition's overrides apply, all or nothing.
 *
 * OWNERSHIP. The asset is plain data with owned strings; the asset manager owns it (load one with
 * AssetManager_LoadInputActions). The input keeps its own copies of what registers, so the definitions are released
 * right after.
 */


// Macros.
/** @brief The most actions one definition declares. */
#define INPUT_ACTIONS_MAX_ACTIONS ((size_t)256)
/** @brief The most action overrides one definition holds. */
#define INPUT_ACTIONS_MAX_OVERRIDES ((size_t)256)
/** @brief The most category overrides one definition holds. */
#define INPUT_ACTIONS_MAX_CATEGORY_OVERRIDES ((size_t)256)
/** @brief A definition's category order when it gives none (after the engine's categories). */
#define INPUT_ACTIONS_DEFAULT_ORDER ((int32_t)1000)


// Types.
/** @brief One action a definition declares. */
typedef struct InputActionsActionStruct
{
    /** @brief The action's full key: the category's namespace, a colon and the action's name. Owned. */
    unsigned char* Key;
    /** @brief The display name (GameActionInfo's rules). Owned. */
    unsigned char* DisplayName;
    /** @brief The description; empty when the file gives none. Owned. */
    unsigned char* Description;
    /** @brief The default bindings; the first DefaultBindingCount are used. */
    GameInputBinding DefaultBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    /** @brief How many default bindings. */
    size_t DefaultBindingCount;
    /** @brief The kind ("button"). */
    GameActionKind Kind;
    /** @brief Whether it always keeps a binding (it has a default binding then). */
    bool KeepsABinding;
} InputActionsAction;

/** @brief One override of an action (GameActionOverride with owned strings and its target's key). */
typedef struct InputActionsOverrideStruct
{
    /** @brief The full key of the action it changes. Owned. */
    unsigned char* Key;
    /** @brief Whether DisplayName replaces the action's display name. */
    bool OverridesDisplayName;
    /** @brief The new display name, or NULL. Owned. */
    unsigned char* DisplayName;
    /** @brief Whether Description replaces the action's description. */
    bool OverridesDescription;
    /** @brief The new description, or NULL. Owned. */
    unsigned char* Description;
    /** @brief Whether DefaultBindings replace the action's defaults. */
    bool OverridesDefaultBindings;
    /** @brief The new defaults; the first DefaultBindingCount are used. */
    GameInputBinding DefaultBindings[GAME_INPUT_MAX_BINDINGS_PER_ACTION];
    /** @brief How many new defaults. */
    size_t DefaultBindingCount;
} InputActionsOverride;

/** @brief One override of a category (GameActionCategoryOverride with owned strings and its target's key). */
typedef struct InputActionsCategoryOverrideStruct
{
    /** @brief The full key of the category it changes. Owned. */
    unsigned char* Key;
    /** @brief Whether DisplayName replaces the category's display name. */
    bool OverridesDisplayName;
    /** @brief The new display name, or NULL. Owned. */
    unsigned char* DisplayName;
    /** @brief Whether Description replaces the category's description. */
    bool OverridesDescription;
    /** @brief The new description, or NULL. Owned. */
    unsigned char* Description;
    /** @brief Whether Order replaces the category's order. */
    bool OverridesOrder;
    /** @brief The new order. */
    int32_t Order;
} InputActionsCategoryOverride;

/**
 * @brief An input_actions asset: a category, its actions in file order, and its overrides in file order.
 *
 * Read-only for everyone but its reader: the asset manager owns it.
 */
typedef struct InputActionsStruct
{
    /** @brief The category's full key: the definition's name. Owned. */
    unsigned char* Name;
    /** @brief The category's display name. Owned. */
    unsigned char* DisplayName;
    /** @brief The category's description; empty when the file gives none. Owned. */
    unsigned char* Description;
    /** @brief The category's order (INPUT_ACTIONS_DEFAULT_ORDER when the file gives none). */
    int32_t Order;
    /** @brief The actions, ActionCount of them. Owned. */
    InputActionsAction* Actions;
    /** @brief How many actions (at most INPUT_ACTIONS_MAX_ACTIONS). */
    size_t ActionCount;
    /** @brief The action overrides, OverrideCount of them. Owned. */
    InputActionsOverride* Overrides;
    /** @brief How many action overrides (at most INPUT_ACTIONS_MAX_OVERRIDES). */
    size_t OverrideCount;
    /** @brief The category overrides, CategoryOverrideCount of them. Owned. */
    InputActionsCategoryOverride* CategoryOverrides;
    /** @brief How many category overrides (at most INPUT_ACTIONS_MAX_CATEGORY_OVERRIDES). */
    size_t CategoryOverrideCount;
} InputActions;


// Functions.
/**
 * @brief Reads an input_actions definition's JSON, checking every rule.
 * @param name The definition's full name (its category's key), as the asset manager read it; must not be NULL.
 * @param root The definition's root object; must not be NULL.
 * @param outActions [out] Receives the new asset (free with InputActions_Destroy); NULL on failure. Must not be NULL.
 * @returns Success; ErrorCode_InvalidAssetDefinition or ErrorCode_InvalidJSON naming the path to the bad value (an
 *          unknown key, a missing or bad display name, a bad description, order, action name, key, binding or kind,
 *          too many actions, bindings or overrides, an override that changes nothing, keeps_a_binding without a
 *          default binding).
 */
Error InputActions_ReadJSON(const unsigned char* name, JSONCompound* root, InputActions** outActions);

/**
 * @brief Copies an asset (every string and array).
 * @param self The asset; must not be NULL.
 * @returns The copy (free with InputActions_Destroy).
 */
InputActions* InputActions_Clone(const InputActions* self);

/**
 * @brief Frees an asset and everything it owns. Safe on NULL.
 * @param self The asset, or NULL.
 */
void InputActions_Destroy(InputActions* self);

/**
 * @brief Registers a definition's category and actions with the input, all or nothing, under the category's key as
 *        the source.
 * @param self The asset; must not be NULL.
 * @param input The input; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation if the category or an action key is registered already (the message
 *          names it and its source); otherwise the input's error. Nothing stays registered on failure.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error InputActions_RegisterCategoryAndActions(const InputActions* self, GameInput* input);

/**
 * @brief Applies a definition's overrides to the input, all or nothing, under the category's key as the source.
 *
 * Every target is checked first: it must be registered, and no other source may override it.
 * @param self The asset; must not be NULL.
 * @param input The input; must not be NULL.
 * @returns Success; ErrorCode_InvalidOperation naming the first target that is not registered, is changed by another
 *          source, or keeps a binding while the override empties its defaults. Nothing changes on failure.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error InputActions_ApplyOverrides(const InputActions* self, GameInput* input);

/**
 * @brief Registers every input_actions definition with the input: lists them in byte order of their names, loads each
 *        under an asset user of its own, registers each one's category and actions (all or nothing per definition),
 *        then applies each registered one's overrides (all or nothing per definition), and releases them.
 *
 * A definition that fails to load or register is logged once and skipped; the others still register. A summary line
 * is logged.
 * @param assets The asset manager with the standard types; must not be NULL.
 * @param input The input; must not be NULL.
 * @param logger The logger, or NULL to log nothing.
 * @returns Success (also when some definitions were skipped); ErrorCode_IllegalArgument for a NULL argument; the asset
 *          manager's error when the definitions cannot be listed or the asset user cannot be made.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error InputActions_RegisterDefinitions(AssetManager* assets, GameInput* input, Logger* logger);
