 #include "PCG/PCGLandscapeSampling.h"
 
 /* PCG */
 #include "PCGCommon.h"
 #include "Data/PCGLandscapeData.h"
#include "Data/PCGPointData.h"

// Additional includes for metadata access when sampling layer weights
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataCommon.h"
// Include spatial data for initializing metadata from a source
#include "Data/PCGSpatialData.h"
 
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
 
    // If the caller requested a landscape layer mask, allocate a single metadata container
    // up front and initialize it from the landscape. This avoids allocating new UObjects
    // during GC and allows reuse of the same metadata throughout the grid sampling. If
    // MaskLayerName is NAME_None, we will leave MetadataPtr null and fall back to density.
    UPCGMetadata* MetadataPtr = nullptr;
    const bool bUseLayerMask = !Settings.MaskLayerName.IsNone();
    if (bUseLayerMask)
    {
        // Allocate a new metadata container. Use NewObject without specifying an Outer to
        // create a transient UObject. This allocation happens once, outside the sampling
        // loops, so it is safe with respect to GC.
        MetadataPtr = NewObject<UPCGMetadata>();

        // Initialize metadata attributes for this landscape. InitializeTargetMetadata will
        // populate the metadata with all layer names defined on the landscape so that
        // ProjectPoint can fill weight values. Use FPCGInitializeFromDataParams with
        // the landscape data as the source, and ensure metadata and attributes are inherited.
        FPCGInitializeFromDataParams InitParams(LandscapeData);
        InitParams.bInheritMetadata = true;
        InitParams.bInheritAttributes = true;
        LandscapeData->InitializeTargetMetadata(InitParams, MetadataPtr);
    }

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
            // Decide whether to request metadata based on MaskLayerName. If we need a layer mask,
            // pass the pre-allocated MetadataPtr to collect layer weights. Otherwise, pass
            // nullptr to avoid unnecessary metadata writes.
            UPCGMetadata* OutMetadata = bUseLayerMask ? MetadataPtr : nullptr;
            const bool bHit =
                LandscapeData->ProjectPoint(
                    QueryTransform,
                    QueryBounds,
                    ProjectionParams,
                    Point,
                    /*OutMetadata=*/OutMetadata);
 
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
 
            // --------------------------------------------------------------
            // Mask sampling: if MaskLayerName is specified and metadata is available,
            // read the layer weight from the metadata. Otherwise, use density.
            // --------------------------------------------------------------
            float MaskValue = 0.0f;
            if (bUseLayerMask && MetadataPtr)
            {
                // Read the layer attribute from the initialized metadata using
                // the projected point's MetadataEntry. Note: Attribute can be null
                // if the layer does not exist or is not enabled on this landscape.
                const auto* Attribute = MetadataPtr->GetConstTypedAttribute<float>(
                    FPCGAttributeIdentifier(Settings.MaskLayerName));
                if (Attribute && Point.MetadataEntry != PCGInvalidEntryKey)
                {
                    MaskValue = Attribute->GetValueFromItemKey(Point.MetadataEntry);
                }
                else
                {
                    // Fallback to density if attribute is missing or point has no metadata entry
                    MaskValue = FMath::Clamp(Point.Density, 0.0f, 1.0f);
                }
            }
            else
            {
                // No mask layer specified or metadata disabled; use density
                MaskValue = FMath::Clamp(Point.Density, 0.0f, 1.0f);
            }

            // Apply optional inversion and clamp
            if (Settings.bInvertMask)
            {
                MaskValue = 1.0f - MaskValue;
            }
            Sample.Mask = FMath::Clamp(MaskValue, 0.0f, 1.0f);
                         }
                 }
 
                 return true;
         }
 }