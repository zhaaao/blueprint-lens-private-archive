"""Build and independently audit the real LC3 value-provenance artifacts."""

from __future__ import annotations

from collections import defaultdict
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .contract_validation import validate_contract_file
from .data_slice import compute_member_variable_data_slice
from .raw_probe import Edge, Graph, Node, Pin, load_blueprint_lens_v1, load_raw_probe
from .typed_ir import TypedIRBuildError, build_typed_ir


LC3_BLUEPRINT_PATH = "/Game/LensCorpus/BP_LC3_ValueProvenance.BP_LC3_ValueProvenance"
LC3_MEMBER_GUID = "175c3370-4752-21d1-da22-4e94033ccbe5"
LC3_MEMBER_NAME = "LC3Score"
LC3_EXPECTED_NODES = 7
LC3_EXPECTED_EDGES = 6
LC3_EXPECTED_EXECUTION_EDGES = 1
LC3_EXPECTED_DATA_EDGES = 5
LC3_VALUE_CONE_GROUP_ID = "group.value_cone.lc3-score"
LC3_VALUE_CONE_TITLE = "LC3Score value provenance"
LC3_QUESTION = "Where does the value assigned to LC3Score come from?"
LC3_FROZEN_LEDGER_FIELDS = (
    "criterion",
    "execution_controller",
    "producer_closure",
    "data_relations",
    "value_cone",
    "source_references",
    "status_coverage",
    "boundaries",
    "exclusions",
    "counts",
    "layout_readiness",
)
PIN_TYPE_FIELDS = (
    "category",
    "subcategory",
    "object_path",
    "container",
    "is_reference",
    "is_const",
    "is_weak_pointer",
    "is_uobject_wrapper",
    "serialize_as_single_precision_float",
)


class LC3ArtifactError(ValueError):
    """Raised when LC3 source evidence does not satisfy the frozen profile."""


class _Inventory:
    def __init__(
        self,
        blueprint_path: str,
        counts: tuple[int, int, int, int, int],
        graphs: frozenset[tuple[str, str]],
        nodes: frozenset[tuple[str, str, str]],
        pins: frozenset[tuple[Any, ...]],
        edges: frozenset[tuple[Any, ...]],
    ) -> None:
        self.blueprint_path = blueprint_path
        self.counts = counts
        self.graphs = graphs
        self.nodes = nodes
        self.pins = pins
        self.edges = edges


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest().upper()


def _sha256_file(path: Path) -> str:
    return _sha256_bytes(path.read_bytes())


def _write_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    _write_bytes(path, _canonical_json_bytes(value))


