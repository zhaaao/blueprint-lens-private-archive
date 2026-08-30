#include "BlueprintLensElkLayoutBackend.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr float LayoutPadding = 8.0f;
constexpr float PortSize = 1.0f;
constexpr TCHAR RequestSchema[] = TEXT("blueprint-lens-layout-request.v1");
constexpr TCHAR ResponseSchema[] = TEXT("blueprint-lens-layout-response.v1");
constexpr TCHAR HelperBackend[] = TEXT("ELK.Layered");
constexpr TCHAR RequiredElkVersion[] = TEXT("ELK.js 0.12.0");

FString NodeId(const int32 Index)
{
	return FString::Printf(TEXT("bl_node_%d"), Index);
}

FString GroupId(const int32 Index)
{
	return FString::Printf(TEXT("bl_group_%d"), Index);
}

FString PortId(const int32 NodeIndex, const int32 PortIndex)
{
	return FString::Printf(TEXT("bl_port_%d_%d"), NodeIndex, PortIndex);
}

FString EdgeId(const int32 Index)
{
	return FString::Printf(TEXT("bl_edge_%d"), Index);
}

FString QuoteProcessArgument(const FString& Value)
{
	FString Escaped = Value;
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	return FString::Printf(TEXT("\"%s\""), *Escaped);
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
	double& OutValue)
{
	if (!Object.IsValid() || !Object->HasTypedField<EJson::Number>(Field))
	{
		return false;
	}
	OutValue = Object->GetNumberField(Field);
	return FMath::IsFinite(static_cast<float>(OutValue));
}

bool TryGetPoint(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FVector2D& OutPoint)
{
	const TSharedPtr<FJsonObject>* PointObject = nullptr;
	if (!Object.IsValid() ||
		!Object->TryGetObjectField(Field, PointObject) ||
		PointObject == nullptr || !PointObject->IsValid())
	{
		return false;
	}
	double X = 0.0;
	double Y = 0.0;
	if (!TryGetNumber(*PointObject, TEXT("x"), X) ||
		!TryGetNumber(*PointObject, TEXT("y"), Y))
	{
		return false;
	}
	OutPoint = FVector2D(static_cast<float>(X), static_cast<float>(Y));
	return true;
}

bool TryParsePointObject(
	const TSharedPtr<FJsonObject>& Object,
	FVector2D& OutPoint)
{
	double X = 0.0;
	double Y = 0.0;
	if (!TryGetNumber(Object, TEXT("x"), X) ||
		!TryGetNumber(Object, TEXT("y"), Y))
	{
		return false;
	}
	OutPoint = FVector2D(static_cast<float>(X), static_cast<float>(Y));
	return true;
}

bool ParseIndexedId(
	const FString& Id,
	const FString& Prefix,
	int32& OutIndex)
{
	if (!Id.StartsWith(Prefix))
	{
		return false;
	}
	const FString Suffix = Id.RightChop(Prefix.Len());
	if (Suffix.IsEmpty())
	{
		return false;
	}
	OutIndex = FCString::Atoi(*Suffix);
	return OutIndex >= 0 && FString::FromInt(OutIndex) == Suffix;
}

bool ParsePortIndexedId(
	const FString& Id,
	int32& OutNodeIndex,
	int32& OutPortIndex)
{
	if (!Id.StartsWith(TEXT("bl_port_")))
	{
		return false;
	}
	TArray<FString> Parts;
	Id.RightChop(8).ParseIntoArray(Parts, TEXT("_"), true);
	if (Parts.Num() != 2 || Parts[0].IsEmpty() || Parts[1].IsEmpty())
	{
		return false;
	}
	OutNodeIndex = FCString::Atoi(*Parts[0]);
	OutPortIndex = FCString::Atoi(*Parts[1]);
	return OutNodeIndex >= 0 && OutPortIndex >= 0 &&
		FString::FromInt(OutNodeIndex) == Parts[0] &&
		FString::FromInt(OutPortIndex) == Parts[1];
}

TSharedPtr<FJsonObject> MakeLayoutOptions(
	const TMap<FString, FString>& Values)
{
	TSharedPtr<FJsonObject> Options = MakeShared<FJsonObject>();
	for (const TPair<FString, FString>& Pair : Values)
	{
		Options->SetStringField(Pair.Key, Pair.Value);
	}
	return Options;
}

void AddChild(
	const TSharedPtr<FJsonObject>& Parent,
	const TSharedPtr<FJsonObject>& Child)
{
	TArray<TSharedPtr<FJsonValue>> Children;
	const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
	if (Parent->TryGetArrayField(TEXT("children"), Existing) && Existing != nullptr)
	{
		Children = *Existing;
	}
	Children.Add(MakeShared<FJsonValueObject>(Child));
	Parent->SetArrayField(TEXT("children"), MoveTemp(Children));
}

void AddEdge(
	const TSharedPtr<FJsonObject>& Parent,
	const TSharedPtr<FJsonObject>& Edge)
{
	TArray<TSharedPtr<FJsonValue>> Edges;
	const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
	if (Parent->TryGetArrayField(TEXT("edges"), Existing) && Existing != nullptr)
	{
		Edges = *Existing;
	}
	Edges.Add(MakeShared<FJsonValueObject>(Edge));
	Parent->SetArrayField(TEXT("edges"), MoveTemp(Edges));
}

struct FElkRawNode
{
	FString UnitId;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D::ZeroVector;
};

