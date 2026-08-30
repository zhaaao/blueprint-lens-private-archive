#include "BlueprintLensGraphvizLayoutBackend.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr float PointsPerInch = 72.0f;
constexpr float LayoutPadding = 8.0f;

struct FGraphvizPoint
{
	float X = 0.0f;
	float Y = 0.0f;
};

struct FGraphvizRawNode
{
	FString DotId;
	FString UnitId;
	FGraphvizPoint Center;
	FVector2D Size = FVector2D::ZeroVector;
	FVector2D Position = FVector2D::ZeroVector;
};

struct FGraphvizRawEdge
{
	FString DotId;
	FString RelationId;
	FString SourceDotId;
	FString TargetDotId;
	TArray<FGraphvizPoint> RoutePoints;
	FGraphvizPoint SourcePoint;
	FGraphvizPoint TargetPoint;
	bool bHasSourcePoint = false;
	bool bHasTargetPoint = false;
};

bool ParseFloat(const FString& Text, float& OutValue)
{
	return LexTryParseString(OutValue, *Text.TrimStartAndEnd());
}

bool ParsePointToken(const FString& Token, FGraphvizPoint& OutPoint)
{
	TArray<FString> Parts;
	Token.ParseIntoArray(Parts, TEXT(","), true);
	if (Parts.Num() != 2 || !ParseFloat(Parts[0], OutPoint.X) ||
		!ParseFloat(Parts[1], OutPoint.Y))
	{
		return false;
	}
	return true;
}

bool ParsePosition(const FString& Text, FGraphvizPoint& OutPoint)
{
	return ParsePointToken(Text, OutPoint);
}

bool ParseBoundingBox(
	const FString& Text,
	float& OutMinX,
	float& OutMinY,
	float& OutMaxX,
	float& OutMaxY)
{
	TArray<FString> Parts;
	Text.ParseIntoArray(Parts, TEXT(","), true);
	return Parts.Num() == 4 && ParseFloat(Parts[0], OutMinX) &&
		ParseFloat(Parts[1], OutMinY) && ParseFloat(Parts[2], OutMaxX) &&
		ParseFloat(Parts[3], OutMaxY) && OutMaxX > OutMinX && OutMaxY > OutMinY;
}

bool ParseSpline(
	const FString& Text,
	FGraphvizRawEdge& OutEdge)
{
	FString Normalized = Text;
	Normalized.ReplaceInline(TEXT(";"), TEXT(" "));
	TArray<FString> Tokens;
	Normalized.ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		FString Prefix;
		FString PointText;
		const bool bHasPrefix = Token.Split(TEXT(","), &Prefix, &PointText);
		FGraphvizPoint Point;
		if (bHasPrefix && (Prefix == TEXT("e") || Prefix == TEXT("s")))
		{
			if (!ParsePointToken(PointText, Point))
			{
				return false;
			}
			if (Prefix == TEXT("e"))
			{
				OutEdge.TargetPoint = Point;
				OutEdge.bHasTargetPoint = true;
			}
			else
			{
				OutEdge.SourcePoint = Point;
				OutEdge.bHasSourcePoint = true;
			}
		}
		else
		{
			if (!ParsePointToken(Token, Point))
			{
				return false;
			}
			OutEdge.RoutePoints.Add(Point);
		}
	}

	if (!OutEdge.bHasSourcePoint && !OutEdge.RoutePoints.IsEmpty())
	{
		OutEdge.SourcePoint = OutEdge.RoutePoints[0];
		OutEdge.bHasSourcePoint = true;
	}
	if (!OutEdge.bHasTargetPoint && !OutEdge.RoutePoints.IsEmpty())
	{
		OutEdge.TargetPoint = OutEdge.RoutePoints.Last();
		OutEdge.bHasTargetPoint = true;
	}
	return OutEdge.bHasSourcePoint && OutEdge.bHasTargetPoint;
}

FVector2D ToUiPoint(
	const FGraphvizPoint& Point,
	const float MinX,
	const float MaxY)
{
	return FVector2D(Point.X - MinX, MaxY - Point.Y);
}

