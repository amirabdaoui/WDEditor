#include "PCG/PCGPointsToDynamicMeshGrid.h"

/* PCG */
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGComponent.h"
#include "Data/PCGPointData.h"

/* PCG GeometryScript Interop */
#include "Data/PCGDynamicMeshData.h"
#include "Elements/PCGDynamicMeshBaseElement.h"

/* Geometry */
#include "UDynamicMesh.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"

/* Misc */
#include "Containers/BitArray.h"
#include "Math/UnrealMathUtility.h"

const FName UPCGPointsToDynamicMeshGridSettings::PointsPinLabel(TEXT("Points"));
const FName UPCGPointsToDynamicMeshGridSettings::DynamicMeshPinLabel(TEXT("DynamicMesh"));

UPCGPointsToDynamicMeshGridSettings::UPCGPointsToDynamicMeshGridSettings()
{
}

TArray<FPCGPinProperties> UPCGPointsToDynamicMeshGridSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace_GetRef(PointsPinLabel, EPCGDataType::Point).SetRequiredPin();
	Pins.Emplace_GetRef(DynamicMeshPinLabel, EPCGDataType::DynamicMesh).SetRequiredPin();
	return Pins;
}

TArray<FPCGPinProperties> UPCGPointsToDynamicMeshGridSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Pins;
	Pins.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::DynamicMesh);
	return Pins;
}

namespace
{
	class FPCGPointsToDynamicMeshGridDataElement final : public IPCGDynamicMeshBaseElement
	{
	protected:
		virtual bool ExecuteInternal(FPCGContext* Context) const override;
	};

	static float ReadFloatAttribute(
		const FPCGPoint& Point,
		const UPCGMetadata* Metadata,
		const FName AttributeName,
		float DefaultValue)
	{
		if (!Metadata || AttributeName.IsNone())
		{
			return DefaultValue;
		}

		if (const FPCGMetadataAttribute<float>* Attr =
			Metadata->GetConstTypedAttribute<float>(AttributeName))
		{
			return Attr->GetValueFromItemKey(Point.MetadataEntry);
		}

		return DefaultValue;
	}

	// Canonical normal-from-rotation (authoritative for this node)
	static FVector ComputeVertexNormalFromPointRotation(const FPCGPoint& Point)
	{
		const FQuat Q = Point.Transform.GetRotation().GetNormalized();
		return Q.RotateVector(FVector::UpVector).GetSafeNormal();
	}

	static uint64 MakeEdgeKey(int32 A, int32 B)
	{
		const uint32 Min = FMath::Min(A, B);
		const uint32 Max = FMath::Max(A, B);
		return (uint64(Min) << 32) | uint64(Max);
	}
}

FPCGElementPtr UPCGPointsToDynamicMeshGridSettings::CreateElement() const
{
	return MakeShared<FPCGPointsToDynamicMeshGridDataElement>();
}

