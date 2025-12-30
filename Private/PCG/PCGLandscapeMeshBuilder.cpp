// Copyright BULKHEAD Limited. All Rights Reserved.

#include "PCG/PCGLandscapeMeshBuilder.h"

/* GeometryCore */
#include "DynamicMesh/MeshNormals.h"
#include "IndexTypes.h"

/* Math */
#include "Math/UnrealMathUtility.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex2i;
using UE::Geometry::FIndex3i;

namespace WDEditor::PCG
{
namespace Builder_Internal
{
	static FORCEINLINE int32 SampleIndex(int32 X, int32 Y, int32 GridX)
	{
		return X + Y * GridX;
	}

	static FORCEINLINE bool IsSolid(float Mask, float Threshold)
	{
		return Mask >= Threshold;
	}

	static FORCEINLINE FVector3d MakePos(const FVector2D& GridMinXY, double CellSize, int32 X, int32 Y, double Z)
	{
		return FVector3d(
			(double)GridMinXY.X + (double)X * CellSize,
			(double)GridMinXY.Y + (double)Y * CellSize,
			Z);
	}

	static FORCEINLINE FVector3d Lerp3(const FVector3d& A, const FVector3d& B, double T)
	{
		return A + (B - A) * T;
	}

	static FVector3d LerpNormalSafe(const FVector3d& A, const FVector3d& B, double T)
	{
		FVector3d N = Lerp3(A, B, T);
		if (!N.Normalize())
		{
			N = FVector3d::UpVector;
		}
		return N;
	}

	/** Undirected key for caching edge-intersection vertices on grid edges. */
	static uint64 MakeGridEdgeKey(int32 X, int32 Y, int32 Dir /*0=H,1=V*/)
	{
		// pack into 64: (Dir<<62) | (Y<<31) | X
		const uint64 UX = (uint32)X;
		const uint64 UY = (uint32)Y;
		return (uint64(Dir) << 62) | (UY << 31) | UX;
	}

	static int32 GetOrCreateEdgeVertex(
		FDynamicMesh3& Mesh,
		TMap<uint64, int32>& EdgeVertexCache,
		TSet<int32>& OutBoundaryVerts,
		const FPCGLandscapeMeshGridDesc& Grid,
		const FPCGLandscapeMeshBuilderSettings& Settings,
		int32 X, int32 Y, int32 Dir)
	{
		const uint64 Key = MakeGridEdgeKey(X, Y, Dir);
		if (const int32* Found = EdgeVertexCache.Find(Key))
		{
			return *Found;
		}

		const int32 GridX = Grid.GridX;
		const int32 GridY = Grid.GridY;
		const TArray<FPCGLandscapeGridSample>& Samples = *Grid.Samples;

		// endpoints
		int32 X0 = X, Y0 = Y;
		int32 X1 = X, Y1 = Y;
		if (Dir == 0) { X1 = X + 1; }
		else          { Y1 = Y + 1; }

		check(X0 >= 0 && X0 < GridX && Y0 >= 0 && Y0 < GridY);
		check(X1 >= 0 && X1 < GridX && Y1 >= 0 && Y1 < GridY);

		const FPCGLandscapeGridSample& S0 = Samples[SampleIndex(X0, Y0, GridX)];
		const FPCGLandscapeGridSample& S1 = Samples[SampleIndex(X1, Y1, GridX)];

		const double M0 = (double)S0.Mask;
		const double M1 = (double)S1.Mask;
		const double Den = (M1 - M0);

		// Threshold-based interpolation. If Den ~ 0, fall back to midpoint.
		double T = 0.5;
		if (FMath::Abs(Den) > 1e-8)
		{
			T = ((double)Settings.MaskThreshold - M0) / Den;
			T = FMath::Clamp(T, 0.0, 1.0);
		}

		const FVector3d P0 = MakePos(Grid.GridMinXY, Settings.CellSize, X0, Y0, S0.Height);
		const FVector3d P1 = MakePos(Grid.GridMinXY, Settings.CellSize, X1, Y1, S1.Height);

		const FVector3d Pos = Lerp3(P0, P1, T);
		const FVector3d Nor = LerpNormalSafe(S0.Normal, S1.Normal, T);

		const int32 Vid = Mesh.AppendVertex(Pos);

		// Mark as mask-boundary vertex (hard constraint for subdivision)
		OutBoundaryVerts.Add(Vid);

		EdgeVertexCache.Add(Key, Vid);
		return Vid;
	}