struct FElkRawPort
{
	FString UnitId;
	FString Label;
	bool bInput = false;
	FVector2D Position = FVector2D::ZeroVector;
};

struct FElkRawEdge
{
	FString RelationId;
	TArray<FVector2D> BendPoints;
};

struct FElkWalkState
{
	const FBlueprintLensLayoutRequest& Request;
	TMap<FString, FElkRawNode> Nodes;
	TMap<FString, FElkRawPort> Ports;
	TMap<FString, FElkRawEdge> Edges;
	TSet<int32> GroupIndexes;
	FVector2D MinPoint = FVector2D(FLT_MAX, FLT_MAX);
	FVector2D MaxPoint = FVector2D(-FLT_MAX, -FLT_MAX);
	FString Diagnostic;

	void IncludePoint(const FVector2D& Point)
	{
		MinPoint.X = FMath::Min(MinPoint.X, Point.X);
		MinPoint.Y = FMath::Min(MinPoint.Y, Point.Y);
		MaxPoint.X = FMath::Max(MaxPoint.X, Point.X);
		MaxPoint.Y = FMath::Max(MaxPoint.Y, Point.Y);
	}

	void Fail(const TCHAR* Code)
	{
		if (Diagnostic.IsEmpty())
		{
			Diagnostic = Code;
		}
	}
};

