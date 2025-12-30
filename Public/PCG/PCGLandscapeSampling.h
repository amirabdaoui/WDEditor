 #pragma once
 
 #include "CoreMinimal.h"
 #include "PCG/PCGLandscapeMeshBuilder.h"
 
 class UPCGLandscapeData;
 
 namespace WDEditor::PCG
 {
         /** Settings for sampling a landscape to a grid. */
         struct FPCGLandscapeSamplingSettings
         {
                 /** World distance between grid vertices. */
                 double CellSize = 100.0;
 
                 /** Optional landscape layer name; falls back to density when None. */
                 FName  MaskLayerName = NAME_None;
 
                 /** Sample normals in addition to heights. */
                 bool   bSampleNormals = true;
 
                 /** Threshold for solid/empty classification. */
                 float MaskThreshold = 0.5f;
 
                 /** Whether to use marching squares in mixed cells (handled by builder). */
                 bool bUseMarchingSquares = true;
 
                 /** Invert the sampled mask (1 – weight) before thresholding. */
                 bool bInvertMask = false;
         };
 
         bool SampleLandscapeToGrid(
                 const UPCGLandscapeData* LandscapeData,
                 const FBox2D& ExpandedBoundsXY,
                 int32 GridX,
                 int32 GridY,
                 const FPCGLandscapeSamplingSettings& Settings,
                 TArray<FPCGLandscapeGridSample>& OutSamples);
 }