def _load_object(path: Path, label: str) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC3ArtifactError(f"cannot read {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise LC3ArtifactError(f"{label} must be an object")
    return value


def _one(values: Iterable[Any], description: str) -> Any:
    matches = list(values)
    if len(matches) != 1:
        raise LC3ArtifactError(
            f"{description} must resolve exactly once; found {len(matches)}"
        )
    return matches[0]


def _short_class(node: Node) -> str:
    return node.class_path.rsplit(".", 1)[-1]


def _symbol(node: Node, *, access: str | None = None) -> Mapping[str, Any]:
    symbol = node.symbol or {}
    if symbol.get("kind") != "variable":
        return {}
    if access is not None and symbol.get("access") != access:
        return {}
    return symbol


def _symbol_name(node: Node, *, access: str | None = None) -> str:
    return str(_symbol(node, access=access).get("name", ""))


def _pin(node: Node, *, name: str, direction: str, kind: str) -> Pin:
    return _one(
        (
            candidate
            for candidate in node.pins
            if candidate.name == name
            and candidate.direction == direction
            and candidate.kind == kind
        ),
        f"{name} {direction}/{kind} pin on {node.id}",
    )


def _pin_by_role(node: Node, role: str) -> Pin:
    return _one(
        (candidate for candidate in node.pins if candidate.pin_role == role),
        f"{role} pin on {node.id}",
    )


def _pin_type_fact(value: Any, context: str) -> tuple[Any, ...]:
    if not isinstance(value, Mapping):
        raise LC3ArtifactError(f"{context} type must be an object")
    missing = [field for field in PIN_TYPE_FIELDS if field not in value]
    if missing:
        raise LC3ArtifactError(f"{context} type is missing exact facts: {missing}")
    text_values = tuple(value[field] for field in PIN_TYPE_FIELDS[:4])
    if not all(isinstance(item, str) for item in text_values):
        raise LC3ArtifactError(f"{context} type text facts must be strings")
    bool_values = tuple(value[field] for field in PIN_TYPE_FIELDS[4:])
    if not all(type(item) is bool for item in bool_values):
        raise LC3ArtifactError(f"{context} type flags must be Booleans")
    return text_values + bool_values


def _pin_type_value(pin: Pin) -> dict[str, Any]:
    return dict(zip(PIN_TYPE_FIELDS, _pin_type_fact(pin.type, f"pin {pin.id}")))


def _parse_inventory_bool(value: str) -> bool:
    if value == "true":
        return True
    if value == "false":
        return False
    raise ValueError(f"invalid Boolean type flag {value!r}")


def _pin_occurrence(pin_id: str) -> int:
    try:
        return int(pin_id.rsplit("-", 1)[1])
    except (IndexError, ValueError) as error:
        raise LC3ArtifactError(
            f"pin locator lacks terminal occurrence: {pin_id}"
        ) from error


def _parse_inventory(path: Path) -> _Inventory:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise LC3ArtifactError(f"cannot read inventory {path}: {error}") from error
    blueprints: list[str] = []
    counts: list[tuple[int, int, int, int, int]] = []
    graphs: set[tuple[str, str]] = set()
    nodes: set[tuple[str, str, str]] = set()
    pins: set[tuple[Any, ...]] = set()
    edges: set[tuple[Any, ...]] = set()
    for line_number, line in enumerate(lines, start=1):
        fields = line.split("\t")
        record = fields[0] if fields else ""
        try:
            if record == "BLUEPRINT" and len(fields) == 2:
                blueprints.append(fields[1])
            elif record == "GRAPH" and len(fields) == 3:
                graphs.add((fields[1], fields[2]))
            elif record == "NODE" and len(fields) == 4:
                nodes.add((fields[1], fields[2], fields[3]))
            elif record == "PIN":
                if len(fields) != 17:
                    raise ValueError("PIN row must include exact type facts")
                pins.add(
                    (
                        fields[1],
                        fields[2],
                        fields[3],
                        fields[4],
                        int(fields[5]),
                        fields[6],
                        fields[7],
                        fields[8],
                        fields[9],
                        fields[10],
                        fields[11],
                        _parse_inventory_bool(fields[12]),
                        _parse_inventory_bool(fields[13]),
                        _parse_inventory_bool(fields[14]),
                        _parse_inventory_bool(fields[15]),
                        _parse_inventory_bool(fields[16]),
                    )
                )
            elif record == "EDGE" and len(fields) == 11:
                edges.add(
                    (
                        fields[1],
                        fields[2],
                        fields[3],
                        fields[4],
                        int(fields[5]),
                        fields[6],
                        fields[7],
                        fields[8],
                        int(fields[9]),
                        fields[10],
                    )
                )
            elif record == "COUNTS" and len(fields) == 6:
                counts.append(tuple(int(value) for value in fields[1:]))
            else:
                raise ValueError(f"unexpected {record!r} record shape")
        except ValueError as error:
            raise LC3ArtifactError(
                f"malformed inventory row {path.name}:{line_number}: {error}"
            ) from error
    if len(blueprints) != 1:
        raise LC3ArtifactError(f"inventory must declare one Blueprint path: {path.name}")
    if len(counts) != 1:
        raise LC3ArtifactError(f"inventory must declare one COUNTS row: {path.name}")
    expected_records = 2 + len(graphs) + len(nodes) + len(pins) + len(edges)
    if expected_records != len(lines):
        raise LC3ArtifactError(f"inventory contains duplicate rows: {path.name}")
    return _Inventory(
        blueprints[0],
        counts[0],
        frozenset(graphs),
        frozenset(nodes),
        frozenset(pins),
        frozenset(edges),
    )


def _raw_inventory(raw: Mapping[str, Any]) -> _Inventory:
    blueprint = raw.get("blueprint")
    if not isinstance(blueprint, Mapping):
        raise LC3ArtifactError("raw blueprint must be an object")
    graphs: set[tuple[str, str]] = set()
    nodes: set[tuple[str, str, str]] = set()
    pins: set[tuple[Any, ...]] = set()
    edges: set[tuple[Any, ...]] = set()
    for graph in blueprint.get("graphs", []):
        graph_id = str(graph["id"])
        graphs.add((graph_id, str(graph["name"])))
        graph_nodes = graph["nodes"]
        node_by_id = {str(node["id"]): node for node in graph_nodes}
        pin_by_id: dict[str, Mapping[str, Any]] = {}
        for node in graph_nodes:
            native_guid = str(node["native_guid"])
            nodes.add((graph_id, native_guid, str(node["class"])))
            for pin in node["pins"]:
                pin_id = str(pin["id"])
                pin_by_id[pin_id] = pin
                pins.add(
                    (
                        graph_id,
                        native_guid,
                        str(pin["name"]),
                        str(pin["direction"]),
                        _pin_occurrence(pin_id),
                        str(pin["kind"]),
                        str(pin["pin_role"]),
                        *_pin_type_fact(pin.get("type"), f"raw pin {pin_id}"),
                    )
                )
        for edge in graph["edges"]:
            source_node = node_by_id[str(edge["source_node_id"])]
            target_node = node_by_id[str(edge["target_node_id"])]
            source_pin = pin_by_id[str(edge["source_pin_id"])]
            target_pin = pin_by_id[str(edge["target_pin_id"])]
            edges.add(
                (
                    graph_id,
                    str(source_node["native_guid"]),
                    str(source_pin["direction"]),
                    str(source_pin["name"]),
                    _pin_occurrence(str(source_pin["id"])),
                    str(target_node["native_guid"]),
                    str(target_pin["direction"]),
                    str(target_pin["name"]),
                    _pin_occurrence(str(target_pin["id"])),
                    str(edge["kind"]),
                )
            )
    raw_counts = raw.get("counts")
    if not isinstance(raw_counts, Mapping):
        raise LC3ArtifactError("raw counts must be an object")
    return _Inventory(
        str(blueprint["path"]),
        (
            int(raw_counts["graphs"]),
            int(raw_counts["nodes"]),
            int(raw_counts["pins"]),
            int(raw_counts["edges"]),
            int(raw_counts["unsupported_nodes"]),
        ),
        frozenset(graphs),
        frozenset(nodes),
        frozenset(pins),
        frozenset(edges),
    )


def _compare_inventory(actual: _Inventory, expected: _Inventory) -> None:
    for label in ("blueprint_path", "counts", "graphs", "nodes", "pins", "edges"):
        actual_value = getattr(actual, label)
        expected_value = getattr(expected, label)
        if actual_value == expected_value:
            continue
        if isinstance(actual_value, frozenset) and isinstance(expected_value, frozenset):
            detail = (
                f": missing={len(expected_value - actual_value)} "
                f"extra={len(actual_value - expected_value)}"
            )
        else:
            detail = ""
        raise LC3ArtifactError(
            f"independent inventory {label} differs from raw export{detail}"
        )


def _edge_fact(edge: Edge, nodes: Mapping[str, Node], pins: Mapping[str, Pin]) -> dict[str, Any]:
    source_node = nodes[edge.source_node_id]
    target_node = nodes[edge.target_node_id]
    source_pin = pins[edge.source_pin_id]
    target_pin = pins[edge.target_pin_id]
    return {
        "edge_id": edge.id,
        "kind": edge.kind,
        "source_node_id": source_node.id,
        "source_native_node_guid": source_node.native_guid,
        "source_pin_id": source_pin.id,
        "source_port_label": source_pin.name,
        "target_node_id": target_node.id,
        "target_native_node_guid": target_node.native_guid,
        "target_pin_id": target_pin.id,
        "target_port_label": target_pin.name,
    }


def _incoming_by_pin(graph: Graph, kind: str) -> Mapping[str, tuple[Edge, ...]]:
    result: dict[str, list[Edge]] = defaultdict(list)
    for edge in graph.edges:
        if edge.kind == kind:
            result[edge.target_pin_id].append(edge)
    return {pin_id: tuple(edges) for pin_id, edges in result.items()}


def _source_references(
    graph: Graph,
    selected_node_ids: set[str],
    selected_edge_ids: set[str],
) -> list[dict[str, Any]]:
    pins = {pin.id: pin for node in graph.nodes for pin in node.pins}
    incident: dict[str, set[str]] = defaultdict(set)
    for edge in graph.edges:
        if edge.id in selected_edge_ids:
            incident[edge.source_node_id].add(edge.source_pin_id)
            incident[edge.target_node_id].add(edge.target_pin_id)
    result: list[dict[str, Any]] = []
    for node in sorted(graph.nodes, key=lambda item: item.id):
        if node.id not in selected_node_ids:
            continue
        selected_pins = set(incident[node.id])
        selected_pins.update(
            pin.id for pin in node.pins if pin.pin_role != "none"
        )
        symbol = node.symbol or {}
        result.append(
            {
                "node_id": node.id,
                "native_node_guid": node.native_guid,
                "class": node.class_path,
                "symbol_name": str(symbol.get("name", "")),
                "symbol_guid": str(symbol.get("guid", "")),
                "symbol_access": str(symbol.get("access", "")),
                "source_pin_ids": sorted(selected_pins),
            }
        )
    if set(pins) and len(result) != len(selected_node_ids):
        raise LC3ArtifactError("LC3 source references do not cover selected nodes")
    return result


def _event_graph(document: Any) -> Graph:
    return _one(
        (graph for graph in document.graphs if graph.name == "EventGraph"),
        "LC3 EventGraph",
    )


def _criterion_node(graph: Graph) -> Node:
    return _one(
        (
            node
            for node in graph.nodes
            if _symbol(node, access="set").get("guid") == LC3_MEMBER_GUID
        ),
        "LC3Score Set node by stable member GUID",
    )


def _incoming_edge(
    incoming: Mapping[str, tuple[Edge, ...]], pin: Pin, description: str
) -> Edge:
    return _one(incoming.get(pin.id, ()), description)


def _require_operator(
    node: Node,
    expected_name: str,
    nodes: Mapping[str, Node],
    pins: Mapping[str, Pin],
    incoming_data: Mapping[str, tuple[Edge, ...]],
    expected_inputs: Mapping[str, str],
) -> dict[str, Any]:
    if _short_class(node) != "K2Node_PromotableOperator":
        raise LC3ArtifactError(f"expected {expected_name} operator: {node.id}")
    if (node.symbol or {}).get("name") != expected_name:
        raise LC3ArtifactError(f"operator symbol mismatch: {node.id}")
    input_pins = {
        name: _pin(node, name=name, direction="input", kind="data")
        for name in ("A", "B")
    }
    _pin(node, name="ReturnValue", direction="output", kind="data")
    facts: list[dict[str, Any]] = []
    for input_name in ("A", "B"):
        edge = _incoming_edge(
            incoming_data,
            input_pins[input_name],
            f"{expected_name}.{input_name} data edge",
        )
        source = nodes[edge.source_node_id]
        source_symbol = _symbol(source, access="get")
        expected_source_name = expected_inputs[input_name]
        if expected_source_name in {"Add_IntInt", "Subtract_IntInt"}:
            if (
                _short_class(source) != "K2Node_PromotableOperator"
                or (source.symbol or {}).get("name") != expected_source_name
            ):
                raise LC3ArtifactError(
                    f"{expected_name}.{input_name} producer must be {expected_source_name}"
                )
        else:
            if _short_class(source) != "K2Node_VariableGet":
                raise LC3ArtifactError(
                    f"{expected_name}.{input_name} producer must be a member Get"
                )
            if (
                source_symbol.get("guid") is None
                or source_symbol.get("name") != expected_source_name
            ):
                raise LC3ArtifactError(
                    f"{expected_name}.{input_name} producer must be Get {expected_source_name}"
                )
        source_pin = pins[edge.source_pin_id]
        expected_source_pin_name = (
            "ReturnValue"
            if expected_source_name in {"Add_IntInt", "Subtract_IntInt"}
            else expected_source_name
        )
        if source_pin.name != expected_source_pin_name:
            raise LC3ArtifactError(
                f"{expected_name}.{input_name} source pin identity mismatch"
            )
        facts.append(_edge_fact(edge, nodes, pins))
    return {"node": node, "input_edges": facts}


def _backward_value_order(
    criterion: Node,
    nodes: Mapping[str, Node],
    pins: Mapping[str, Pin],
    incoming_data: Mapping[str, tuple[Edge, ...]],
) -> tuple[list[str], list[str]]:
    ordered_nodes: list[str] = []
    ordered_edges: list[str] = []
    visited: set[str] = set()
    input_rank = {"A": 0, "B": 1, LC3_MEMBER_NAME: 0}

    def visit(node: Node) -> None:
        if node.id in visited:
            return
        visited.add(node.id)
        ordered_nodes.append(node.id)
        input_pins = sorted(
            (
                pin
                for pin in node.pins
                if pin.kind == "data" and pin.direction == "input"
                and pin.name in input_rank
            ),
            key=lambda pin: (input_rank[pin.name], pin.name),
        )
        for target_pin in input_pins:
            edge = _incoming_edge(
                incoming_data,
                target_pin,
                f"backward value edge for {node.id}.{target_pin.name}",
            )
            ordered_edges.append(edge.id)
            visit(nodes[edge.source_node_id])

    visit(criterion)
    if len(ordered_nodes) != 6 or len(ordered_edges) != 5:
        raise LC3ArtifactError("LC3 backward value closure is not exactly 6 nodes/5 edges")
    return ordered_nodes, ordered_edges


def _build_lc3_truth(
    document: Any,
    graph: Graph,
    *,
    source: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    nodes = {node.id: node for node in graph.nodes}
    pins = {pin.id: pin for node in graph.nodes for pin in node.pins}
    incoming_data = _incoming_by_pin(graph, "data")
    incoming_execution = _incoming_by_pin(graph, "execution")
    criterion = _criterion_node(graph)
    criterion_symbol = _symbol(criterion, access="set")
    if criterion_symbol.get("name") != LC3_MEMBER_NAME:
        raise LC3ArtifactError("LC3 criterion source identity is not Set LC3Score")
    criterion_value_pin = _pin_by_role(criterion, "variable_set_value")
    if criterion_value_pin.name != LC3_MEMBER_NAME:
        raise LC3ArtifactError("LC3 criterion value pin is not LC3Score")
    criterion_value_edge = _incoming_edge(
        incoming_data,
        criterion_value_pin,
        "LC3Score value edge",
    )
    subtract = nodes[criterion_value_edge.source_node_id]
    subtract_fact = _require_operator(
        subtract,
        "Subtract_IntInt",
        nodes,
        pins,
        incoming_data,
        {"A": "Add_IntInt", "B": "Penalty"},
    )
    add = nodes[subtract_fact["input_edges"][0]["source_node_id"]]
    add_fact = _require_operator(
        add,
        "Add_IntInt",
        nodes,
        pins,
        incoming_data,
        {"A": "BaseScore", "B": "BonusScore"},
    )
    criterion_execution_pin = _pin(
        criterion,
        name="execute",
        direction="input",
        kind="execution",
    )
    execution_edge = _incoming_edge(
        incoming_execution,
        criterion_execution_pin,
        "direct LC3Score execution controller",
    )
    controller = nodes[execution_edge.source_node_id]
    if _short_class(controller) != "K2Node_Event":
        raise LC3ArtifactError("LC3Score direct execution controller must be BeginPlay Event")
    execution_source_pin = pins[execution_edge.source_pin_id]
    execution_target_pin = pins[execution_edge.target_pin_id]
    if execution_source_pin.name != "then" or execution_target_pin.name != "execute":
        raise LC3ArtifactError("LC3 direct execution endpoint identity is not then -> execute")

    selected_node_ids = {
        criterion.id,
        controller.id,
        subtract.id,
        add.id,
        *(fact["source_node_id"] for fact in subtract_fact["input_edges"]),
        *(fact["source_node_id"] for fact in add_fact["input_edges"]),
    }
    selected_edge_ids = {
        execution_edge.id,
        criterion_value_edge.id,
        *(fact["edge_id"] for fact in subtract_fact["input_edges"]),
        *(fact["edge_id"] for fact in add_fact["input_edges"]),
    }
    if len(selected_node_ids) != LC3_EXPECTED_NODES:
        raise LC3ArtifactError(
            f"real LC3 slice inventory mismatch: {len(selected_node_ids)} nodes"
        )
    if len(selected_edge_ids) != LC3_EXPECTED_EDGES:
        raise LC3ArtifactError(
            f"real LC3 slice inventory mismatch: {len(selected_edge_ids)} edges"
        )
    if len(graph.nodes) != LC3_EXPECTED_NODES or len(graph.edges) != LC3_EXPECTED_EDGES:
        raise LC3ArtifactError(
            "real LC3 EventGraph must contain exactly seven nodes and six edges"
        )
    if any(edge.id not in selected_edge_ids for edge in graph.edges):
        raise LC3ArtifactError("LC3 selected closure does not contain every EventGraph edge")
    data_edges = [edge for edge in graph.edges if edge.kind == "data"]
    execution_edges = [edge for edge in graph.edges if edge.kind == "execution"]
    if len(data_edges) != LC3_EXPECTED_DATA_EDGES or len(execution_edges) != LC3_EXPECTED_EXECUTION_EDGES:
        raise LC3ArtifactError("LC3 edge-kind inventory must be exactly 5 data/1 execution")

    ordered_node_ids, ordered_edge_ids = _backward_value_order(
        criterion, nodes, pins, incoming_data
    )
    producer_node_ids = [node_id for node_id in ordered_node_ids if node_id != criterion.id]
    data_relation_edges = sorted(data_edges, key=lambda edge: edge.id)
    data_relations = [
        _edge_fact(edge, nodes, pins) | {"relation_kind": "provides_value"}
        for edge in data_relation_edges
    ]
    source_references = _source_references(graph, selected_node_ids, selected_edge_ids)
    if len(source_references) != LC3_EXPECTED_NODES:
        raise LC3ArtifactError("LC3 source ownership must cover all seven selected nodes")
    status_coverage = [
        {
            "node_id": node.id,
            "semantic_status": node.semantic_status,
            "semantic_reason": node.semantic_reason,
        }
        for node in sorted(graph.nodes, key=lambda item: item.id)
    ]
    if any(
        item["semantic_status"] != "supported" or item["semantic_reason"]
        for item in status_coverage
    ):
        raise LC3ArtifactError("LC3 selected nodes must all be supported")

    exclusions = [
        {
            "graph_id": other.id,
            "reason": "excluded because member-variable data slicing is graph-local to EventGraph",
        }
        for other in sorted(
            (candidate for candidate in document.graphs if candidate.id != graph.id),
            key=lambda candidate: candidate.id,
        )
    ]
    if not exclusions:
        raise LC3ArtifactError("LC3 truth requires an explicit excluded non-EventGraph region")

    execution_controller = {
        "node_id": controller.id,
        "native_node_guid": controller.native_guid,
        "label": "BeginPlay",
        "edge": _edge_fact(execution_edge, nodes, pins),
    }
    criterion_fact = {
        "node_id": criterion.id,
        "native_node_guid": criterion.native_guid,
        "member_guid": LC3_MEMBER_GUID,
        "member_name": LC3_MEMBER_NAME,
        "value_pin_id": criterion_value_pin.id,
        "value_port_label": criterion_value_pin.name,
        "value_edge_id": criterion_value_edge.id,
    }
    value_cone = {
        "group_id": LC3_VALUE_CONE_GROUP_ID,
        "kind": "value_cone",
        "title": LC3_VALUE_CONE_TITLE,
        "ordered_node_ids": ordered_node_ids,
        "ordered_edge_ids": ordered_edge_ids,
        "entry_node_id": criterion.id,
        "parent_group_id": None,
        "entered_by": None,
        "member_count": len(ordered_node_ids),
        "projection_status": "COMPLETE",
        "diagnostic_code": "",
        "claim_evidence": [
            {
                "component": "id",
                "fact_owner": "value_cone.group_id",
                "source": "lc3_value_truth.value_cone.group_id",
            },
            {
                "component": "kind",
                "fact_owner": "value_cone.kind",
                "source": "lc3_value_truth.value_cone.kind",
            },
            {
                "component": "title",
                "fact_owner": "criterion.member_name",
                "source": "lc3_value_truth.criterion.member_name",
            },
            {
                "component": "ordered_unit_ids",
                "fact_owner": "value_cone.ordered_node_ids",
                "source": "lc3_value_truth.value_cone.ordered_node_ids",
            },
            {
                "component": "ordered_relation_ids",
                "fact_owner": "value_cone.ordered_edge_ids",
                "source": "lc3_value_truth.value_cone.ordered_edge_ids",
            },
            {
                "component": "entry_unit_id",
                "fact_owner": "value_cone.entry_node_id",
                "source": "lc3_value_truth.value_cone.entry_node_id",
            },
            {
                "component": "parent_group_id",
                "fact_owner": "value_cone.parent_group_id",
                "source": "lc3_value_truth.value_cone.parent_group_id",
            },
            {
                "component": "entered_by",
                "fact_owner": "value_cone.entered_by",
                "source": "lc3_value_truth.value_cone.entered_by",
            },
            {
                "component": "member_count",
                "fact_owner": "value_cone.member_count",
                "source": "lc3_value_truth.value_cone.member_count",
            },
            {
                "component": "projection_status",
                "fact_owner": "value_cone.projection_status",
                "source": "lc3_value_truth.value_cone.projection_status",
            },
            {
                "component": "diagnostic_code",
                "fact_owner": "value_cone.diagnostic_code",
                "source": "lc3_value_truth.value_cone.diagnostic_code",
            },
        ],
    }
    ledger = {
        "format": "blueprint-lens-lc3-value-truth",
        "schema_version": "1.0.0",
        "source": dict(source or {}),
        "criterion": criterion_fact,
        "execution_controller": execution_controller,
        "producer_closure": [
            {
                "node_id": nodes[node_id].id,
                "native_node_guid": nodes[node_id].native_guid,
                "class": nodes[node_id].class_path,
                "symbol_name": str((nodes[node_id].symbol or {}).get("name", "")),
                "symbol_guid": str((nodes[node_id].symbol or {}).get("guid", "")),
                "symbol_access": str((nodes[node_id].symbol or {}).get("access", "")),
            }
            for node_id in producer_node_ids
        ],
        "data_relations": data_relations,
        "value_cone": value_cone,
        "source_references": source_references,
        "status_coverage": status_coverage,
        "boundaries": [],
        "exclusions": exclusions,
        "counts": {
            "nodes": len(selected_node_ids),
            "edges": len(selected_edge_ids),
            "execution_edges": len(execution_edges),
            "data_edges": len(data_edges),
            "producer_nodes": len(producer_node_ids),
            "producer_gets": sum(
                _short_class(nodes[node_id]) == "K2Node_VariableGet"
                for node_id in producer_node_ids
            ),
            "operations": sum(
                _short_class(nodes[node_id]) == "K2Node_PromotableOperator"
                for node_id in producer_node_ids
            ),
            "excluded_regions": len(exclusions),
        },
        "layout_readiness": {
            "status": "COMPLETE",
            "diagnostic_code": "",
            "endpoint_count": len(data_relations),
        },
    }
    return ledger


def _criterion_from_ir(ir: Mapping[str, Any]) -> tuple[str, str]:
    matches: list[tuple[str, str]] = []
    for graph in ir["blueprint"]["graphs"]:
        for node in graph["nodes"]:
            symbol = node.get("symbol") or {}
            if (
                symbol.get("kind") == "variable"
                and symbol.get("access") == "set"
                and symbol.get("guid") == LC3_MEMBER_GUID
            ):
                matches.append((str(graph["id"]), str(node["id"])))
    return _one(matches, "LC3Score criterion by stable member GUID")


def _build_slice(ir_path: Path, ir: Mapping[str, Any]) -> dict[str, Any]:
    document = load_blueprint_lens_v1(ir_path)
    graph_id, criterion_node_id = _criterion_from_ir(ir)
    result = compute_member_variable_data_slice(document, graph_id, LC3_MEMBER_GUID)
    if result.graph_id != graph_id or result.member_name != LC3_MEMBER_NAME:
        raise LC3ArtifactError("LC3 member slice criterion changed during slicing")
    if len(result.node_ids) != LC3_EXPECTED_NODES or len(result.edge_ids) != LC3_EXPECTED_EDGES:
        raise LC3ArtifactError(
            "real LC3 slice inventory mismatch: "
            f"{len(result.node_ids)} nodes/{len(result.edge_ids)} edges"
        )
    selected = set(result.node_ids)
    boundaries = [
        {
            "node_id": node.id,
            "status": node.semantic_status,
            "reason": node.semantic_reason,
        }
        for node in sorted(document.nodes, key=lambda item: item.id)
        if node.id in selected and node.semantic_status != "supported"
    ]
    return {
        "format": "blueprint-lens-slice",
        "schema_version": "1.0.0",
        "rules_version": "1.0.0",
        "source_fixture": ir_path.name,
        "source_sha256": _sha256_file(ir_path),
        "slice_kind": "member_variable_data_dependency",
        "criterion": {
            "graph_id": graph_id,
            "member_guid": LC3_MEMBER_GUID,
            "member_name": LC3_MEMBER_NAME,
            "question": LC3_QUESTION,
        },
        "graph_id": result.graph_id,
        "node_ids": list(result.node_ids),
        "edge_ids": list(result.edge_ids),
        "inclusion_reasons": {
            node_id: list(reasons)
            for node_id, reasons in result.inclusion_reasons.items()
        },
        "boundaries": boundaries,
        "counts": {"nodes": len(result.node_ids), "edges": len(result.edge_ids)},
    }


def _stable_node_id(graph_id: str, native_guid: str) -> str:
    return f"{graph_id}::node::{native_guid}"


def _stable_pin_id(
    graph_id: str,
    native_guid: str,
    direction: str,
    name: str,
    occurrence: int,
) -> str:
    return (
        f"{_stable_node_id(graph_id, native_guid)}::pin::locator-"
        f"{direction}-{name}-{occurrence}"
    )


def _stable_inventory_edge_id(edge: tuple[Any, ...]) -> str:
    (
        graph_id,
        source_native_guid,
        source_direction,
        source_name,
        source_occurrence,
        target_native_guid,
        target_direction,
        target_name,
        target_occurrence,
        _kind,
    ) = edge
    return (
        f"{graph_id}::edge::{_stable_pin_id(graph_id, source_native_guid, source_direction, source_name, source_occurrence)}"
        f"->{_stable_pin_id(graph_id, target_native_guid, target_direction, target_name, target_occurrence)}"
    )


def _raw_symbol_value(node: Mapping[str, Any]) -> Mapping[str, Any]:
    symbol = node.get("symbol")
    return symbol if isinstance(symbol, Mapping) else {}


def _raw_short_class(node: Mapping[str, Any]) -> str:
    return str(node.get("class", "")).rsplit(".", 1)[-1]


def _inventory_incoming_edge(
    edges: Iterable[tuple[Any, ...]],
    graph_id: str,
    target_native_guid: str,
    target_pin_name: str,
    kind: str,
    description: str,
) -> tuple[Any, ...]:
    return _one(
        (
            edge
            for edge in edges
            if edge[0] == graph_id
            and edge[5] == target_native_guid
            and edge[6] == "input"
            and edge[7] == target_pin_name
            and edge[9] == kind
        ),
        description,
    )


def _build_inventory_expected_sets(
    inventory: _Inventory,
    raw: Mapping[str, Any],
    graph_id: str,
) -> tuple[list[str], list[str], Mapping[str, str]]:
    event_graph_id = _one(
        (candidate_id for candidate_id, name in inventory.graphs if name == "EventGraph"),
        "LC3 EventGraph in independent inventory",
    )
    if event_graph_id != graph_id:
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: EventGraph identity changed"
        )

    blueprint = raw.get("blueprint")
    if not isinstance(blueprint, Mapping):
        raise LC3ArtifactError("raw blueprint must be an object")
    raw_graph = _one(
        (
            candidate
            for candidate in blueprint.get("graphs", [])
            if isinstance(candidate, Mapping)
            and str(candidate.get("id")) == graph_id
            and str(candidate.get("name")) == "EventGraph"
        ),
        "LC3 EventGraph in raw export",
    )
    raw_graph_nodes = raw_graph.get("nodes")
    if not isinstance(raw_graph_nodes, list):
        raise LC3ArtifactError("raw EventGraph nodes must be an array")
    raw_nodes: dict[str, Mapping[str, Any]] = {}
    for node in raw_graph_nodes:
        if not isinstance(node, Mapping):
            raise LC3ArtifactError("raw EventGraph node must be an object")
        native_guid = str(node.get("native_guid"))
        if native_guid in raw_nodes:
            raise LC3ArtifactError(
                "independent inventory/raw closure disagreement: duplicate EventGraph node identity"
            )
        raw_nodes[native_guid] = node

    inventory_nodes = {
        native_guid: class_path
        for candidate_graph_id, native_guid, class_path in inventory.nodes
        if candidate_graph_id == graph_id
    }
    if set(inventory_nodes) != set(raw_nodes):
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: EventGraph node identities differ"
        )
    if any(
        str(raw_nodes[native_guid].get("class")) != class_path
        for native_guid, class_path in inventory_nodes.items()
    ):
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: EventGraph node classes differ"
        )

    event_edges = tuple(
        edge for edge in inventory.edges if edge[0] == graph_id
    )
    criterion_guid = _one(
        (
            native_guid
            for native_guid, node in raw_nodes.items()
            if _raw_short_class(node) == "K2Node_VariableSet"
            and _raw_symbol_value(node).get("kind") == "variable"
            and _raw_symbol_value(node).get("access") == "set"
            and _raw_symbol_value(node).get("guid") == LC3_MEMBER_GUID
            and _raw_symbol_value(node).get("name") == LC3_MEMBER_NAME
        ),
        "LC3Score Set in independent inventory/raw closure",
    )

    def require_operator(native_guid: str, expected_name: str) -> None:
        node = raw_nodes[native_guid]
        if (
            _raw_short_class(node) != "K2Node_PromotableOperator"
            or _raw_symbol_value(node).get("name") != expected_name
        ):
            raise LC3ArtifactError(
                "independent inventory/raw closure disagreement: "
                f"expected {expected_name} operator at {native_guid}"
            )

    def require_get(native_guid: str, expected_name: str) -> None:
        node = raw_nodes[native_guid]
        symbol = _raw_symbol_value(node)
        if (
            _raw_short_class(node) != "K2Node_VariableGet"
            or symbol.get("access") != "get"
            or symbol.get("guid") is None
            or symbol.get("name") != expected_name
        ):
            raise LC3ArtifactError(
                "independent inventory/raw closure disagreement: "
                f"expected {expected_name} member Get at {native_guid}"
            )

    criterion_value_edge = _inventory_incoming_edge(
        event_edges,
        graph_id,
        criterion_guid,
        LC3_MEMBER_NAME,
        "data",
        "LC3Score value edge in independent inventory",
    )
    subtract_guid = str(criterion_value_edge[1])
    require_operator(subtract_guid, "Subtract_IntInt")
    subtract_a_edge = _inventory_incoming_edge(
        event_edges,
        graph_id,
        subtract_guid,
        "A",
        "data",
        "Subtract_IntInt.A edge in independent inventory",
    )
    subtract_b_edge = _inventory_incoming_edge(
        event_edges,
        graph_id,
        subtract_guid,
        "B",
        "data",
        "Subtract_IntInt.B edge in independent inventory",
    )
    add_guid = str(subtract_a_edge[1])
    require_operator(add_guid, "Add_IntInt")
    require_get(str(subtract_b_edge[1]), "Penalty")
    add_a_edge = _inventory_incoming_edge(
        event_edges,
        graph_id,
        add_guid,
        "A",
        "data",
        "Add_IntInt.A edge in independent inventory",
    )
    add_b_edge = _inventory_incoming_edge(
        event_edges,
        graph_id,
        add_guid,
        "B",
        "data",
        "Add_IntInt.B edge in independent inventory",
    )
    require_get(str(add_a_edge[1]), "BaseScore")
    require_get(str(add_b_edge[1]), "BonusScore")

    execution_edge = _inventory_incoming_edge(
        event_edges,
        graph_id,
        criterion_guid,
        "execute",
        "execution",
        "LC3Score execution edge in independent inventory",
    )
    if _raw_short_class(raw_nodes[str(execution_edge[1])]) != "K2Node_Event":
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: "
            "LC3Score execution controller is not a BeginPlay Event"
        )

    selected_native_guids = {
        criterion_guid,
        str(execution_edge[1]),
        subtract_guid,
        add_guid,
        str(subtract_b_edge[1]),
        str(add_a_edge[1]),
        str(add_b_edge[1]),
    }
    selected_edges = {
        criterion_value_edge,
        execution_edge,
        subtract_a_edge,
        subtract_b_edge,
        add_a_edge,
        add_b_edge,
    }
    induced_edges = {
        edge
        for edge in event_edges
        if edge[1] in selected_native_guids and edge[5] in selected_native_guids
    }
    if induced_edges != selected_edges:
        missing = selected_edges - induced_edges
        extra = induced_edges - selected_edges
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: induced EventGraph edges "
            f"missing={len(missing)} extra={len(extra)}"
        )
    producer_guids = selected_native_guids - {
        criterion_guid,
        str(execution_edge[1]),
    }
    if len(selected_native_guids) != LC3_EXPECTED_NODES:
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: expected seven EventGraph nodes"
        )
    if len(induced_edges) != LC3_EXPECTED_EDGES:
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: expected six induced EventGraph edges"
        )
    if sum(_raw_short_class(raw_nodes[native_guid]) == "K2Node_VariableGet" for native_guid in producer_guids) != 3:
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: expected three producer Gets"
        )
    if sum(_raw_short_class(raw_nodes[native_guid]) == "K2Node_PromotableOperator" for native_guid in producer_guids) != 2:
        raise LC3ArtifactError(
            "independent inventory/raw closure disagreement: expected two producer operations"
        )
    expected_node_ids = sorted(
        _stable_node_id(graph_id, native_guid)
        for native_guid in selected_native_guids
    )
    expected_edge_ids = sorted(
        _stable_inventory_edge_id(edge) for edge in induced_edges
    )
    edge_kinds = {
        _stable_inventory_edge_id(edge): str(edge[9]) for edge in induced_edges
    }
    return expected_node_ids, expected_edge_ids, edge_kinds


