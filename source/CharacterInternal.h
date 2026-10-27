#pragma once
#include "CharacterAsset.h"
#include "CollisionLayer.h"
#include "CollisionShape.h"
#include "PropertyDescriptor.h"

/*
 * Private header (source/): the property rows of a character's settings, shared by the character asset's table
 * (CharacterAsset.c: the definition's keys) and the character component's table (WorldCharacterComponent.c: a placement's
 * own values behind their override flags). Each macro holds a row's key, type, default, rules, flags and texts, so a
 * setting has them once wherever it is stored; a table's row adds its wire id, its member (PROPERTY_MEMBER) and its
 * SinceVersion:
 *
 *     { CHARACTER_ROW_RADIUS, .WireId = 3U, PROPERTY_MEMBER(CharacterSettings, Radius), .SinceVersion = 1U }
 *
 * Both tables give a setting the same wire id: CHARACTER_VALUE_WIRE_ID (the component puts each override flag on the id
 * just before, CHARACTER_FLAG_WIRE_ID).
 */


// Macros: the wire ids. Setting i's flag is 2 + 2i and its value 3 + 2i on the component (1 is the character's name);
// the asset's table uses the value ids. Never renumber: the ids are the save schema.
#define CHARACTER_FLAG_WIRE_ID(setting) ((uint16_t)(2U + 2U * (unsigned int)(setting)))
#define CHARACTER_VALUE_WIRE_ID(setting) ((uint16_t)(3U + 2U * (unsigned int)(setting)))


// Macros: the inspector's categories.
#define CHARACTER_CATEGORY_BODY ((const unsigned char*)u8"Body")
#define CHARACTER_CATEGORY_WALKING ((const unsigned char*)u8"Walking")
#define CHARACTER_CATEGORY_TURNING ((const unsigned char*)u8"Turning")
#define CHARACTER_CATEGORY_CROUCHING ((const unsigned char*)u8"Crouching")
#define CHARACTER_CATEGORY_JUMPING ((const unsigned char*)u8"Jumping")
#define CHARACTER_CATEGORY_FALLING ((const unsigned char*)u8"Falling")
#define CHARACTER_CATEGORY_FLYING ((const unsigned char*)u8"Flying")
#define CHARACTER_CATEGORY_INTERACTION ((const unsigned char*)u8"Interaction")


// Macros: the rules.
/** A length above 0. */
#define CHARACTER_RULES_SIZE \
    { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, .Maximum = CHARACTER_MAX_SIZE }
/** A length of at least 0. */
#define CHARACTER_RULES_LENGTH PROPERTY_RULES_BETWEEN(0.0, CHARACTER_MAX_SIZE)
/** A speed of at least 0. */
#define CHARACTER_RULES_SPEED PROPERTY_RULES_BETWEEN(0.0, CHARACTER_MAX_SPEED)
/** A speed above 0. */
#define CHARACTER_RULES_POSITIVE_SPEED \
    { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, .Maximum = CHARACTER_MAX_SPEED }
/** An acceleration of at least 0. */
#define CHARACTER_RULES_ACCELERATION PROPERTY_RULES_BETWEEN(0.0, CHARACTER_MAX_ACCELERATION)
/** An acceleration above 0. */
#define CHARACTER_RULES_POSITIVE_ACCELERATION \
    { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, \
        .Maximum = CHARACTER_MAX_ACCELERATION }
/** A time of at least 0. */
#define CHARACTER_RULES_SECONDS PROPERTY_RULES_BETWEEN(0.0, CHARACTER_MAX_SECONDS)
/** The largest turn speed, radians per second (about 160 turns a second). */
#define CHARACTER_MAX_TURN_SPEED 1000.0
/** The largest gravity multiplier. */
#define CHARACTER_MAX_GRAVITY_MULTIPLIER 100.0
/** A quarter turn in radians: the slope limit stays below it. */
#define CHARACTER_QUARTER_TURN 1.5707963267948966
/** The largest air drag, per metre (a terminal speed of a third of a metre per second on Earth). */
#define CHARACTER_MAX_AIR_DRAG 100.0


