// UpdatedLandscapeSampling.cpp
// Implements a helper function that samples a landscape into a grid using
// configurable settings, including mask sampling from a named landscape layer
// and optional mask inversion.

#include "UpdatedLandscapeSettings.h"
#include "PCGLandscapeData.h"
#include "PCGPoint.h"
#include "Containers/Array.h"
#include "Math/Box2D.h"
#include "Math/Vector.h"
#include "Math/UnrealMathUtility.h"

/**
 * Simple struct representing a sample of landscape data at a grid cell.
 * Height and Mask are floats; Normal stores the surface normal if
 * sampled.  This mirrors the fields used by the PCG landscape mesh builder.
 */
struct FPCGLandscapeGridSample
{
    float Height = 0.0f;
    FVector3d Normal = FVector3d::UpVector;
    float Mask = 0.0f;
};

/**
 * SampleLandscapeToGrid fills OutSamples with height, normal and mask values
 * for a regular grid spanning ExpandedBoundsXY.  It leverages the provided
 * Settings to control sampling resolution, masking behaviour and marching
 * squares options.  The LandscapeData pointer must be valid.
 */
bool SampleLandscapeToGrid(
    const UPCGLandscapeData* LandscapeData,
    const FBox2D& ExpandedBoundsXY,
    int32 GridX,
    int32 GridY,
    const FUpdatedLandscapeSamplingSettings& Settings,
    TArray<FPCGLandscapeGridSample>& OutSamples)
{
    if (!LandscapeData || GridX <= 0 || GridY <= 0)
    {
        return false;
    }

    // Resize output array to hold one sample per cell
    OutSamples.SetNum(GridX * GridY);

    // Precompute cell size along X and Y for efficiency
    const double CellSizeX = Settings.CellSize;
    const double CellSizeY = Settings.CellSize;

    for (int32 Y = 0; Y < GridY; ++Y)
    {
        for (int32 X = 0; X < GridX; ++X)
        {
            const int32 Index = X + Y * GridX;
            FPCGLandscapeGridSample& Sample = OutSamples[Index];

            // Compute the centre of the current cell in world space
            const double LocX = ExpandedBoundsXY.Min.X + (X + 0.5) * CellSizeX;
            const double LocY = ExpandedBoundsXY.Min.Y + (Y + 0.5) * CellSizeY;
            FVector WorldPos(LocX, LocY, 0.0);

            // Project the centre point onto the landscape
            FPCGPoint Point;
            bool bHit = LandscapeData->ProjectPoint(
                FTransform(FQuat::Identity, WorldPos),
                /*Bounds*/FBox(FVector(-1.0f, -1.0f, -1.0f), FVector(1.0f, 1.0f, 1.0f)),
                /*ProjectionParams*/{},
                Point,
                /*OutMetadata*/nullptr);

            if (!bHit)
            {
                // If projection fails, mark the sample as empty
                Sample.Height = 0.0f;
                Sample.Normal = FVector3d::UpVector;
                Sample.Mask   = 0.0f;
                continue;
            }

            // Record height and optionally normal
            Sample.Height = Point.Transform.GetLocation().Z;
            if (Settings.bSampleNormals)
            {
                Sample.Normal = Point.Normal;
            }
            else
            {
                Sample.Normal = FVector3d::UpVector;
            }

            // Determine mask from landscape layer or fallback to density
            float MaskValue = 1.0f;
            if (Settings.MaskLayerName != NAME_None)
            {
                // Try to sample the weight of the specified layer.  This call
                // requires a helper function on UPCGLandscapeData; substitute
                // your own implementation if necessary.  On the Unreal Engine
                // side, the landscape data exposes a method GetLayerWeightAt.
                MaskValue = LandscapeData->GetLayerWeightAt(
                    Point.Transform.GetLocation(),
                    Settings.MaskLayerName);
            }
            else
            {
                // Fallback: use the projected point’s density (visibility)
                MaskValue = FMath::Clamp(Point.Density, 0.0f, 1.0f);
            }

            // Apply optional inversion
            if (Settings.bInvertMask)
            {
                MaskValue = 1.0f - MaskValue;
            }

            Sample.Mask = FMath::Clamp(MaskValue, 0.0f, 1.0f);
        }
    }

    return true;
}