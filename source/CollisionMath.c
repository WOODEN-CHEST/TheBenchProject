#include "CollisionMath.h"

/*
 * The shared collision maths (source/CollisionMath.h): orthonormal bases, closest points on segments and triangles,
 * affine transforms and rigid poses.
 */


// Macros.
/** Singular transforms: a determinant below this share of the product of the column lengths is refused. */
#define AFFINE_SINGULAR_TOLERANCE 1e-7f
/** Below this cosine of half the turn, slerp falls back to a normalised lerp (the arcs agree there). */
#define SLERP_LINEAR_LIMIT 0.9995f


// Static functions.
static float Clamp01(float value)
{
    return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value);
}

/* The closest point of edge (p, q), as weights in double precision. */
static void ClosestOnEdgeDouble(const double* p, const double* q, const double* point, double* outWeightP,
    double* outWeightQ)
{
    double Edge[3] = { q[0] - p[0], q[1] - p[1], q[2] - p[2] };
    double ToPoint[3] = { point[0] - p[0], point[1] - p[1], point[2] - p[2] };
    double LengthSquared = (Edge[0] * Edge[0]) + (Edge[1] * Edge[1]) + (Edge[2] * Edge[2]);
    double T = (LengthSquared > 0.0)
        ? (((ToPoint[0] * Edge[0]) + (ToPoint[1] * Edge[1]) + (ToPoint[2] * Edge[2])) / LengthSquared) : 0.0;
    T = (T < 0.0) ? 0.0 : ((T > 1.0) ? 1.0 : T);
    *outWeightP = 1.0 - T;
    *outWeightQ = T;
}


// Public functions: vectors.
void CollisionMath_GetBasis(Vector3 normal, Vector3* outU, Vector3* outV)
{
    // Duff et al., "Building an Orthonormal Basis, Revisited" (2017): branchless apart from the sign, no division by
    // a small number.
    float Sign = copysignf(1.0f, normal.z);
    float A = -1.0f / (Sign + normal.z);
    float B = normal.x * normal.y * A;
    *outU = CollisionMath_Vector(1.0f + (Sign * normal.x * normal.x * A), Sign * B, -Sign * normal.x);
    *outV = CollisionMath_Vector(B, Sign + (normal.y * normal.y * A), -normal.y);
}

float CollisionMath_GetClosestSegmentParameter(Vector3 a, Vector3 b, Vector3 point)
{
    Vector3 Edge = CollisionMath_Subtract(b, a);
    float LengthSquared = CollisionMath_LengthSquared(Edge);
    if (LengthSquared <= COLLISION_MATH_EPSILON)
    {
        return 0.0f;
    }
    return Clamp01(CollisionMath_Dot(CollisionMath_Subtract(point, a), Edge) / LengthSquared);
}

void CollisionMath_GetClosestSegmentParameters(Vector3 p1, Vector3 q1, Vector3 p2, Vector3 q2, float* outS,
    float* outT)
{
    Vector3 D1 = CollisionMath_Subtract(q1, p1);
    Vector3 D2 = CollisionMath_Subtract(q2, p2);
    Vector3 R = CollisionMath_Subtract(p1, p2);
    float A = CollisionMath_Dot(D1, D1);
    float E = CollisionMath_Dot(D2, D2);
    float F = CollisionMath_Dot(D2, R);
    float S = 0.0f;
    float T = 0.0f;
    if ((A <= COLLISION_MATH_EPSILON) && (E <= COLLISION_MATH_EPSILON))
    {
        *outS = 0.0f;
        *outT = 0.0f;
        return;
    }
    if (A <= COLLISION_MATH_EPSILON)
    {
        T = Clamp01(F / E);
    }
    else
    {
        float C = CollisionMath_Dot(D1, R);
        if (E <= COLLISION_MATH_EPSILON)
        {
            S = Clamp01(-C / A);
        }
        else
        {
            float B = CollisionMath_Dot(D1, D2);
            float Denominator = (A * E) - (B * B);
            S = (Denominator > COLLISION_MATH_EPSILON * A * E) ? Clamp01(((B * F) - (C * E)) / Denominator) : 0.0f;
            T = ((B * S) + F) / E;
            if (T < 0.0f)
            {
                T = 0.0f;
                S = Clamp01(-C / A);
            }
            else if (T > 1.0f)
            {
                T = 1.0f;
                S = Clamp01((B - C) / A);
            }
        }
    }
    *outS = S;
    *outT = T;
}

