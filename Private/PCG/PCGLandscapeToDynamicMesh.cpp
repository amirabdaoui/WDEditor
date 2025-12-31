 #include "PCG/PCGLandscapeToDynamicMesh.h"
 
 /* PCG */
 #include "PCGComponent.h"
 #include "PCGContext.h"
 #include "PCGElement.h"
 #include "Data/PCGLandscapeData.h"
 #include "PCGGraphExecutionStateInterface.h"
 
 /* PCG GeometryScript Interop */
 #include "Data/PCGDynamicMeshData.h"
 #include "Elements/PCGDynamicMeshBaseElement.h"
 
 /* Geometry */
 #include "UDynamicMesh.h"
 #include "DynamicMesh/DynamicMesh3.h"
 
 /* WDEditor */
 #include "PCG/PCGLandscapeSampling.h"
 #include "PCG/PCGLandscapeMeshBuilder.h"

// Materials
#include "Materials/MaterialInterface.h"
 
 /* Math */
 #include "Math/UnrealMathUtility.h"
 
 const FName UPCGLandscapeToDynamicMeshSettings::LandscapePinLabel(TEXT("Landscape"));
 const FName UPCGLandscapeToDynamicMeshSettings::BoundsPinLabel(TEXT("Bounds"));
 const FName UPCGLandscapeToDynamicMeshSettings::DynamicMeshPinLabel(TEXT("DynamicMesh"));
 
 UPCGLandscapeToDynamicMeshSettings::UPCGLandscapeToDynamicMeshSettings() {}
 
 TArray<FPCGPinProperties> UPCGLandscapeToDynamicMeshSettings::InputPinProperties() const
 {
         TArray<FPCGPinProperties> Pins;
         Pins.Emplace_GetRef(LandscapePinLabel, EPCGDataType::Surface).SetRequiredPin();
         Pins.Emplace(BoundsPinLabel, EPCGDataType::Spatial);
         Pins.Emplace_GetRef(DynamicMeshPinLabel, EPCGDataType::DynamicMesh).SetRequiredPin();
         return Pins;
 }
 
 TArray<FPCGPinProperties> UPCGLandscapeToDynamicMeshSettings::OutputPinProperties() const
 {
         TArray<FPCGPinProperties> Pins;
         Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh);
         return Pins;
 }
 
 namespace
 {
         class FPCGLandscapeToDynamicMeshElement final : public IPCGDynamicMeshBaseElement
         {
         protected:
                 virtual bool ExecuteInternal(FPCGContext* Context) const override;
         };
 }
 
 FPCGElementPtr UPCGLandscapeToDynamicMeshSettings::CreateElement() const
 {
         return MakeShared<FPCGLandscapeToDynamicMeshElement>();
 }
 
 bool FPCGLandscapeToDynamicMeshElement::ExecuteInternal(FPCGContext* Context) const
 {
        const auto* Settings =
                Context->GetInputSettings<UPCGLandscapeToDynamicMeshSettings>();
         check(Settings);

        // ----------------------------------------------------------------------
        // Determine the generation grid size once up front.  The PCG component
        // exposes the partition/grid size via GetGenerationGridSize().  We cache
        // this value here so that it can be used later when applying a vertical
        // offset to the generated mesh.  Retrieving it once avoids repeated
        // property accesses during the build process.
        // Determine the generation grid size once up front.  The execution state
        // exposes the partition/grid size via GetGenerationGridSize().  We cache
        // this value here so that it can be used later when applying a vertical
        // offset to the generated mesh.  Retrieving it once avoids repeated
        // property accesses during the build process.  We do not use
        // Context->SourceComponent here because that API is deprecated.
        uint32 CachedGridSize = 0;
        if (Context && Context->ExecutionSource.IsValid())
        {
                // Retrieve the underlying execution source object.  ExecutionSource is a
                // TWeakInterfacePtr<IPCGGraphExecutionSource>, so calling Get() returns
                // the pointer to the interface.  The PCG component implements this
                // interface, so we can cast the interface pointer to UPCGComponent.
                IPCGGraphExecutionSource* ExecSourceObj = Context->ExecutionSource.Get();
                const UPCGComponent* PCGComp = Cast<UPCGComponent>(ExecSourceObj);
                if (PCGComp)
                {
                        CachedGridSize = PCGComp->GetGenerationGridSize();
                }
        }
 
         const auto LandscapeInputs =
                 Context->InputData.GetInputsByPin(
                         UPCGLandscapeToDynamicMeshSettings::LandscapePinLabel);
 
         const auto BoundsInputs =
                 Context->InputData.GetInputsByPin(
                         UPCGLandscapeToDynamicMeshSettings::BoundsPinLabel);
 
         const auto MeshInputs =
                 Context->InputData.GetInputsByPin(
                         UPCGLandscapeToDynamicMeshSettings::DynamicMeshPinLabel);
 
         if (LandscapeInputs.IsEmpty() || MeshInputs.IsEmpty())
         {
                 return true;
         }
 
         const UPCGLandscapeData* LandscapeData =
                 Cast<UPCGLandscapeData>(LandscapeInputs[0].Data);
         if (!LandscapeData)
         {
                 return true;
         }
 
         UPCGDynamicMeshData* OutMeshData =
                 CopyOrSteal(MeshInputs[0], Context);
         if (!OutMeshData)
         {
                 return true;
         }
 
         auto EmitOutput = [&]()
         {
                 FPCGTaggedData& Out =
                         Context->OutputData.TaggedData.Emplace_GetRef();
                 Out.Data = OutMeshData;
                 Out.Pin  = PCGPinConstants::DefaultOutputLabel;
                 Out.Tags = MeshInputs[0].Tags;
         };
 
         // ============================================================
         // Resolve crop bounds
         // ============================================================
 
         const FBox LandscapeBounds = LandscapeData->GetBounds();
 
         const UPCGSpatialData* BoundsSpatial =
                 (!BoundsInputs.IsEmpty())
                         ? Cast<UPCGSpatialData>(BoundsInputs[0].Data)
                         : nullptr;
 
         const FBox BoundsBox =
                 BoundsSpatial ? BoundsSpatial->GetBounds() : FBox(ForceInit);
 
         FBox CropBounds3D(ForceInit);
 
         switch (Settings->BoundsMode)
         {
         case EPCGLandscapeBoundsIntersectMode::Ignore:
         {
                 // UE5.6+ compliant: use execution state's bounds (partition / execution bounds)
                 if (Context->ExecutionSource.IsValid())
                 {
                         CropBounds3D = Context->ExecutionSource->GetExecutionState().GetBounds();
                 }
                 else
                 {
                         // Fallback: landscape bounds (still guarded by grid cap)
                         CropBounds3D = LandscapeBounds;
                 }
                 break;
         }
 
         case EPCGLandscapeBoundsIntersectMode::Strict:
                 if (!BoundsSpatial)
                 {
                         EmitOutput();
                         return true;
                 }
                 CropBounds3D = BoundsBox;
                 break;
 
         case EPCGLandscapeBoundsIntersectMode::Intersect:
         default:
                 if (BoundsSpatial)
                 {
                         if (!LandscapeBounds.Intersect(BoundsBox))
                         {
                                 EmitOutput();
                                 return true;
                         }
 
                         // Manual intersection (UE::Math::TBox API)
                         CropBounds3D = FBox(
                                 LandscapeBounds.Min.ComponentMax(BoundsBox.Min),
                                 LandscapeBounds.Max.ComponentMin(BoundsBox.Max));
                 }
                 else
                 {
                         CropBounds3D = LandscapeBounds;
                 }
                 break;
         }
 
         if (!CropBounds3D.IsValid)
         {
                 EmitOutput();
                 return true;
         }
         // Convert crop bounds to XY (authoritative bounds for mesh)
         const FBox2D CropBoundsXY(
                 FVector2D(
                         (float)CropBounds3D.Min.X,
                         (float)CropBounds3D.Min.Y),
                 FVector2D(
                         (float)CropBounds3D.Max.X,
                         (float)CropBounds3D.Max.Y));
 
         // ============================================================
         // Overscan
         // ============================================================
 
         const double CellSize = FMath::Max(1.0, Settings->CellSize);
         const double OverscanWorld =
                 (double)FMath::Max(0, Settings->OverscanCells) * CellSize;
 
         FBox2D ExpandedBoundsXY = CropBoundsXY;
         ExpandedBoundsXY.Min -= FVector2D((float)OverscanWorld);
         ExpandedBoundsXY.Max += FVector2D((float)OverscanWorld);
 
         const FVector2D Size = ExpandedBoundsXY.GetSize();
         const int32 GridX =
                 FMath::Max(2, FMath::FloorToInt(Size.X / CellSize) + 1);
         const int32 GridY =
                 FMath::Max(2, FMath::FloorToInt(Size.Y / CellSize) + 1);
         // ============================================================
         // Safety: prevent insane allocations
         // ============================================================
 
         constexpr int64 MaxGridPoints = 16ll * 1024ll * 1024ll; // 16 million (safe upper bound)
 
         const int64 TotalPoints = (int64)GridX * (int64)GridY;
         if (TotalPoints <= 0 || TotalPoints > MaxGridPoints)
         {
                 UE_LOG(LogPCG, Warning,
                         TEXT("PCGLandscapeToDynamicMesh: Aborting build. Grid too large (%lld points). "
                              "BoundsMode=%d, CellSize=%.2f"),
                         TotalPoints,
                         (int32)Settings->BoundsMode,
                         CellSize);
 
                 EmitOutput();
                 return true;
         }
 
         // ============================================================
         // Sample landscape
         // ============================================================
 
         WDEditor::PCG::FPCGLandscapeSamplingSettings Samp;
         Samp.CellSize = CellSize;
         Samp.MaskLayerName = Settings->MaskLayerName;
         Samp.bSampleNormals = true;
         // Propagate inversion flag to sampling settings
         Samp.bInvertMask = Settings->bInvertMask;
 
         TArray<WDEditor::PCG::FPCGLandscapeGridSample> Samples;
         if (!WDEditor::PCG::SampleLandscapeToGrid(
                         LandscapeData,
                         ExpandedBoundsXY,
                         GridX,
                         GridY,
                         Samp,
                         Samples))
         {
                 EmitOutput();
                 return true;
         }
 
         // ============================================================
         // Build mesh
         // ============================================================
 
         WDEditor::PCG::FPCGLandscapeMeshGridDesc GridDesc;
         GridDesc.GridX = GridX;
         GridDesc.GridY = GridY;
         GridDesc.GridMinXY = ExpandedBoundsXY.Min;
         GridDesc.Samples = &Samples;
 
         WDEditor::PCG::FPCGLandscapeMeshBuilderSettings Build;
         Build.CellSize = CellSize;
         Build.MaskThreshold = Settings->MaskThreshold;
         Build.bUseMarchingSquares = Settings->bUseMarchingSquares;
        // Subdivision settings have been deprecated in this node. Refinement should be handled
        // by a separate subdivision node. The build settings related to subdivision are left
        // at their defaults. We continue to set removal and compaction flags.
        Build.bRemoveIsolatedVertices = Settings->bRemoveIsolatedVertices;
        // Do not alter bConstrainCropBoundary based on subdivision. Always constrain the crop
        // boundary so that the mesh matches the landscape bounds.
        Build.bConstrainCropBoundary = true;

        // Padding: include overscan padding faces and assign them to a separate polygroup
        Build.bIncludePadding = Settings->bIncludePadding;
        Build.PaddingPolygroupID = Settings->PaddingPolygroupID;
 
         UE::Geometry::FDynamicMesh3 BuiltMesh;
         WDEditor::PCG::FPCGLandscapeMeshConstraints Constraints;
 
         if (!WDEditor::PCG::BuildMeshFromSamples(
                         GridDesc,
                         Build,
                         CropBoundsXY,
                         BuiltMesh,
                         Constraints,
                         nullptr))
         {
                 EmitOutput();
                 return true;
         }

        // ============================================================
        // Assign material to the dynamic mesh data
        // ============================================================
        // If a material is specified in the settings, assign it to the output mesh. Note that
        // SetMaterials expects an array of materials corresponding to material slots on the
        // dynamic mesh. We only assign a single material slot here.
        if (Settings->Material)
        {
            TArray<UMaterialInterface*> MatArray;
            MatArray.Add(Settings->Material);
            OutMeshData->SetMaterials(MatArray);
        }
 
         // ============================================================
         // Write result
         // ============================================================
 
         UDynamicMesh* DynMesh = OutMeshData->GetMutableDynamicMesh();
         check(DynMesh);
 
        DynMesh->EditMesh(
                [&](UE::Geometry::FDynamicMesh3& Mesh)
                {
                        // Move the freshly built mesh into the dynamic mesh
                        Mesh = MoveTemp(BuiltMesh);

                        // Optional: compact the mesh to remove unused vertices/attributes
                        if (Settings->bCompactAtEnd)
                        {
                                Mesh.CompactInPlace();
                        }

                        // Apply a vertical offset to the mesh to align it with the landscape.
                        // We subtract half of the partition grid size along Z so that the
                        // generated mesh sits correctly on top of the landscape tile.  The
                        // offset is only applied if a valid grid size was retrieved.  Because
                        // FDynamicMesh3 in UE5.6 does not provide a Translate() method, we
                        // manually adjust each vertex position.
                        if (CachedGridSize > 0)
                        {
                                const double OffsetZ = -0.5 * static_cast<double>(CachedGridSize);
                                for (int32 Vid : Mesh.VertexIndicesItr())
                                {
                                        FVector3d Position = Mesh.GetVertex(Vid);
                                        Position.Z += OffsetZ;
                                        Mesh.SetVertex(Vid, Position);
                                }
                        }
                },
                EDynamicMeshChangeType::GeneralEdit,
                EDynamicMeshAttributeChangeFlags::Unknown,
                true);
 
         EmitOutput();
         return true;
 }