#include "PCG/PCGLandscapeSampling.h"

/* PCG */
#include "PCGCommon.h"
#include "Data/PCGLandscapeData.h"
#include "Data/PCGPointData.h"

namespace WDEditor::PCG
{
	// ------------------------------------------------------------
	// Grid → World (NO Y FLIP)
	// Must match PCGLandscapeMeshBuilder: GridMinXY + (X,Y)*CellSize
	// ------------------------------------------------------------
	static FORCEINLINE FVector MakeWorldPos2D(
		const FBox2D& Bounds,
		double CellSize,
		int32 X,
		int32 Y)
	{
		return FVector(
			(double)Bounds.Min.X + (double)X * CellSize,
			(double)Bounds.Min.Y + (double)Y * CellSize,
			0.0);
	}

	bool SampleLandscapeToGrid(
		const UPCGLandscapeData* LandscapeData,
		const FBox2D& ExpandedBoundsXY,
		int32 GridX,
		int32 GridY,
		const FPCGLandscapeSamplingSettings& Settings,
		TArray<FPCGLandscapeGridSample>& OutSamples)
	{
		if (!LandscapeData || GridX < 1 || GridY < 1)
		{
			return false;
		}

		OutSamples.SetNum(GridX * GridY);

		// Use ProjectPoint – SamplePoint does NOT project to surface
		const FBox QueryBounds(
			FVector(-1.0, -1.0, -1.0),
			FVector( 1.0,  1.0,  1.0));

		FPCGProjectionParams ProjectionParams;
		ProjectionParams.bProjectPositions = true;
		ProjectionParams.bProjectRotations = true;
		ProjectionParams.bProjectScales    = false;

		for (int32 Y = 0; Y < GridY; ++Y)
		{
			for (int32 X = 0; X < GridX; ++X)
			{
				const int32 Index = X + Y * GridX;
				FPCGLandscapeGridSample& Sample = OutSamples[Index];

				const FVector WorldPos =
					MakeWorldPos2D(
						ExpandedBoundsXY,
						Settings.CellSize,
						X,
						Y);

				// Z is arbitrary – ProjectPoint will resolve it
				const FTransform QueryTransform(
					FQuat::Identity,
					FVector(WorldPos.X, WorldPos.Y, 0.0));

				FPCGPoint Point;
				const bool bHit =
					LandscapeData->ProjectPoint(
						QueryTransform,
						QueryBounds,
						ProjectionParams,
						Point,
						/*OutMetadata=*/nullptr);

				if (!bHit)
				{
					Sample.Height = 0.0;
					Sample.Normal = FVector3d::UpVector;
					Sample.Mask   = 0.0f;
					continue;
				}

				// Height from projected transform
				const FVector PosWS = Point.Transform.GetLocation();
				Sample.Height = PosWS.Z;

				// Normal from projected rotation
				if (Settings.bSampleNormals)
				{
					const FVector NormalWS =
						Point.Transform.GetUnitAxis(EAxis::Z);
					Sample.Normal =
						FVector3d(NormalWS).GetSafeNormal();
				}
				else
				{
					Sample.Normal = FVector3d::UpVector;
				}

				// Mask: use Density (visibility & layers already baked in)
				Sample.Mask = FMath::Clamp(Point.Density, 0.0f, 1.0f);
			}
		}

		return true;
	}
}
