#!/usr/bin/env python3
"""Generates the placeholder humanoid: box-figure models and their motion library, as binary glTF (.glb).

Run it from anywhere (only the Python standard library is needed):

    python3 asset_source/scripts/placeholder_humanoid.py

It writes, relative to the repository root:

    compile/out/asset/models/placeholder_humanoid/tall.glb        a 1.78 m box figure, 19 bones, skinned (LOD 0)
    compile/out/asset/models/placeholder_humanoid/tall_lod1.glb   the same skeleton with fewer boxes (LOD 1)
    compile/out/asset/models/placeholder_humanoid/short.glb       a shorter, stockier figure: the same bone names and
                                                                  axes, other bone lengths (one library plays on both)
    compile/out/asset/animation_libraries/placeholder_humanoid/motions.glb
                                                                  the clips on the tall skeleton, with no mesh and no
                                                                  skin (what Blender writes when it exports actions only)
    tests/data/placeholder_humanoid_with_clips.glb                the tall model, its skin and the clips in one file
    compile/out/asset/models/placeholder_props/bench.glb          the test props: a bench (its seat 0.45 m up) and a
    compile/out/asset/models/placeholder_props/cup.glb            cup, rigid (no skin)

Conventions: metres, +Y up, the figure faces +Z (its left is +X) and stands on the origin. Every bone's rest rotation is
the identity, so in the rest pose a bone's axes are the model's. Clips are keyed 24 times a second, as Blender exports
them, with the last key exactly at the clip's end (the case raylib's own glTF clip loader gets wrong), and move in place
(no root motion). The legs are placed by a two-bone IK in the side view, so the feet stay on the ground while they
should.

The action clips (sit_down, sit_idle, stand_up, pick_up, hold_r, put_down, open_door) are made for the test
props and the anchors of their actions: an action first slides the character's feet (the model's origin) onto a socket
of its target, so these clips know where the seat, the cup and the door are from there. Their arms are placed by the
same kind of IK (the right hand meets the cup's grip and the door), checked by forward kinematics when they are written.

Placeholder art for checking the code, not game art: replace it with the owner's character.
"""

import json
import math
import os
import struct


# Paths.
REPOSITORY = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ASSET = os.path.join(REPOSITORY, "compile", "out", "asset")
MODEL_DIRECTORY = os.path.join(ASSET, "models", "placeholder_humanoid")
PROP_DIRECTORY = os.path.join(ASSET, "models", "placeholder_props")
LIBRARY_DIRECTORY = os.path.join(ASSET, "animation_libraries", "placeholder_humanoid")
TEST_DATA_DIRECTORY = os.path.join(REPOSITORY, "tests", "data")

KEYS_PER_SECOND = 24

# glTF constants.
FLOAT = 5126
UNSIGNED_BYTE = 5121
UNSIGNED_SHORT = 5123
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


# Colours are picked as they look (sRGB, 0 to 1); glTF's baseColorFactor is linear, so they are written raised to the
# game's gamma (its renderer encodes and decodes material colours with it).
SRGB_GAMMA = 2.2


def base_color_factor(color):
    """A colour picked as it looks (sRGB) as glTF's linear baseColorFactor, opaque."""
    return [round(channel ** SRGB_GAMMA, 6) for channel in color] + [1.0]


# The skeleton: name, parent, joint position in model space (the tall figure's rest pose, which is its bind pose).
BONES = [
    ("hips", None, (0.0, 0.95, 0.0)),
    ("spine", "hips", (0.0, 1.07, 0.0)),
    ("chest", "spine", (0.0, 1.27, 0.0)),
    ("neck", "chest", (0.0, 1.47, 0.0)),
    ("head", "neck", (0.0, 1.55, 0.0)),
    ("shoulder_l", "chest", (0.07, 1.43, 0.0)),
    ("upper_arm_l", "shoulder_l", (0.19, 1.43, 0.0)),
    ("lower_arm_l", "upper_arm_l", (0.19, 1.15, 0.0)),
    ("hand_l", "lower_arm_l", (0.19, 0.90, 0.0)),
    ("shoulder_r", "chest", (-0.07, 1.43, 0.0)),
    ("upper_arm_r", "shoulder_r", (-0.19, 1.43, 0.0)),
    ("lower_arm_r", "upper_arm_r", (-0.19, 1.15, 0.0)),
    ("hand_r", "lower_arm_r", (-0.19, 0.90, 0.0)),
    ("upper_leg_l", "hips", (0.09, 0.92, 0.0)),
    ("lower_leg_l", "upper_leg_l", (0.09, 0.50, 0.0)),
    ("foot_l", "lower_leg_l", (0.09, 0.08, 0.0)),
    ("upper_leg_r", "hips", (-0.09, 0.92, 0.0)),
    ("lower_leg_r", "upper_leg_r", (-0.09, 0.50, 0.0)),
    ("foot_r", "lower_leg_r", (-0.09, 0.08, 0.0)),
]
BONE_INDEX = {name: index for index, (name, _, _) in enumerate(BONES)}

# Leg lengths of the tall figure (hip joint to knee, knee to ankle), for the IK, and where the ankle rests.
THIGH_LENGTH = 0.42
SHIN_LENGTH = 0.42
HIP_JOINT_HEIGHT = 0.92
ANKLE_HEIGHT = 0.08
HIPS_HEIGHT = 0.95

MATERIALS = [
    ("skin", (0.85, 0.64, 0.50)),
    ("shirt", (0.20, 0.45, 0.75)),
    ("trousers", (0.25, 0.26, 0.32)),
    ("shoes", (0.13, 0.10, 0.08)),
    ("hair", (0.16, 0.10, 0.05)),
]
MATERIAL_INDEX = {name: index for index, (name, _) in enumerate(MATERIALS)}

# The torso's rings: height, and the bones its vertices follow there (it bends with the spine).
TORSO_RINGS = [
    (1.00, [("hips", 1.0)]),
    (1.10, [("hips", 0.5), ("spine", 0.5)]),
    (1.22, [("spine", 1.0)]),
    (1.34, [("spine", 0.5), ("chest", 0.5)]),
    (1.46, [("chest", 1.0)]),
]