// Macros: the rows (in the table's order, CharacterSetting's).
#define CHARACTER_ROW_RADIUS \
    .Key = u8"radius", .Type = PropertyType_Float, .Default.FloatValue = CHARACTER_DEFAULT_RADIUS, \
    .Rules = CHARACTER_RULES_SIZE, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_BODY, \
    .DisplayName = u8"Radius", \
    .Description = u8"The radius of the character's upright capsule, in metres: how close it comes to walls and how " \
        u8"narrow a gap it fits through."

#define CHARACTER_ROW_HEIGHT \
    .Key = u8"height", .Type = PropertyType_Float, .Default.FloatValue = CHARACTER_DEFAULT_HEIGHT, \
    .Rules = CHARACTER_RULES_SIZE, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_BODY, \
    .DisplayName = u8"Height", \
    .Description = u8"The standing capsule's whole height in metres, from the feet to the top of the head (at least " \
        u8"twice the radius)."

#define CHARACTER_ROW_EYE_HEIGHT \
    .Key = u8"eye_height", .Type = PropertyType_Float, .Default.FloatValue = CHARACTER_DEFAULT_EYE_HEIGHT, \
    .Rules = CHARACTER_RULES_LENGTH, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_BODY, \
    .DisplayName = u8"Eye height", \
    .Description = u8"How high the eyes are above the feet while standing, in metres: where the player's camera sits " \
        u8"and where the character looks from."

#define CHARACTER_ROW_MASS \
    .Key = u8"mass", .Type = PropertyType_Float, .Default.FloatValue = CHARACTER_DEFAULT_MASS, \
    .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, \
        .Maximum = COLLISION_MAX_MASS }, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_BODY, .DisplayName = u8"Mass", \
    .Description = u8"Kilograms. Characters and crates push each other by mass: equal masses give way equally, a heavy " \
        u8"one barely moves."

#define CHARACTER_ROW_IS_PUSHABLE \
    .Key = u8"is_pushable", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_BODY, .DisplayName = u8"Pushable", \
    .Description = u8"Whether others push it (other characters, doors, platforms). Off, it is never pushed: it pushes " \
        u8"others and stops when it cannot."

#define CHARACTER_ROW_COLLIDES_WITH \
    .Key = u8"collides_with", .Type = PropertyType_EnumSet, .Default.EnumSetValue = CHARACTER_DEFAULT_COLLIDES_WITH, \
    .EnumTable = &COLLISION_LAYER_ENUM, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_BODY, \
    .DisplayName = u8"Collides with", \
    .Description = u8"The collision layers the character collides with; things on other layers let it through."

#define CHARACTER_ROW_WALK_SPEED \
    .Key = u8"walk_speed", .Type = PropertyType_Float, .Default.FloatValue = 1.4f, .Rules = CHARACTER_RULES_SPEED, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Walking speed", \
    .Description = u8"Metres per second."

#define CHARACTER_ROW_CAN_RUN \
    .Key = u8"can_run", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Can run", \
    .Description = u8"Whether the character may run; off, asking to run walks."

#define CHARACTER_ROW_RUN_SPEED \
    .Key = u8"run_speed", .Type = PropertyType_Float, .Default.FloatValue = 3.5f, .Rules = CHARACTER_RULES_SPEED, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Running speed", \
    .Description = u8"Metres per second."

#define CHARACTER_ROW_ACCELERATION \
    .Key = u8"acceleration", .Type = PropertyType_Float, .Default.FloatValue = 8.0f, \
    .Rules = CHARACTER_RULES_POSITIVE_ACCELERATION, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Acceleration", \
    .Description = u8"How hard its movement keys push it on the ground, in metres per second squared, in whatever " \
        u8"direction they point: a turn back too."

#define CHARACTER_ROW_GROUND_FRICTION \
    .Key = u8"ground_friction", .Type = PropertyType_Float, .Default.FloatValue = 10.0f, \
    .Rules = CHARACTER_RULES_ACCELERATION, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_WALKING, \
    .DisplayName = u8"Ground friction", \
    .Description = u8"How quickly the ground slows it, in metres per second squared: it stops when the keys let go, " \
        u8"and loses speed the keys do not drive. 0 slides like ice."