FString EscapeDotString(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
	return Escaped;
}

FString NodeDotId(const int32 Index)
{
	return FString::Printf(TEXT("bl_node_%d"), Index);
}

FString EdgeDotId(const int32 Index)
{
	return FString::Printf(TEXT("bl_edge_%d"), Index);
}

FString GroupDotId(const int32 Index)
{
	return FString::Printf(TEXT("cluster_bl_group_%d"), Index);
}

FString GraphvizVersion(const FString& StandardError)
{
	const int32 Marker = StandardError.Find(TEXT("version "));
	if (Marker == INDEX_NONE)
	{
		return StandardError.TrimStartAndEnd();
	}
	FString Version = StandardError.Mid(Marker + 8).TrimStartAndEnd();
	int32 End = INDEX_NONE;
	if (Version.FindChar(TEXT(' '), End) || Version.FindChar(TEXT('\r'), End) ||
		Version.FindChar(TEXT('\n'), End))
	{
		Version.LeftInline(End);
	}
	return Version;
}

bool TryGetString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& OutValue)
{
	return Object.IsValid() && Object->TryGetStringField(Field, OutValue) &&
		!OutValue.IsEmpty();
}

bool TryGetNumber(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32& OutValue)
{
	if (!Object.IsValid() || !Object->HasTypedField<EJson::Number>(Field))
	{
		return false;
	}
	OutValue = static_cast<int32>(Object->GetNumberField(Field));
	return true;
}
}

FBlueprintLensGraphvizLayoutBackend::FBlueprintLensGraphvizLayoutBackend(
	const FBlueprintLensGraphvizLayoutOptions& InOptions)
	: Options(InOptions)
{
}

EBlueprintLensLayoutBackendKind
FBlueprintLensGraphvizLayoutBackend::GetBackendKind() const
{
	return EBlueprintLensLayoutBackendKind::GraphvizDot;
}

FString FBlueprintLensGraphvizLayoutBackend::ResolveExecutable(
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (!Options.ExecutablePath.IsEmpty())
	{
		if (FPaths::FileExists(Options.ExecutablePath))
		{
			return FPaths::ConvertRelativePathToFull(Options.ExecutablePath);
		}
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EXECUTABLE_OVERRIDE_MISSING");
		return FString();
	}
	return FBlueprintLensExternalLayoutProcess::ResolveExecutable(
		TEXT("BLUEPRINT_LENS_GRAPHVIZ_ROOT"),
		TEXT("dot"),
		OutDiagnostic);
}

bool FBlueprintLensGraphvizLayoutBackend::IsAvailable(
	FString& OutDiagnostic) const
{
	const FString Executable = ResolveExecutable(OutDiagnostic);
	if (Executable.IsEmpty())
	{
		return false;
	}
	OutDiagnostic = FString::Printf(
		TEXT("BLUEPRINT_LENS_GRAPHVIZ_AVAILABLE:%s"),
		*Executable);
	return true;
}

