# Paths

A line through nodes, any length, that bends: the one tool behind fences, hedges, railings, roads, kerbs, walls, streams, rows
of lamps. A generator (a path object, a sweep, a terrain effect) reads a path and makes what it makes along it. A closed path is
also a **region**: an area for scattering, painting and scripts ("is the player in the park").

A path holds **raw geometry only**. Nothing on it says anything about the ground: whatever reads it decides whether and how to
snap, so two generators on one path can differ, and only a node's section settings can say otherwise for one stretch (a bridge).

| Module | Files | What it is |
|---|---|---|
| PathCurve | `include/PathCurve.h`, `source/PathCurve.c` | A path as pure maths, with no world and no GL: segments, arc length, frames, values, sections, samples, the nearest point, the region. |
| WorldPathComponent | `include/WorldPathComponent.h`, `source/WorldPathComponent.c` | Component type 10, `path`: the nodes and the settings, the curve kept up to date, finding a path through the hierarchy, watching it, positions and regions in the world. |
| PathGround | `include/PathGround.h`, `source/PathGround.c` | Ground projection: the one function every generator shares. |
| WorldPathDebug (private) | `source/WorldPathDebug.{h,c}` | What the debug view draws, as a list of lines. The renderer draws any such list (`WorldRenderer_SetDebugLines`, `source/WorldRendererDebugLines.c`). |
| Test content | objects 95 to 98 of the test world, `source/WorldTestPaths.{h,c}` | A walk, a park, two helices; the view's keys. |

The format: `references/world_structure.md`, "The path block". Tests: `tests/PathCurveTests.c`, `tests/WorldPathTests.c` (also
173 km out), the record list cases of `tests/PropertyDescriptorTests.c`, the unknown names in records in `tests/WorldFileTests.c`.

## Nodes

`PathNode`: a position (double, relative to the object), a mode, in and out handles, a roll, a width, a scale, and the settings
of the section it starts. In a component they are a `RecordList` of the `path_node` table
([PropertyDescriptors.md](PropertyDescriptors.md), "Record lists"), so a node's rows, rules and JSON come from one table and the
editor can show a selected node like any row set. `PathNode_CreateDefault` gives what the table's defaults give.

**Between two nodes lies one segment,** a line or a cubic Bezier, by the modes at its ends:

| From \ to | smooth | corner | bezier |
|---|---|---|---|
| **smooth** | Catmull-Rom | line | curve: the smooth node's tangent, the handle |
| **corner** | line | line | curve: the handle alone |
| **bezier** | curve | curve: the handle alone | curve: both handles |

- **Smooth** is centripetal Catmull-Rom (its knots spaced by the square root of the chord), which never loops or cusps however
  unevenly the nodes lie. It is held as a Bezier: `P1 = P + T * gap / 3` from the derivative `T` in the knot parameter, which is
  one vector at a node, so a smooth node has no kink. An open end reflects its neighbour, which aims the first control point
  along the chord.
- **A segment touching a corner is straight,** and a smooth node next to a corner takes that line's direction, so it has no
  kink: straight, bend, straight is corner, smooth, smooth, corner. A smooth node between two corners has straights on both sides,
  and so the one kink a smooth node can have.
- **A handle wins over a corner's straightness:** a handle that did nothing while being dragged would be worse. A retracted handle
  makes a straight line anyway (no node has to be a corner for that).

## What a point is

`PathCurve_GetPointAt(curve, distance, &point)` gives a position, a frame and the values there.