bool WalkContainer(
	const TSharedPtr<FJsonObject>& Container,
	const FVector2D& ParentOffset,
	FElkWalkState& State)
{
	if (!Container.IsValid() || !State.Diagnostic.IsEmpty())
	{
		return false;
	}
	FString ContainerId;
	Container->TryGetStringField(TEXT("id"), ContainerId);
	const bool bRoot = ContainerId == TEXT("root");
	double LocalX = 0.0;
	double LocalY = 0.0;
	double ContainerWidth = 0.0;
	double ContainerHeight = 0.0;
	FVector2D ContainerOffset = ParentOffset;
	if (!bRoot)
	{
		if (ContainerId.IsEmpty() || !TryGetNumber(Container, TEXT("x"), LocalX) ||
			!TryGetNumber(Container, TEXT("y"), LocalY) ||
			!TryGetNumber(Container, TEXT("width"), ContainerWidth) ||
			!TryGetNumber(Container, TEXT("height"), ContainerHeight) ||
			ContainerWidth <= 0.0 || ContainerHeight <= 0.0)
		{
			State.Fail(TEXT("BLUEPRINT_LENS_ELK_CONTAINER_GEOMETRY_MISSING"));
			return false;
		}
		ContainerOffset += FVector2D(
			static_cast<float>(LocalX), static_cast<float>(LocalY));
	}

	int32 NodeIndex = INDEX_NONE;
	int32 GroupIndex = INDEX_NONE;
	if (!bRoot && ParseIndexedId(ContainerId, TEXT("bl_node_"), NodeIndex))
	{
		if (!State.Request.Nodes.IsValidIndex(NodeIndex) ||
			State.Nodes.Contains(ContainerId))
		{
			State.Fail(TEXT("BLUEPRINT_LENS_ELK_NODE_ID_UNKNOWN"));
			return false;
		}
		FElkRawNode RawNode;
		RawNode.UnitId = State.Request.Nodes[NodeIndex].UnitId;
		RawNode.Position = ContainerOffset;
		RawNode.Size = State.Request.Nodes[NodeIndex].DesiredSize;
		State.Nodes.Add(ContainerId, RawNode);
		State.IncludePoint(RawNode.Position);
		State.IncludePoint(RawNode.Position + RawNode.Size);

		const TArray<TSharedPtr<FJsonValue>>* Ports = nullptr;
		if (Container->TryGetArrayField(TEXT("ports"), Ports) && Ports != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& PortValue : *Ports)
			{
				const TSharedPtr<FJsonObject> Port = PortValue.IsValid()
					? PortValue->AsObject()
					: nullptr;
				FString PortObjectId;
				double PortX = 0.0;
				double PortY = 0.0;
				double PortWidth = 0.0;
				double PortHeight = 0.0;
				int32 PortNodeIndex = INDEX_NONE;
				int32 PortIndex = INDEX_NONE;
				if (!Port.IsValid() || !TryGetString(Port, TEXT("id"), PortObjectId) ||
					!ParsePortIndexedId(PortObjectId, PortNodeIndex, PortIndex) ||
					PortNodeIndex != NodeIndex ||
					!State.Request.Nodes[NodeIndex].Ports.IsValidIndex(PortIndex) ||
					!TryGetNumber(Port, TEXT("x"), PortX) ||
					!TryGetNumber(Port, TEXT("y"), PortY) ||
					!TryGetNumber(Port, TEXT("width"), PortWidth) ||
					!TryGetNumber(Port, TEXT("height"), PortHeight) ||
					State.Ports.Contains(PortObjectId))
				{
					State.Fail(TEXT("BLUEPRINT_LENS_ELK_PORT_GEOMETRY_MISSING"));
					return false;
				}
				const FBlueprintLensLayoutPortRequest& RequestedPort =
					State.Request.Nodes[NodeIndex].Ports[PortIndex];
				FElkRawPort RawPort;
				RawPort.UnitId = RawNode.UnitId;
				RawPort.Label = RequestedPort.Label;
				RawPort.bInput = RequestedPort.bInput;
				RawPort.Position = RawNode.Position + FVector2D(
					static_cast<float>(PortX + PortWidth * 0.5),
					static_cast<float>(PortY + PortHeight * 0.5));
				State.Ports.Add(PortObjectId, RawPort);
				State.IncludePoint(RawPort.Position);
			}
		}
	}
	else if (!bRoot && ParseIndexedId(ContainerId, TEXT("bl_group_"), GroupIndex))
	{
		if (!State.Request.Groups.IsValidIndex(GroupIndex) ||
			State.GroupIndexes.Contains(GroupIndex))
		{
			State.Fail(TEXT("BLUEPRINT_LENS_ELK_GROUP_ID_UNKNOWN"));
			return false;
		}
		State.GroupIndexes.Add(GroupIndex);
		State.IncludePoint(ContainerOffset);
		State.IncludePoint(ContainerOffset + FVector2D(
			static_cast<float>(ContainerWidth),
			static_cast<float>(ContainerHeight)));
	}
	else if (!bRoot)
	{
		State.Fail(TEXT("BLUEPRINT_LENS_ELK_OBJECT_ID_UNKNOWN"));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
	if (Container->TryGetArrayField(TEXT("edges"), Edges) && Edges != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& EdgeValue : *Edges)
		{
			const TSharedPtr<FJsonObject> Edge = EdgeValue.IsValid()
				? EdgeValue->AsObject()
				: nullptr;
			FString EdgeObjectId;
			int32 EdgeRequestIndex = INDEX_NONE;
			const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Sections = nullptr;
			if (!Edge.IsValid() || !TryGetString(Edge, TEXT("id"), EdgeObjectId) ||
				!ParseIndexedId(EdgeObjectId, TEXT("bl_edge_"), EdgeRequestIndex) ||
				!State.Request.Edges.IsValidIndex(EdgeRequestIndex) ||
				State.Edges.Contains(EdgeObjectId) ||
				!Edge->TryGetArrayField(TEXT("sources"), Sources) || Sources == nullptr ||
				!Edge->TryGetArrayField(TEXT("targets"), Targets) || Targets == nullptr ||
				Sources->Num() != 1 || Targets->Num() != 1 ||
				!Edge->TryGetArrayField(TEXT("sections"), Sections) || Sections == nullptr ||
				Sections->Num() == 0)
			{
				State.Fail(TEXT("BLUEPRINT_LENS_ELK_EDGE_GEOMETRY_MISSING"));
				return false;
			}
			FString SourcePortObjectId;
			FString TargetPortObjectId;
			if (!(*Sources)[0].IsValid() || !(*Sources)[0]->TryGetString(SourcePortObjectId) ||
				!(*Targets)[0].IsValid() || !(*Targets)[0]->TryGetString(TargetPortObjectId))
			{
				State.Fail(TEXT("BLUEPRINT_LENS_ELK_EDGE_ENDPOINT_MISSING"));
				return false;
			}
			const FBlueprintLensLayoutEdgeRequest& RequestedEdge =
				State.Request.Edges[EdgeRequestIndex];
			int32 SourceNodeIndex = INDEX_NONE;
			int32 SourcePortIndex = INDEX_NONE;
			int32 TargetNodeIndex = INDEX_NONE;
			int32 TargetPortIndex = INDEX_NONE;
			if (!ParsePortIndexedId(
					SourcePortObjectId, SourceNodeIndex, SourcePortIndex) ||
				!ParsePortIndexedId(
					TargetPortObjectId, TargetNodeIndex, TargetPortIndex) ||
				!State.Request.Nodes.IsValidIndex(SourceNodeIndex) ||
				!State.Request.Nodes.IsValidIndex(TargetNodeIndex) ||
				!State.Request.Nodes[SourceNodeIndex].Ports.IsValidIndex(SourcePortIndex) ||
				!State.Request.Nodes[TargetNodeIndex].Ports.IsValidIndex(TargetPortIndex))
			{
				State.Fail(TEXT("BLUEPRINT_LENS_ELK_EDGE_ENDPOINT_MISSING"));
				return false;
			}
			const FBlueprintLensLayoutNodeRequest& SourceNode =
				State.Request.Nodes[SourceNodeIndex];
			const FBlueprintLensLayoutNodeRequest& TargetNode =
				State.Request.Nodes[TargetNodeIndex];
			const FBlueprintLensLayoutPortRequest& SourcePort =
				SourceNode.Ports[SourcePortIndex];
			const FBlueprintLensLayoutPortRequest& TargetPort =
				TargetNode.Ports[TargetPortIndex];
			if (SourceNode.UnitId != RequestedEdge.SourceUnitId ||
				TargetNode.UnitId != RequestedEdge.TargetUnitId ||
				SourcePort.bInput || SourcePort.Label != RequestedEdge.SourcePortLabel ||
				!TargetPort.bInput || TargetPort.Label != RequestedEdge.TargetPortLabel)
			{
				State.Fail(TEXT("BLUEPRINT_LENS_ELK_EDGE_ENDPOINT_MISMATCH"));
				return false;
			}
			FElkRawEdge RawEdge;
			RawEdge.RelationId = RequestedEdge.RelationId;
			for (const TSharedPtr<FJsonValue>& SectionValue : *Sections)
			{
				const TSharedPtr<FJsonObject> Section = SectionValue.IsValid()
					? SectionValue->AsObject()
					: nullptr;
				FVector2D StartPoint;
				FVector2D EndPoint;
				if (!TryGetPoint(Section, TEXT("startPoint"), StartPoint) ||
					!TryGetPoint(Section, TEXT("endPoint"), EndPoint))
				{
					State.Fail(TEXT("BLUEPRINT_LENS_ELK_EDGE_ROUTE_MISSING"));
					return false;
				}
				const TArray<TSharedPtr<FJsonValue>>* Bends = nullptr;
				if (Section->TryGetArrayField(TEXT("bendPoints"), Bends) && Bends != nullptr)
				{
					for (const TSharedPtr<FJsonValue>& BendValue : *Bends)
					{
						const TSharedPtr<FJsonObject> Bend = BendValue.IsValid()
							? BendValue->AsObject()
							: nullptr;
						FVector2D BendPoint;
						if (!TryParsePointObject(Bend, BendPoint))
						{
							State.Fail(TEXT("BLUEPRINT_LENS_ELK_EDGE_BEND_INVALID"));
							return false;
						}
						RawEdge.BendPoints.Add(BendPoint + ContainerOffset);
						State.IncludePoint(BendPoint + ContainerOffset);
					}
				}
				State.IncludePoint(StartPoint + ContainerOffset);
				State.IncludePoint(EndPoint + ContainerOffset);
			}
			State.Edges.Add(EdgeObjectId, MoveTemp(RawEdge));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (!bRoot && ContainerId.StartsWith(TEXT("bl_node_")))
	{
		if (Container->TryGetArrayField(TEXT("children"), Children) &&
			Children != nullptr && Children->Num() > 0)
		{
			State.Fail(TEXT("BLUEPRINT_LENS_ELK_NODE_CHILDREN_INVALID"));
			return false;
		}
	}
	else
	{
		if (!Container->TryGetArrayField(TEXT("children"), Children) || Children == nullptr)
		{
			if (!bRoot && ContainerId.StartsWith(TEXT("bl_group_")))
			{
				State.Fail(TEXT("BLUEPRINT_LENS_ELK_GROUP_CHILDREN_MISSING"));
				return false;
			}
			return true;
		}
		for (const TSharedPtr<FJsonValue>& ChildValue : *Children)
		{
			const TSharedPtr<FJsonObject> Child = ChildValue.IsValid()
				? ChildValue->AsObject()
				: nullptr;
			if (!WalkContainer(Child, ContainerOffset, State))
			{
				return false;
			}
		}
	}
	return State.Diagnostic.IsEmpty();
}

FString ResponseBackendVersion(
	const TSharedPtr<FJsonObject>& Root,
	const FString& Override,
	FString& OutDiagnostic)
{
	if (!Override.IsEmpty())
	{
		return Override;
	}
	FString Version;
	if (!TryGetString(Root, TEXT("backend_version"), Version) ||
		Version != RequiredElkVersion)
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_VERSION_UNSUPPORTED");
		return FString();
	}
	return Version;
}
}

