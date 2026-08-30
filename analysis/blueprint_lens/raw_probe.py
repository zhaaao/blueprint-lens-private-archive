"""Load and reconstruct Blueprint Lens raw-probe and frozen v1 documents.

This module owns graph-local typed reconstruction and invariants. It deliberately
does not resolve call targets or access UE/Asset Registry state; those concerns
belong to the future interprocedural provider/resolver seam.
"""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass, replace
import json
from pathlib import Path
from typing import Any, Iterable, Mapping


class ReconstructionError(ValueError):
    """Raised when an exported document violates a reconstruction invariant."""


@dataclass(frozen=True, slots=True)
class Pin:
    id: str
    node_id: str
    persistent_guid: str
    identity_source: str
    name: str
    direction: str
    kind: str
    pin_role: str
    type: Mapping[str, Any]
    default: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class Node:
    id: str
    graph_id: str
    native_guid: str
    identity_source: str
    class_path: str
    title: str
    semantic_status: str
    semantic_reason: str
    symbol: Mapping[str, Any] | None
    pins: tuple[Pin, ...]


@dataclass(frozen=True, slots=True)
class Edge:
    id: str
    graph_id: str
    kind: str
    source_node_id: str
    source_pin_id: str
    target_node_id: str
    target_pin_id: str


@dataclass(frozen=True, slots=True)
class Graph:
    id: str
    name: str
    kind: str
    class_path: str
    nodes: tuple[Node, ...]
    edges: tuple[Edge, ...]

    def edges_of_kind(self, kind: str) -> tuple[Edge, ...]:
        return tuple(edge for edge in self.edges if edge.kind == kind)

    def adjacency(self, kind: str) -> Mapping[str, tuple[str, ...]]:
        successors: dict[str, set[str]] = defaultdict(set)
        for edge in self.edges_of_kind(kind):
            successors[edge.source_node_id].add(edge.target_node_id)
        return {
            node_id: tuple(sorted(targets))
            for node_id, targets in sorted(successors.items())
        }


@dataclass(frozen=True, slots=True)
class BlueprintDocument:
    format: str
    format_version: str
    schema_status: str
    engine_version: str
    blueprint_id: str
    blueprint_name: str
    blueprint_path: str
    parent_class: str
    graphs: tuple[Graph, ...]

    @property
    def nodes(self) -> tuple[Node, ...]:
        return tuple(node for graph in self.graphs for node in graph.nodes)

    @property
    def pins(self) -> tuple[Pin, ...]:
        return tuple(pin for node in self.nodes for pin in node.pins)

    @property
    def edges(self) -> tuple[Edge, ...]:
        return tuple(edge for graph in self.graphs for edge in graph.edges)


def _mapping(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ReconstructionError(f"{context} must be an object")
    return value


def _list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise ReconstructionError(f"{context} must be an array")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str):
        raise ReconstructionError(f"{context} must be a string")
    return value


def _required(obj: Mapping[str, Any], field: str, context: str) -> Any:
    if field not in obj:
        raise ReconstructionError(f"{context}.{field} is required")
    return obj[field]


def _unique_add(seen: set[str], entity_id: str, context: str) -> None:
    if entity_id in seen:
        raise ReconstructionError(f"duplicate identity at {context}: {entity_id}")
    seen.add(entity_id)


def _parse_pin(raw: Any, node_id: str, context: str) -> Pin:
    obj = _mapping(raw, context)
    pin_id = _string(_required(obj, "id", context), f"{context}.id")
    exported_node_id = _string(
        _required(obj, "node_id", context), f"{context}.node_id"
    )
    if exported_node_id != node_id:
        raise ReconstructionError(
            f"{context}.node_id does not match containing node: "
            f"{exported_node_id} != {node_id}"
        )

    direction = _string(
        _required(obj, "direction", context), f"{context}.direction"
    )
    if direction not in {"input", "output"}:
        raise ReconstructionError(f"{context}.direction is invalid: {direction}")

    kind = _string(_required(obj, "kind", context), f"{context}.kind")
    if kind not in {"execution", "data"}:
        raise ReconstructionError(f"{context}.kind is invalid: {kind}")

    return Pin(
        id=pin_id,
        node_id=node_id,
        persistent_guid=_string(
            obj.get("persistent_guid", ""), f"{context}.persistent_guid"
        ),
        identity_source=_string(
            _required(obj, "identity_source", context),
            f"{context}.identity_source",
        ),
        name=_string(_required(obj, "name", context), f"{context}.name"),
        direction=direction,
        kind=kind,
        pin_role=_string(obj.get("pin_role", "none"), f"{context}.pin_role"),
        type=_mapping(_required(obj, "type", context), f"{context}.type"),
        default=_mapping(_required(obj, "default", context), f"{context}.default"),
    )