FString FBlueprintLensGraphvizLayoutBackend::SerializeDot(
	const FBlueprintLensLayoutRequest& Request,
	FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (!Request.IsValid())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_REQUEST_INVALID");
		return FString();
	}

	const bool bVertical = Request.Profile != EBlueprintLensLayoutProfile::Linear &&
		Request.TargetWidth < 680.0f;
	FString Dot = TEXT("digraph BlueprintLens {\n");
	Dot += FString::Printf(
		TEXT("  graph [rankdir=%s, margin=0, pad=0.08, nodesep=0.25, ranksep=0.45, splines=polyline, compound=true];\n"),
		bVertical ? TEXT("TB") : TEXT("LR"));
	Dot += TEXT("  node [shape=box, fixedsize=true, margin=0, label=\"\"];\n");

	for (int32 Index = 0; Index < Request.Nodes.Num(); ++Index)
	{
		const FBlueprintLensLayoutNodeRequest& Node = Request.Nodes[Index];
		Dot += FString::Printf(
			TEXT("  %s [id=\"%s\", width=%.6f, height=%.6f];\n"),
			*NodeDotId(Index),
			*EscapeDotString(NodeDotId(Index)),
			Node.DesiredSize.X / PointsPerInch,
			Node.DesiredSize.Y / PointsPerInch);
	}

	for (int32 Index = 0; Index < Request.Groups.Num(); ++Index)
	{
		const FBlueprintLensLayoutGroupRequest& Group = Request.Groups[Index];
		Dot += FString::Printf(
			TEXT("  subgraph %s { id=\"%s\"; color=\"transparent\";\n"),
			*GroupDotId(Index),
			*EscapeDotString(GroupDotId(Index)));
		for (const FString& UnitId : Group.MemberUnitIds)
		{
			const int32 NodeIndex = Request.Nodes.IndexOfByPredicate(
				[&UnitId](const FBlueprintLensLayoutNodeRequest& Candidate)
				{
					return Candidate.UnitId == UnitId;
				});
			if (NodeIndex == INDEX_NONE)
			{
				OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_GROUP_MEMBER_MISSING");
				return FString();
			}
			Dot += FString::Printf(TEXT("    %s;\n"), *NodeDotId(NodeIndex));
		}
		Dot += TEXT("  }\n");
	}

	for (int32 Index = 0; Index < Request.Edges.Num(); ++Index)
	{
		const FBlueprintLensLayoutEdgeRequest& Edge = Request.Edges[Index];
		const int32 SourceIndex = Request.Nodes.IndexOfByPredicate(
			[&Edge](const FBlueprintLensLayoutNodeRequest& Candidate)
			{
				return Candidate.UnitId == Edge.SourceUnitId;
			});
		const int32 TargetIndex = Request.Nodes.IndexOfByPredicate(
			[&Edge](const FBlueprintLensLayoutNodeRequest& Candidate)
			{
				return Candidate.UnitId == Edge.TargetUnitId;
			});
		if (SourceIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EDGE_NODE_MISSING");
			return FString();
		}
		Dot += FString::Printf(
			TEXT("  %s -> %s [id=\"%s\", tailport=e, headport=w, arrowhead=none, constraint=%s];\n"),
			*NodeDotId(SourceIndex),
			*NodeDotId(TargetIndex),
			*EscapeDotString(EdgeDotId(Index)),
			Edge.bParticipatesInRank ? TEXT("true") : TEXT("false"));
	}
	Dot += TEXT("}\n");
	return Dot;
}

