// Copyright BULKHEAD Limited. All Rights Reserved.

#include "PCG/PCGLandscapeMeshSubdivision.h"

/* Geometry */
#include "DynamicMesh/DynamicMesh3.h"

/* Math */
#include "Math/Vector.h"
#include "Math/UnrealMathUtility.h"

/* GeometryCore */
#include "IndexTypes.h"

/* Containers */
#include "Containers/Map.h"

using UE::Geometry::FDynamicMesh3;
using UE::Geometry::FIndex3i;

namespace WDEditor::PCG
{
namespace Subdivision_Internal
{
	static uint64 MakeUndirectedEdgeKey(int32 A, int32 B)
	{
		const uint32 MinV = (uint32)FMath::Min(A, B);
		const uint32 MaxV = (uint32)FMath::Max(A, B);
		return (uint64(MinV) << 32) | uint64(MaxV);
	}

	/** Compute simple per-vertex normals (triangle-area weighted). */
	static void ComputeVertexNormals(
		const FDynamicMesh3& Mesh,
		TArray<FVector3d>& OutNormals)
	{
		OutNormals.SetNumZeroed(Mesh.MaxVertexID());

		for (int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (!Mesh.IsTriangle(Tid))
			{
				continue;
			}

			const FIndex3i Tri = Mesh.GetTriangle(Tid);

			const FVector3d A = Mesh.GetVertex(Tri.A);
			const FVector3d B = Mesh.GetVertex(Tri.B);
			const FVector3d C = Mesh.GetVertex(Tri.C);

			const FVector3d N = (B - A).Cross(C - A);
			const double Len = N.Length();
			if (Len > UE_KINDA_SMALL_NUMBER)
			{
				const FVector3d Nn = N / Len;
				OutNormals[Tri.A] += Nn;
				OutNormals[Tri.B] += Nn;
				OutNormals[Tri.C] += Nn;
			}
		}

		for (int32 Vid : Mesh.VertexIndicesItr())
		{
			FVector3d& N = OutNormals[Vid];
			if (!N.Normalize())
			{
				N = FVector3d::UpVector;
			}
		}
	}

	static FVector3d PN_EdgeMidpoint(
		const FVector3d& A, const FVector3d& NA,
		const FVector3d& B, const FVector3d& NB,
		double Strength)
	{
		const FVector3d M = (A + B) * 0.5;

		FVector3d N = NA + NB;
		if (!N.Normalize())
		{
			N = FVector3d::UpVector;
		}

		const FVector3d AB = B - A;
		const double dA = AB.Dot(NA);
		const double dB = (-AB).Dot(NB);

		return M + Strength * (dA - dB) * N;
	}
}

	bool ApplyPNSubdivideInterior(
		FDynamicMesh3& Mesh,
		const FPCGLandscapeMeshConstraints& Constraints,
		const FPCGLandscapePNSubdivideSettings& Settings,
		FPCGLandscapeSubdivisionStats* OutStats)
	{
		FPCGLandscapeSubdivisionStats Stats;
		Stats.NumLevels = Settings.SubdivisionLevels;

		if (Settings.SubdivisionLevels <= 0)
		{
			if (OutStats)
			{
				*OutStats = Stats;
			}
			return false;
		}

		bool bAnyRefined = false;

		for (int32 Level = 0; Level < Settings.SubdivisionLevels; ++Level)
		{
			TArray<FVector3d> VertexNormals;
			Subdivision_Internal::ComputeVertexNormals(Mesh, VertexNormals);

			TMap<uint64, int32> EdgeMidpointVertex;
			TArray<int32> TrianglesToRefine;

			for (int32 Tid : Mesh.TriangleIndicesItr())
			{
				if (!Mesh.IsTriangle(Tid))
				{
					continue;
				}

				const FIndex3i Tri = Mesh.GetTriangle(Tid);

				if (Constraints.IsVertexConstrained(Tri.A) ||
					Constraints.IsVertexConstrained(Tri.B) ||
					Constraints.IsVertexConstrained(Tri.C))
				{
					continue;
				}

				const FIndex3i TriEdges = Mesh.GetTriEdges(Tid);

				if (Constraints.IsEdgeConstrained(TriEdges.A) ||
					Constraints.IsEdgeConstrained(TriEdges.B) ||
					Constraints.IsEdgeConstrained(TriEdges.C))
				{
					continue;
				}

				TrianglesToRefine.Add(Tid);
			}

			if (TrianglesToRefine.IsEmpty())
			{
				break;
			}

			TrianglesToRefine.Sort();

			for (int32 Tid : TrianglesToRefine)
			{
				if (!Mesh.IsTriangle(Tid))
				{
					continue;
				}

				const FIndex3i Tri = Mesh.GetTriangle(Tid);
				const int32 A = Tri.A;
				const int32 B = Tri.B;
				const int32 C = Tri.C;

				auto GetMidpoint = [&](int32 V0, int32 V1) -> int32
				{
					const uint64 Key = Subdivision_Internal::MakeUndirectedEdgeKey(V0, V1);
					if (const int32* Found = EdgeMidpointVertex.Find(Key))
					{
						return *Found;
					}

					const FVector3d P =
						Subdivision_Internal::PN_EdgeMidpoint(
							Mesh.GetVertex(V0), VertexNormals[V0],
							Mesh.GetVertex(V1), VertexNormals[V1],
							Settings.PNStrength);

					const int32 NewVid = Mesh.AppendVertex(P);
					EdgeMidpointVertex.Add(Key, NewVid);
					Stats.NumVerticesAdded++;
					return NewVid;
				};

				const int32 AB = GetMidpoint(A, B);
				const int32 BC = GetMidpoint(B, C);
				const int32 CA = GetMidpoint(C, A);

				Mesh.RemoveTriangle(Tid, false);

				Mesh.AppendTriangle(A,  AB, CA);
				Mesh.AppendTriangle(AB, B,  BC);
				Mesh.AppendTriangle(CA, BC, C);
				Mesh.AppendTriangle(AB, BC, CA);

				Stats.NumTrianglesRefined++;
				Stats.NumTrianglesAdded += 4;
				bAnyRefined = true;
			}
		}

		if (OutStats)
		{
			*OutStats = Stats;
		}

		return bAnyRefined;
	}
}