def _legacy_pin_role(
    class_path: str,
    symbol: Mapping[str, Any] | None,
    pin: Pin,
) -> str:
    short_class = class_path.rsplit(".", 1)[-1]
    if (
        short_class == "K2Node_VariableSet"
        and pin.kind == "data"
        and pin.direction == "input"
        and symbol is not None
        and pin.name == symbol.get("name")
    ):
        return "variable_set_value"
    if (
        short_class == "K2Node_IfThenElse"
        and pin.kind == "data"
        and pin.direction == "input"
        and pin.name == "Condition"
    ):
        return "branch_condition"
    return "none"


def _parse_node(raw: Any, graph_id: str, index: int) -> Node:
    context = f"graph[{graph_id}].nodes[{index}]"
    obj = _mapping(raw, context)
    node_id = _string(_required(obj, "id", context), f"{context}.id")
    native_guid = _string(
        _required(obj, "native_guid", context), f"{context}.native_guid"
    )
    class_path = _string(_required(obj, "class", context), f"{context}.class")
    symbol = obj.get("symbol")
    if symbol is not None:
        symbol = _mapping(symbol, f"{context}.symbol")
    pins = tuple(
        _parse_pin(pin, node_id, f"{context}.pins[{pin_index}]")
        for pin_index, pin in enumerate(
            _list(_required(obj, "pins", context), f"{context}.pins")
        )
    )
    pins = tuple(
        replace(pin, pin_role=_legacy_pin_role(class_path, symbol, pin))
        if "pin_role" not in _mapping(raw_pin, f"{context}.pins[{pin_index}]")
        else pin
        for pin_index, (raw_pin, pin) in enumerate(
            zip(_list(_required(obj, "pins", context), f"{context}.pins"), pins)
        )
    )

    return Node(
        id=node_id,
        graph_id=graph_id,
        native_guid=native_guid,
        identity_source=_string(
            obj.get(
                "identity_source",
                "node_guid" if native_guid else "fallback",
            ),
            f"{context}.identity_source",
        ),
        class_path=class_path,
        title=_string(_required(obj, "title", context), f"{context}.title"),
        semantic_status=_string(
            _required(obj, "semantic_status", context),
            f"{context}.semantic_status",
        ),
        semantic_reason=_string(
            _required(obj, "semantic_reason", context),
            f"{context}.semantic_reason",
        ),
        symbol=symbol,
        pins=pins,
    )


def _parse_edge(raw: Any, graph_id: str, index: int) -> Edge:
    context = f"graph[{graph_id}].edges[{index}]"
    obj = _mapping(raw, context)
    kind = _string(_required(obj, "kind", context), f"{context}.kind")
    if kind not in {"execution", "data"}:
        raise ReconstructionError(f"{context}.kind is invalid: {kind}")
    if obj.get("direction_is_valid") is not True:
        raise ReconstructionError(f"{context} has an invalid exported direction")
    return Edge(
        id=_string(_required(obj, "id", context), f"{context}.id"),
        graph_id=graph_id,
        kind=kind,
        source_node_id=_string(
            _required(obj, "source_node_id", context),
            f"{context}.source_node_id",
        ),
        source_pin_id=_string(
            _required(obj, "source_pin_id", context),
            f"{context}.source_pin_id",
        ),
        target_node_id=_string(
            _required(obj, "target_node_id", context),
            f"{context}.target_node_id",
        ),
        target_pin_id=_string(
            _required(obj, "target_pin_id", context),
            f"{context}.target_pin_id",
        ),
    )


