# GameMath

`include/GameMath.h` (header-only), for large worlds. Tests: `tests/WRVectorMathTests.c`.

The bridge between WRFramework's double-precision maths, `WRVectorMath` (`include/wr/WRVectorMath.h`:
`Vector3Double`, `Matrix4Double`, `BoundingBoxDouble`), and raylib's float `Vector3` and `Matrix`. The double types
live in WRFramework so other projects can reuse them; WRFramework uses no raylib, so the conversions live here. Owns
nothing; no lifecycle. `WRVectorMath` also has a `Vector2Double` (for future 2D projects); there is
no bridge to raylib's `Vector2` yet, since this game has no 2D doubles. Add `GameMath_FromVector2` and its siblings,
like the `Vector3` ones, when one is needed.

**Look in `WRVectorMath` before writing vector maths.** Both vector types have raymath's operations in double: besides
the arithmetic, perpendiculars, projection and rejection (a motion along a wall), reflection and refraction, moving
towards a target, clamping per axis and by length, barycentric weights, angles between vectors (signed in 2D), 2D
rotation and headings (`Vector2Double_FromAngle`, `_GetAngle`), and 3D rotation about an axis. The list is in
`agents/WRFramework.md`, the contracts in `include/wr/WRVectorMath.h`.

## The rule: far positions become floats only relative to an origin

A float's step is 0.24 mm at 2–4 km from the origin, 1 mm past 8 km and 1 m at 10 000 km; a double's is 1.9 nm at
10 000 km. Positions that can be far from the world's origin stay doubles, and become floats only as an offset from a
nearby origin (the camera for rendering, a query's start for collision), subtracted in double and rounded once:

| Function | Gives |
|---|---|
| `GameMath_ToVector3Relative(vector, origin)` | `vector - origin` as a `Vector3` (`Vector3Double_ToFloatsRelative`) |
| `GameMath_ToMatrixRelative(&matrix, origin)` | the matrix with its translation minus the origin, as a `Matrix`: for a world matrix, the same placement relative to the origin (`Matrix4Double_ToFloatsRelative`) |
| `GameMath_ToVector3(vector)`, `GameMath_ToMatrix(&matrix)` | plain rounding: for directions, rotations, sizes, or values known to be near the origin |
| `GameMath_FromVector3(vector)`, `GameMath_FromMatrix(matrix)` | raylib's floats widened to double, exactly |
| `GameMath_MatrixFromFloats(values)` | a `Matrix` from 16 column-major floats: the inverse of raymath's `MatrixToFloatV` |

Never round a far position to float and subtract the origin after: the float keeps the absolute position's error (up
to half a metre at 10 000 km), which is what makes a far-away world shake. The tests show both.

## raylib's Matrix: field by field, never memcpy

raylib's `Matrix` declares its fields row by row (`m0, m4, m8, m12`, then `m1, m5, ...`), although its maths is
column-major: field `mN` is entry N of `MatrixToFloatV`, which is also `Matrix4Double`'s `M[N]`. Its memory is
therefore the transpose of the column-major order, and every conversion goes field by field (`tests/WRVectorMathTests.c`
pins it). Never `memcpy` between a `Matrix` and a `Matrix4Double` or a float array.

## WRVectorMath's conventions (what code written against it relies on)

The header documents every function; the essentials:

- **raymath's conventions, in double**, so float code ports operand for operand:
  - `Matrix4Double` is column-major: row r of column c is `M[c * 4 + r]`, the translation `M[12..14]`.
  - `Matrix4Double_Multiply(&a, &b)` applies a first, then b (the product b·a), as `MatrixMultiply(a, b)` does: a
    child's world matrix is `Multiply(&local, &parentWorld)`. Matrices go by pointer, so a scale, then a rotation, then
    a translation is two multiplies with the first product in a named variable.
  - `Matrix4Double_FromRotationXYZ(angles)` is `MatrixRotateXYZ` (radians): X(x)·Y(y)·Z(z) on column vectors, so Z turns
    first. `FromRotationX/Y/Z`, `FromTranslation` and `FromScale` match their raymath counterparts.
  - Points (`TransformPoint`), directions (`TransformDirection`) and boxes (`BoundingBoxDouble_Transform`) are moved as
    by an affine matrix: the bottom row is ignored.
- **Passing:** vectors and boxes by value; matrices (128 bytes) by const pointer, results by value. The one fallible
  operation, inversion, returns a `bool` with an out pointer (which may point to the input).
- **Inversion has no tolerance.** `Matrix4Double_TryInvert` (any 4x4) and `Matrix4Double_TryInvertAffine` refuse a
  zero, NaN or infinite determinant, and a reciprocal or an inverse that would not be finite: the singular matrices, a
  NaN translation, and a few of extreme scale (a uniform scale past about 5.6e102 or below about 1.8e-103). A nearly
  singular matrix inverts to large entries. Code with a policy keeps its own check, as
  `WorldTransform_TryGetLocalMatrix` does with `MINIMUM_INVERTIBLE_VOLUME`.
- **Far out, subtract first.** At 10 000 km a stored translation is rounded to the double's 1.9 nm step. A matrix times
  its inverse there cancels terms as large as the translation: measurements found up to 22 nm in the translation
  (2.5e-15 of it), and placing a child 0.5 mm from its parent as `inverse(parent) × world` came within 7 nm, as
  subtracting the translations first and turning the difference by the inverse's linear part came within 2 nm (the
  world translation's own rounding). Relative placements should subtract translations first.
- `BoundingBoxDouble_Transform` sums its products in `TransformPoint`'s order, so the box holds every corner exactly as
  `TransformPoint` moves it and touches the outermost ones. A zero matrix entry adds nothing, so infinite bounds (a
  floor slab, a half-space) transform correctly; the box measures (centre, extents, size, area) need finite bounds.
  `BoundingBoxDouble_IntersectsRay` takes the origin in double, allows zero direction components, counts faces as hits,
  and never hits with bad input (NaN, an inverted box, a negative length).
- `Vector3Double_Length` overflows past components of about 1e154 and reads 0 below about 1e-162;
  `Vector3Double_NormalizeOr` has neither limit (it scales by the largest component first).

## Users

- **The renderer (render space: WorldRuntime.md):** `GameCamera.Position` is a `Vector3Double`; the render cache
  (`WorldRenderCache_Sync`) reads each object's double world matrix and position and makes their render-space floats
  with `GameMath_ToMatrixRelative` and `GameMath_ToVector3Relative` against the frame's origin; the collision debug
  draws and the debug grid convert the same way; `WorldRenderSpace` snaps the shadow cascades in double.
- **The world (WorldData.md, "Positions are doubles"):** objects' positions and world matrices, the hierarchy and
  `WorldTransform` are doubles; queries compare distances in double; files and `vector3` data values keep doubles.
- **Collision (WorldCollision.md):** placements and poses are double; before every narrowphase call the world
  makes them relative to an origin near the test (`GameMath_ToVector3Relative`, and the collision world's own
  `WorldCollisionInternal_ToRelative` built on it); `GameMath_FromMatrix` widens a shape's float transform to compose it
  in double.
- **Characters (Characters.md):** the feet, ground points and eyes are doubles; moves (floats) are added with
  `GameMath_FromVector3`, and differences of positions come back as floats with `GameMath_ToVector3Relative`.
- **The test frame:** the flycam moves the double camera; F11's far shift moves objects, the camera and the frame's
  own places in double.