bool FPCGPointsToDynamicMeshGridDataElement::ExecuteInternal(FPCGContext* Context) const
{
	check(Context);

	const UPCGPointsToDynamicMeshGridSettings* Settings =
		Context->GetInputSettings<UPCGPointsToDynamicMeshGridSettings>();
	check(Settings);

	const auto PointInputs =
		Context->InputData.GetInputsByPin(UPCGPointsToDynamicMeshGridSettings::PointsPinLabel);
	const auto MeshInputs =
		Context->InputData.GetInputsByPin(UPCGPointsToDynamicMeshGridSettings::DynamicMeshPinLabel);

	if (PointInputs.IsEmpty() || MeshInputs.IsEmpty())
	{
		return true;
	}

	const UPCGPointData* PointData = Cast<UPCGPointData>(PointInputs[0].Data);
	if (!PointData)
	{
		return true;
	}

	UPCGDynamicMeshData* OutMeshData = CopyOrSteal(MeshInputs[0], Context);
	if (!OutMeshData)
	{
		return true;
	}

	const TArray<FPCGPoint>& Points = PointData->GetPoints();
	const int32 NumPoints = Points.Num();

	auto EmitOutput = [&]()
	{
		FPCGTaggedData& Out = Context->OutputData.TaggedData.Emplace_GetRef();
		Out.Data = OutMeshData;
		Out.Pin  = PCGPinConstants::DefaultOutputLabel;
		Out.Tags = MeshInputs[0].Tags;
	};

	if (NumPoints < 4)
	{
		EmitOutput();
		return true;
	}

	const int32 GridSize = FMath::RoundToInt(FMath::Sqrt((float)NumPoints));
	if (GridSize < 2 || GridSize * GridSize != NumPoints)
	{
		EmitOutput();
		return true;
	}

	const FBox Bounds = PointData->GetBounds();
	const FVector Center = Bounds.GetCenter();
	const FVector OriginXY(Center.X, Center.Y, 0.0f);

	const UPCGMetadata* Metadata = PointData->Metadata;
	UDynamicMesh* DynMesh = OutMeshData->GetMutableDynamicMesh();

	DynMesh->EditMesh(
		[&](UE::Geometry::FDynamicMesh3& Mesh)
		{
			Mesh.Clear();
			Mesh.EnableAttributes();

			auto* Normals = Mesh.Attributes()->PrimaryNormals();

			TArray<int32> NormalIDs;
			NormalIDs.SetNumUninitialized(NumPoints);

			TArray<bool> Solid;
			Solid.SetNumUninitialized(NumPoints);

			// Base vertices
			for (int32 i = 0; i < NumPoints; ++i)
			{
				const FPCGPoint& P = Points[i];
				const float Mask = ReadFloatAttribute(P, Metadata, Settings->KeepMaskAttribute, 1.0f);

				Solid[i] =
					!Settings->bInvertMask
						? (Mask >= Settings->MaskThreshold)
						: (Mask < Settings->MaskThreshold);

				FVector Pos = P.Transform.GetLocation() - OriginXY;
				Pos.Z = P.Transform.GetLocation().Z;

				Mesh.AppendVertex((FVector3d)Pos);
				NormalIDs[i] = Normals->AppendElement(
					(FVector3f)ComputeVertexNormalFromPointRotation(P));
			}

			auto Idx = [GridSize](int32 X, int32 Y) { return Y * GridSize + X; };

			TMap<uint64, int32> EdgeVertexMap;
			TMap<uint64, int32> EdgeNormalMap;

			for (int32 y = 0; y < GridSize - 1; ++y)
			{
				for (int32 x = 0; x < GridSize - 1; ++x)
				{
					const int32 v[4] =
					{
						Idx(x,     y),
						Idx(x,     y + 1),
						Idx(x + 1, y + 1),
						Idx(x + 1, y)
					};

					const int SolidCount =
						(Solid[v[0]] ? 1 : 0) +
						(Solid[v[1]] ? 1 : 0) +
						(Solid[v[2]] ? 1 : 0) +
						(Solid[v[3]] ? 1 : 0);

// ------------------------------------------------------------
// Uniform Grid mode: NEVER use marching squares
// ------------------------------------------------------------
if (Settings->TopologyMode == EPCGGridTopologyMode::UniformGrid)
{
	// Emit nothing only if fully empty
	if (SolidCount == 0)
	{
		continue;
	}

	// Otherwise emit classic grid
	const int32 T0 = Mesh.AppendTriangle(v[0], v[2], v[3]);
	if (T0 >= 0)
	{
		Normals->SetTriangle(T0, { NormalIDs[v[0]], NormalIDs[v[2]], NormalIDs[v[3]] });
	}

	const int32 T1 = Mesh.AppendTriangle(v[0], v[1], v[2]);
	if (T1 >= 0)
	{
		Normals->SetTriangle(T1, { NormalIDs[v[0]], NormalIDs[v[1]], NormalIDs[v[2]] });
	}

	continue;
}

// ------------------------------------------------------------
// Landscape Parity (hybrid marching squares)
// ------------------------------------------------------------

// Fully solid → uniform grid
if (SolidCount == 4)
{
	const int32 T0 = Mesh.AppendTriangle(v[0], v[2], v[3]);
	if (T0 >= 0)
	{
		Normals->SetTriangle(T0, { NormalIDs[v[0]], NormalIDs[v[2]], NormalIDs[v[3]] });
	}

	const int32 T1 = Mesh.AppendTriangle(v[0], v[1], v[2]);
	if (T1 >= 0)
	{
		Normals->SetTriangle(T1, { NormalIDs[v[0]], NormalIDs[v[1]], NormalIDs[v[2]] });
	}

	continue;
}

// Fully empty → nothing
if (SolidCount == 0)
{
	continue;
}

					// Mixed → marching squares (edge-shared)
					TArray<int32> PolyVIDs;
					TArray<int32> PolyNIDs;

					auto AddCorner = [&](int c)
					{
						PolyVIDs.Add(v[c]);
						PolyNIDs.Add(NormalIDs[v[c]]);
					};

					auto AddEdge = [&](int a, int b)
					{
						const uint64 Key = MakeEdgeKey(v[a], v[b]);
						if (!EdgeVertexMap.Contains(Key))
						{
const FVector PA = Mesh.GetVertex(v[a]);
const FVector PB = Mesh.GetVertex(v[b]);

const float VA = ReadFloatAttribute(
	Points[v[a]], Metadata, Settings->KeepMaskAttribute, 1.0f);

const float VB = ReadFloatAttribute(
	Points[v[b]], Metadata, Settings->KeepMaskAttribute, 1.0f);

// Match Landscape semantics
const float T =
	(VA != VB)
		? FMath::Clamp(
			(Settings->MaskThreshold - VA) / (VB - VA),
			0.0f, 1.0f)
		: 0.5f;

const FVector P = FMath::Lerp(PA, PB, T);


							const int32 VID = Mesh.AppendVertex((FVector3d)P);
							const int32 NID = Normals->AppendElement(
								(FVector3f)ComputeVertexNormalFromPointRotation(Points[v[a]]));

							EdgeVertexMap.Add(Key, VID);
							EdgeNormalMap.Add(Key, NID);
						}

						PolyVIDs.Add(EdgeVertexMap[Key]);
						PolyNIDs.Add(EdgeNormalMap[Key]);
					};

					if (Solid[v[0]]) AddCorner(0);
					if (Solid[v[0]] != Solid[v[1]]) AddEdge(0, 1);
					if (Solid[v[1]]) AddCorner(1);
					if (Solid[v[1]] != Solid[v[2]]) AddEdge(1, 2);
					if (Solid[v[2]]) AddCorner(2);
					if (Solid[v[2]] != Solid[v[3]]) AddEdge(2, 3);
					if (Solid[v[3]]) AddCorner(3);
					if (Solid[v[3]] != Solid[v[0]]) AddEdge(3, 0);

					for (int32 i = 1; i + 1 < PolyVIDs.Num(); ++i)
					{
						const int32 T = Mesh.AppendTriangle(
							PolyVIDs[0], PolyVIDs[i], PolyVIDs[i + 1]);

						if (T >= 0)
						{
							Normals->SetTriangle(
								T,
								{ PolyNIDs[0], PolyNIDs[i], PolyNIDs[i + 1] });
						}
					}
				}
			}

			if (Settings->bRemoveIsolatedVertices)
			{
				for (int32 V : Mesh.VertexIndicesItr())
				{
					if (Mesh.GetVtxTriangleCount(V) == 0)
					{
						Mesh.RemoveVertex(V);
					}
				}
			}

			if (Settings->bCompactAtEnd)
			{
				Mesh.CompactInPlace();
			}
		},
		EDynamicMeshChangeType::GeneralEdit,
		EDynamicMeshAttributeChangeFlags::Unknown,
		true);

	EmitOutput();
	return true;
}