def _parse_graph(raw: Any, index: int) -> Graph:
    context = f"blueprint.graphs[{index}]"
    obj = _mapping(raw, context)
    graph_id = _string(_required(obj, "id", context), f"{context}.id")
    nodes = tuple(
        _parse_node(node, graph_id, node_index)
        for node_index, node in enumerate(
            _list(_required(obj, "nodes", context), f"{context}.nodes")
        )
    )
    edges = tuple(
        _parse_edge(edge, graph_id, edge_index)
        for edge_index, edge in enumerate(
            _list(_required(obj, "edges", context), f"{context}.edges")
        )
    )
    return Graph(
        id=graph_id,
        name=_string(_required(obj, "name", context), f"{context}.name"),
        kind=_string(_required(obj, "kind", context), f"{context}.kind"),
        class_path=_string(_required(obj, "class", context), f"{context}.class"),
        nodes=nodes,
        edges=edges,
    )


def _validate_references(document: BlueprintDocument) -> None:
    seen_ids: set[str] = set()
    graph_ids: set[str] = set()
    node_by_id: dict[str, Node] = {}
    pin_by_id: dict[str, Pin] = {}

    for graph in document.graphs:
        _unique_add(seen_ids, graph.id, f"graph {graph.name}")
        graph_ids.add(graph.id)
        for node in graph.nodes:
            _unique_add(seen_ids, node.id, f"node {node.title}")
            node_by_id[node.id] = node
            for pin in node.pins:
                _unique_add(seen_ids, pin.id, f"pin {node.title}.{pin.name}")
                pin_by_id[pin.id] = pin
        for edge in graph.edges:
            _unique_add(seen_ids, edge.id, f"edge in {graph.name}")

    for graph in document.graphs:
        for edge in graph.edges:
            source_node = node_by_id.get(edge.source_node_id)
            target_node = node_by_id.get(edge.target_node_id)
            source_pin = pin_by_id.get(edge.source_pin_id)
            target_pin = pin_by_id.get(edge.target_pin_id)
            if any(
                value is None
                for value in (source_node, target_node, source_pin, target_pin)
            ):
                raise ReconstructionError(f"edge has a dangling reference: {edge.id}")
            assert source_node is not None
            assert target_node is not None
            assert source_pin is not None
            assert target_pin is not None
            if source_node.graph_id != graph.id or target_node.graph_id != graph.id:
                raise ReconstructionError(f"edge crosses graph storage: {edge.id}")
            if source_pin.node_id != source_node.id:
                raise ReconstructionError(f"edge source pin/node mismatch: {edge.id}")
            if target_pin.node_id != target_node.id:
                raise ReconstructionError(f"edge target pin/node mismatch: {edge.id}")
            if source_pin.direction != "output" or target_pin.direction != "input":
                raise ReconstructionError(f"edge direction mismatch: {edge.id}")
            if source_pin.kind != edge.kind or target_pin.kind != edge.kind:
                raise ReconstructionError(f"edge kind disagrees with its pins: {edge.id}")


def _validate_counts(raw_counts: Any, document: BlueprintDocument) -> None:
    counts = _mapping(raw_counts, "counts")
    expected = {
        "graphs": len(document.graphs),
        "nodes": len(document.nodes),
        "pins": len(document.pins),
        "edges": len(document.edges),
        "unsupported_nodes": sum(
            node.semantic_status == "unsupported" for node in document.nodes
        ),
    }
    for field, actual in expected.items():
        exported = counts.get(field)
        if exported != actual:
            raise ReconstructionError(
                f"counts.{field} mismatch: exported {exported}, reconstructed {actual}"
            )