def _build_ground_truth(
    ir_path: Path,
    graph: Graph,
    inventory: _Inventory,
    raw: Mapping[str, Any],
) -> dict[str, Any]:
    node_ids, edge_ids, edge_kinds = _build_inventory_expected_sets(
        inventory, raw, graph.id
    )
    return {
        "format": "blueprint-lens-ground-truth",
        "schema_version": "1.0.0",
        "review": {
            "status": "frozen",
            "annotators": [
                "author_source_review",
                "independent_inventory_review",
            ],
            "reviewed_at": "2026-08-10",
            "notes": (
                "Reviewed from the real UE source export and independent TSV "
                "inventory before comparison with the generated member-variable slice."
            ),
        },
        "source_fixture": ir_path.name,
        "source_sha256": _sha256_file(ir_path),
        "slice_kind": "member_variable_data_dependency",
        "criterion": {
            "graph_id": graph.id,
            "member_guid": LC3_MEMBER_GUID,
            "member_name": LC3_MEMBER_NAME,
            "question": LC3_QUESTION,
        },
        "rules_version": "1.0.0",
        "expected": {
            "node_ids": node_ids,
            "edge_ids": edge_ids,
            "counts": {
                "nodes": len(node_ids),
                "edges": len(edge_ids),
                "execution_edges": sum(
                    edge_kinds[edge_id] == "execution" for edge_id in edge_ids
                ),
            "data_edges": sum(
                    edge_kinds[edge_id] == "data" for edge_id in edge_ids
                ),
            },
        },
        "excluded_regions": [
            str(candidate_id)
            + ": excluded because member-variable data slicing is graph-local to EventGraph"
            for candidate_id, name in sorted(inventory.graphs)
            if candidate_id != graph.id
        ],
    }