#define CHARACTER_ROW_STEP_HEIGHT \
    .Key = u8"step_height", .Type = PropertyType_Float, .Default.FloatValue = 0.35f, .Rules = CHARACTER_RULES_LENGTH, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Step height", \
    .Description = u8"The highest step it walks up, in metres; anything higher is a wall."

#define CHARACTER_ROW_SLOPE_LIMIT \
    .Key = u8"slope_limit", .Type = PropertyType_Angle, .Default.FloatValue = 0.78539816f, \
    .Rules = PROPERTY_RULES_HALF_OPEN(0.0, CHARACTER_QUARTER_TURN), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Slope limit", \
    .Description = u8"The steepest ground it walks on, in degrees (below 90); steeper ground is a wall it slides down."

#define CHARACTER_ROW_GROUND_SNAP_DISTANCE \
    .Key = u8"ground_snap_distance", .Type = PropertyType_Float, .Default.FloatValue = 0.0f, \
    .Rules = CHARACTER_RULES_LENGTH, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_WALKING, \
    .DisplayName = u8"Step down", \
    .Description = u8"How far down, in metres, it steps without falling (a stair's riser, at once); 0 falls off " \
        u8"every step. Walkable slopes are followed whatever this is."

#define CHARACTER_ROW_TURN_SPEED \
    .Key = u8"turn_speed", .Type = PropertyType_Angle, .Default.FloatValue = 6.28318531f, \
    .Rules = { .HasMinimum = true, .IsMinimumExclusive = true, .Minimum = 0.0, .HasMaximum = true, \
        .Maximum = CHARACTER_MAX_TURN_SPEED }, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_TURNING, .DisplayName = u8"Turn speed", \
    .Description = u8"How fast its body turns, in degrees per second."

#define CHARACTER_ROW_IS_TURN_INSTANT \
    .Key = u8"is_turn_instant", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_TURNING, .DisplayName = u8"Turns at once", \
    .Description = u8"Turns its body at once instead of at its turn speed."

#define CHARACTER_ROW_CAN_CROUCH \
    .Key = u8"can_crouch", .Type = PropertyType_Bool, .Default.BoolValue = true, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_CROUCHING, .DisplayName = u8"Can crouch", \
    .Description = u8"Whether the character may crouch."

#define CHARACTER_ROW_CROUCH_HEIGHT \
    .Key = u8"crouch_height", .Type = PropertyType_Float, .Default.FloatValue = 1.15f, .Rules = CHARACTER_RULES_SIZE, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_CROUCHING, .DisplayName = u8"Crouched height", \
    .Description = u8"The crouched capsule's whole height in metres (at least twice the radius, at most the height): " \
        u8"what it fits under."

#define CHARACTER_ROW_CROUCH_EYE_HEIGHT \
    .Key = u8"crouch_eye_height", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, \
    .Rules = CHARACTER_RULES_LENGTH, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_CROUCHING, \
    .DisplayName = u8"Crouched eye height", \
    .Description = u8"How high the eyes are above the feet while crouched, in metres."

#define CHARACTER_ROW_CROUCH_SPEED \
    .Key = u8"crouch_speed", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, .Rules = CHARACTER_RULES_SPEED, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_CROUCHING, .DisplayName = u8"Crouched speed", \
    .Description = u8"Metres per second while crouched."

#define CHARACTER_ROW_CROUCH_TRANSITION_SECONDS \
    .Key = u8"crouch_transition_seconds", .Type = PropertyType_Float, .Default.FloatValue = 0.2f, \
    .Rules = CHARACTER_RULES_SECONDS, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_CROUCHING, \
    .DisplayName = u8"Crouch time", \
    .Description = u8"Seconds the eyes take to go down or up; 0 at once. The body changes at once."

#define CHARACTER_ROW_CAN_JUMP \
    .Key = u8"can_jump", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_JUMPING, .DisplayName = u8"Can jump", \
    .Description = u8"Whether the character may jump."