def _validate_identity_and_semantics(document: BlueprintDocument) -> None:
    member_names: dict[str, str] = {}
    allowed_roles = {"none", "variable_set_value", "branch_condition"}
    for node in document.nodes:
        if node.identity_source == "node_guid":
            if not node.native_guid:
                raise ReconstructionError(
                    f"node_guid identity requires native_guid: {node.id}"
                )
            expected = f"{node.graph_id}::node::{node.native_guid}"
            if node.id != expected:
                raise ReconstructionError(
                    f"node identity does not match native_guid: {node.id}"
                )
        elif node.identity_source == "fallback":
            if node.native_guid or "::node::object-" not in node.id:
                raise ReconstructionError(
                    f"fallback node identity is incoherent: {node.id}"
                )
        else:
            raise ReconstructionError(
                f"invalid node identity_source: {node.identity_source}"
            )

        if (
            node.symbol
            and node.symbol.get("kind") == "variable"
            and not node.symbol.get("is_local_scope", False)
        ):
            guid = str(node.symbol.get("guid") or "")
            name = str(node.symbol.get("name") or "")
            if guid:
                previous = member_names.setdefault(guid, name)
                if previous != name:
                    raise ReconstructionError(
                        f"member GUID resolves to conflicting names: "
                        f"{guid}: {previous!r} != {name!r}"
                    )

        special_roles = Counter(pin.pin_role for pin in node.pins if pin.pin_role != "none")
        for pin in node.pins:
            if pin.pin_role not in allowed_roles:
                raise ReconstructionError(
                    f"invalid pin_role {pin.pin_role!r}: {pin.id}"
                )
            if pin.identity_source == "persistent_guid":
                if not pin.persistent_guid:
                    raise ReconstructionError(
                        f"persistent_guid identity requires a GUID: {pin.id}"
                    )
                expected = f"{node.id}::pin::persistent-{pin.persistent_guid}"
                if pin.id != expected:
                    raise ReconstructionError(
                        f"pin identity does not match persistent_guid: {pin.id}"
                    )
            elif pin.identity_source == "pin_locator":
                if pin.persistent_guid:
                    raise ReconstructionError(
                        f"pin_locator must not claim a persistent GUID: {pin.id}"
                    )
                if f"{node.id}::pin::locator-" not in pin.id:
                    raise ReconstructionError(
                        f"pin locator identity is incoherent: {pin.id}"
                    )
            else:
                raise ReconstructionError(
                    f"invalid pin identity_source: {pin.identity_source}"
                )

            container = pin.type.get("container")
            has_map_value = "map_value_type" in pin.type
            if (container == "map") != has_map_value:
                raise ReconstructionError(
                    f"map pin type/map_value_type mismatch: {pin.id}"
                )
            if pin.pin_role != "none" and (
                pin.kind != "data" or pin.direction != "input"
            ):
                raise ReconstructionError(
                    f"special pin_role requires a data input pin: {pin.id}"
                )

        short_class = node.class_path.rsplit(".", 1)[-1]
        expected_role = {
            "K2Node_VariableSet": "variable_set_value",
            "K2Node_IfThenElse": "branch_condition",
        }.get(short_class)
        if expected_role is not None and special_roles[expected_role] != 1:
            raise ReconstructionError(
                f"{short_class} requires exactly one {expected_role} pin: {node.id}"
            )
        if expected_role != "variable_set_value" and special_roles["variable_set_value"]:
            raise ReconstructionError(
                f"variable_set_value role on wrong node family: {node.id}"
            )
        if expected_role != "branch_condition" and special_roles["branch_condition"]:
            raise ReconstructionError(
                f"branch_condition role on wrong node family: {node.id}"
            )


def load_raw_probe(path: str | Path) -> BlueprintDocument:
    """Load, validate, and reconstruct an M1 raw-probe JSON document."""

    source = Path(path)
    try:
        raw = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReconstructionError(f"cannot read {source}: {error}") from error

    root = _mapping(raw, "root")
    format_name = _string(_required(root, "format", "root"), "root.format")
    format_version = _string(
        _required(root, "format_version", "root"), "root.format_version"
    )
    schema_status = _string(
        _required(root, "schema_status", "root"), "root.schema_status"
    )
    if (
        format_name != "blueprint-lens-raw-probe"
        or format_version not in {"0.1", "0.2"}
        or schema_status != "unfrozen"
    ):
        raise ReconstructionError(
            "unsupported raw-probe format tuple: "
            f"{format_name!r}, {format_version!r}, {schema_status!r}"
        )

    blueprint = _mapping(_required(root, "blueprint", "root"), "blueprint")
    graphs = tuple(
        _parse_graph(graph, index)
        for index, graph in enumerate(
            _list(_required(blueprint, "graphs", "blueprint"), "blueprint.graphs")
        )
    )
    document = BlueprintDocument(
        format=format_name,
        format_version=format_version,
        schema_status=schema_status,
        engine_version=_string(
            _required(root, "engine_version", "root"), "root.engine_version"
        ),
        blueprint_id=_string(
            _required(blueprint, "id", "blueprint"), "blueprint.id"
        ),
        blueprint_name=_string(
            _required(blueprint, "name", "blueprint"), "blueprint.name"
        ),
        blueprint_path=_string(
            _required(blueprint, "path", "blueprint"), "blueprint.path"
        ),
        parent_class=_string(
            _required(blueprint, "parent_class", "blueprint"),
            "blueprint.parent_class",
        ),
        graphs=graphs,
    )
    _validate_references(document)
    _validate_counts(_required(root, "counts", "root"), document)
    _validate_identity_and_semantics(document)
    return document