def _precision_recall(actual: Sequence[str], expected: Sequence[str]) -> tuple[float, float]:
    actual_set = set(map(str, actual))
    expected_set = set(map(str, expected))
    intersection = actual_set & expected_set
    precision = len(intersection) / len(actual_set) if actual_set else 1.0
    recall = len(intersection) / len(expected_set) if expected_set else 1.0
    return precision, recall


def _precision_recall_diagnostic(
    label: str,
    actual: Sequence[str],
    expected: Sequence[str],
    precision: float,
    recall: float,
) -> str:
    actual_set = set(map(str, actual))
    expected_set = set(map(str, expected))
    return (
        f"{label} precision={precision} recall={recall} "
        f"missing={sorted(expected_set - actual_set)} "
        f"extra={sorted(actual_set - expected_set)}"
    )


def _validate_slice_contract(slice_value: Mapping[str, Any]) -> None:
    node_ids = slice_value.get("node_ids")
    edge_ids = slice_value.get("edge_ids")
    if not isinstance(node_ids, list) or not all(isinstance(item, str) for item in node_ids):
        raise LC3ArtifactError("slice.node_ids must be an array of strings")
    if not isinstance(edge_ids, list) or not all(isinstance(item, str) for item in edge_ids):
        raise LC3ArtifactError("slice.edge_ids must be an array of strings")
    if len(node_ids) != len(set(node_ids)):
        raise LC3ArtifactError("LC3 slice.node_ids contains duplicate entries")
    if len(edge_ids) != len(set(edge_ids)):
        raise LC3ArtifactError("LC3 slice.edge_ids contains duplicate entries")
    expected_lengths = {"nodes": len(node_ids), "edges": len(edge_ids)}
    if slice_value.get("counts") != expected_lengths:
        raise LC3ArtifactError("LC3 slice.counts must equal node_ids/edge_ids lengths")
    if expected_lengths != {"nodes": LC3_EXPECTED_NODES, "edges": LC3_EXPECTED_EDGES}:
        raise LC3ArtifactError("LC3 slice.counts must be exactly 7 nodes/6 edges")


