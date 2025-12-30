// Copyright BULKHEAD Limited. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMesh3.h"

#include "PCG/PCGLandscapeMeshSubdivision.h"

/**
 * Note: This header is a patched version of the original
 * PCGLandscapeMeshBuilder.h from the WDEditor repository.  It adds
 * support for optional padding (overscan) triangles and custom
 * polygroups.  These fields are used by the corresponding builder
 * implementation to decide whether to crop the overscan region and
 * what polygroup ID to assign to triangles that lie outside the final
 * crop bounds.  Subdivision-related fields have been retained for
 * backwards compatibility, but the node no longer uses them.
 */

namespace WDEditor::PCG
{
        /** One sample at a grid vertex (corner). */
        struct FPCGLandscapeGridSample
        {
                double Height = 0.0;
                FVector3d Normal = FVector3d::UpVector;
                float Mask = 0.0f;
        };

        /** Builder settings (topology + refinement + crop). */
        struct FPCGLandscapeMeshBuilderSettings
        {
                /** World distance between grid vertices. */
                double CellSize = 100.0;

                /** Mask threshold: >= is solid. */
                float MaskThreshold = 0.5f;

                /** If true, mixed cells use marching squares; if false, mixed cells are skipped. */
                bool bUseMarchingSquares = true;

                /** Split direction for all-solid quads (deterministic). */
                bool bSolidQuadsUseDiagBLtoTR = true;

                /** If true, enforce deterministic triangulation fan around polygon centroid. */
                bool bDeterministicTriangulation = true;

                /** If true, add crop boundary constraints (recommended when subdividing). */
                bool bConstrainCropBoundary = true;

                /** Epsilon used when testing crop boundary membership in XY. */
                double CropBoundaryEpsilon = 0.01;

                /** Apply PN subdivision. */
                bool bEnableSubdivision = false;

                /** PN subdivision settings. */
                FPCGLandscapePNSubdivideSettings Subdivide;

                /** Remove isolated vertices after everything (crop + subdiv). */
                bool bRemoveIsolatedVertices = true;

                /**
                 * If true, retain triangles in the overscan/padding region outside the crop
                 * bounds. When false (default), the builder crops the mesh back to the
                 * partition bounds and discards any triangles outside.
                 */
                bool bIncludePadding = false;

                /**
                 * Polygroup ID assigned to triangles that lie outside the crop bounds
                 * when bIncludePadding is true. Set to a non-negative integer to
                 * assign a specific group; set to -1 to leave these triangles in
                 * the default group.  This ID must be within the range allowed by
                 * your downstream tools (e.g. 0-255 for 8-bit groups).
                 */
                int32 PaddingPolygroupID = -1;
        };

        /** Input grid description. Samples must be GridX*GridY and row-major (X changes fastest). */
        struct FPCGLandscapeMeshGridDesc
        {
                int32 GridX = 0; // vertices in X
                int32 GridY = 0; // vertices in Y

                /** World-space min corner of the grid (ExpandedBounds.Min). */
                FVector2D GridMinXY = FVector2D::ZeroVector;

                /** Samples at each grid vertex: index = x + y*GridX. */
                const TArray<FPCGLandscapeGridSample>* Samples = nullptr;
        };

        /** Stats returned from the builder. */
        struct FPCGLandscapeMeshBuilderStats
        {
                int32 GridX = 0;
                int32 GridY = 0;

                int32 NumCellsTotal = 0;
                int32 NumCellsSolid = 0;
                int32 NumCellsEmpty = 0;
                int32 NumCellsMixed = 0;

                int32 NumTrianglesBeforeCrop = 0;
                int32 NumTrianglesAfterCrop = 0;

                FPCGLandscapeSubdivisionStats SubdivisionStats;
        };

        /**
         * Build the mesh from overscanned grid samples, optionally subdivide interior-only,
         * then crop back to CropBounds in XY.  Subdivision is currently disabled in
         * the PCG node and should be implemented via a separate refinement node.
         *
         * ExpandedBounds is implied by GridDesc (GridMinXY + GridX/GridY * CellSize).
         *
         * @param GridDesc: describes the overscanned grid.
         * @param Settings: builder settings.
         * @param CropBoundsXY: final partition bounds (XY used, Z ignored).
         * @param OutMesh: output dynamic mesh.
         * @param OutConstraints: constraints used for subdivision (mask boundary + crop boundary).
         * @param OutStats: optional stats.
         * @return true if mesh contains triangles at end.
         */
        bool BuildMeshFromSamples(
                const FPCGLandscapeMeshGridDesc& GridDesc,
                const FPCGLandscapeMeshBuilderSettings& Settings,
                const FBox2D& CropBoundsXY,
                UE::Geometry::FDynamicMesh3& OutMesh,
                FPCGLandscapeMeshConstraints& OutConstraints,
                FPCGLandscapeMeshBuilderStats* OutStats = nullptr);
} // namespace WDEditor::PCG