# Rigid boxes: material, bone, min corner, max corner (model space, the tall figure).
BOXES = [
    ("trousers", "hips", (-0.16, 0.84, -0.10), (0.16, 1.02, 0.10)),
    ("skin", "neck", (-0.045, 1.44, -0.045), (0.045, 1.56, 0.045)),
    ("skin", "head", (-0.10, 1.54, -0.11), (0.10, 1.76, 0.11)),
    ("skin", "head", (-0.02, 1.62, 0.11), (0.02, 1.67, 0.15)),       # the nose: the figure faces +Z
    ("hair", "head", (-0.105, 1.70, -0.115), (0.105, 1.78, 0.105)),
    ("shirt", "upper_arm_l", (0.145, 1.13, -0.045), (0.235, 1.46, 0.045)),
    ("shirt", "lower_arm_l", (0.15, 0.91, -0.04), (0.23, 1.17, 0.04)),
    ("skin", "hand_l", (0.155, 0.79, -0.035), (0.225, 0.93, 0.035)),
    ("shirt", "upper_arm_r", (-0.235, 1.13, -0.045), (-0.145, 1.46, 0.045)),
    ("shirt", "lower_arm_r", (-0.23, 0.91, -0.04), (-0.15, 1.17, 0.04)),
    ("skin", "hand_r", (-0.225, 0.79, -0.035), (-0.155, 0.93, 0.035)),
    ("trousers", "upper_leg_l", (0.025, 0.48, -0.065), (0.155, 0.94, 0.065)),
    ("trousers", "lower_leg_l", (0.035, 0.07, -0.055), (0.145, 0.52, 0.055)),
    ("shoes", "foot_l", (0.04, 0.0, -0.06), (0.14, 0.10, 0.17)),
    ("trousers", "upper_leg_r", (-0.155, 0.48, -0.065), (-0.025, 0.94, 0.065)),
    ("trousers", "lower_leg_r", (-0.145, 0.07, -0.055), (-0.035, 0.52, 0.055)),
    ("shoes", "foot_r", (-0.14, 0.0, -0.06), (-0.04, 0.10, 0.17)),
]
TORSO_HALF_SIZE = (0.16, 0.10)  # x, z

# Boxes LOD 1 leaves out, and the torso rings it keeps (one box from the hips to the chest).
LOD1_DROPPED_BOXES = {3, 4}
LOD1_TORSO_RINGS = [TORSO_RINGS[0], TORSO_RINGS[-1]]

# The short figure: every position scaled (x, y, z): 85% of the height, 15% wider, 10% deeper.
SHORT_SCALE = (1.15, 0.85, 1.10)

# The actions' places, from the feet where the anchor puts them (z ahead, y up). Sitting: the seat's top 0.45 m up
# with its front edge 0.36 m behind the feet; the hips joint seated rests 0.11 m above it (the hips box's underside on
# the seat), 0.48 m behind the feet; the ankles 5 cm ahead. Picking up and putting down: the right hand's socket (the
# palm, HAND_SOCKET) meets the test cup's grip on the test desk, 0.52 m ahead and 0.83 m up; opening the door: it
# pushes the panel. The props' sockets put their targets there (the script prints where the palm ends up across).
SEAT_HEIGHT = 0.45
SEATED_HIPS_HEIGHT = 0.56
SEATED_HIPS_BACK = -0.48
SEATED_ANKLE_FORWARD = 0.05
REACH_GRIP = (0.52, 0.83)
DOOR_PUSH = (0.48, 1.05)
# The hand_r socket in the hand bone's space (the test humanoid's world model says the same): 3 cm toward the palm's
# side, 7 cm down the fingers.
HAND_SOCKET = (0.03, -0.07, 0.0)
# The right arm's bones, the spine's joints (heights above the hips joint, the chest's above the spine's, the
# shoulder's above the chest's) and the hand socket's offset along the forearm, for the arm IK.
UPPER_ARM_LENGTH = 0.28
LOWER_ARM_LENGTH = 0.25
SPINE_ABOVE_HIPS = 0.12
CHEST_ABOVE_SPINE = 0.20
SHOULDER_ABOVE_CHEST = 0.16
# The holding pose of the right arm (degrees): the upper arm forward, out to the side, the elbow bent; the hand socket
# turns the other way (+100 degrees about X), so what it holds stands upright here. A reach that carries something
# bends the wrist to keep the hand at that turn (HAND_UPRIGHT), so the cup stays upright on its way to the table.
HOLD_ARM = (20.0, 4.0, 80.0)
HAND_UPRIGHT = HOLD_ARM[0] + HOLD_ARM[2]


# Quaternions (x, y, z, w); q_mul(a, b) turns by b first, then by a.
def q_axis(axis, degrees):
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


IDENTITY = (0.0, 0.0, 0.0, 1.0)
X_AXIS = (1.0, 0.0, 0.0)
Y_AXIS = (0.0, 1.0, 0.0)
Z_AXIS = (0.0, 0.0, 1.0)


def rx(degrees):
    return q_axis(X_AXIS, degrees)


def ry(degrees):
    return q_axis(Y_AXIS, degrees)


def rz(degrees):
    return q_axis(Z_AXIS, degrees)