/* The region tests and weights run in double precision: GJK builds slivers (two support points of a smooth shape a
 * millimetre apart, the third a metre away) whose differences of products cancel in float, so the weights would no
 * longer give the closest point, and the witness points they place would lie millimetres off. */
Vector3 CollisionMath_GetClosestTrianglePoint(Vector3 a, Vector3 b, Vector3 c, Vector3 point, Vector3* outWeights,
    uint32_t* outRegion)
{
    const double A[3] = { a.x, a.y, a.z };
    const double B[3] = { b.x, b.y, b.z };
    const double C[3] = { c.x, c.y, c.z };
    const double P[3] = { point.x, point.y, point.z };
    double Ab[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
    double Ac[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
    double Ap[3] = { P[0] - A[0], P[1] - A[1], P[2] - A[2] };
    double Bp[3] = { P[0] - B[0], P[1] - B[1], P[2] - B[2] };
    double Cp[3] = { P[0] - C[0], P[1] - C[1], P[2] - C[2] };
#define DOT3(u, v) (((u)[0] * (v)[0]) + ((u)[1] * (v)[1]) + ((u)[2] * (v)[2]))
    double D1 = DOT3(Ab, Ap);
    double D2 = DOT3(Ac, Ap);
    double D3 = DOT3(Ab, Bp);
    double D4 = DOT3(Ac, Bp);
    double D5 = DOT3(Ab, Cp);
    double D6 = DOT3(Ac, Cp);
#undef DOT3
    double Weight[3] = { 0.0, 0.0, 0.0 };
    uint32_t Region = 0U;
    double Va = (D3 * D6) - (D5 * D4);
    double Vb = (D5 * D2) - (D1 * D6);
    double Vc = (D1 * D4) - (D3 * D2);
    if ((D1 <= 0.0) && (D2 <= 0.0))
    {
        Weight[0] = 1.0;
        Region = 4U;
    }
    else if ((D3 >= 0.0) && (D4 <= D3))
    {
        Weight[1] = 1.0;
        Region = 5U;
    }
    else if ((Vc <= 0.0) && (D1 >= 0.0) && (D3 <= 0.0))
    {
        ClosestOnEdgeDouble(A, B, P, &Weight[0], &Weight[1]);
        Region = 1U;
    }
    else if ((D6 >= 0.0) && (D5 <= D6))
    {
        Weight[2] = 1.0;
        Region = 6U;
    }
    else if ((Vb <= 0.0) && (D2 >= 0.0) && (D6 <= 0.0))
    {
        ClosestOnEdgeDouble(A, C, P, &Weight[0], &Weight[2]);
        Region = 3U;
    }
    else if ((Va <= 0.0) && ((D4 - D3) >= 0.0) && ((D5 - D6) >= 0.0))
    {
        ClosestOnEdgeDouble(B, C, P, &Weight[1], &Weight[2]);
        Region = 2U;
    }
    else
    {
        double Sum = Va + Vb + Vc;
        if (!(fabs(Sum) > 0.0))
        {
            // A degenerate triangle: the nearer of edges ab and bc.
            double Wa = 0.0;
            double Wb = 0.0;
            double Wc = 0.0;
            double Wd = 0.0;
            ClosestOnEdgeDouble(A, B, P, &Wa, &Wb);
            ClosestOnEdgeDouble(B, C, P, &Wc, &Wd);
            double OnAb[3] = { (A[0] * Wa) + (B[0] * Wb), (A[1] * Wa) + (B[1] * Wb), (A[2] * Wa) + (B[2] * Wb) };
            double OnBc[3] = { (B[0] * Wc) + (C[0] * Wd), (B[1] * Wc) + (C[1] * Wd), (B[2] * Wc) + (C[2] * Wd) };
            double ToAb = 0.0;
            double ToBc = 0.0;
            for (uint32_t Axis = 0U; Axis < 3U; Axis++)
            {
                ToAb += (OnAb[Axis] - P[Axis]) * (OnAb[Axis] - P[Axis]);
                ToBc += (OnBc[Axis] - P[Axis]) * (OnBc[Axis] - P[Axis]);
            }
            bool IsAbNearer = ToAb <= ToBc;
            Weight[0] = IsAbNearer ? Wa : 0.0;
            Weight[1] = IsAbNearer ? Wb : Wc;
            Weight[2] = IsAbNearer ? 0.0 : Wd;
            Region = IsAbNearer ? 1U : 2U;
        }
        else
        {
            Weight[1] = Vb / Sum;
            Weight[2] = Vc / Sum;
            Weight[0] = 1.0 - Weight[1] - Weight[2];
            Region = 0U;
        }
    }
    *outWeights = CollisionMath_Vector((float)Weight[0], (float)Weight[1], (float)Weight[2]);
    *outRegion = Region;
    if (Region == 0U)
    {
        // The point moved along the plane's normal: for a large triangle near the point, a + Ab V + Ac W tilts the
        // direction to the point (GJK's direction to the origin: a resting box read as apart).
        double Normal[3] = { (Ab[1] * Ac[2]) - (Ab[2] * Ac[1]), (Ab[2] * Ac[0]) - (Ab[0] * Ac[2]),
            (Ab[0] * Ac[1]) - (Ab[1] * Ac[0]) };
        double NormalSquared = (Normal[0] * Normal[0]) + (Normal[1] * Normal[1]) + (Normal[2] * Normal[2]);
        if (NormalSquared > 0.0)
        {
            double Height = ((Normal[0] * Ap[0]) + (Normal[1] * Ap[1]) + (Normal[2] * Ap[2])) / NormalSquared;
            return CollisionMath_Vector((float)(P[0] - (Normal[0] * Height)), (float)(P[1] - (Normal[1] * Height)),
                (float)(P[2] - (Normal[2] * Height)));
        }
    }
    return CollisionMath_Vector((float)((A[0] * Weight[0]) + (B[0] * Weight[1]) + (C[0] * Weight[2])),
        (float)((A[1] * Weight[0]) + (B[1] * Weight[1]) + (C[1] * Weight[2])),
        (float)((A[2] * Weight[0]) + (B[2] * Weight[1]) + (C[2] * Weight[2])));
}


// Public functions: affine transforms.
CollisionAffine CollisionAffine_Multiply(const CollisionAffine* outer, const CollisionAffine* inner)
{
    return (CollisionAffine)
    {
        .X = CollisionAffine_TransformVector(outer, inner->X),
        .Y = CollisionAffine_TransformVector(outer, inner->Y),
        .Z = CollisionAffine_TransformVector(outer, inner->Z),
        .Translation = CollisionAffine_TransformPoint(outer, inner->Translation)
    };
}

float CollisionAffine_GetDeterminant(const CollisionAffine* self)
{
    return CollisionMath_Dot(self->X, CollisionMath_Cross(self->Y, self->Z));
}

bool CollisionAffine_TryInvert(const CollisionAffine* self, CollisionAffine* outInverse)
{
    *outInverse = CollisionAffine_Identity();
    float Determinant = CollisionAffine_GetDeterminant(self);
    float Scale = CollisionMath_Length(self->X) * CollisionMath_Length(self->Y) * CollisionMath_Length(self->Z);
    if (!isfinite(Determinant) || !(fabsf(Determinant) > (AFFINE_SINGULAR_TOLERANCE * Scale))
        || !(Scale > 0.0f))
    {
        return false;
    }
    // The inverse's rows are the cross products of the columns over the determinant.
    float Reciprocal = 1.0f / Determinant;
    Vector3 Row0 = CollisionMath_Scale(CollisionMath_Cross(self->Y, self->Z), Reciprocal);
    Vector3 Row1 = CollisionMath_Scale(CollisionMath_Cross(self->Z, self->X), Reciprocal);
    Vector3 Row2 = CollisionMath_Scale(CollisionMath_Cross(self->X, self->Y), Reciprocal);
    CollisionAffine Inverse;
    Inverse.X = CollisionMath_Vector(Row0.x, Row1.x, Row2.x);
    Inverse.Y = CollisionMath_Vector(Row0.y, Row1.y, Row2.y);
    Inverse.Z = CollisionMath_Vector(Row0.z, Row1.z, Row2.z);
    Inverse.Translation = CollisionMath_Negate(CollisionAffine_TransformVector(&Inverse, self->Translation));
    *outInverse = Inverse;
    return true;
}

bool CollisionAffine_IsSimilarity(const CollisionAffine* self, float* outScale)
{
    float LengthX = CollisionMath_Length(self->X);
    float LengthY = CollisionMath_Length(self->Y);
    float LengthZ = CollisionMath_Length(self->Z);
    float Mean = (LengthX + LengthY + LengthZ) / 3.0f;
    *outScale = Mean;
    if (!(Mean > 0.0f))
    {
        return false;
    }
    float Tolerance = COLLISION_SIMILARITY_TOLERANCE * Mean;
    if ((fabsf(LengthX - Mean) > Tolerance) || (fabsf(LengthY - Mean) > Tolerance) || (fabsf(LengthZ - Mean) > Tolerance))
    {
        return false;
    }
    float SquaredTolerance = COLLISION_SIMILARITY_TOLERANCE * Mean * Mean;
    return (fabsf(CollisionMath_Dot(self->X, self->Y)) <= SquaredTolerance)
        && (fabsf(CollisionMath_Dot(self->Y, self->Z)) <= SquaredTolerance)
        && (fabsf(CollisionMath_Dot(self->Z, self->X)) <= SquaredTolerance);
}

float CollisionAffine_GetMaxStretch(const CollisionAffine* self)
{
    float Similar = 0.0f;
    if (CollisionAffine_IsSimilarity(self, &Similar))
    {
        // A tiny margin keeps the bound an upper bound through the tolerance above.
        return Similar * (1.0f + (2.0f * COLLISION_SIMILARITY_TOLERANCE));
    }
    return sqrtf(CollisionMath_LengthSquared(self->X) + CollisionMath_LengthSquared(self->Y)
        + CollisionMath_LengthSquared(self->Z));
}

CollisionAffine CollisionAffine_FromMatrix(Matrix matrix)
{
    return (CollisionAffine)
    {
        .X = { matrix.m0, matrix.m1, matrix.m2 }, .Y = { matrix.m4, matrix.m5, matrix.m6 },
        .Z = { matrix.m8, matrix.m9, matrix.m10 }, .Translation = { matrix.m12, matrix.m13, matrix.m14 }
    };
}

Matrix CollisionAffine_ToMatrix(const CollisionAffine* self)
{
    Matrix Result = MatrixIdentity();
    Result.m0 = self->X.x;
    Result.m1 = self->X.y;
    Result.m2 = self->X.z;
    Result.m4 = self->Y.x;
    Result.m5 = self->Y.y;
    Result.m6 = self->Y.z;
    Result.m8 = self->Z.x;
    Result.m9 = self->Z.y;
    Result.m10 = self->Z.z;
    Result.m12 = self->Translation.x;
    Result.m13 = self->Translation.y;
    Result.m14 = self->Translation.z;
    return Result;
}

CollisionAffine CollisionAffine_FromCenterRotation(Vector3 center, Vector3 rotation)
{
    CollisionAffine Result = CollisionAffine_FromMatrix(MatrixRotateXYZ(rotation));
    Result.Translation = center;
    return Result;
}

void CollisionAffine_TransformBounds(const CollisionAffine* self, Vector3 min, Vector3 max, Vector3* outMin,
    Vector3* outMax)
{
    // Arvo's method: each output axis takes, per input axis, the smaller and the larger of the two products.
    Vector3 Center = CollisionMath_Scale(CollisionMath_Add(min, max), 0.5f);
    Vector3 Half = CollisionMath_Scale(CollisionMath_Subtract(max, min), 0.5f);
    Vector3 WorldCenter = CollisionAffine_TransformPoint(self, Center);
    Vector3 Extent = CollisionMath_Vector(
        (fabsf(self->X.x) * Half.x) + (fabsf(self->Y.x) * Half.y) + (fabsf(self->Z.x) * Half.z),
        (fabsf(self->X.y) * Half.x) + (fabsf(self->Y.y) * Half.y) + (fabsf(self->Z.y) * Half.z),
        (fabsf(self->X.z) * Half.x) + (fabsf(self->Y.z) * Half.y) + (fabsf(self->Z.z) * Half.z));
    *outMin = CollisionMath_Subtract(WorldCenter, Extent);
    *outMax = CollisionMath_Add(WorldCenter, Extent);
}


// Public functions: poses.
CollisionAffine CollisionPose_ToAffine(const CollisionPose* self)
{
    CollisionAffine Result = CollisionAffine_FromMatrix(QuaternionToMatrix(self->Rotation));
    Result.Translation = self->Position;
    return Result;
}

CollisionPose CollisionPose_Interpolate(const CollisionPose* from, const CollisionPose* to, float t)
{
    Quaternion A = from->Rotation;
    Quaternion B = to->Rotation;
    float Cosine = (A.x * B.x) + (A.y * B.y) + (A.z * B.z) + (A.w * B.w);
    if (Cosine < 0.0f)
    {
        B = (Quaternion){ .x = -B.x, .y = -B.y, .z = -B.z, .w = -B.w };
        Cosine = -Cosine;
    }
    Quaternion Rotation;
    if (Cosine > SLERP_LINEAR_LIMIT)
    {
        Rotation = QuaternionNormalize((Quaternion){ .x = A.x + ((B.x - A.x) * t), .y = A.y + ((B.y - A.y) * t),
            .z = A.z + ((B.z - A.z) * t), .w = A.w + ((B.w - A.w) * t) });
    }
    else
    {
        // A true slerp (constant turning speed): the rotating casts bound their approach by the total angle.
        float HalfAngle = acosf(Cosine);
        float Sine = sinf(HalfAngle);
        float WeightA = sinf((1.0f - t) * HalfAngle) / Sine;
        float WeightB = sinf(t * HalfAngle) / Sine;
        Rotation = (Quaternion){ .x = (A.x * WeightA) + (B.x * WeightB), .y = (A.y * WeightA) + (B.y * WeightB),
            .z = (A.z * WeightA) + (B.z * WeightB), .w = (A.w * WeightA) + (B.w * WeightB) };
    }
    return (CollisionPose){ .Position = CollisionMath_Lerp(from->Position, to->Position, t), .Rotation = Rotation };
}

float CollisionPose_GetRotationAngle(const CollisionPose* from, const CollisionPose* to)
{
    Quaternion A = from->Rotation;
    Quaternion B = to->Rotation;
    float Cosine = fabsf((A.x * B.x) + (A.y * B.y) + (A.z * B.z) + (A.w * B.w));
    return 2.0f * acosf((Cosine > 1.0f) ? 1.0f : Cosine);
}