FBlueprintLensElkLayoutBackend::FBlueprintLensElkLayoutBackend(
	const FBlueprintLensElkLayoutOptions& InOptions)
	: Options(InOptions)
{
}

EBlueprintLensLayoutBackendKind
FBlueprintLensElkLayoutBackend::GetBackendKind() const
{
	return EBlueprintLensLayoutBackendKind::ElkLayered;
}

FString FBlueprintLensElkLayoutBackend::ResolveNodeExecutable(
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	if (!Options.NodeExecutablePath.IsEmpty())
	{
		if (FPaths::FileExists(Options.NodeExecutablePath))
		{
			return FPaths::ConvertRelativePathToFull(Options.NodeExecutablePath);
		}
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_NODE_OVERRIDE_MISSING");
		return FString();
	}
	return FBlueprintLensExternalLayoutProcess::ResolveExecutable(
		TEXT("BLUEPRINT_LENS_NODE_EXE"), TEXT("node"), OutDiagnostic);
}

FString FBlueprintLensElkLayoutBackend::ResolveElkRoot(
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	FString Root = Options.ElkJsRoot;
	if (Root.IsEmpty())
	{
		Root = FPlatformMisc::GetEnvironmentVariable(TEXT("BLUEPRINT_LENS_ELKJS_ROOT"));
	}
	if (Root.IsEmpty() || !FPaths::DirectoryExists(Root) ||
		!FPaths::FileExists(FPaths::Combine(Root, TEXT("package.json"))))
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_ROOT_MISSING");
		return FString();
	}
	return FPaths::ConvertRelativePathToFull(Root);
}

FString FBlueprintLensElkLayoutBackend::ResolveHelper(
	FString& OutDiagnostic) const
{
	OutDiagnostic.Reset();
	FString Helper = Options.HelperPath;
	if (Helper.IsEmpty())
	{
		Helper = FPlatformMisc::GetEnvironmentVariable(TEXT("BLUEPRINT_LENS_ELK_HELPER"));
	}
	if (Helper.IsEmpty())
	{
		Helper = FPaths::Combine(
			FPaths::ProjectDir(), TEXT(".."), TEXT(".."), TEXT("tools"),
			TEXT("layout"), TEXT("blueprint_lens_elk_layout.mjs"));
		FPaths::CollapseRelativeDirectories(Helper);
	}
	if (!FPaths::FileExists(Helper))
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_HELPER_MISSING");
		return FString();
	}
	return FPaths::ConvertRelativePathToFull(Helper);
}