def _assert_equal_frozen(declared: Any, derived: Any, path: str) -> None:
    if isinstance(derived, Mapping):
        if not isinstance(declared, Mapping) or set(declared) != set(derived):
            raise LC3ArtifactError(
                f"LC3 truth ledger drift at {path}: declared {declared!r}, derived {derived!r}"
            )
        for key in sorted(derived):
            _assert_equal_frozen(declared[key], derived[key], f"{path}.{key}")
        return
    if isinstance(derived, list):
        if not isinstance(declared, list) or len(declared) != len(derived):
            raise LC3ArtifactError(
                f"LC3 truth ledger drift at {path}: declared {declared!r}, derived {derived!r}"
            )
        for index, value in enumerate(derived):
            _assert_equal_frozen(declared[index], value, f"{path}[{index}]")
        return
    if declared != derived:
        raise LC3ArtifactError(
            f"LC3 truth ledger drift at {path}: declared {declared!r}, derived {derived!r}"
        )


def validate_lc3_truth_ledger(
    ledger_path: Path,
    ir_path: Path,
    slice_path: Path,
    asset_path: Path,
) -> Mapping[str, Any]:
    ledger_path = Path(ledger_path).resolve()
    ir_path = Path(ir_path).resolve()
    slice_path = Path(slice_path).resolve()
    asset_path = Path(asset_path).resolve()
    ledger = _load_object(ledger_path, "LC3 value truth")
    if ledger.get("format") != "blueprint-lens-lc3-value-truth":
        raise LC3ArtifactError("LC3 value truth format is not supported")
    source = ledger.get("source")
    if not isinstance(source, Mapping):
        raise LC3ArtifactError("LC3 value truth source must be an object")
    for path, field, label in (
        (ir_path, "ir_sha256", "IR"),
        (slice_path, "slice_sha256", "slice"),
        (asset_path, "asset_sha256", "asset"),
    ):
        actual = _sha256_file(path)
        if source.get(field) != actual:
            raise LC3ArtifactError(
                f"LC3 value truth {label} SHA-256 mismatch: declared {source.get(field)}, actual {actual}"
            )
    if source.get("blueprint_asset_path") != LC3_BLUEPRINT_PATH:
        raise LC3ArtifactError("LC3 value truth Blueprint path does not match contract")
    slice_value = _load_object(slice_path, "LC3 slice")
    _validate_slice_contract(slice_value)
    if slice_value.get("source_sha256") != _sha256_file(ir_path):
        raise LC3ArtifactError("LC3 slice source hash does not match IR")
    criterion = slice_value.get("criterion")
    if not isinstance(criterion, Mapping) or criterion.get("member_guid") != LC3_MEMBER_GUID:
        raise LC3ArtifactError("LC3 slice must be bound to the stable LC3Score member GUID")
    document = load_blueprint_lens_v1(ir_path)
    graph_id = str(slice_value.get("graph_id", ""))
    graph = _one((candidate for candidate in document.graphs if candidate.id == graph_id), "LC3 slice graph")
    derived = _build_lc3_truth(document, graph)
    for field in LC3_FROZEN_LEDGER_FIELDS:
        if field not in ledger:
            raise LC3ArtifactError(f"LC3 truth ledger field is missing: {field}")
        _assert_equal_frozen(ledger[field], derived[field], field)
    expected_counts = {
        "nodes": LC3_EXPECTED_NODES,
        "edges": LC3_EXPECTED_EDGES,
        "execution_edges": LC3_EXPECTED_EXECUTION_EDGES,
        "data_edges": LC3_EXPECTED_DATA_EDGES,
        "producer_nodes": 5,
        "producer_gets": 3,
        "operations": 2,
        "excluded_regions": len(ledger["exclusions"]),
    }
    if ledger["counts"] != expected_counts:
        raise LC3ArtifactError("LC3 truth ledger frozen counts are not exact")
    expected_slice_nodes = set(str(item) for item in ledger["source_references"])
    if set(slice_value.get("node_ids", ())) != {
        str(item["node_id"]) for item in ledger["source_references"]
    }:
        raise LC3ArtifactError("LC3 truth ledger membership disagrees with slice nodes")
    if set(slice_value.get("edge_ids", ())) != {
        str(ledger["execution_controller"]["edge"]["edge_id"]),
        *(str(item["edge_id"]) for item in ledger["data_relations"]),
    }:
        raise LC3ArtifactError("LC3 truth ledger membership disagrees with slice edges")
    if expected_slice_nodes and len(expected_slice_nodes) != LC3_EXPECTED_NODES:
        raise LC3ArtifactError("LC3 source reference membership is not seven nodes")
    return ledger