def reconstruct_blueprint_lens_v1(value: Mapping[str, Any]) -> BlueprintDocument:
    """Reconstruct and validate one already-decoded frozen v1 document."""

    root = _mapping(value, "root")
    format_name = _string(_required(root, "format", "root"), "root.format")
    schema_version = _string(
        _required(root, "schema_version", "root"), "root.schema_version"
    )
    if (format_name, schema_version) != ("blueprint-lens", "1.0.0"):
        raise ReconstructionError(
            f"unsupported Blueprint Lens schema tuple: {format_name!r}, {schema_version!r}"
        )

    blueprint = _mapping(_required(root, "blueprint", "root"), "blueprint")
    graphs = tuple(
        _parse_graph(graph, index)
        for index, graph in enumerate(
            _list(_required(blueprint, "graphs", "blueprint"), "blueprint.graphs")
        )
    )
    document = BlueprintDocument(
        format=format_name,
        format_version=schema_version,
        schema_status="frozen",
        engine_version=_string(
            _required(root, "engine_version", "root"), "root.engine_version"
        ),
        blueprint_id=_string(
            _required(blueprint, "id", "blueprint"), "blueprint.id"
        ),
        blueprint_name=_string(
            _required(blueprint, "name", "blueprint"), "blueprint.name"
        ),
        blueprint_path=_string(
            _required(blueprint, "path", "blueprint"), "blueprint.path"
        ),
        parent_class=_string(
            _required(blueprint, "parent_class", "blueprint"),
            "blueprint.parent_class",
        ),
        graphs=graphs,
    )
    _validate_references(document)
    counts = _mapping(_required(root, "counts", "root"), "counts")
    _validate_counts(counts, document)

    status_counts = Counter(node.semantic_status for node in document.nodes)
    allowed = {"supported", "opaque", "uncertain", "unsupported"}
    unexpected = set(status_counts) - allowed
    if unexpected:
        raise ReconstructionError(f"invalid v1 semantic statuses: {sorted(unexpected)}")
    for node in document.nodes:
        if node.semantic_status != "supported" and not node.semantic_reason:
            raise ReconstructionError(
                f"non-supported node requires semantic_reason: {node.id}"
            )
    for status in sorted(allowed):
        field = f"{status}_nodes"
        if counts.get(field) != status_counts[status]:
            raise ReconstructionError(
                f"counts.{field} mismatch: exported {counts.get(field)}, "
                f"reconstructed {status_counts[status]}"
            )
    _validate_identity_and_semantics(document)
    return document


def load_blueprint_lens_v1(path: str | Path) -> BlueprintDocument:
    """Load a schema-v1 document and enforce cross-reference/count invariants."""

    source = Path(path)
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ReconstructionError(f"cannot read {source}: {error}") from error
    return reconstruct_blueprint_lens_v1(_mapping(value, "root"))


def _short_class(class_path: str) -> str:
    return class_path.rsplit(".", 1)[-1]


def _node_label(node: Node) -> str:
    guid = node.native_guid[:8] if node.native_guid else "fallback"
    return f"{node.title} [{_short_class(node.class_path)}; {guid}]"