bool FBlueprintLensElkLayoutBackend::IsAvailable(FString& OutDiagnostic) const
{
	FString Diagnostic;
	const FString Node = ResolveNodeExecutable(Diagnostic);
	if (Node.IsEmpty())
	{
		OutDiagnostic = Diagnostic;
		return false;
	}
	const FString Root = ResolveElkRoot(Diagnostic);
	if (Root.IsEmpty())
	{
		OutDiagnostic = Diagnostic;
		return false;
	}
	const FString Helper = ResolveHelper(Diagnostic);
	if (Helper.IsEmpty())
	{
		OutDiagnostic = Diagnostic;
		return false;
	}
	OutDiagnostic = FString::Printf(
		TEXT("BLUEPRINT_LENS_ELK_AVAILABLE:node=%s;root=%s;helper=%s"),
		*Node, *Root, *Helper);
	return true;
}

FString FBlueprintLensElkLayoutBackend::SerializeRequest(
	const FBlueprintLensLayoutRequest& Request,
	FString& OutDiagnostic)
{
	OutDiagnostic.Reset();
	if (!Request.IsValid())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_REQUEST_INVALID");
		return FString();
	}

	TSharedPtr<FJsonObject> Graph = MakeShared<FJsonObject>();
	Graph->SetStringField(TEXT("id"), TEXT("root"));
	const bool bVertical = Request.Profile != EBlueprintLensLayoutProfile::Linear &&
		Request.TargetWidth < 680.0f;
	Graph->SetObjectField(TEXT("layoutOptions"), MakeLayoutOptions({
		{TEXT("elk.algorithm"), TEXT("layered")},
		{TEXT("elk.direction"), bVertical ? TEXT("DOWN") : TEXT("RIGHT")},
		{TEXT("elk.hierarchyHandling"), TEXT("INCLUDE_CHILDREN")},
		{TEXT("elk.edgeRouting"), TEXT("ORTHOGONAL")},
		{TEXT("elk.spacing.nodeNode"), TEXT("24")},
		{TEXT("elk.layered.spacing.nodeNodeBetweenLayers"), TEXT("48")},
		{TEXT("elk.padding"), TEXT("[top=16,left=16,bottom=16,right=16]")}}));

	TArray<TSharedPtr<FJsonObject>> NodeObjects;
	TArray<TSharedPtr<FJsonObject>> GroupObjects;
	NodeObjects.SetNum(Request.Nodes.Num());
	GroupObjects.SetNum(Request.Groups.Num());
	TMap<FString, int32> NodeIndexes;
	TMap<FString, int32> GroupIndexes;
	TMap<FString, int32> UnitGroupIndexes;
	for (int32 Index = 0; Index < Request.Nodes.Num(); ++Index)
	{
		const FBlueprintLensLayoutNodeRequest& Node = Request.Nodes[Index];
		NodeIndexes.Add(Node.UnitId, Index);
		TSharedPtr<FJsonObject> NodeObject = MakeShared<FJsonObject>();
		NodeObject->SetStringField(TEXT("id"), NodeId(Index));
		NodeObject->SetNumberField(TEXT("width"), Node.DesiredSize.X);
		NodeObject->SetNumberField(TEXT("height"), Node.DesiredSize.Y);
		NodeObject->SetObjectField(TEXT("layoutOptions"), MakeLayoutOptions({
			{TEXT("org.eclipse.elk.portConstraints"), TEXT("FIXED_ORDER")}}));
		TArray<TSharedPtr<FJsonValue>> Ports;
		for (int32 PortIndex = 0; PortIndex < Node.Ports.Num(); ++PortIndex)
		{
			const FBlueprintLensLayoutPortRequest& Port = Node.Ports[PortIndex];
			TSharedPtr<FJsonObject> PortObject = MakeShared<FJsonObject>();
			PortObject->SetStringField(TEXT("id"), PortId(Index, PortIndex));
			PortObject->SetNumberField(TEXT("width"), PortSize);
			PortObject->SetNumberField(TEXT("height"), PortSize);
			const TCHAR* PortSide = Port.bInput
				? (bVertical ? TEXT("NORTH") : TEXT("WEST"))
				: (bVertical ? TEXT("SOUTH") : TEXT("EAST"));
			PortObject->SetObjectField(TEXT("layoutOptions"), MakeLayoutOptions({
				{TEXT("org.eclipse.elk.port.side"), PortSide},
				{TEXT("org.eclipse.elk.port.index"), FString::FromInt(Port.Order)}}));
			Ports.Add(MakeShared<FJsonValueObject>(PortObject));
		}
		NodeObject->SetArrayField(TEXT("ports"), MoveTemp(Ports));
		NodeObjects[Index] = MoveTemp(NodeObject);
	}

	for (int32 Index = 0; Index < Request.Groups.Num(); ++Index)
	{
		const FBlueprintLensLayoutGroupRequest& Group = Request.Groups[Index];
		GroupIndexes.Add(Group.GroupId, Index);
		TSharedPtr<FJsonObject> GroupObject = MakeShared<FJsonObject>();
		GroupObject->SetStringField(TEXT("id"), GroupId(Index));
		GroupObject->SetObjectField(TEXT("layoutOptions"), MakeLayoutOptions({
			{TEXT("elk.algorithm"), TEXT("layered")},
			{TEXT("elk.direction"), bVertical ? TEXT("DOWN") : TEXT("RIGHT")},
			{TEXT("elk.padding"), TEXT("[top=18,left=18,bottom=18,right=18]")}}));
		GroupObjects[Index] = MoveTemp(GroupObject);
	}
	for (int32 Index = 0; Index < Request.Groups.Num(); ++Index)
	{
		TSet<int32> Ancestors;
		int32 Current = Index;
		while (Request.Groups.IsValidIndex(Current))
		{
			if (Ancestors.Contains(Current))
			{
				OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_GROUP_PARENT_CYCLE");
				return FString();
			}
			Ancestors.Add(Current);
			const FString& ParentId = Request.Groups[Current].ParentGroupId;
			const int32* Parent = ParentId.IsEmpty()
				? nullptr
				: GroupIndexes.Find(ParentId);
			Current = Parent == nullptr ? INDEX_NONE : *Parent;
		}
	}

	for (int32 Index = 0; Index < Request.Groups.Num(); ++Index)
	{
		const FBlueprintLensLayoutGroupRequest& Group = Request.Groups[Index];
		for (const FString& UnitId : Group.MemberUnitIds)
		{
			const int32* NodeIndex = NodeIndexes.Find(UnitId);
			if (NodeIndex == nullptr || UnitGroupIndexes.Contains(UnitId))
			{
				OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_GROUP_MEMBER_DUPLICATE");
				return FString();
			}
			UnitGroupIndexes.Add(UnitId, Index);
			AddChild(GroupObjects[Index], NodeObjects[*NodeIndex]);
		}
	}
	for (int32 Index = 0; Index < Request.Groups.Num(); ++Index)
	{
		const FString& ParentGroupId = Request.Groups[Index].ParentGroupId;
		if (ParentGroupId.IsEmpty())
		{
			AddChild(Graph, GroupObjects[Index]);
			continue;
		}
		const int32* ParentIndex = GroupIndexes.Find(ParentGroupId);
		if (ParentIndex == nullptr || *ParentIndex == Index)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_GROUP_PARENT_INVALID");
			return FString();
		}
		AddChild(GroupObjects[*ParentIndex], GroupObjects[Index]);
	}
	for (int32 Index = 0; Index < Request.Nodes.Num(); ++Index)
	{
		if (!UnitGroupIndexes.Contains(Request.Nodes[Index].UnitId))
		{
			AddChild(Graph, NodeObjects[Index]);
		}
	}
	const auto CommonGroupIndex =
		[&Request, &GroupIndexes, &UnitGroupIndexes](
			const FString& SourceUnitId,
			const FString& TargetUnitId) -> int32
		{
			const int32* SourceDirect = UnitGroupIndexes.Find(SourceUnitId);
			const int32* TargetDirect = UnitGroupIndexes.Find(TargetUnitId);
			if (SourceDirect == nullptr || TargetDirect == nullptr)
			{
				return INDEX_NONE;
			}
			TSet<int32> TargetAncestors;
			int32 Current = *TargetDirect;
			while (Request.Groups.IsValidIndex(Current) &&
				!TargetAncestors.Contains(Current))
			{
				TargetAncestors.Add(Current);
				const FString& ParentId = Request.Groups[Current].ParentGroupId;
				const int32* Parent = ParentId.IsEmpty()
					? nullptr
					: GroupIndexes.Find(ParentId);
				Current = Parent == nullptr ? INDEX_NONE : *Parent;
			}
		TSet<int32> SourceAncestors;
		Current = *SourceDirect;
		while (Request.Groups.IsValidIndex(Current) &&
			!SourceAncestors.Contains(Current))
		{
			if (TargetAncestors.Contains(Current))
			{
				return Current;
			}
			SourceAncestors.Add(Current);
			const FString& ParentId = Request.Groups[Current].ParentGroupId;
			const int32* Parent = ParentId.IsEmpty()
				? nullptr
				: GroupIndexes.Find(ParentId);
			Current = Parent == nullptr ? INDEX_NONE : *Parent;
		}
		return INDEX_NONE;
	};

	for (int32 Index = 0; Index < Request.Edges.Num(); ++Index)
	{
		const FBlueprintLensLayoutEdgeRequest& Edge = Request.Edges[Index];
		const int32* SourceNodeIndex = NodeIndexes.Find(Edge.SourceUnitId);
		const int32* TargetNodeIndex = NodeIndexes.Find(Edge.TargetUnitId);
		if (SourceNodeIndex == nullptr || TargetNodeIndex == nullptr)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_EDGE_NODE_MISSING");
			return FString();
		}
		const FBlueprintLensLayoutNodeRequest& SourceNode = Request.Nodes[*SourceNodeIndex];
		const FBlueprintLensLayoutNodeRequest& TargetNode = Request.Nodes[*TargetNodeIndex];
		const int32 SourcePortIndex = SourceNode.Ports.IndexOfByPredicate(
			[&Edge](const FBlueprintLensLayoutPortRequest& Port)
			{
				return !Port.bInput && Port.Label == Edge.SourcePortLabel;
			});
		const int32 TargetPortIndex = TargetNode.Ports.IndexOfByPredicate(
			[&Edge](const FBlueprintLensLayoutPortRequest& Port)
			{
				return Port.bInput && Port.Label == Edge.TargetPortLabel;
			});
		if (SourcePortIndex == INDEX_NONE || TargetPortIndex == INDEX_NONE)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_EDGE_PORT_MISSING");
			return FString();
		}
		TSharedPtr<FJsonObject> EdgeObject = MakeShared<FJsonObject>();
		EdgeObject->SetStringField(TEXT("id"), EdgeId(Index));
		TArray<TSharedPtr<FJsonValue>> Sources;
		Sources.Add(MakeShared<FJsonValueString>(PortId(*SourceNodeIndex, SourcePortIndex)));
		TArray<TSharedPtr<FJsonValue>> Targets;
		Targets.Add(MakeShared<FJsonValueString>(PortId(*TargetNodeIndex, TargetPortIndex)));
		EdgeObject->SetArrayField(TEXT("sources"), MoveTemp(Sources));
		EdgeObject->SetArrayField(TEXT("targets"), MoveTemp(Targets));
		const int32 ContainerGroupIndex = CommonGroupIndex(
			Edge.SourceUnitId, Edge.TargetUnitId);
		if (ContainerGroupIndex == INDEX_NONE)
		{
			AddEdge(Graph, EdgeObject);
		}
		else
		{
			AddEdge(GroupObjects[ContainerGroupIndex], EdgeObject);
		}
	}

	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetStringField(TEXT("schema_version"), RequestSchema);
	Envelope->SetObjectField(TEXT("graph"), Graph);
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	if (!FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer))
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_REQUEST_SERIALIZE_FAILED");
		return FString();
	}
	return Output;
}