def _build_lc3_artifacts_in_directory(
    raw_run1_path: Path,
    raw_run2_path: Path,
    inventory_run1_path: Path,
    inventory_run2_path: Path,
    asset_path: Path,
    output_dir: Path,
    graph_schema_path: Path,
    slice_schema_path: Path,
    ground_truth_schema_path: Path,
) -> dict[str, Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    asset_hash_before = _sha256_file(asset_path)
    raw_bytes = raw_run1_path.read_bytes()
    if raw_bytes != raw_run2_path.read_bytes():
        raise LC3ArtifactError("LC3 raw exports are not byte-identical")
    inventory_bytes = inventory_run1_path.read_bytes()
    if inventory_bytes != inventory_run2_path.read_bytes():
        raise LC3ArtifactError("LC3 inventory exports are not byte-identical")
    load_raw_probe(raw_run1_path)
    load_raw_probe(raw_run2_path)
    raw = _load_object(raw_run1_path, "LC3 raw export")
    if raw.get("blueprint", {}).get("path") != LC3_BLUEPRINT_PATH:
        raise LC3ArtifactError("LC3 source Blueprint path does not match contract")
    inventory = _parse_inventory(inventory_run1_path)
    _compare_inventory(inventory, _raw_inventory(raw))

    canonical_raw_path = output_dir / "BP_LC3_ValueProvenance.raw-0.2.json"
    canonical_inventory_path = output_dir / "BP_LC3_ValueProvenance.inventory.tsv"
    _write_bytes(canonical_raw_path, raw_bytes)
    _write_bytes(canonical_inventory_path, inventory_bytes)
    try:
        ir = build_typed_ir(raw, expected_blueprint_path=LC3_BLUEPRINT_PATH)
    except TypedIRBuildError as error:
        raise LC3ArtifactError(str(error)) from error
    ir_path = output_dir / "BP_LC3_ValueProvenance.ir.v1.json"
    _write_json(ir_path, ir)
    validate_contract_file(ir_path, graph_schema_path)
    document = load_blueprint_lens_v1(ir_path)
    graph = _event_graph(document)
    graph_id, criterion_node_id = _criterion_from_ir(ir)
    if graph.id != graph_id:
        raise LC3ArtifactError("LC3 criterion graph changed during slicing")
    slice_value = _build_slice(ir_path, ir)
    slice_path = output_dir / "BP_LC3_ValueProvenance.data.slice.v1.json"
    _write_json(slice_path, slice_value)
    validate_contract_file(slice_path, slice_schema_path)
    ledger = _build_lc3_truth(document, graph)
    if ledger["criterion"]["node_id"] != criterion_node_id:
        raise LC3ArtifactError("LC3 truth criterion does not match stable member slice")
    ground_truth = _build_ground_truth(ir_path, graph, inventory, raw)
    ground_truth_path = output_dir / "BP_LC3_ValueProvenance.data.ground-truth.v1.json"
    _write_json(ground_truth_path, ground_truth)
    validate_contract_file(ground_truth_path, ground_truth_schema_path)
    node_precision, node_recall = _precision_recall(
        slice_value["node_ids"], ground_truth["expected"]["node_ids"]
    )
    edge_precision, edge_recall = _precision_recall(
        slice_value["edge_ids"], ground_truth["expected"]["edge_ids"]
    )
    metrics = {
        "node_precision": node_precision,
        "node_recall": node_recall,
        "edge_precision": edge_precision,
        "edge_recall": edge_recall,
    }
    if any(value != 1.0 for value in metrics.values()):
        diagnostics = []
        for label, actual, expected in (
            ("nodes", slice_value["node_ids"], ground_truth["expected"]["node_ids"]),
            ("edges", slice_value["edge_ids"], ground_truth["expected"]["edge_ids"]),
        ):
            precision = metrics[f"{label[:-1]}_precision"]
            recall = metrics[f"{label[:-1]}_recall"]
            if precision != 1.0 or recall != 1.0:
                diagnostics.append(
                    _precision_recall_diagnostic(
                        label, actual, expected, precision, recall
                    )
                )
        raise LC3ArtifactError(
            "LC3 slice precision/recall must all equal 1.0 against independent "
            "inventory/raw closure: "
            + "; ".join(diagnostics)
        )
    ledger["source"] = {
        "blueprint_asset_path": LC3_BLUEPRINT_PATH,
        "asset_file": asset_path.name,
        "asset_sha256": asset_hash_before,
        "raw_file": canonical_raw_path.name,
        "raw_sha256": _sha256_file(canonical_raw_path),
        "inventory_file": canonical_inventory_path.name,
        "inventory_sha256": _sha256_file(canonical_inventory_path),
        "ir_file": ir_path.name,
        "ir_sha256": _sha256_file(ir_path),
        "slice_file": slice_path.name,
        "slice_sha256": _sha256_file(slice_path),
        "ground_truth_file": ground_truth_path.name,
        "ground_truth_sha256": _sha256_file(ground_truth_path),
        "graph_id": graph.id,
    }
    value_truth_path = output_dir / "BP_LC3_ValueProvenance.value-truth.v1.json"
    _write_json(value_truth_path, ledger)
    validate_lc3_truth_ledger(value_truth_path, ir_path, slice_path, asset_path)
    asset_hash_after = _sha256_file(asset_path)
    checks = {
        "asset_hash_stable": asset_hash_before == asset_hash_after,
        "raw_runs_byte_identical": True,
        "inventory_runs_byte_identical": True,
        "inventory_matches_raw_export": True,
        "inventory_and_raw_independently_extracted_from_asset": True,
        "typed_ir_contract_valid": True,
        "slice_contract_valid": True,
        "ground_truth_contract_valid": True,
        "selected_membership_7_6": ledger["counts"]["nodes"] == 7
        and ledger["counts"]["edges"] == 6,
        "producer_closure_exact": ledger["counts"]["producer_gets"] == 3
        and ledger["counts"]["operations"] == 2,
        "execution_controller_exact": ledger["counts"]["execution_edges"] == 1,
        "data_endpoint_ledger_exact": ledger["layout_readiness"]["endpoint_count"] == 5,
        "precision_recall_1_0": all(value == 1.0 for value in metrics.values()),
        "status_and_boundary_coverage": not ledger["boundaries"]
        and all(item["semantic_status"] == "supported" for item in ledger["status_coverage"]),
    }
    verified_at_publication = {
        key: checks[key]
        for key in (
            "ground_truth_contract_valid",
            "inventory_matches_raw_export",
            "inventory_runs_byte_identical",
            "precision_recall_1_0",
            "raw_runs_byte_identical",
            "slice_contract_valid",
            "typed_ir_contract_valid",
        )
    }
    declared_by_builder = {
        key: checks[key]
        for key in (
            "asset_hash_stable",
            "data_endpoint_ledger_exact",
            "execution_controller_exact",
            "inventory_and_raw_independently_extracted_from_asset",
            "producer_closure_exact",
            "selected_membership_7_6",
            "status_and_boundary_coverage",
        )
    }
    if not all(checks.values()):
        failed = sorted(key for key, passed in checks.items() if not passed)
        raise LC3ArtifactError(f"LC3 readiness checks failed: {failed}")
    readiness = {
        "format": "blueprint-lens-lc3-readiness",
        "schema_version": "1.0.0",
        "status": "TRUTH_FROZEN",
        "verified_at_publication": verified_at_publication,
        "declared_by_builder": declared_by_builder,
        "metrics": metrics,
        "counts": dict(ledger["counts"]),
        "hashes": {
            "asset_sha256_before": asset_hash_before,
            "asset_sha256_after": asset_hash_after,
            "raw_run1_sha256": _sha256_file(raw_run1_path),
            "raw_run2_sha256": _sha256_file(raw_run2_path),
            "inventory_run1_sha256": _sha256_file(inventory_run1_path),
            "inventory_run2_sha256": _sha256_file(inventory_run2_path),
            "ir_sha256": _sha256_file(ir_path),
            "slice_sha256": _sha256_file(slice_path),
            "ground_truth_sha256": _sha256_file(ground_truth_path),
            "value_truth_sha256": _sha256_file(value_truth_path),
        },
        "artifacts": {
            "canonical_raw": canonical_raw_path.name,
            "canonical_inventory": canonical_inventory_path.name,
            "typed_ir": ir_path.name,
            "data_slice": slice_path.name,
            "reviewed_ground_truth": ground_truth_path.name,
            "value_truth": value_truth_path.name,
        },
        "layout_readiness": dict(ledger["layout_readiness"]),
        "limitations": [
            "The value cone is a static backward data closure; execution control is represented separately.",
            "No visual comprehension or scalability evidence is claimed by this fixture.",
        ],
    }
    readiness_path = output_dir / "readiness.v1.json"
    _write_json(readiness_path, readiness)
    if _sha256_file(asset_path) != asset_hash_before:
        raise LC3ArtifactError("LC3 Blueprint package changed during read-only analysis")
    return {
        "canonical_raw": canonical_raw_path,
        "canonical_inventory": canonical_inventory_path,
        "ir": ir_path,
        "slice": slice_path,
        "ground_truth": ground_truth_path,
        "value_truth": value_truth_path,
        "readiness": readiness_path,
    }


def build_lc3_artifacts(
    raw_run1_path: Path,
    raw_run2_path: Path,
    inventory_run1_path: Path,
    inventory_run2_path: Path,
    asset_path: Path,
    output_dir: Path,
    graph_schema_path: Path,
    slice_schema_path: Path,
    ground_truth_schema_path: Path,
) -> dict[str, Path]:
    """Build LC3 artifacts in staging and publish them only after validation."""

    raw_run1_path = Path(raw_run1_path).resolve()
    raw_run2_path = Path(raw_run2_path).resolve()
    inventory_run1_path = Path(inventory_run1_path).resolve()
    inventory_run2_path = Path(inventory_run2_path).resolve()
    asset_path = Path(asset_path).resolve()
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "readiness.v1.json").unlink(missing_ok=True)
    try:
        with tempfile.TemporaryDirectory(prefix=".lc3-staging-", dir=output_dir) as staging:
            staged = _build_lc3_artifacts_in_directory(
                raw_run1_path,
                raw_run2_path,
                inventory_run1_path,
                inventory_run2_path,
                asset_path,
                Path(staging),
                graph_schema_path,
                slice_schema_path,
                ground_truth_schema_path,
            )
            published: dict[str, Path] = {}
            for key, staged_path in staged.items():
                destination = output_dir / staged_path.name
                os.replace(staged_path, destination)
                published[key] = destination
            return published
    except Exception:
        (output_dir / "readiness.v1.json").unlink(missing_ok=True)
        raise