bool FBlueprintLensGraphvizLayoutBackend::NormalizeJson(
	const FString& Json,
	const FBlueprintLensLayoutRequest& Request,
	const FString& BackendVersion,
	const FString& ConfigurationFingerprint,
	FBlueprintLensLayoutLedger& OutLedger,
	FString& OutDiagnostic)
{
	OutLedger = FBlueprintLensLayoutLedger();
	OutDiagnostic.Reset();
	if (!Request.IsValid())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_REQUEST_INVALID");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_JSON_MALFORMED");
		return false;
	}

	FString BoundingBox;
	const TArray<TSharedPtr<FJsonValue>>* Objects = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
	if (!TryGetString(Root, TEXT("bb"), BoundingBox) ||
		!Root->TryGetArrayField(TEXT("objects"), Objects) ||
		!Root->TryGetArrayField(TEXT("edges"), Edges) ||
		Objects == nullptr || Edges == nullptr)
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_OUTPUT_FIELDS_MISSING");
		return false;
	}
	if (Edges->Num() != Request.Edges.Num())
	{
		OutDiagnostic = FString::Printf(
			TEXT("BLUEPRINT_LENS_GRAPHVIZ_COVERAGE_MISMATCH:objects=%d;nodes=%d;edges=%d/%d"),
			Objects->Num(),
			Request.Nodes.Num(),
			Edges->Num(),
			Request.Edges.Num());
		return false;
	}

	float MinX = 0.0f;
	float MinY = 0.0f;
	float MaxX = 0.0f;
	float MaxY = 0.0f;
	if (!ParseBoundingBox(BoundingBox, MinX, MinY, MaxX, MaxY))
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_BOUNDS_MISSING");
		return false;
	}

	TMap<FString, FGraphvizRawNode> RawNodesByDotId;
	TMap<FString, FString> DotIdsByUnitId;
	TMap<int32, FString> DotIdsByGraphvizIndex;
	TSet<int32> SeenGroupIndexes;
	for (int32 ObjectIndex = 0; ObjectIndex < Objects->Num(); ++ObjectIndex)
	{
		const TSharedPtr<FJsonObject> Object = (*Objects)[ObjectIndex].IsValid()
			? (*Objects)[ObjectIndex]->AsObject()
			: nullptr;
		FString DotId;
		if (!Object.IsValid() || !TryGetString(Object, TEXT("id"), DotId))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_OBJECT_ID_MISSING");
			return false;
		}
		if (DotId.StartsWith(TEXT("cluster_bl_group_")))
		{
			const int32 GroupIndex = FCString::Atoi(*DotId.RightChop(17));
			if (!Request.Groups.IsValidIndex(GroupIndex) ||
				SeenGroupIndexes.Contains(GroupIndex))
			{
				OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_GROUP_ID_UNKNOWN");
				return false;
			}
			SeenGroupIndexes.Add(GroupIndex);
			continue;
		}
		if (!DotId.StartsWith(TEXT("bl_node_")))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_OBJECT_ID_UNKNOWN");
			return false;
		}
		FString Position;
		FString Width;
		FString Height;
		int32 GraphvizIndex = INDEX_NONE;
		if (!TryGetString(Object, TEXT("pos"), Position) ||
			!TryGetString(Object, TEXT("width"), Width) ||
			!TryGetString(Object, TEXT("height"), Height) ||
			!TryGetNumber(Object, TEXT("_gvid"), GraphvizIndex))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_NODE_GEOMETRY_MISSING");
			return false;
		}
		const int32 NodeIndex = DotId.StartsWith(TEXT("bl_node_"))
			? FCString::Atoi(*DotId.RightChop(8))
			: INDEX_NONE;
		if (!Request.Nodes.IsValidIndex(NodeIndex))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_NODE_ID_UNKNOWN");
			return false;
		}
		FGraphvizPoint Center;
		float WidthInches = 0.0f;
		float HeightInches = 0.0f;
		if (!ParsePosition(Position, Center) || !ParseFloat(Width, WidthInches) ||
			!ParseFloat(Height, HeightInches) || WidthInches <= 0.0f ||
			HeightInches <= 0.0f || RawNodesByDotId.Contains(DotId))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_NODE_GEOMETRY_INVALID");
			return false;
		}
		FGraphvizRawNode RawNode;
		RawNode.DotId = DotId;
		RawNode.UnitId = Request.Nodes[NodeIndex].UnitId;
		RawNode.Center = Center;
		RawNode.Size = Request.Nodes[NodeIndex].DesiredSize;
		RawNode.Position = FVector2D(
			Center.X - RawNode.Size.X * 0.5f - MinX,
			MaxY - (Center.Y + RawNode.Size.Y * 0.5f));
		DotIdsByUnitId.Add(RawNode.UnitId, DotId);
		RawNodesByDotId.Add(DotId, MoveTemp(RawNode));
		DotIdsByGraphvizIndex.Add(GraphvizIndex, DotId);
	}
	if (RawNodesByDotId.Num() != Request.Nodes.Num() ||
		SeenGroupIndexes.Num() != Request.Groups.Num())
	{
		OutDiagnostic = FString::Printf(
			TEXT("BLUEPRINT_LENS_GRAPHVIZ_COVERAGE_MISMATCH:nodes=%d/%d;groups=%d/%d"),
			RawNodesByDotId.Num(),
			Request.Nodes.Num(),
			SeenGroupIndexes.Num(),
			Request.Groups.Num());
		return false;
	}

	TMap<FString, FGraphvizRawEdge> RawEdgesByDotId;
	TMap<FString, FString> DotIdsByRelationId;
	for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
	{
		const TSharedPtr<FJsonObject> EdgeObject = EdgeValue.IsValid()
			? EdgeValue->AsObject()
			: nullptr;
		FString DotId;
		FString Position;
		int32 TailIndex = INDEX_NONE;
		int32 HeadIndex = INDEX_NONE;
		if (!EdgeObject.IsValid() || !TryGetString(EdgeObject, TEXT("id"), DotId) ||
			!TryGetString(EdgeObject, TEXT("pos"), Position) ||
			!TryGetNumber(EdgeObject, TEXT("tail"), TailIndex) ||
			!TryGetNumber(EdgeObject, TEXT("head"), HeadIndex))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EDGE_GEOMETRY_MISSING");
			return false;
		}
		const int32 EdgeIndex = DotId.StartsWith(TEXT("bl_edge_"))
			? FCString::Atoi(*DotId.RightChop(8))
			: INDEX_NONE;
		const FString* SourceDotId = DotIdsByGraphvizIndex.Find(TailIndex);
		const FString* TargetDotId = DotIdsByGraphvizIndex.Find(HeadIndex);
		if (!Request.Edges.IsValidIndex(EdgeIndex) || SourceDotId == nullptr ||
			TargetDotId == nullptr || RawEdgesByDotId.Contains(DotId))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EDGE_ID_UNKNOWN");
			return false;
		}
		const FBlueprintLensLayoutEdgeRequest& RequestedEdge = Request.Edges[EdgeIndex];
		const FGraphvizRawNode* SourceNode = RawNodesByDotId.Find(*SourceDotId);
		const FGraphvizRawNode* TargetNode = RawNodesByDotId.Find(*TargetDotId);
		if (SourceNode == nullptr || TargetNode == nullptr ||
			SourceNode->UnitId != RequestedEdge.SourceUnitId ||
			TargetNode->UnitId != RequestedEdge.TargetUnitId)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EDGE_ENDPOINT_MISMATCH");
			return false;
		}
		FGraphvizRawEdge RawEdge;
		RawEdge.DotId = DotId;
		RawEdge.RelationId = RequestedEdge.RelationId;
		RawEdge.SourceDotId = *SourceDotId;
		RawEdge.TargetDotId = *TargetDotId;
		if (!ParseSpline(Position, RawEdge) || RawEdge.RoutePoints.Num() > 1 &&
			(!RawEdge.bHasSourcePoint || !RawEdge.bHasTargetPoint))
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EDGE_ROUTE_MISSING");
			return false;
		}
		DotIdsByRelationId.Add(RawEdge.RelationId, DotId);
		RawEdgesByDotId.Add(DotId, MoveTemp(RawEdge));
	}

	OutLedger.Backend = EBlueprintLensLayoutBackendKind::GraphvizDot;
	OutLedger.BackendVersion = BackendVersion;
	OutLedger.ConfigurationFingerprint = ConfigurationFingerprint;
	OutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	for (const FBlueprintLensLayoutNodeRequest& RequestedNode : Request.Nodes)
	{
		const FString* DotId = DotIdsByUnitId.Find(RequestedNode.UnitId);
		const FGraphvizRawNode* RawNode = DotId == nullptr
			? nullptr
			: RawNodesByDotId.Find(*DotId);
		if (RawNode == nullptr)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_NODE_COVERAGE_MISMATCH");
			OutLedger.DiagnosticCode = OutDiagnostic;
			return false;
		}
		FBlueprintLensLayoutNodePlacement Placement;
		Placement.UnitId = RequestedNode.UnitId;
		Placement.Position = RawNode->Position;
		Placement.Size = RequestedNode.DesiredSize;
		OutLedger.Nodes.Add(MoveTemp(Placement));
	}

	for (const FBlueprintLensLayoutNodeRequest& RequestedNode : Request.Nodes)
	{
		const FBlueprintLensLayoutNodePlacement* NodePlacement =
			OutLedger.Nodes.FindByPredicate(
				[&RequestedNode](const FBlueprintLensLayoutNodePlacement& Candidate)
				{
					return Candidate.UnitId == RequestedNode.UnitId;
				});
		if (NodePlacement == nullptr)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_PORT_NODE_MISSING");
			return false;
		}
		int32 InputCount = 0;
		int32 OutputCount = 0;
		for (const FBlueprintLensLayoutPortRequest& Port : RequestedNode.Ports)
		{
			if (Port.bInput)
			{
				++InputCount;
			}
			else
			{
				++OutputCount;
			}
		}
		for (const FBlueprintLensLayoutPortRequest& Port : RequestedNode.Ports)
		{
			const int32 Count = Port.bInput ? InputCount : OutputCount;
			if (Count <= 0 || Port.Order < 0 || Port.Order >= Count)
			{
				OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_PORT_ORDER_INVALID");
				return false;
			}
			FBlueprintLensLayoutPortPlacement Placement;
			Placement.UnitId = RequestedNode.UnitId;
			Placement.Label = Port.Label;
			Placement.bInput = Port.bInput;
			Placement.Position = FVector2D(
				Port.bInput
					? NodePlacement->Position.X
					: NodePlacement->Position.X + NodePlacement->Size.X,
				NodePlacement->Position.Y + NodePlacement->Size.Y *
					static_cast<float>(Port.Order + 1) /
					static_cast<float>(Count + 1));
			OutLedger.Ports.Add(MoveTemp(Placement));
		}
	}

	for (const FBlueprintLensLayoutEdgeRequest& RequestedEdge : Request.Edges)
	{
		const FString* DotId = DotIdsByRelationId.Find(RequestedEdge.RelationId);
		const FGraphvizRawEdge* RawEdge = DotId == nullptr
			? nullptr
			: RawEdgesByDotId.Find(*DotId);
		if (RawEdge == nullptr || !RawEdge->bHasSourcePoint ||
			!RawEdge->bHasTargetPoint)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_EDGE_COVERAGE_MISMATCH");
			return false;
		}
		FBlueprintLensLayoutEdgePlacement Placement;
		Placement.RelationId = RequestedEdge.RelationId;
		Placement.SourceUnitId = RequestedEdge.SourceUnitId;
		Placement.TargetUnitId = RequestedEdge.TargetUnitId;
		Placement.SourcePortLabel = RequestedEdge.SourcePortLabel;
		Placement.TargetPortLabel = RequestedEdge.TargetPortLabel;
		Placement.Family = RequestedEdge.Family;
		const int32 StartIndex = RawEdge->RoutePoints.Num() > 1 ? 1 : 0;
		const int32 EndIndex = RawEdge->RoutePoints.Num() > 1
			? RawEdge->RoutePoints.Num() - 1
			: RawEdge->RoutePoints.Num();
		for (int32 PointIndex = StartIndex; PointIndex < EndIndex; ++PointIndex)
		{
			Placement.BendPoints.Add(ToUiPoint(RawEdge->RoutePoints[PointIndex], MinX, MaxY));
		}
		OutLedger.Edges.Add(MoveTemp(Placement));
	}

	float TranslateX = LayoutPadding;
	float TranslateY = LayoutPadding;
	float MaxLedgerX = 0.0f;
	float MaxLedgerY = 0.0f;
	for (FBlueprintLensLayoutNodePlacement& Node : OutLedger.Nodes)
	{
		TranslateX = FMath::Max(TranslateX, LayoutPadding - Node.Position.X);
		TranslateY = FMath::Max(TranslateY, LayoutPadding - Node.Position.Y);
	}
	for (FBlueprintLensLayoutNodePlacement& Node : OutLedger.Nodes)
	{
		Node.Position.X += TranslateX;
		Node.Position.Y += TranslateY;
		MaxLedgerX = FMath::Max(MaxLedgerX, Node.Position.X + Node.Size.X);
		MaxLedgerY = FMath::Max(MaxLedgerY, Node.Position.Y + Node.Size.Y);
	}
	for (FBlueprintLensLayoutPortPlacement& Port : OutLedger.Ports)
	{
		Port.Position.X += TranslateX;
		Port.Position.Y += TranslateY;
		MaxLedgerX = FMath::Max(MaxLedgerX, Port.Position.X);
		MaxLedgerY = FMath::Max(MaxLedgerY, Port.Position.Y);
	}
	for (FBlueprintLensLayoutEdgePlacement& Edge : OutLedger.Edges)
	{
		for (FVector2D& Point : Edge.BendPoints)
		{
			Point.X += TranslateX;
			Point.Y += TranslateY;
			MaxLedgerX = FMath::Max(MaxLedgerX, Point.X);
			MaxLedgerY = FMath::Max(MaxLedgerY, Point.Y);
		}
	}
	OutLedger.CanvasSize = FVector2D(
		FMath::Max(MaxLedgerX + LayoutPadding, LayoutPadding * 2.0f),
		FMath::Max(MaxLedgerY + LayoutPadding, LayoutPadding * 2.0f));

	if (!OutLedger.IsCompleteFor(Request))
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_GRAPHVIZ_NORMALIZED_LEDGER_INVALID");
		OutLedger.DiagnosticCode = OutDiagnostic;
		return false;
	}
	return true;
}