#define CHARACTER_ROW_JUMP_SPEED \
    .Key = u8"jump_speed", .Type = PropertyType_Float, .Default.FloatValue = 3.2f, .Rules = CHARACTER_RULES_SPEED, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, .DisplayName = u8"Jump speed", \
    .Description = u8"The upward speed a jump starts with, in metres per second: 3.2 jumps about half a metre high on " \
        u8"Earth."

#define CHARACTER_ROW_CAN_JUMP_WHILE_CROUCHED \
    .Key = u8"can_jump_while_crouched", .Type = PropertyType_Bool, .Default.BoolValue = false, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, .DisplayName = u8"Jumps crouched", \
    .Description = u8"Whether a crouched character may jump."

#define CHARACTER_ROW_COYOTE_SECONDS \
    .Key = u8"coyote_seconds", .Type = PropertyType_Float, .Default.FloatValue = 0.1f, .Rules = CHARACTER_RULES_SECONDS, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, .DisplayName = u8"Late jump time", \
    .Description = u8"Seconds after walking off an edge in which a jump still starts."

#define CHARACTER_ROW_JUMP_BUFFER_SECONDS \
    .Key = u8"jump_buffer_seconds", .Type = PropertyType_Float, .Default.FloatValue = 0.1f, \
    .Rules = CHARACTER_RULES_SECONDS, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, \
    .DisplayName = u8"Early jump time", \
    .Description = u8"Seconds a jump asked for in the air waits for the ground, so a press just before landing jumps."

#define CHARACTER_ROW_GRAVITY_MULTIPLIER \
    .Key = u8"gravity_multiplier", .Type = PropertyType_Float, .Default.FloatValue = 1.0f, \
    .Rules = PROPERTY_RULES_BETWEEN(0.0, CHARACTER_MAX_GRAVITY_MULTIPLIER), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_FALLING, .DisplayName = u8"Gravity", \
    .Description = u8"Times the world's gravity while walking and falling; flying has none."

#define CHARACTER_ROW_AIR_ACCELERATION \
    .Key = u8"air_acceleration", .Type = PropertyType_Float, .Default.FloatValue = 0.0f, \
    .Rules = CHARACTER_RULES_ACCELERATION, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_FALLING, \
    .DisplayName = u8"Air control", \
    .Description = u8"How hard its movement keys push it in the air, in metres per second squared; 0, none: only " \
        u8"gravity and the air move it. They steer and brake, never speed it up past walking or running."

#define CHARACTER_ROW_MAX_FALL_SPEED \
    .Key = u8"max_fall_speed", .Type = PropertyType_Float, .Default.FloatValue = 55.0f, \
    .Rules = CHARACTER_RULES_POSITIVE_SPEED, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_FALLING, \
    .DisplayName = u8"Fastest fall", \
    .Description = u8"The fastest it falls, in metres per second (a person reaches about 55)."

#define CHARACTER_ROW_CAN_FLY \
    .Key = u8"can_fly", .Type = PropertyType_Bool, .Default.BoolValue = false, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_FLYING, .DisplayName = u8"Can fly", \
    .Description = u8"Whether the character may fly (birds)."

#define CHARACTER_ROW_FLY_SPEED \
    .Key = u8"fly_speed", .Type = PropertyType_Float, .Default.FloatValue = 5.0f, .Rules = CHARACTER_RULES_SPEED, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_FLYING, .DisplayName = u8"Flying speed", \
    .Description = u8"Metres per second."

#define CHARACTER_ROW_FLY_FAST_SPEED \
    .Key = u8"fly_fast_speed", .Type = PropertyType_Float, .Default.FloatValue = 10.0f, .Rules = CHARACTER_RULES_SPEED, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_FLYING, .DisplayName = u8"Fast flying speed", \
    .Description = u8"Metres per second while running."

#define CHARACTER_ROW_FLY_ACCELERATION \
    .Key = u8"fly_acceleration", .Type = PropertyType_Float, .Default.FloatValue = 6.0f, \
    .Rules = CHARACTER_RULES_POSITIVE_ACCELERATION, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_FLYING, .DisplayName = u8"Flying acceleration", \
    .Description = u8"How quickly it speeds up while flying, in metres per second squared."

