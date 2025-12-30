// Copyright BULKHEAD Limited. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

/**
 * PN-style interior-only subdivision utilities for PCG-generated meshes.
 *
 * This module:
 *  - Refines only interior triangles
 *  - Never splits or moves constrained vertices / edges
 *  - Is deterministic and partition-safe
 *
 * Intended for use by PCGLandscapeToDynamicMesh and similar nodes.
 */

namespace WDEditor::PCG
{
	/** Stats returned from subdivision. */
	struct FPCGLandscapeSubdivisionStats
	{
		int32 NumLevels = 0;
		int32 NumTrianglesRefined = 0;
		int32 NumVerticesAdded = 0;
		int32 NumTrianglesAdded = 0;
	};

	/**
	 * Hard constraints for refinement.
	 * These must be populated by the caller.
	 */
	struct FPCGLandscapeMeshConstraints
	{
		/** Vertices that must never move or be refined. */
		TSet<int32> ConstrainedVertices;

		/** Edges that must never be split. */
		TSet<int32> ConstrainedEdges;

		FORCEINLINE bool IsVertexConstrained(int32 Vid) const
		{
			return ConstrainedVertices.Contains(Vid);
		}

		FORCEINLINE bool IsEdgeConstrained(int32 Eid) const
		{
			return ConstrainedEdges.Contains(Eid);
		}
	};

	/** Subdivision parameters. */
	struct FPCGLandscapePNSubdivideSettings
	{
		/** Number of interior refinement passes. */
		int32 SubdivisionLevels = 0;

		/**
		 * PN curvature strength.
		 * 0 = linear midpoint
		 * Typical values: 0.15–0.35
		 */
		float PNStrength = 0.25f;

		/**
		 * Guard ring around constrained regions (in topological rings).
		 * Recommended: 1
		 */
		int32 ConstraintGuardRing = 1;

		/**
		 * Require all neighboring triangles to be refinable to avoid T-junctions.
		 * Should stay true for terrain.
		 */
		bool bRequireNeighborAgreement = true;

		/** Recompute normals after each refinement level. */
		bool bRecomputeNormalsEachLevel = true;

		/**
		 * Ensure a primary normal overlay exists and is filled.
		 * Safe to keep enabled.
		 */
		bool bEnsureNormalOverlay = true;
	};

	/**
	 * Applies interior-only PN-style subdivision.
	 *
	 * @return true if any triangles were refined.
	 */
	bool ApplyPNSubdivideInterior(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FPCGLandscapeMeshConstraints& Constraints,
		const FPCGLandscapePNSubdivideSettings& Settings,
		FPCGLandscapeSubdivisionStats* OutStats = nullptr);
}