	static void AddTriDeterministic(FDynamicMesh3& Mesh, int32 A, int32 B, int32 C)
	{
		// Flip winding so front faces point up (fix backface-only rendering)
		Mesh.AppendTriangle(A, C, B);
	}

	static void AccumulateConstraintEdgesFromVertices(
		FDynamicMesh3& Mesh,
		FPCGLandscapeMeshConstraints& Constraints)
	{
		for (int32 Vid : Constraints.ConstrainedVertices)
		{
			if (!Mesh.IsVertex(Vid))
			{
				continue;
			}

			Mesh.EnumerateVertexEdges(Vid, [&](int32 Eid)
			{
				if (Mesh.IsEdge(Eid))
				{
					Constraints.ConstrainedEdges.Add(Eid);
				}
			});
		}
	}

	static void AddCropBoundaryConstraints(
		const FBox2D& CropBoundsXY,
		const FPCGLandscapeMeshBuilderSettings& Settings,
		FDynamicMesh3& Mesh,
		FPCGLandscapeMeshConstraints& Constraints)
	{
		const double Eps = FMath::Max(0.0, Settings.CropBoundaryEpsilon);

		const double MinX = (double)CropBoundsXY.Min.X;
		const double MaxX = (double)CropBoundsXY.Max.X;
		const double MinY = (double)CropBoundsXY.Min.Y;
		const double MaxY = (double)CropBoundsXY.Max.Y;

		for (int32 Vid : Mesh.VertexIndicesItr())
		{
			if (!Mesh.IsVertex(Vid))
			{
				continue;
			}

			const FVector3d P = Mesh.GetVertex(Vid);
			const bool bOnX = (FMath::Abs(P.X - MinX) <= Eps) || (FMath::Abs(P.X - MaxX) <= Eps);
			const bool bOnY = (FMath::Abs(P.Y - MinY) <= Eps) || (FMath::Abs(P.Y - MaxY) <= Eps);

			if (bOnX || bOnY)
			{
				Constraints.ConstrainedVertices.Add(Vid);
			}
		}
	}

	static bool TriangleCentroidInsideXY(
		const FDynamicMesh3& Mesh,
		int32 Tid,
		const FBox2D& CropBoundsXY)
	{
		const FIndex3i T = Mesh.GetTriangle(Tid);
		const FVector3d A = Mesh.GetVertex(T.A);
		const FVector3d B = Mesh.GetVertex(T.B);
		const FVector3d C = Mesh.GetVertex(T.C);

		const FVector2D Centroid(
			(float)((A.X + B.X + C.X) / 3.0),
			(float)((A.Y + B.Y + C.Y) / 3.0));

		return CropBoundsXY.IsInside(Centroid);
	}

	static void CropMeshToBoundsXY(FDynamicMesh3& Mesh, const FBox2D& CropBoundsXY)
	{
		TArray<int32> ToRemove;
		ToRemove.Reserve(Mesh.TriangleCount());

		for (int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (!Mesh.IsTriangle(Tid))
			{
				continue;
			}

			if (!TriangleCentroidInsideXY(Mesh, Tid, CropBoundsXY))
			{
				ToRemove.Add(Tid);
			}
		}

		for (int32 Tid : ToRemove)
		{
			if (Mesh.IsTriangle(Tid))
			{
				Mesh.RemoveTriangle(Tid, false);
			}
		}
	}