FBlueprintLensLayoutLedger FBlueprintLensGraphvizLayoutBackend::Layout(
	const FBlueprintLensLayoutRequest& Request) const
{
	FBlueprintLensLayoutLedger Ledger;
	FString Diagnostic;
	if (!Request.IsValid())
	{
		Ledger.DiagnosticCode = TEXT("BLUEPRINT_LENS_GRAPHVIZ_REQUEST_INVALID");
		return Ledger;
	}
	const FString Executable = ResolveExecutable(Diagnostic);
	if (Executable.IsEmpty())
	{
		Ledger.DiagnosticCode = Diagnostic;
		return Ledger;
	}

	FString DotDiagnostic;
	const FString Dot = SerializeDot(Request, DotDiagnostic);
	if (Dot.IsEmpty())
	{
		Ledger.DiagnosticCode = DotDiagnostic;
		return Ledger;
	}

	FBlueprintLensExternalLayoutProcessOptions VersionOptions;
	VersionOptions.ExecutablePath = Executable;
	VersionOptions.Arguments = TEXT("-V");
	VersionOptions.TimeoutSeconds = Options.TimeoutSeconds;
	const FBlueprintLensExternalLayoutProcessResult VersionResult =
		FBlueprintLensExternalLayoutProcess::Run(VersionOptions);
	if (!VersionResult.IsSuccess())
	{
		Ledger.DiagnosticCode = VersionResult.DiagnosticCode;
		return Ledger;
	}

	FBlueprintLensExternalLayoutProcessOptions LayoutOptions;
	LayoutOptions.ExecutablePath = Executable;
	LayoutOptions.Arguments = TEXT("-Tjson0");
	LayoutOptions.StandardInput = Dot;
	LayoutOptions.TimeoutSeconds = Options.TimeoutSeconds;
	const FBlueprintLensExternalLayoutProcessResult LayoutResult =
		FBlueprintLensExternalLayoutProcess::Run(LayoutOptions);
	if (!LayoutResult.IsSuccess())
	{
		Ledger.DiagnosticCode = LayoutResult.DiagnosticCode;
		return Ledger;
	}

	const FString BackendVersion = Options.BackendVersionOverride.IsEmpty()
		? GraphvizVersion(VersionResult.StandardError)
		: Options.BackendVersionOverride;
	if (BackendVersion.IsEmpty())
	{
		Ledger.DiagnosticCode = TEXT("BLUEPRINT_LENS_GRAPHVIZ_VERSION_MISSING");
		return Ledger;
	}
	const FString ConfigurationFingerprint = FString::Printf(
		TEXT("dot-json0;profile=%s;rankdir=%s;target-width=%.2f;units=points;y-origin=top"),
		BlueprintLensLayoutProfileName(Request.Profile),
		Request.Profile != EBlueprintLensLayoutProfile::Linear && Request.TargetWidth < 680.0f
			? TEXT("TB")
			: TEXT("LR"),
		Request.TargetWidth);
	if (!NormalizeJson(
		LayoutResult.StandardOutput,
		Request,
		BackendVersion,
		ConfigurationFingerprint,
		Ledger,
		Diagnostic))
	{
		Ledger.DiagnosticCode = Diagnostic;
	}
	return Ledger;
}