#define CHARACTER_ROW_FLY_DECELERATION \
    .Key = u8"fly_deceleration", .Type = PropertyType_Float, .Default.FloatValue = 6.0f, \
    .Rules = CHARACTER_RULES_POSITIVE_ACCELERATION, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_FLYING, .DisplayName = u8"Flying deceleration", \
    .Description = u8"How quickly it slows down while flying, in metres per second squared."

#define CHARACTER_ROW_INTERACTION_REACH \
    .Key = u8"interaction_reach", .Type = PropertyType_Float, .Default.FloatValue = 2.5f, .Rules = CHARACTER_RULES_SIZE, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_INTERACTION, .DisplayName = u8"Reach", \
    .Description = u8"How far from its eyes it can look at and use things, in metres."

#define CHARACTER_ROW_INTERACTION_LAYERS \
    .Key = u8"interaction_layers", .Type = PropertyType_EnumSet, \
    .Default.EnumSetValue = CHARACTER_DEFAULT_INTERACTION_LAYERS, .EnumTable = &COLLISION_LAYER_ENUM, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_INTERACTION, .DisplayName = u8"Sees", \
    .Description = u8"The collision layers its look and use ray sees; triggers are never seen."

// The settings from here on come last in CharacterSetting, whatever their category (their wire ids follow the enum).
#define CHARACTER_ROW_GROUND_CONTROL \
    .Key = u8"ground_control", .Type = PropertyType_Enum, .Default.EnumValue = CharacterGroundControl_Steer, \
    .EnumTable = &CHARACTER_GROUND_CONTROL_ENUM, .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_WALKING, .DisplayName = u8"Ground control", \
    .Description = u8"How its movement keys act on the ground: steer turns the whole motion toward them; push adds " \
        u8"motion along their direction and friction takes the rest, so turns slide (momentum: ice, a heavy animal)."

#define CHARACTER_ROW_WALK_JUMP_BOOST \
    .Key = u8"walk_jump_boost", .Type = PropertyType_Float, .Default.FloatValue = 0.0f, \
    .Rules = CHARACTER_RULES_SPEED, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, \
    .DisplayName = u8"Walking jump boost", \
    .Description = u8"Metres per second a jump adds along the movement while walking; never past the walking speed " \
        u8"plus this, so jumps in a row do not add up."

#define CHARACTER_ROW_RUN_JUMP_BOOST \
    .Key = u8"run_jump_boost", .Type = PropertyType_Float, .Default.FloatValue = 0.0f, \
    .Rules = CHARACTER_RULES_SPEED, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, \
    .DisplayName = u8"Running jump boost", \
    .Description = u8"Metres per second a jump adds along the movement while running (sprint-jumping); never past " \
        u8"the running speed plus this."

#define CHARACTER_ROW_CAN_AUTO_JUMP \
    .Key = u8"can_auto_jump", .Type = PropertyType_Bool, .Default.BoolValue = true, \
    .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, .DisplayName = u8"Auto-jump", \
    .Description = u8"Whether a jump held down jumps again whenever it is on the ground."

#define CHARACTER_ROW_AUTO_JUMP_DELAY_SECONDS \
    .Key = u8"auto_jump_delay_seconds", .Type = PropertyType_Float, .Default.FloatValue = 0.25f, \
    .Rules = CHARACTER_RULES_SECONDS, .Flags = PROPERTY_FLAGS_SETTING, .Category = CHARACTER_CATEGORY_JUMPING, \
    .DisplayName = u8"Auto-jump delay", \
    .Description = u8"The least seconds from one jump to the next that a held jump starts."

#define CHARACTER_ROW_AIR_DRAG \
    .Key = u8"air_drag", .Type = PropertyType_Float, .Default.FloatValue = 0.004f, \
    .Rules = PROPERTY_RULES_BETWEEN(0.0, CHARACTER_MAX_AIR_DRAG), .Flags = PROPERTY_FLAGS_SETTING, \
    .Category = CHARACTER_CATEGORY_FALLING, .DisplayName = u8"Air drag", \
    .Description = u8"How much the air slows it, per metre: a deceleration of this times its speed squared. 0.004 " \
        u8"gives a person's 50 m/s terminal speed on Earth; at walking and running speeds it is hardly felt."