- **Distance** is arc length in the object's own space. A scaled object scales the distances along with whatever is generated
  in that space. The solve is exact: Gauss-Legendre over a table of 4 to 32 intervals per cubic (Wang's bound at 1 cm), then a
  safeguarded Newton step inside the interval, to about a micrometre. On a closed path a distance wraps; on an open one it
  clamps.
- **At a corner** a point leaves it: it has the tangent of the segment after it. Something that must end at a corner with the
  tangent it arrived with (the last ring of a sweep) reads through `PathCurve_GetSectionPoint`, which stays inside its section.
- **The frame** is unit and square, `Right = Tangent x Up`: right as you look along the path, seen from above.
  - `upright`: the up stays as near the object's +Y as the tangent allows. A run straight up or down keeps the right of what came
    before it (after it, for a path that starts so), since nothing else can say which way is right.
  - `rotation_minimizing`: the frame turns as little as it can, by the double reflection method over the table, starting upright,
    with the smallest rotation across each kink. A closed loop arrives back twisted (a loop out of plane has to); the twist is spread
    along its length, so its ends meet and no stretch stands out. Doubling straight back turns about the up.
  - **The roll** then turns the frame about the tangent; positive leans the up toward the right, as a road banks into a right-hand
    bend.
- **Roll, width and scale** reach the points between nodes through a monotone cubic over the distance (Steffen's slopes): as smooth
  as the curve, a steady ramp stays one, a flat stretch stays flat, and never past either neighbour's value, so a width of 0
  next to a 2 stays between them. Width and scale are factors of what the reader's own settings say, default 1, at least 0.

Nothing here gives a NaN: zero-length segments (nodes at one place, which editing makes every day), cusps, retracted handles,
and a path all at one place have a tangent from a secant (of differences from the segment's start, so it stays exact far from the
object's origin) or a neighbour, and a path of no length runs along +Z. `PathCurve_Build` refuses a coordinate past
`PATH_CURVE_COORDINATE_LIMIT` (1e12 m, far past what a row allows), so squared lengths cannot overflow.

## Sections

Node 0 of an open path, every `corner` and every node with `starts_section` start a section, which runs to the next start. A
section's settings are its first node's: `IsSkipped` (a gate's opening) and the terrain snapping's override pair
(`PathCurveSection_ResolveSnapsToTerrain(section, readerDefault)`). What they mean is up to whoever reads the path.

The last node of an open path starts nothing. A closed path's last section runs through the end of the list to the first start, so
`StartDistance + Length` may pass the path's length (`GetPointAt` wraps it, and `GetSectionPoint` takes a distance from the section's
start). A closed path with no start at all is one loop from node 0. Two corners at one place make a section of no length, which no
point of `GetPointAt` falls in. One node alone is a section of no segments, so the section a point names always exists; only a path
with no node has none.

## Samples

`PathCurve_WriteSamples` appends distances along the whole path, and `PathCurve_WriteSectionSamples` local distances along one
section: 0, every node, the end, and between them at most the spacing apart, closer where the curve bends (`PathSampleOptions`: the
spacing, how far the curve may leave the chord, how far the tangent may turn). A step is halved while its middle or its quarters leave
its chord, or its tangent turns too far across either half: an S has parallel ends and its middle on its chord, and only that looks at
it properly. A straight segment takes the spacing and nothing more. Each step is halved at most 12 times, so a cusp cannot hold it up,
and one call writes at most `PATH_CURVE_MAX_SAMPLES` (past that it refuses, so a very long path with a very short spacing costs no memory
for it). Distances strictly rise, and a section's last is exactly its length, whatever the sums rounded to. The component's
`sample_spacing` is what readers use by default.

## The nearest point

`PathCurve_FindNearest(curve, point, ignoresHeight, &nearest, &separation)`: segments are skipped by their control points' boxes
against the best so far. A cubic can pass a point more than once, so every chord of its table nearer than its neighbours, that the
table's flatness leaves in reach of the best, gets a golden section around it. With `ignoresHeight` everything is measured in X and Z
(how far is this tree from the road, in plan). On a tie, the first along the path. Two passes within a chord or two of each other on a
very bent cubic can still settle for the worse by a sliver (under a millimetre on a cubic a metre across).

## Regions

A closed path encloses the polygon of its table's points in its X and Z, to about a centimetre (more on a very long segment, whose
table is capped: a 200 m radius from four nodes stays within 6 cm). **Inside is by non-zero winding,** so it does not matter which
way it is drawn, and a loop drawn twice round is still inside. Height does not count: a region is a prism along the object's Y.

- `PathCurve_IsPointInside(curve, x, z)` in the path's space; `WorldPathComponent_IsWorldPointInside(path, world)` in the world, in
  double, 100 km out as at the origin. O(polygon points) after a test against its box.
- `World_Query` filters by region: `InsideRegionObject` (an id) and `InsideRegionTag` (any closed path whose object carries the tag:
  tags name regions) ([ObjectData.md](ObjectData.md), "Queries").

## The component

`WorldPathComponent`: `nodes`, `is_closed`, `up_mode`, `sample_spacing`. Not a transform driver.

**The curve is always valid.** `WorldPathComponent_GetCurve` hands it out; `OnPropertySet` rebuilds it whenever a row that shapes it
changes, by a setter, a table write, a read or a copy, so read-only code never builds and the renderer's debug view never writes.
`_SetNodes`, `_SetNode`, `_InsertNode` and `_RemoveNode` take the row's rules and change nothing on a refusal. They cost O(nodes), for
an editor's drag or a script, not for something every tick on a long path.

**Readers** find their path with `WorldPathComponent_FindForObject(object)`: its own, or the nearest ancestor's. A lone fence keeps
its path and settings on one object; a road's parts are its children. They rebuild live, never only at load, through a
`WorldPathWatch` they keep and ask once a tick: `HasPathChanged` for a change of any row, `HasPlaceChanged` when the path's object
moved in the world (what lies under that object moves with it, but what was snapped to the ground needs doing again). A path found,
lost or replaced by another component counts as both (the watch compares serials, which a reused address cannot fool).

**In the world:** `_LocalToWorld`, `_WorldToLocal` (the translation is taken off before the inverse, so it is exact far out) and
`_PointToWorld` (a curve point's frame turned, and kept unit and square under an uneven scale). Scaled flat, nothing comes back.

## The ground

`PathGround_Project(collision, point, &settings, &hit)`: a straight ray down the world's Y, from the point's own height plus a
search height (5 m by default) down to a search depth below it (1 km), not from the sky, so a path in a tunnel finds the tunnel's
floor and not the hill above it. Onto terrain only (`IsTerrainOnly` in the filter) or onto chosen collision layers (a railing on a
bridge deck); an offset lifts the answer, an object and its subtree can be ignored (a reader's own colliders). A surface the ray
starts inside does not count. When it finds nothing, the reader keeps the point's own height.

It goes through the collision world, which already holds every terrain chunk in a tree: what it can find is what collides, so a
terrain with its collision off is not found, a one-sided terrain is met only from above, and the collision world must have synced
since the ground last changed.

## The debug view

`WorldPathDebug_Build` makes a list of lines on the update side, where it can ask the collision world, and
`WorldRenderer_SetDebugLines` hands it to the renderer, which only reads it: each line solid where it is in view and faint behind
things, against the eye's exposure so its colour stays itself. Curves by section (a skipped one dim), node crosses by mode (white,
yellow, cyan) with a bezier node's handles, a tall tick at each section's start, a closed path's area hatched at its middle height;
in detail, a tick per sample and every 2 m a frame (tangent blue, up green at the scale's size, right red across the width) with
the ground under it (grey) or a violet tick where a section turns snapping off. At most 200 000 lines.

In the test frame: Backspace steps the view, Shift with Backspace moves one of the walk's nodes up and down, and the camera entering
or leaving the park (`tbp:test_park`) is logged. The test world holds:

- **`path_walk` (95):** corner, a smooth bend (banked, widening), a corner into a stretch that ends with a skipped section (a gate), a
  bridge section over the valley that turns snapping off, and a bezier section;
- **`path_park` (96):** a closed smooth ring, tagged `tbp:test_park`;
- **`path_helix` (97) and `path_helix_upright` (98):** two turns climbing 10 m, rotation minimizing and upright, to compare their frames.

## Limits and costs

- 16 384 nodes a path (`WORLD_PATH_MAX_NODES`). A curve takes 160 bytes a segment, 40 a node and 8 a table entry (12 more when
  rotation minimizing, 16 more when closed), with about 25 entries for a curved segment of 15 m and 2 for a straight one: about 40 KB
  for an open path of 100 curved nodes, 110 KB closed and rotation minimizing, at most about 22 MB for one path at the node limit.
  Curves are built for every path a world holds; a world file's limit on its parsed tree, where a node takes at least 1.6 KB, stops a
  hostile one at about 650 000 nodes in either format: about 1 GB of curves.
- A point at a distance: about 0.3 microseconds. The nearest point: about 75 microseconds on 100 curved nodes, 0.7 ms on 1000, so
  anything calling it per terrain vertex or per scattered copy goes through `PathCurve_GetBounds` first. Inside a region: about 3
  microseconds on 100 curved nodes. Samples: about 0.7 microseconds each. Building: about 1.1 ms per thousand curved nodes (4.4 ms
  closed and rotation minimizing), on every change: a drag on a very long path is the place this shows.
