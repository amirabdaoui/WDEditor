// Copyright BULKHEAD Limited. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMesh3.h"

#include "PCG/PCGLandscapeMeshSubdivision.h"

/**
 * Builds a DynamicMesh from an overscanned grid of landscape samples (height/normal/mask),
 * with hybrid topology (uniform for full quads, marching squares for mixed cells),
 * plus optional interior-only PN subdivision and final crop to partition bounds.
 *
 * This module is intentionally independent from PCG sampling APIs.
 * Your PCG node is expected to:
 *   1) Determine Bounds (CropBounds) and ExpandedBounds (Overscan)
 *   2) Sample the landscape into arrays
 *   3) Call BuildMeshFromSamples(...)
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
	 * then crop back to CropBounds in XY.
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