def smoothstep(edge0, edge1, x):
    t = min(1.0, max(0.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


# Two-bone leg IK in the side view (y up, z forward), relative to the hip joint. Returns the thigh's forward swing and
# the knee's bend in degrees: the thigh turns by rx(-swing), the shin by rx(+bend), and a foot turned by rx(swing - bend)
# stays level. A target out of reach straightens the leg toward it.
def leg_ik(target_z, target_y):
    distance = math.hypot(target_z, target_y)
    reach = THIGH_LENGTH + SHIN_LENGTH - 1e-6
    distance = min(max(distance, 1e-6), reach)
    cos_bend = (distance * distance - THIGH_LENGTH * THIGH_LENGTH - SHIN_LENGTH * SHIN_LENGTH) / (
        2.0 * THIGH_LENGTH * SHIN_LENGTH)
    bend = math.acos(min(1.0, max(-1.0, cos_bend)))
    toward_target = math.atan2(target_z, -target_y)
    offset = math.atan2(SHIN_LENGTH * math.sin(bend), THIGH_LENGTH + SHIN_LENGTH * math.cos(bend))
    return math.degrees(toward_target + offset), math.degrees(bend)


# One frame of a clip: each bone's local rotation, and the hips' translation.
class Pose:
    def __init__(self):
        self.rotations = {name: IDENTITY for name, _, _ in BONES}
        self.hips = (0.0, HIPS_HEIGHT, 0.0)

    def turn(self, bone, rotation):
        self.rotations[bone] = q_mul(rotation, self.rotations[bone])

    def legs(self, hips_drop, left_target, right_target, left_toe=0.0, right_toe=0.0):
        """Lowers the hips and places each ankle at (z, lift above its rest height) by the IK."""
        self.hips = (self.hips[0], HIPS_HEIGHT - hips_drop, self.hips[2])
        hip_joint = HIP_JOINT_HEIGHT - hips_drop
        for side, (target_z, lift), toe in (("l", left_target, left_toe), ("r", right_target, right_toe)):
            swing, bend = leg_ik(target_z, ANKLE_HEIGHT + lift - hip_joint)
            self.turn("upper_leg_" + side, rx(-swing))
            self.turn("lower_leg_" + side, rx(bend))
            self.turn("foot_" + side, rx(swing - bend + toe))

    def arm(self, side, forward, outward, elbow):
        """Swings an arm forward (degrees), out to its side, and bends its elbow forward."""
        sign = 1.0 if side == "l" else -1.0
        self.turn("upper_arm_" + side, q_mul(rz(sign * outward), rx(-forward)))
        self.turn("lower_arm_" + side, rx(-elbow))


def stride_target(phase, stance_share, half_length, lift_height):
    """Where a foot is in a gait: on the ground sliding back over the stance, lifted and swung forward after it."""
    phase %= 1.0
    if phase < stance_share:
        u = phase / stance_share
        return (half_length - 2.0 * half_length * u, 0.0)
    u = (phase - stance_share) / (1.0 - stance_share)
    eased = 0.5 - 0.5 * math.cos(math.pi * u)
    return (-half_length + 2.0 * half_length * eased, lift_height * math.sin(math.pi * u))


def gait(t, period, stance_share, half_length, lift_height, hips_drop, bob, arm_swing, elbow, lean):
    phase = t / period
    pose = Pose()
    left = stride_target(phase, stance_share, half_length, lift_height)
    right = stride_target(phase + 0.5, stance_share, half_length, lift_height)
    drop = hips_drop + bob * 0.5 * (1.0 - math.cos(4.0 * math.pi * phase))
    pose.legs(drop, left, right)
    pose.turn("spine", rx(lean * 0.6))
    pose.turn("chest", rx(lean * 0.4))
    twist = 5.0 * math.sin(2.0 * math.pi * phase)
    pose.turn("spine", ry(-twist * 0.5))
    pose.turn("chest", ry(-twist * 0.5))
    pose.turn("head", rx(-lean))
    # Each arm swings with the other side's leg.
    pose.arm("l", arm_swing * right[0] / half_length, 6.0, elbow)
    pose.arm("r", arm_swing * left[0] / half_length, 6.0, elbow)
    return pose


def idle(t):
    phase = t / 3.0
    pose = Pose()
    breath = math.sin(2.0 * math.pi * phase)
    pose.legs(0.004 * (0.5 - 0.5 * math.cos(2.0 * math.pi * phase)), (0.03, 0.0), (-0.03, 0.0))
    pose.turn("spine", rx(0.6 * breath))
    pose.turn("chest", rx(1.2 * breath))
    pose.turn("head", q_mul(ry(10.0 * math.sin(2.0 * math.pi * phase)), rx(2.0 * math.sin(4.0 * math.pi * phase))))
    pose.arm("l", 3.0 * math.sin(2.0 * math.pi * phase + 1.0), 5.0, 8.0)
    pose.arm("r", 3.0 * math.sin(2.0 * math.pi * phase + 2.0), 5.0, 8.0)
    return pose


def walk(t):
    return gait(t, 1.0, 0.6, 0.33, 0.09, 0.075, 0.015, 22.0, 15.0, 3.0)


def run(t):
    return gait(t, 0.68, 0.38, 0.30, 0.28, 0.06, 0.035, 40.0, 85.0, 12.0)


def crouch_pose(t, period):
    pose = Pose()
    breath = math.sin(2.0 * math.pi * t / period)
    pose.turn("spine", rx(20.0))
    pose.turn("chest", rx(10.0 + 1.0 * breath))
    pose.turn("head", rx(-28.0))
    pose.arm("l", 30.0, 8.0, 35.0)
    pose.arm("r", 30.0, 8.0, 35.0)
    return pose


def crouch_idle(t):
    pose = crouch_pose(t, 2.0)
    pose.legs(0.42, (0.06, 0.0), (-0.04, 0.0))
    return pose


def crouch_walk(t):
    period = 1.4
    phase = t / period
    pose = crouch_pose(t, period)
    left = stride_target(phase, 0.6, 0.22, 0.06)
    right = stride_target(phase + 0.5, 0.6, 0.22, 0.06)
    pose.legs(0.40 + 0.01 * 0.5 * (1.0 - math.cos(4.0 * math.pi * phase)), left, right)
    return pose


def jump(t):
    pose = Pose()
    crouch = smoothstep(0.0, 0.2, t) * (1.0 - smoothstep(0.2, 0.32, t))
    extended = smoothstep(0.24, 0.4, t)
    pose.legs(0.20 * crouch, (0.02, 0.0), (0.02, 0.0), 35.0 * extended, 35.0 * extended)
    pose.turn("spine", rx(15.0 * crouch))
    pose.turn("head", rx(-10.0 * crouch))
    arm = lerp(-30.0 * crouch, 150.0, extended)
    pose.arm("l", arm, 10.0, 10.0)
    pose.arm("r", arm, 10.0, 10.0)
    return pose


def fall(t):
    phase = t / 1.0
    pose = Pose()
    flail = math.sin(2.0 * math.pi * phase)
    pose.turn("upper_leg_l", rx(-18.0 - 6.0 * flail))
    pose.turn("lower_leg_l", rx(30.0 + 6.0 * flail))
    pose.turn("upper_leg_r", rx(-6.0 + 6.0 * flail))
    pose.turn("lower_leg_r", rx(22.0 - 6.0 * flail))
    pose.turn("spine", rx(-4.0))
    pose.arm("l", 20.0 + 10.0 * flail, 70.0 + 12.0 * flail, 30.0)
    pose.arm("r", 20.0 - 10.0 * flail, 70.0 - 12.0 * flail, 30.0)
    return pose


def land(t):
    pose = Pose()
    impact = smoothstep(0.0, 0.08, t) * (1.0 - smoothstep(0.12, 0.5, t))
    pose.legs(0.26 * impact, (0.04, 0.0), (-0.02, 0.0))
    pose.turn("spine", rx(22.0 * impact))
    pose.turn("head", rx(-18.0 * impact))
    pose.arm("l", 35.0 * impact, 12.0 + 10.0 * impact, 25.0 * impact)
    pose.arm("r", 35.0 * impact, 12.0 + 10.0 * impact, 25.0 * impact)
    return pose


def wave(t):
    pose = idle(t)
    raised = smoothstep(0.0, 0.35, t) * (1.0 - smoothstep(1.6, 2.0, t))
    waving = math.sin(2.0 * math.pi * 2.5 * (t - 0.35)) * smoothstep(0.3, 0.45, t) * (1.0 - smoothstep(1.45, 1.6, t))
    pose.rotations["upper_arm_r"] = rz(-100.0 * raised)
    pose.rotations["lower_arm_r"] = rz(-(55.0 + 22.0 * waving) * raised)
    pose.turn("head", ry(-12.0 * raised))
    return pose


# The actions' clips.
def lean_shoulder(hips_y, hips_z, spine_lean, chest_lean):
    """Where the right shoulder joint is in the side view (z, y) with the hips at (hips_y, hips_z) and the spine and the
    chest leaning forward by their degrees."""
    spine = (hips_z, hips_y + SPINE_ABOVE_HIPS)
    first = math.radians(spine_lean)
    both = math.radians(spine_lean + chest_lean)
    chest = (spine[0] + CHEST_ABOVE_SPINE * math.sin(first), spine[1] + CHEST_ABOVE_SPINE * math.cos(first))
    return (chest[0] + SHOULDER_ABOVE_CHEST * math.sin(both), chest[1] + SHOULDER_ABOVE_CHEST * math.cos(both))


def arm_ik(shoulder, wrist, torso_lean):
    """The right arm's forward swing (relative to the chest, which leans by torso_lean degrees) and its elbow's bend in
    degrees, so the wrist reaches wrist (z, y) from shoulder in the side view, the elbow folding the forearm forward. A
    target out of reach straightens the arm toward it."""
    dz = wrist[0] - shoulder[0]
    dy = wrist[1] - shoulder[1]
    distance = min(max(math.hypot(dz, dy), 1e-6), UPPER_ARM_LENGTH + LOWER_ARM_LENGTH - 1e-6)
    cos_bend = (distance * distance - UPPER_ARM_LENGTH ** 2 - LOWER_ARM_LENGTH ** 2) / (
        2.0 * UPPER_ARM_LENGTH * LOWER_ARM_LENGTH)
    bend = math.acos(min(1.0, max(-1.0, cos_bend)))
    toward = math.atan2(dz, -dy)
    inner = math.atan2(LOWER_ARM_LENGTH * math.sin(bend), UPPER_ARM_LENGTH + LOWER_ARM_LENGTH * math.cos(bend))
    return math.degrees(toward - inner) + torso_lean, math.degrees(bend)


def legs_to(pose, hips_y, hips_z, ankle_z, ankle_y=ANKLE_HEIGHT):
    """Puts the hips at (hips_y, hips_z) and both ankles at (ankle_z, ankle_y) in model space, by the leg IK."""
    pose.hips = (0.0, hips_y, hips_z)
    hip_joint_y = hips_y - (HIPS_HEIGHT - HIP_JOINT_HEIGHT)
    for side in ("l", "r"):
        swing, bend = leg_ik(ankle_z - hips_z, ankle_y - hip_joint_y)
        pose.turn("upper_leg_" + side, rx(-swing))
        pose.turn("lower_leg_" + side, rx(bend))
        pose.turn("foot_" + side, rx(swing - bend))


def sitting(u, breath=0.0):
    """Between standing (u = 0) and seated (u = 1): the hips go back and down onto the seat, the body leans forward on
    the way (most halfway) and sits upright, the hands come to rest on the thighs."""
    pose = Pose()
    hips_y = lerp(HIPS_HEIGHT, SEATED_HIPS_HEIGHT, u)
    hips_z = lerp(0.0, SEATED_HIPS_BACK, u)
    legs_to(pose, hips_y, hips_z, SEATED_ANKLE_FORWARD * u)
    lean = 38.0 * math.sin(math.pi * u)
    pose.turn("spine", rx(lean * 0.6 + 0.6 * breath))
    pose.turn("chest", rx(lean * 0.4 + 1.2 * breath))
    pose.turn("head", rx(-lean * 0.5))
    arm = lerp(0.0, 38.0, u) + 12.0 * math.sin(math.pi * u)
    elbow = lerp(8.0, 52.0, u)
    pose.arm("l", arm, 5.0, elbow)
    pose.arm("r", arm, 5.0, elbow)
    return pose


def sit_down(t):
    return sitting(smoothstep(0.0, 1.1, t))


def sit_idle(t):
    phase = t / 4.0
    pose = sitting(1.0, math.sin(2.0 * math.pi * phase))
    pose.turn("head", ry(9.0 * math.sin(2.0 * math.pi * phase)))
    return pose


def stand_up(t):
    return sitting(1.0 - smoothstep(0.15, 1.3, t))


REST_ARM = (0.0, 0.0, 8.0)
REACH_OUTWARD = 4.0
# A reach comes and goes through a point this far above its target, the body fully bent by then (BEND_SHARE of the
# way), so what the hand carries clears a table's edge and goes down onto it.
REACH_CLEARANCE = 0.10
BEND_SHARE = 0.65


def bent(u, hips_drop, spine_lean, chest_lean, arm, upright=0.0):
    """The body bent toward a reach by u (0 standing, 1 all the way), the right arm at arm (forward, outward, elbow),
    the wrist turning the hand upright (HAND_UPRIGHT) by the share upright."""
    pose = Pose()
    legs_to(pose, HIPS_HEIGHT - hips_drop * u, 0.0, 0.0)
    pose.turn("spine", rx(spine_lean * u))
    pose.turn("chest", rx(chest_lean * u))
    pose.turn("head", rx(-0.6 * (spine_lean + chest_lean) * u))
    pose.arm("r", arm[0], arm[1], arm[2])
    pose.arm("l", 12.0 * u, 6.0, 15.0 * u)
    if upright > 0.0:
        wrist = HAND_UPRIGHT - (arm[0] - (spine_lean + chest_lean) * u + arm[2])
        pose.turn("hand_r", rx(-wrist * upright))
    return pose


def solve_reach(grip, hips_drop, spine_lean, chest_lean, upright):
    """The right arm (forward, outward, elbow) that puts the hand's socket on grip (z, y) with the body bent all the way
    (the wrist upright by that share): the side view's IK for the wrist, its target moved by what forward kinematics
    finds the socket missed by."""
    shoulder = lean_shoulder(HIPS_HEIGHT - hips_drop, 0.0, spine_lean, chest_lean)
    target = grip
    arm = REST_ARM
    for _ in range(20):
        forward, elbow = arm_ik(shoulder, target, spine_lean + chest_lean)
        arm = (forward, REACH_OUTWARD, elbow)
        palm = hand_socket_position(bent(1.0, hips_drop, spine_lean, chest_lean, arm, upright))
        if math.hypot(grip[0] - palm[2], grip[1] - palm[1]) < 1e-4:
            break
        target = (target[0] + grip[0] - palm[2], target[1] + grip[1] - palm[1])
    return arm


def reaching(u, grip, hips_drop, spine_lean, chest_lean, from_arm, to_arm=None, upright_at_rest=0.0,
             upright_at_reach=0.0):
    """The body bent toward a reach (u = 1: the hand's socket on grip) from standing (u = 0) with the right arm at
    from_arm (forward, outward, elbow); to_arm, when given, is where the arm ends at u = 0 instead (the return of a
    reach that took something). The hand passes REACH_CLEARANCE above grip on the way, and the wrist keeps it upright
    by a share going from upright_at_rest (u = 0) to upright_at_reach (u = 1): 1 while it carries something."""
    above = solve_reach((grip[0], grip[1] + REACH_CLEARANCE), hips_drop, spine_lean, chest_lean, upright_at_reach)
    reach = solve_reach(grip, hips_drop, spine_lean, chest_lean, upright_at_reach)
    rest = from_arm if to_arm is None else to_arm
    if u < BEND_SHARE:
        share = u / BEND_SHARE
        arm = tuple(lerp(rest[axis], above[axis], share) for axis in range(3))
    else:
        share = (u - BEND_SHARE) / (1.0 - BEND_SHARE)
        arm = tuple(lerp(above[axis], reach[axis], share) for axis in range(3))
    return bent(min(1.0, u / BEND_SHARE), hips_drop, spine_lean, chest_lean, arm,
                lerp(upright_at_rest, upright_at_reach, u))


def pick_up(t):
    # In (0 to 0.6 s; the hand turning upright to take the cup as it is), the grab at 0.65 s, back up with the arm in
    # the holding pose (0.75 to 1.5 s), the cup upright all the way.
    if t < 0.75:
        return reaching(smoothstep(0.0, 0.6, t), REACH_GRIP, 0.12, 24.0, 16.0, REST_ARM, None, 0.0, 1.0)
    return reaching(1.0 - smoothstep(0.75, 1.5, t), REACH_GRIP, 0.12, 24.0, 16.0, REST_ARM, HOLD_ARM, 1.0, 1.0)


def hold_r(t):
    phase = t / 2.0
    pose = Pose()
    pose.arm("r", HOLD_ARM[0] + 2.0 * math.sin(2.0 * math.pi * phase), HOLD_ARM[1],
             HOLD_ARM[2] + 3.0 * math.sin(2.0 * math.pi * phase + 1.0))
    return pose


def put_down(t):
    # From holding to the reach (0 to 0.75 s, the cup upright), the release at 0.8 s, back to standing with the arm down
    # (0.85 to 1.6 s, the empty hand relaxing).
    if t < 0.8:
        return reaching(smoothstep(0.0, 0.75, t), REACH_GRIP, 0.12, 24.0, 16.0, HOLD_ARM, None, 1.0, 1.0)
    return reaching(1.0 - smoothstep(0.85, 1.6, t), REACH_GRIP, 0.12, 24.0, 16.0, REST_ARM, None, 0.0, 1.0)


def open_door(t):
    # The hand to the panel (0 to 0.5 s), the push at 0.55 s, a lean into it, back (0.7 to 1.4 s).
    if t < 0.7:
        return reaching(smoothstep(0.0, 0.5, t), DOOR_PUSH, 0.04, 12.0, 8.0, REST_ARM)
    return reaching(1.0 - smoothstep(0.7, 1.4, t), DOOR_PUSH, 0.04, 12.0, 8.0, REST_ARM)


# Forward kinematics, to check the IK: each bone's joint in model space for a pose.
def q_rotate(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def joint_frames(pose):
    """Each bone's joint position and rotation in model space (the tall figure)."""
    offsets = local_offsets(bone_positions((1.0, 1.0, 1.0)))
    rotations = {}
    positions = {}
    for index, (name, parent, _) in enumerate(BONES):
        if parent is None:
            positions[name] = pose.hips
            rotations[name] = pose.rotations[name]
            continue
        moved = q_rotate(rotations[parent], offsets[index])
        positions[name] = tuple(positions[parent][axis] + moved[axis] for axis in range(3))
        rotations[name] = q_mul(rotations[parent], pose.rotations[name])
    return positions, rotations


def hand_socket_position(pose):
    positions, rotations = joint_frames(pose)
    offset = q_rotate(rotations["hand_r"], HAND_SOCKET)
    return tuple(positions["hand_r"][axis] + offset[axis] for axis in range(3))


def check_reach(name, at, grip):
    """The hand's socket must be on grip (z, y) at the reach's time (a few millimetres off at most); returns where it is
    across (x), for the target's socket."""
    palm = hand_socket_position(dict(CLIPS_BY_NAME)[name](at))
    error = math.hypot(palm[2] - grip[0], palm[1] - grip[1])
    assert error < 0.005, "%s: the hand's socket is at (%.3f, %.3f), %.1f cm from (%.3f, %.3f)" % (
        name, palm[2], palm[1], 100.0 * error, grip[0], grip[1])
    return palm[0]


def check_seat():
    """Seated, the ankles rest on the floor ahead of the feet and the hips joint sits above the seat behind them."""
    joints = joint_frames(sitting(1.0))[0]
    for side in ("l", "r"):
        ankle = joints["foot_" + side]
        assert abs(ankle[1] - ANKLE_HEIGHT) < 0.005 and abs(ankle[2] - SEATED_ANKLE_FORWARD) < 0.005, (side, ankle)
    hips = joints["hips"]
    assert abs(hips[1] - SEATED_HIPS_HEIGHT) < 1e-6 and abs(hips[2] - SEATED_HIPS_BACK) < 1e-6, hips


# Name, duration in seconds, pose function. Looping is the library definition's choice.
CLIPS = [
    ("idle", 3.0, idle),
    ("walk", 1.0, walk),
    ("run", 0.68, run),
    ("crouch_idle", 2.0, crouch_idle),
    ("crouch_walk", 1.4, crouch_walk),
    ("jump", 0.6, jump),
    ("fall", 1.0, fall),
    ("land", 0.5, land),
    ("wave", 2.0, wave),
    ("sit_down", 1.3, sit_down),
    ("sit_idle", 4.0, sit_idle),
    ("stand_up", 1.3, stand_up),
    ("pick_up", 1.5, pick_up),
    ("hold_r", 2.0, hold_r),
    ("put_down", 1.6, put_down),
    ("open_door", 1.4, open_door),
]
CLIPS_BY_NAME = [(name, pose_at) for name, _, pose_at in CLIPS]


# The binary glTF writer.
class GltfWriter:
    def __init__(self):
        self.binary = bytearray()
        self.buffer_views = []
        self.accessors = []

    def add_view(self, data, target=None):
        while len(self.binary) % 4:
            self.binary.append(0)
        view = {"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.binary += data
        self.buffer_views.append(view)
        return len(self.buffer_views) - 1

    def add_floats(self, rows, type_name, target=None, with_bounds=False):
        width = len(rows[0])
        data = b"".join(struct.pack("<%df" % width, *row) for row in rows)
        accessor = {"bufferView": self.add_view(data, target), "componentType": FLOAT, "count": len(rows),
                    "type": type_name}
        if with_bounds:
            accessor["min"] = [min(row[i] for row in rows) for i in range(width)]
            accessor["max"] = [max(row[i] for row in rows) for i in range(width)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def add_scalars(self, values, with_bounds=False):
        return self.add_floats([(value,) for value in values], "SCALAR", None, with_bounds)

    def add_joints(self, rows):
        data = b"".join(struct.pack("<4B", *row) for row in rows)
        self.accessors.append({"bufferView": self.add_view(data, ARRAY_BUFFER), "componentType": UNSIGNED_BYTE,
                               "count": len(rows), "type": "VEC4"})
        return len(self.accessors) - 1

    def add_indices(self, indices):
        data = struct.pack("<%dH" % len(indices), *indices)
        self.accessors.append({"bufferView": self.add_view(data, ELEMENT_ARRAY_BUFFER),
                               "componentType": UNSIGNED_SHORT, "count": len(indices), "type": "SCALAR"})
        return len(self.accessors) - 1

    def write(self, path, document):
        document["asset"] = {"version": "2.0", "generator": "TheBenchProject placeholder_humanoid.py"}
        document["bufferViews"] = self.buffer_views
        document["accessors"] = self.accessors
        while len(self.binary) % 4:
            self.binary.append(0)
        document["buffers"] = [{"byteLength": len(self.binary)}]
        text = json.dumps(document, separators=(",", ":")).encode("utf-8")
        while len(text) % 4:
            text += b" "
        total = 12 + 8 + len(text) + 8 + len(self.binary)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as output:
            output.write(struct.pack("<III", 0x46546C67, 2, total))
            output.write(struct.pack("<II", len(text), 0x4E4F534A))
            output.write(text)
            output.write(struct.pack("<II", len(self.binary), 0x004E4942))
            output.write(self.binary)
        print("wrote %s (%d bytes)" % (os.path.relpath(path, REPOSITORY), total))


# Geometry.
def scaled(point, scale):
    return (point[0] * scale[0], point[1] * scale[1], point[2] * scale[2])


def subtract(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


class Primitive:
    def __init__(self):
        self.positions = []
        self.normals = []
        self.joints = []
        self.weights = []
        self.indices = []

    def add_quad(self, corners, normal, influences):
        """corners: four positions; influences: a list of (bone, weight) per corner. Wound counter-clockwise seen from
        outside, as glTF's front faces are."""
        if dot(cross(subtract(corners[1], corners[0]), subtract(corners[2], corners[0])), normal) < 0.0:
            corners = [corners[0], corners[3], corners[2], corners[1]]
            influences = [influences[0], influences[3], influences[2], influences[1]]
        base = len(self.positions)
        for corner, influence in zip(corners, influences):
            self.positions.append(corner)
            self.normals.append(normal)
            joints = [BONE_INDEX[bone] for bone, _ in influence] + [0] * (4 - len(influence))
            weights = [weight for _, weight in influence] + [0.0] * (4 - len(influence))
            self.joints.append(tuple(joints))
            self.weights.append(tuple(weights))
        self.indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def add_box(self, low, high, bottom_influence, top_influence):
        """A box whose bottom four corners follow bottom_influence and the top four top_influence."""
        x0, y0, z0 = low
        x1, y1, z1 = high
        def influence(y):
            return bottom_influence if y == y0 else top_influence
        faces = [
            ((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
            ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
            ((0, 1, 0), [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)]),
            ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
            ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
            ((0, 0, -1), [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)]),
        ]
        for normal, corners in faces:
            self.add_quad(corners, normal, [influence(corner[1]) for corner in corners])


def build_primitives(scale, dropped_boxes, torso_rings):
    primitives = [Primitive() for _ in MATERIALS]
    for index, (material, bone, low, high) in enumerate(BOXES):
        if index in dropped_boxes:
            continue
        influence = [(bone, 1.0)]
        primitives[MATERIAL_INDEX[material]].add_box(scaled(low, scale), scaled(high, scale), influence, influence)
    half_x, half_z = TORSO_HALF_SIZE
    for (bottom, bottom_influence), (top, top_influence) in zip(torso_rings, torso_rings[1:]):
        primitives[MATERIAL_INDEX["shirt"]].add_box(scaled((-half_x, bottom, -half_z), scale),
                                                     scaled((half_x, top, half_z), scale),
                                                     bottom_influence, top_influence)
    return primitives


def bone_positions(scale):
    return [scaled(position, scale) for _, _, position in BONES]


def local_offsets(positions):
    offsets = []
    for index, (_, parent, _) in enumerate(BONES):
        parent_position = positions[BONE_INDEX[parent]] if parent is not None else (0.0, 0.0, 0.0)
        offsets.append(subtract(positions[index], parent_position))
    return offsets


def joint_nodes(offsets, first_node):
    """The skeleton's nodes, numbered from first_node in the skeleton's order, each with its rest offset."""
    nodes = []
    for index, (name, _, _) in enumerate(BONES):
        children = [first_node + child for child, (_, parent, _) in enumerate(BONES) if parent == name]
        node = {"name": name, "translation": list(offsets[index])}
        if children:
            node["children"] = children
        nodes.append(node)
    return nodes


def add_mesh(writer, document, scale, dropped_boxes, torso_rings, joint_first_node):
    primitives = []
    for material_index, primitive in enumerate(build_primitives(scale, dropped_boxes, torso_rings)):
        if not primitive.positions:
            continue
        attributes = {
            "POSITION": writer.add_floats(primitive.positions, "VEC3", ARRAY_BUFFER, True),
            "NORMAL": writer.add_floats(primitive.normals, "VEC3", ARRAY_BUFFER),
            "JOINTS_0": writer.add_joints(primitive.joints),
            "WEIGHTS_0": writer.add_floats(primitive.weights, "VEC4", ARRAY_BUFFER),
        }
        primitives.append({"attributes": attributes, "indices": writer.add_indices(primitive.indices),
                           "material": material_index})
    document["materials"] = [
        {"name": name, "pbrMetallicRoughness": {"baseColorFactor": base_color_factor(color),
                                                "metallicFactor": 0.0, "roughnessFactor": 0.85}}
        for name, color in MATERIALS
    ]
    document["meshes"] = [{"name": "body", "primitives": primitives}]
    positions = bone_positions(scale)
    inverse_binds = [(1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -p[0], -p[1], -p[2], 1.0)
                     for p in positions]
    document["skins"] = [{"name": "skeleton", "joints": [joint_first_node + i for i in range(len(BONES))],
                          "inverseBindMatrices": writer.add_floats(inverse_binds, "MAT4"),
                          "skeleton": joint_first_node}]


def add_clips(writer, document, offsets, joint_first_node):
    animations = []
    hips_rest = offsets[BONE_INDEX["hips"]]
    for name, duration, pose_at in CLIPS:
        key_count = int(round(duration * KEYS_PER_SECOND)) + 1
        times = [k / KEYS_PER_SECOND for k in range(key_count - 1)] + [duration]
        poses = [pose_at(time) for time in times]
        samplers = []
        channels = []
        time_accessor = writer.add_scalars(times, True)
        for bone_index, (bone_name, _, _) in enumerate(BONES):
            rotations = []
            for pose in poses:
                rotation = pose.rotations[bone_name]
                if rotations and dot(rotation[:3], rotations[-1][:3]) + rotation[3] * rotations[-1][3] < 0.0:
                    rotation = tuple(-component for component in rotation)  # keep neighbours on one hemisphere
                rotations.append(rotation)
            samplers.append({"input": time_accessor, "output": writer.add_floats(rotations, "VEC4"),
                             "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1,
                             "target": {"node": joint_first_node + bone_index, "path": "rotation"}})
        # The hips move as the clip says (the rest offset plus the pose's change from the rest height).
        translations = [(hips_rest[0] + pose.hips[0], hips_rest[1] + (pose.hips[1] - HIPS_HEIGHT),
                         hips_rest[2] + pose.hips[2]) for pose in poses]
        samplers.append({"input": time_accessor, "output": writer.add_floats(translations, "VEC3"),
                         "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1,
                         "target": {"node": joint_first_node + BONE_INDEX["hips"], "path": "translation"}})
        animations.append({"name": name, "samplers": samplers, "channels": channels})
    document["animations"] = animations


def write_model(path, scale, dropped_boxes, torso_rings, with_clips):
    writer = GltfWriter()
    offsets = local_offsets(bone_positions(scale))
    # Node 0 is the mesh (at the root, untransformed: glTF ignores a skinned mesh node's transform anyway), the joints
    # follow from node 1.
    document = {"scene": 0, "scenes": [{"nodes": [0, 1]}],
                "nodes": [{"name": "body", "mesh": 0, "skin": 0}] + joint_nodes(offsets, 1)}
    add_mesh(writer, document, scale, dropped_boxes, torso_rings, 1)
    if with_clips:
        add_clips(writer, document, offsets, 1)
    writer.write(path, document)


def write_library(path):
    writer = GltfWriter()
    offsets = local_offsets(bone_positions((1.0, 1.0, 1.0)))
    # As Blender exports an armature's actions alone: the armature object as a root node above the bones, no skin.
    document = {"scene": 0, "scenes": [{"nodes": [0]}],
                "nodes": [{"name": "armature", "children": [1]}] + joint_nodes(offsets, 1)}
    add_clips(writer, document, offsets, 1)
    writer.write(path, document)


# The props: rigid boxes, one primitive per material, no skin. The bench faces +Z (its sitter's way), its seat's
# top SEAT_HEIGHT up, 0.38 m deep around z = 0; the cup stands on its origin, its grip on its -X side.
BENCH_MATERIALS = [("wood", (0.45, 0.30, 0.18)), ("iron", (0.12, 0.12, 0.13))]
BENCH_BOXES = [
    ("wood", (-0.70, 0.41, -0.19), (0.70, SEAT_HEIGHT, 0.19)),     # the seat
    ("wood", (-0.70, 0.56, -0.20), (0.70, 0.84, -0.16)),           # the backrest
    ("iron", (-0.64, 0.0, -0.17), (-0.58, 0.41, 0.17)),            # the left frame
    ("iron", (0.58, 0.0, -0.17), (0.64, 0.41, 0.17)),              # the right frame
    ("iron", (-0.64, 0.41, -0.20), (-0.58, 0.56, -0.16)),          # the backrest's posts
    ("iron", (0.58, 0.41, -0.20), (0.64, 0.56, -0.16)),
]
CUP_MATERIALS = [("ceramic", (0.86, 0.84, 0.78)), ("coffee", (0.22, 0.12, 0.06))]
CUP_BOXES = [
    ("ceramic", (-0.04, 0.0, -0.04), (0.04, 0.09, 0.04)),          # the cup
    ("ceramic", (0.04, 0.025, -0.008), (0.065, 0.07, 0.008)),      # its handle
    ("coffee", (-0.034, 0.0901, -0.034), (0.034, 0.0905, 0.034)),  # the coffee's top
]


def write_prop(path, materials, boxes):
    writer = GltfWriter()
    material_index = {name: index for index, (name, _) in enumerate(materials)}
    rows = [([], [], []) for _ in materials]
    for material, (x0, y0, z0), (x1, y1, z1) in boxes:
        positions, normals, indices = rows[material_index[material]]
        faces = [
            ((1, 0, 0), [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)]),
            ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
            ((0, 1, 0), [(x0, y1, z0), (x0, y1, z1), (x1, y1, z1), (x1, y1, z0)]),
            ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
            ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
            ((0, 0, -1), [(x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (x1, y0, z0)]),
        ]
        for normal, corners in faces:
            if dot(cross(subtract(corners[1], corners[0]), subtract(corners[2], corners[0])), normal) < 0.0:
                corners = [corners[0], corners[3], corners[2], corners[1]]
            base = len(positions)
            positions += corners
            normals += [normal] * 4
            indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    primitives = []
    for index, (positions, normals, indices) in enumerate(rows):
        if positions:
            attributes = {"POSITION": writer.add_floats(positions, "VEC3", ARRAY_BUFFER, True),
                          "NORMAL": writer.add_floats(normals, "VEC3", ARRAY_BUFFER)}
            primitives.append({"attributes": attributes, "indices": writer.add_indices(indices), "material": index})
    document = {"scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"name": "prop", "mesh": 0}],
                "meshes": [{"name": "prop", "primitives": primitives}],
                "materials": [{"name": name, "pbrMetallicRoughness": {
                    "baseColorFactor": base_color_factor(color), "metallicFactor": 0.0,
                    "roughnessFactor": 0.8}} for name, color in materials]}
    writer.write(path, document)


def main():
    write_model(os.path.join(MODEL_DIRECTORY, "tall.glb"), (1.0, 1.0, 1.0), set(), TORSO_RINGS, False)
    write_model(os.path.join(MODEL_DIRECTORY, "tall_lod1.glb"), (1.0, 1.0, 1.0), LOD1_DROPPED_BOXES, LOD1_TORSO_RINGS,
                False)
    write_model(os.path.join(MODEL_DIRECTORY, "short.glb"), SHORT_SCALE, set(), TORSO_RINGS, False)
    write_library(os.path.join(LIBRARY_DIRECTORY, "motions.glb"))
    write_model(os.path.join(TEST_DATA_DIRECTORY, "placeholder_humanoid_with_clips.glb"), (1.0, 1.0, 1.0), set(),
                TORSO_RINGS, True)
    write_prop(os.path.join(PROP_DIRECTORY, "bench.glb"), BENCH_MATERIALS, BENCH_BOXES)
    write_prop(os.path.join(PROP_DIRECTORY, "cup.glb"), CUP_MATERIALS, CUP_BOXES)
    # The actions' clips meet their props: the seat, and the hand's socket on the cup's grip and the door's panel at
    # the grab, the release and the push. Across, the palm is where the props' sockets must put their targets.
    check_seat()
    for name, at, grip in (("pick_up", 0.65, REACH_GRIP), ("put_down", 0.8, REACH_GRIP), ("open_door", 0.55, DOOR_PUSH)):
        print("%s: the hand's socket is %.3f m across (+X is the figure's left) at %.2f s" % (
            name, check_reach(name, at, grip), at))


if __name__ == "__main__":
    main()