	static void RemoveIsolatedVertices(FDynamicMesh3& Mesh)
	{
		TArray<int32> VertsToRemove;
		for (int32 Vid : Mesh.VertexIndicesItr())
		{
			if (!Mesh.IsVertex(Vid))
			{
				continue;
			}

			if (Mesh.GetVtxTriangleCount(Vid) == 0)
			{
				VertsToRemove.Add(Vid);
			}
		}

		for (int32 Vid : VertsToRemove)
		{
			if (Mesh.IsVertex(Vid) && Mesh.GetVtxTriangleCount(Vid) == 0)
			{
				Mesh.RemoveVertex(Vid, false);
			}
		}
	}

	static void BuildCellPolygon_MarchingSquares(
		FDynamicMesh3& Mesh,
		TMap<uint64, int32>& EdgeVertexCache,
		TSet<int32>& BoundaryVerts,
		const FPCGLandscapeMeshGridDesc& Grid,
		const FPCGLandscapeMeshBuilderSettings& Settings,
		int32 CellX, int32 CellY,
		int32 V00, int32 V10, int32 V11, int32 V01,
		bool bS00, bool bS10, bool bS11, bool bS01,
		TArray<int32>& OutPoly)
	{
		OutPoly.Reset();

		const int32 Case =
			(bS00 ? 1 : 0) |
			(bS10 ? 2 : 0) |
			(bS11 ? 4 : 0) |
			(bS01 ? 8 : 0);

		check(Case != 0 && Case != 15);

		const bool bE0 = (bS00 != bS10);
		const bool bE1 = (bS10 != bS11);
		const bool bE2 = (bS01 != bS11);
		const bool bE3 = (bS00 != bS01);

		const int32 E0 = bE0 ? GetOrCreateEdgeVertex(Mesh, EdgeVertexCache, BoundaryVerts, Grid, Settings, CellX,     CellY,     0) : -1;
		const int32 E1 = bE1 ? GetOrCreateEdgeVertex(Mesh, EdgeVertexCache, BoundaryVerts, Grid, Settings, CellX + 1, CellY,     1) : -1;
		const int32 E2 = bE2 ? GetOrCreateEdgeVertex(Mesh, EdgeVertexCache, BoundaryVerts, Grid, Settings, CellX,     CellY + 1, 0) : -1;
		const int32 E3 = bE3 ? GetOrCreateEdgeVertex(Mesh, EdgeVertexCache, BoundaryVerts, Grid, Settings, CellX,     CellY,     1) : -1;

		switch (Case)
		{
		case 1:  OutPoly = { V00, E0, E3 }; break;
		case 2:  OutPoly = { V10, E1, E0 }; break;
		case 3:  OutPoly = { V00, V10, E1, E3 }; break;
		case 4:  OutPoly = { V11, E2, E1 }; break;
		case 5:  OutPoly = { V00, E0, E1, V11, E2, E3 }; break;
		case 6:  OutPoly = { V10, V11, E2, E0 }; break;
		case 7:  OutPoly = { V00, V10, V11, E2, E3 }; break;
		case 8:  OutPoly = { V01, E3, E2 }; break;
		case 9:  OutPoly = { V00, E0, E2, V01 }; break;
		case 10: OutPoly = { V10, E1, E2, V01, E3, E0 }; break;
		case 11: OutPoly = { V00, V10, E1, E2, V01 }; break;
		case 12: OutPoly = { V11, V01, E3, E1 }; break;
		case 13: OutPoly = { V00, E0, E1, V11, V01 }; break;
		case 14: OutPoly = { V10, V11, V01, E3, E0 }; break;
		default: break;
		}
	}

	static void TriangulatePolygonFan(
		FDynamicMesh3& Mesh,
		const TArray<int32>& Poly,
		bool bDeterministic)
	{
		const int32 N = Poly.Num();
		if (N < 3)
		{
			return;
		}

		const int32 Root = Poly[0];
		for (int32 i = 1; i < N - 1; ++i)
		{
			AddTriDeterministic(Mesh, Root, Poly[i], Poly[i + 1]);
		}
	}