bool FBlueprintLensElkLayoutBackend::NormalizeResponse(
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
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_REQUEST_INVALID");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_JSON_MALFORMED");
		return false;
	}
	FString Schema;
	FString Backend;
	if (!TryGetString(Root, TEXT("schema_version"), Schema) ||
		Schema != ResponseSchema || !TryGetString(Root, TEXT("backend"), Backend) ||
		Backend != HelperBackend)
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_RESPONSE_SCHEMA_INVALID");
		return false;
	}
	FString VersionDiagnostic;
	const FString EffectiveVersion = ResponseBackendVersion(
		Root, BackendVersion, VersionDiagnostic);
	if (EffectiveVersion.IsEmpty())
	{
		OutDiagnostic = VersionDiagnostic;
		return false;
	}
	const TSharedPtr<FJsonObject>* Graph = nullptr;
	if (!Root->TryGetObjectField(TEXT("graph"), Graph) ||
		Graph == nullptr || !Graph->IsValid())
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_GRAPH_MISSING");
		return false;
	}

	FElkWalkState State{Request};
	if (!WalkContainer(*Graph, FVector2D::ZeroVector, State))
	{
		OutDiagnostic = State.Diagnostic.IsEmpty()
			? TEXT("BLUEPRINT_LENS_ELK_WALK_FAILED")
			: State.Diagnostic;
		return false;
	}
	if (State.Nodes.Num() != Request.Nodes.Num() ||
		State.Edges.Num() != Request.Edges.Num() ||
		State.GroupIndexes.Num() != Request.Groups.Num())
	{
		OutDiagnostic = FString::Printf(
			TEXT("BLUEPRINT_LENS_ELK_COVERAGE_MISMATCH:nodes=%d/%d;ports=%d;edges=%d/%d;groups=%d/%d"),
			State.Nodes.Num(), Request.Nodes.Num(), State.Ports.Num(),
			State.Edges.Num(), Request.Edges.Num(), State.GroupIndexes.Num(),
			Request.Groups.Num());
		return false;
	}

	int32 RequestedPortCount = 0;
	for (const FBlueprintLensLayoutNodeRequest& Node : Request.Nodes)
	{
		RequestedPortCount += Node.Ports.Num();
	}
	if (State.Ports.Num() != RequestedPortCount)
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_PORT_COVERAGE_MISMATCH");
		return false;
	}

	OutLedger.Backend = EBlueprintLensLayoutBackendKind::ElkLayered;
	OutLedger.BackendVersion = EffectiveVersion;
	OutLedger.ConfigurationFingerprint = ConfigurationFingerprint;
	OutLedger.DiagnosticCode = TEXT("BLUEPRINT_LENS_LAYOUT_COMPLETE");
	const float TranslateX = LayoutPadding - State.MinPoint.X;
	const float TranslateY = LayoutPadding - State.MinPoint.Y;
	float MaxX = LayoutPadding;
	float MaxY = LayoutPadding;
	for (int32 NodeIndex = 0; NodeIndex < Request.Nodes.Num(); ++NodeIndex)
	{
		const FBlueprintLensLayoutNodeRequest& RequestedNode = Request.Nodes[NodeIndex];
		const FString Id = NodeId(NodeIndex);
		const FElkRawNode* RawNode = State.Nodes.Find(Id);
		if (RawNode == nullptr)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_NODE_COVERAGE_MISMATCH");
			return false;
		}
		FBlueprintLensLayoutNodePlacement Placement;
		Placement.UnitId = RequestedNode.UnitId;
		Placement.Position = RawNode->Position + FVector2D(TranslateX, TranslateY);
		Placement.Size = RequestedNode.DesiredSize;
		MaxX = FMath::Max(MaxX, Placement.Position.X + Placement.Size.X);
		MaxY = FMath::Max(MaxY, Placement.Position.Y + Placement.Size.Y);
		OutLedger.Nodes.Add(MoveTemp(Placement));
	}
	for (int32 NodeIndex = 0; NodeIndex < Request.Nodes.Num(); ++NodeIndex)
	{
		const FBlueprintLensLayoutNodeRequest& RequestedNode = Request.Nodes[NodeIndex];
		for (int32 PortIndex = 0; PortIndex < RequestedNode.Ports.Num(); ++PortIndex)
		{
			const FElkRawPort* RawPort = State.Ports.Find(PortId(NodeIndex, PortIndex));
			if (RawPort == nullptr)
			{
				OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_PORT_COVERAGE_MISMATCH");
				return false;
			}
			FBlueprintLensLayoutPortPlacement Placement;
			Placement.UnitId = RequestedNode.UnitId;
			Placement.Label = RequestedNode.Ports[PortIndex].Label;
			Placement.bInput = RequestedNode.Ports[PortIndex].bInput;
			Placement.Position = RawPort->Position + FVector2D(TranslateX, TranslateY);
			MaxX = FMath::Max(MaxX, Placement.Position.X);
			MaxY = FMath::Max(MaxY, Placement.Position.Y);
			OutLedger.Ports.Add(MoveTemp(Placement));
		}
	}
	for (int32 EdgeIndex = 0; EdgeIndex < Request.Edges.Num(); ++EdgeIndex)
	{
		const FElkRawEdge* RawEdge = State.Edges.Find(EdgeId(EdgeIndex));
		if (RawEdge == nullptr)
		{
			OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_EDGE_COVERAGE_MISMATCH");
			return false;
		}
		const FBlueprintLensLayoutEdgeRequest& RequestedEdge = Request.Edges[EdgeIndex];
		FBlueprintLensLayoutEdgePlacement Placement;
		Placement.RelationId = RequestedEdge.RelationId;
		Placement.SourceUnitId = RequestedEdge.SourceUnitId;
		Placement.TargetUnitId = RequestedEdge.TargetUnitId;
		Placement.SourcePortLabel = RequestedEdge.SourcePortLabel;
		Placement.TargetPortLabel = RequestedEdge.TargetPortLabel;
		Placement.Family = RequestedEdge.Family;
		for (const FVector2D& Point : RawEdge->BendPoints)
		{
			const FVector2D Normalized = Point + FVector2D(TranslateX, TranslateY);
			Placement.BendPoints.Add(Normalized);
			MaxX = FMath::Max(MaxX, Normalized.X);
			MaxY = FMath::Max(MaxY, Normalized.Y);
		}
		OutLedger.Edges.Add(MoveTemp(Placement));
	}
	OutLedger.CanvasSize = FVector2D(
		FMath::Max(MaxX + LayoutPadding, LayoutPadding * 2.0f),
		FMath::Max(MaxY + LayoutPadding, LayoutPadding * 2.0f));
	if (!OutLedger.IsCompleteFor(Request))
	{
		OutDiagnostic = TEXT("BLUEPRINT_LENS_ELK_NORMALIZED_LEDGER_INVALID");
		OutLedger.DiagnosticCode = OutDiagnostic;
		return false;
	}
	return true;
}

