#pragma once

#include "CoreMinimal.h"
#include "PCG/PCGLandscapeMeshBuilder.h"

class UPCGLandscapeData;

namespace WDEditor::PCG
{
	struct FPCGLandscapeSamplingSettings
	{
		double CellSize = 100.0;
		FName  MaskLayerName = NAME_None; // kept for parity, but Density is used
		bool   bSampleNormals = true;
	};

	bool SampleLandscapeToGrid(
		const UPCGLandscapeData* LandscapeData,
		const FBox2D& ExpandedBoundsXY,
		int32 GridX,
		int32 GridY,
		const FPCGLandscapeSamplingSettings& Settings,
		TArray<FPCGLandscapeGridSample>& OutSamples);
}