	// ------------------------------------------------------------
	// Final world->local XY translation (after crop/normals)
	// ------------------------------------------------------------
	static void TranslateMeshToLocalXY(FDynamicMesh3& Mesh, const FVector2D& OriginXY)
	{
		for (int32 Vid : Mesh.VertexIndicesItr())
		{
			if (!Mesh.IsVertex(Vid))
			{
				continue;
			}

			FVector3d P = Mesh.GetVertex(Vid);
			P.X -= (double)OriginXY.X;
			P.Y -= (double)OriginXY.Y;
			Mesh.SetVertex(Vid, P);
		}
	}

// ------------------------------------------------------------
// Compute normals into the mesh's primary normal overlay (API-correct)
// ------------------------------------------------------------
static void ComputeAndAssignNormals(FDynamicMesh3& Mesh)
{
	// Ensure attributes exist
	if (!Mesh.HasAttributes())
	{
		Mesh.EnableAttributes();
	}

	// Get the primary normal overlay
	UE::Geometry::FDynamicMeshNormalOverlay* NormalOverlay =
		Mesh.Attributes()->PrimaryNormals();

	// In this GeometryCore version, the overlay is created on-demand
	if (!NormalOverlay)
	{
		NormalOverlay = Mesh.Attributes()->PrimaryNormals();
	}

	check(NormalOverlay);

	// 1) Initialize the overlay layout (one normal per vertex, wired to triangles)
	UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(
		NormalOverlay,
		/*bUseMeshVertexNormalsIfAvailable=*/false);

	// 2) Recompute the actual normal values
	UE::Geometry::FMeshNormals::QuickRecomputeOverlayNormals(
		Mesh,
		/*bInvert=*/false,
		/*bWeightByArea=*/true,
		/*bWeightByAngle=*/true,
		/*bParallelCompute=*/true);
}

	// ------------------------------------------------------------
	// Override crop-boundary normals from sampled landscape normals (seam killer)
	// ------------------------------------------------------------
static void OverrideBoundaryNormalsFromSamples(
    FDynamicMesh3& Mesh,
    const FBox2D& CropBoundsXY,
    const FPCGLandscapeMeshGridDesc& Grid,
    const TArray<FPCGLandscapeGridSample>& Samples,
    double CellSize)

	{
		if (!Mesh.HasAttributes())
		{
			return;
		}

		UE::Geometry::FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();
		if (!Normals)
		{
			return;
		}

		const double Eps = 1e-4;

		for (int32 Vid : Mesh.VertexIndicesItr())
		{
			if (!Mesh.IsVertex(Vid))
			{
				continue;
			}

			const FVector3d P = Mesh.GetVertex(Vid);

			const bool bOnBoundary =
				FMath::Abs(P.X - (double)CropBoundsXY.Min.X) <= Eps ||
				FMath::Abs(P.X - (double)CropBoundsXY.Max.X) <= Eps ||
				FMath::Abs(P.Y - (double)CropBoundsXY.Min.Y) <= Eps ||
				FMath::Abs(P.Y - (double)CropBoundsXY.Max.Y) <= Eps;

			if (!bOnBoundary)
			{
				continue;
			}


const double fx = (P.X - (double)Grid.GridMinXY.X) / CellSize;
const double fy = (P.Y - (double)Grid.GridMinXY.Y) / CellSize;

const double ix = FMath::RoundToDouble(fx);
const double iy = FMath::RoundToDouble(fy);

// Skip edge-intersection vertices (marching squares mid-edges)
if (FMath::Abs(fx - ix) > 1e-4 || FMath::Abs(fy - iy) > 1e-4)
{
    continue;
}

const int32 GX = (int32)ix;
const int32 GY = (int32)iy;

if (GX < 0 || GX >= Grid.GridX || GY < 0 || GY >= Grid.GridY)
{
    continue;
}

const int32 SIdx = GX + GY * Grid.GridX;
const FVector3f N = (FVector3f)Samples[SIdx].Normal;


			// Add a new normal element and assign it anywhere this vertex is used
			const int32 Elem = Normals->AppendElement(N);

			// Iterate triangles of this vertex and overwrite the corner normal id
			Mesh.EnumerateVertexTriangles(Vid, [&](int32 Tid)
			{
				if (!Mesh.IsTriangle(Tid))
				{
					return;
				}

				const FIndex3i TriV = Mesh.GetTriangle(Tid);
				FIndex3i TriN = Normals->GetTriangle(Tid);

				if (TriV.A == Vid) TriN.A = Elem;
				if (TriV.B == Vid) TriN.B = Elem;
				if (TriV.C == Vid) TriN.C = Elem;

				Normals->SetTriangle(Tid, TriN);
			});
		}
	}

} // namespace Builder_Internal