FBlueprintLensLayoutLedger FBlueprintLensElkLayoutBackend::Layout(
	const FBlueprintLensLayoutRequest& Request) const
{
	FBlueprintLensLayoutLedger Ledger;
	FString Diagnostic;
	if (!Request.IsValid())
	{
		Ledger.DiagnosticCode = TEXT("BLUEPRINT_LENS_ELK_REQUEST_INVALID");
		return Ledger;
	}
	const FString Node = ResolveNodeExecutable(Diagnostic);
	if (Node.IsEmpty())
	{
		Ledger.DiagnosticCode = Diagnostic;
		return Ledger;
	}
	const FString Root = ResolveElkRoot(Diagnostic);
	if (Root.IsEmpty())
	{
		Ledger.DiagnosticCode = Diagnostic;
		return Ledger;
	}
	const FString Helper = ResolveHelper(Diagnostic);
	if (Helper.IsEmpty())
	{
		Ledger.DiagnosticCode = Diagnostic;
		return Ledger;
	}
	FString RequestDiagnostic;
	const FString RequestJson = SerializeRequest(Request, RequestDiagnostic);
	if (RequestJson.IsEmpty())
	{
		Ledger.DiagnosticCode = RequestDiagnostic;
		return Ledger;
	}
	FBlueprintLensExternalLayoutProcessOptions ProcessOptions;
	ProcessOptions.ExecutablePath = Node;
	ProcessOptions.Arguments = FString::Printf(
		TEXT("%s --elk-root %s"),
		*QuoteProcessArgument(Helper), *QuoteProcessArgument(Root));
	ProcessOptions.StandardInput = RequestJson;
	ProcessOptions.TimeoutSeconds = Options.TimeoutSeconds;
	const FBlueprintLensExternalLayoutProcessResult Result =
		FBlueprintLensExternalLayoutProcess::Run(ProcessOptions);
	if (!Result.IsSuccess())
	{
		Ledger.DiagnosticCode = Result.DiagnosticCode;
		return Ledger;
	}
	const FString Fingerprint = FString::Printf(
		TEXT("elk-layered;schema=%s;direction=%s;target-width=%.2f;units=points;y-origin=top;ports=fixed-order"),
		RequestSchema,
		Request.Profile != EBlueprintLensLayoutProfile::Linear && Request.TargetWidth < 680.0f
			? TEXT("DOWN")
			: TEXT("RIGHT"),
		Request.TargetWidth);
	if (!NormalizeResponse(
		Result.StandardOutput,
		Request,
		Options.BackendVersionOverride,
		Fingerprint,
		Ledger,
		Diagnostic))
	{
		Ledger.DiagnosticCode = Diagnostic;
	}
	return Ledger;
}