def _edge_lines(graph: Graph, edge_kind: str) -> Iterable[str]:
    nodes = {node.id: node for node in graph.nodes}
    pins = {pin.id: pin for node in graph.nodes for pin in node.pins}
    for edge in sorted(graph.edges_of_kind(edge_kind), key=lambda item: item.id):
        source = _node_label(nodes[edge.source_node_id])
        target = _node_label(nodes[edge.target_node_id])
        source_pin = pins[edge.source_pin_id].name
        target_pin = pins[edge.target_pin_id].name
        yield (
            f"- `{source}`.`{source_pin}` → "
            f"`{target}`.`{target_pin}`"
        )


def render_reconstruction_report(
    document: BlueprintDocument, source_path: str | Path
) -> str:
    """Render a deterministic, human-checkable Markdown reconstruction report."""

    edge_counts = Counter(edge.kind for edge in document.edges)
    class_counts = Counter(_short_class(node.class_path) for node in document.nodes)
    unsupported = sorted(
        (node for node in document.nodes if node.semantic_status == "unsupported"),
        key=lambda node: node.id,
    )

    lines = [
        "# BP_SlicingProbe Reconstruction Report",
        "",
        "## Input and validation",
        "",
        f"- Source: `{Path(source_path)}`",
        f"- Format: `{document.format}` `{document.format_version}` "
        f"(`{document.schema_status}`)",
        f"- Engine: `{document.engine_version}`",
        "- Identity/reference validation: **PASS**",
        "- Exported counts equal reconstructed counts: **PASS**",
        "",
        "## Reconstructed totals",
        "",
        "| Graphs | Nodes | Pins | Execution edges | Data edges |",
        "|---:|---:|---:|---:|---:|",
        f"| {len(document.graphs)} | {len(document.nodes)} | "
        f"{len(document.pins)} | {edge_counts['execution']} | "
        f"{edge_counts['data']} |",
        "",
        "## Graph inventory",
        "",
        "| Graph | Kind | Nodes | Execution edges | Data edges |",
        "|---|---|---:|---:|---:|",
    ]
    for graph in sorted(document.graphs, key=lambda item: item.id):
        lines.append(
            f"| `{graph.name}` | `{graph.kind}` | {len(graph.nodes)} | "
            f"{len(graph.edges_of_kind('execution'))} | "
            f"{len(graph.edges_of_kind('data'))} |"
        )

    lines.extend(["", "## Node-family inventory", ""])
    for class_name, count in sorted(class_counts.items()):
        lines.append(f"- `{class_name}`: {count}")

    for graph in sorted(document.graphs, key=lambda item: item.id):
        lines.extend(["", f"## Graph: `{graph.name}`", ""])
        for edge_kind in ("execution", "data"):
            lines.extend([f"### {edge_kind.title()} pin-level edges", ""])
            edge_lines = list(_edge_lines(graph, edge_kind))
            lines.extend(edge_lines or ["- None"])

    lines.extend(["", "## Unsupported nodes", ""])
    if unsupported:
        for node in unsupported:
            lines.append(
                f"- `{_node_label(node)}`: `{node.semantic_reason}`"
            )
    else:
        lines.append("- None")

    variable_groups: dict[str, list[Node]] = defaultdict(list)
    for node in document.nodes:
        if node.symbol and node.symbol.get("kind") == "variable":
            key = str(node.symbol.get("guid") or node.symbol.get("name"))
            variable_groups[key].append(node)
    lines.extend(["", "## Variable references", ""])
    if variable_groups:
        for variable_id, nodes in sorted(variable_groups.items()):
            names = sorted({str(node.symbol.get("name")) for node in nodes if node.symbol})
            accesses = Counter(
                str(node.symbol.get("access")) for node in nodes if node.symbol
            )
            lines.append(
                f"- `{variable_id}` ({', '.join(names)}): "
                + ", ".join(
                    f"{access}={count}" for access, count in sorted(accesses.items())
                )
            )
    else:
        lines.append("- None")

    lines.extend(
        [
            "",
            "## Scope note",
            "",
            "This is an M1 raw-probe reconstruction. It demonstrates lossless "
            "pin-level graph recovery for this controlled Blueprint, but it does "
            "not freeze Schema v1 or claim slicing semantics for unclassified nodes.",
            "",
        ]
    )
    return "\n".join(lines)