	bool BuildMeshFromSamples(
		const FPCGLandscapeMeshGridDesc& GridDesc,
		const FPCGLandscapeMeshBuilderSettings& Settings,
		const FBox2D& CropBoundsXY,
		FDynamicMesh3& OutMesh,
		FPCGLandscapeMeshConstraints& OutConstraints,
		FPCGLandscapeMeshBuilderStats* OutStats)
	{
		if (!GridDesc.Samples || GridDesc.GridX < 2 || GridDesc.GridY < 2)
		{
			return false;
		}

		const int32 GridX = GridDesc.GridX;
		const int32 GridY = GridDesc.GridY;

		const TArray<FPCGLandscapeGridSample>& Samples = *GridDesc.Samples;
		if (Samples.Num() != GridX * GridY)
		{
			return false;
		}

		FPCGLandscapeMeshBuilderStats Stats;
		Stats.GridX = GridX;
		Stats.GridY = GridY;
		Stats.NumCellsTotal = (GridX - 1) * (GridY - 1);

		OutMesh.Clear();
		OutConstraints.ConstrainedVertices.Reset();
		OutConstraints.ConstrainedEdges.Reset();

		// 1) Create base grid corner vertices (world space for now)
		TArray<int32> CornerVID;
		CornerVID.SetNumUninitialized(GridX * GridY);

		for (int32 Y = 0; Y < GridY; ++Y)
		{
			for (int32 X = 0; X < GridX; ++X)
			{
				const FPCGLandscapeGridSample& S = Samples[Builder_Internal::SampleIndex(X, Y, GridX)];
				const FVector3d P = Builder_Internal::MakePos(GridDesc.GridMinXY, Settings.CellSize, X, Y, S.Height);
				const int32 Vid = OutMesh.AppendVertex(P);
				CornerVID[Builder_Internal::SampleIndex(X, Y, GridX)] = Vid;
			}
		}

		// Marching squares edge vertex cache + boundary vertices set
		TMap<uint64, int32> EdgeVertexCache;
		EdgeVertexCache.Reserve(Stats.NumCellsTotal * 2);

		TSet<int32> MaskBoundaryVerts;

		// 2) Build topology per cell (hybrid)
		TArray<int32> Poly;
		Poly.Reserve(8);

		for (int32 Y = 0; Y < GridY - 1; ++Y)
		{
			for (int32 X = 0; X < GridX - 1; ++X)
			{
				const int32 I00 = Builder_Internal::SampleIndex(X,     Y,     GridX);
				const int32 I10 = Builder_Internal::SampleIndex(X + 1, Y,     GridX);
				const int32 I11 = Builder_Internal::SampleIndex(X + 1, Y + 1, GridX);
				const int32 I01 = Builder_Internal::SampleIndex(X,     Y + 1, GridX);

				const float M00 = Samples[I00].Mask;
				const float M10 = Samples[I10].Mask;
				const float M11 = Samples[I11].Mask;
				const float M01 = Samples[I01].Mask;

				const bool S00 = Builder_Internal::IsSolid(M00, Settings.MaskThreshold);
				const bool S10 = Builder_Internal::IsSolid(M10, Settings.MaskThreshold);
				const bool S11 = Builder_Internal::IsSolid(M11, Settings.MaskThreshold);
				const bool S01 = Builder_Internal::IsSolid(M01, Settings.MaskThreshold);

				const int32 NumSolid = (int32)S00 + (int32)S10 + (int32)S11 + (int32)S01;

				if (NumSolid == 0)
				{
					Stats.NumCellsEmpty++;
					continue;
				}
				if (NumSolid == 4)
				{
					Stats.NumCellsSolid++;

					const int32 V00 = CornerVID[I00];
					const int32 V10 = CornerVID[I10];
					const int32 V11 = CornerVID[I11];
					const int32 V01 = CornerVID[I01];

					if (Settings.bSolidQuadsUseDiagBLtoTR)
					{
						Builder_Internal::AddTriDeterministic(OutMesh, V00, V10, V11);
						Builder_Internal::AddTriDeterministic(OutMesh, V00, V11, V01);
					}
					else
					{
						Builder_Internal::AddTriDeterministic(OutMesh, V00, V10, V01);
						Builder_Internal::AddTriDeterministic(OutMesh, V10, V11, V01);
					}

					continue;
				}

				Stats.NumCellsMixed++;

				if (!Settings.bUseMarchingSquares)
				{
					continue;
				}

				const int32 V00 = CornerVID[I00];
				const int32 V10 = CornerVID[I10];
				const int32 V11 = CornerVID[I11];
				const int32 V01 = CornerVID[I01];

				Builder_Internal::BuildCellPolygon_MarchingSquares(
					OutMesh,
					EdgeVertexCache,
					MaskBoundaryVerts,
					GridDesc,
					Settings,
					X, Y,
					V00, V10, V11, V01,
					S00, S10, S11, S01,
					Poly);

				Builder_Internal::TriangulatePolygonFan(OutMesh, Poly, Settings.bDeterministicTriangulation);
			}
		}

		Stats.NumTrianglesBeforeCrop = OutMesh.TriangleCount();

		// 3) Promote mask-boundary vertices to hard constraints
		for (int32 Vid : MaskBoundaryVerts)
		{
			OutConstraints.ConstrainedVertices.Add(Vid);
		}

		// 4) Add crop boundary constraints (tile seam safety)
		if (Settings.bConstrainCropBoundary)
		{
			Builder_Internal::AddCropBoundaryConstraints(CropBoundsXY, Settings, OutMesh, OutConstraints);
		}

		// 5) Convert constrained vertices -> constrained edges (incident edges)
		Builder_Internal::AccumulateConstraintEdgesFromVertices(OutMesh, OutConstraints);

		// 6) Subdivision DISABLED (requested)
		// if (Settings.bEnableSubdivision && Settings.Subdivide.SubdivisionLevels > 0)
		// {
		//     ApplyPNSubdivideInterior(OutMesh, OutConstraints, Settings.Subdivide, &Stats.SubdivisionStats);
		// }

		// 7) Crop back to partition bounds (XY) -- still in world space here
		Builder_Internal::CropMeshToBoundsXY(OutMesh, CropBoundsXY);
		Stats.NumTrianglesAfterCrop = OutMesh.TriangleCount();

		// 8) Optional cleanup
		if (Settings.bRemoveIsolatedVertices)
		{
			Builder_Internal::RemoveIsolatedVertices(OutMesh);
		}

		// 9) Compute normals in WORLD space
		Builder_Internal::ComputeAndAssignNormals(OutMesh);

		// 10) Override boundary normals from sampled landscape normals (seam-free)
		Builder_Internal::OverrideBoundaryNormalsFromSamples(OutMesh, CropBoundsXY, GridDesc, Samples, Settings.CellSize);


		// 11) Convert mesh to local space in XY (does not affect normals)
		const FVector2D OriginXY = CropBoundsXY.GetCenter();
		Builder_Internal::TranslateMeshToLocalXY(OutMesh, OriginXY);

		if (OutStats)
		{
			*OutStats = Stats;
		}

		return (OutMesh.TriangleCount() > 0);
	}
} // namespace WDEditor::PCG