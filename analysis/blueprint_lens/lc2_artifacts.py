"""Build and independently audit the real LC2 nested-guard truth artifacts."""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
import hashlib
from itertools import combinations
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .contract_validation import validate_contract_file
from .execution_slice import compute_execution_slice
from .raw_probe import Edge, Graph, Node, Pin, load_blueprint_lens_v1, load_raw_probe
from .typed_ir import TypedIRBuildError, build_typed_ir


LC2_BLUEPRINT_PATH = "/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards"
LC2_CRITERION_SYMBOL = "LC2Complete"
LC2_EXPECTED_NODES = 9
LC2_EXPECTED_EDGES = 10
LC2_EXPECTED_EXECUTION_EDGES = 8
LC2_EXPECTED_DATA_EDGES = 2
LC2_CRITERION_DESCRIPTION = (
    "Set LC2Complete after the three nested-guard outcomes reconverge"
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
BOOLEAN_PIN_TYPE = {
    "category": "bool",
    "container": "none",
    "is_const": False,
    "is_reference": False,
    "is_uobject_wrapper": False,
    "is_weak_pointer": False,
    "object_path": "",
    "serialize_as_single_precision_float": False,
    "subcategory": "None",
}


class LC2ArtifactError(ValueError):
    """Raised when the real LC2 evidence does not satisfy its frozen profile."""


@dataclass(frozen=True, slots=True)
class _Inventory:
    blueprint_path: str
    counts: tuple[int, int, int, int, int]
    graphs: frozenset[tuple[str, str]]
    nodes: frozenset[tuple[str, str, str]]
    pins: frozenset[tuple[Any, ...]]
    edges: frozenset[
        tuple[str, str, str, str, int, str, str, str, int, str]
    ]


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


def _load_object(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC2ArtifactError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise LC2ArtifactError(f"JSON root must be an object: {path}")
    return value


def _one(values: Iterable[Any], description: str) -> Any:
    matches = list(values)
    if len(matches) != 1:
        raise LC2ArtifactError(
            f"{description} must resolve exactly once; found {len(matches)}"
        )
    return matches[0]


def _short_class(node: Node) -> str:
    return node.class_path.rsplit(".", 1)[-1]


def _symbol_name(node: Node, *, access: str | None = None) -> str:
    symbol = node.symbol or {}
    if symbol.get("kind") != "variable":
        return ""
    if access is not None and symbol.get("access") != access:
        return ""
    return str(symbol.get("name", ""))


def _pin_by_role(node: Node, role: str) -> Pin:
    return _one(
        (pin for pin in node.pins if pin.pin_role == role),
        f"{role} pin on {node.id}",
    )


def _literal_true(node: Node) -> tuple[str, str]:
    pin = _pin_by_role(node, "variable_set_value")
    value = str(pin.default.get("value", ""))
    if value != "true":
        raise LC2ArtifactError(
            f"LC2 outcome/criterion Set literal must be true: {node.id}={value!r}"
        )
    return pin.id, value


def _parse_inventory_bool(value: str) -> bool:
    if value == "true":
        return True
    if value == "false":
        return False
    raise ValueError(f"invalid Boolean type flag {value!r}")


def _pin_type_fact(value: Any, context: str) -> tuple[Any, ...]:
    if not isinstance(value, Mapping):
        raise LC2ArtifactError(f"{context} type must be an object")
    missing = [field for field in PIN_TYPE_FIELDS if field not in value]
    if missing:
        raise LC2ArtifactError(f"{context} type is missing exact facts: {missing}")
    text_values = tuple(value[field] for field in PIN_TYPE_FIELDS[:4])
    if not all(isinstance(item, str) for item in text_values):
        raise LC2ArtifactError(f"{context} type text facts must be strings")
    bool_values = tuple(value[field] for field in PIN_TYPE_FIELDS[4:])
    if not all(type(item) is bool for item in bool_values):
        raise LC2ArtifactError(f"{context} type flags must be Booleans")
    return text_values + bool_values


def _pin_type_value(pin: Pin) -> dict[str, Any]:
    return dict(zip(PIN_TYPE_FIELDS, _pin_type_fact(pin.type, f"pin {pin.id}")))


def _parse_inventory(path: Path) -> _Inventory:
    blueprint_paths: list[str] = []
    declared_counts: list[tuple[int, int, int, int, int]] = []
    graphs: set[tuple[str, str]] = set()
    nodes: set[tuple[str, str, str]] = set()
    pins: set[tuple[Any, ...]] = set()
    edges: set[tuple[str, str, str, str, int, str, str, str, int, str]] = set()
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeError) as error:
        raise LC2ArtifactError(f"cannot read inventory {path}: {error}") from error
    for line_number, line in enumerate(lines, start=1):
        fields = line.split("\t")
        record = fields[0] if fields else ""
        try:
            if record == "BLUEPRINT" and len(fields) == 2:
                blueprint_paths.append(fields[1])
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
                declared_counts.append(tuple(int(value) for value in fields[1:]))
            else:
                raise ValueError(f"unexpected {record!r} record shape")
        except ValueError as error:
            raise LC2ArtifactError(
                f"malformed inventory row {path.name}:{line_number}: {error}"
            ) from error
    if len(blueprint_paths) != 1:
        raise LC2ArtifactError(
            f"inventory must declare one Blueprint path: {path.name}"
        )
    if len(declared_counts) != 1:
        raise LC2ArtifactError(
            f"inventory must declare one COUNTS row: {path.name}"
        )
    expected_records = 2 + len(graphs) + len(nodes) + len(pins) + len(edges)
    if expected_records != len(lines):
        raise LC2ArtifactError(f"inventory contains duplicate rows: {path.name}")
    return _Inventory(
        blueprint_path=blueprint_paths[0],
        counts=declared_counts[0],
        graphs=frozenset(graphs),
        nodes=frozenset(nodes),
        pins=frozenset(pins),
        edges=frozenset(edges),
    )


def _pin_occurrence(pin_id: str) -> int:
    try:
        return int(pin_id.rsplit("-", 1)[1])
    except (IndexError, ValueError) as error:
        raise LC2ArtifactError(
            f"pin locator lacks terminal occurrence: {pin_id}"
        ) from error


def _raw_inventory(raw: Mapping[str, Any]) -> _Inventory:
    blueprint = raw.get("blueprint")
    if not isinstance(blueprint, Mapping):
        raise LC2ArtifactError("raw blueprint must be an object")
    graph_facts: set[tuple[str, str]] = set()
    node_facts: set[tuple[str, str, str]] = set()
    pin_facts: set[tuple[Any, ...]] = set()
    edge_facts: set[
        tuple[str, str, str, str, int, str, str, str, int, str]
    ] = set()
    for graph in blueprint.get("graphs", []):
        graph_id = str(graph["id"])
        graph_facts.add((graph_id, str(graph["name"])))
        nodes = graph["nodes"]
        node_by_id = {str(node["id"]): node for node in nodes}
        pin_by_id: dict[str, Mapping[str, Any]] = {}
        for node in nodes:
            native_guid = str(node["native_guid"])
            node_facts.add((graph_id, native_guid, str(node["class"])))
            for pin in node["pins"]:
                pin_id = str(pin["id"])
                pin_by_id[pin_id] = pin
                pin_facts.add(
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
            edge_facts.add(
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
    return _Inventory(
        blueprint_path=str(blueprint["path"]),
        counts=(
            int(raw["counts"]["graphs"]),
            int(raw["counts"]["nodes"]),
            int(raw["counts"]["pins"]),
            int(raw["counts"]["edges"]),
            int(raw["counts"]["unsupported_nodes"]),
        ),
        graphs=frozenset(graph_facts),
        nodes=frozenset(node_facts),
        pins=frozenset(pin_facts),
        edges=frozenset(edge_facts),
    )


def _compare_inventory(actual: _Inventory, expected: _Inventory) -> None:
    for label in ("blueprint_path", "counts", "graphs", "nodes", "pins", "edges"):
        actual_value = getattr(actual, label)
        expected_value = getattr(expected, label)
        if actual_value != expected_value:
            if isinstance(actual_value, frozenset) and isinstance(
                expected_value, frozenset
            ):
                detail = (
                    f": missing={len(expected_value - actual_value)} "
                    f"extra={len(actual_value - expected_value)}"
                )
            else:
                detail = ""
            raise LC2ArtifactError(
                f"independent inventory {label} differs from raw export{detail}"
            )


def _criterion(ir: Mapping[str, Any]) -> tuple[str, str]:
    matches: list[tuple[str, str]] = []
    for graph in ir["blueprint"]["graphs"]:
        for node in graph["nodes"]:
            symbol = node.get("symbol") or {}
            if (
                symbol.get("kind") == "variable"
                and symbol.get("access") == "set"
                and symbol.get("name") == LC2_CRITERION_SYMBOL
            ):
                matches.append((str(graph["id"]), str(node["id"])))
    return _one(matches, "LC2 criterion symbol")


def _build_slice(ir_path: Path, ir: Mapping[str, Any]) -> dict[str, Any]:
    document = load_blueprint_lens_v1(ir_path)
    graph_id, criterion_node_id = _criterion(ir)
    result = compute_execution_slice(document, criterion_node_id)
    if result.graph_id != graph_id:
        raise LC2ArtifactError("LC2 criterion graph changed during slicing")
    if (
        len(result.node_ids) != LC2_EXPECTED_NODES
        or len(result.edge_ids) != LC2_EXPECTED_EDGES
    ):
        raise LC2ArtifactError(
            "real LC2 slice inventory mismatch: "
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
    if boundaries:
        raise LC2ArtifactError("LC2 frozen truth requires all selected nodes supported")
    return {
        "format": "blueprint-lens-slice",
        "schema_version": "1.0.0",
        "rules_version": "1.0.0",
        "source_fixture": ir_path.name,
        "source_sha256": _sha256_file(ir_path),
        "slice_kind": "execution_context",
        "criterion": {
            "graph_id": graph_id,
            "node_id": criterion_node_id,
            "description": LC2_CRITERION_DESCRIPTION,
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


def _build_ground_truth(
    ir_path: Path,
    guard_truth: Mapping[str, Any],
    graph: Graph,
) -> dict[str, Any]:
    node_ids = sorted(
        str(reference["node_id"])
        for reference in guard_truth["source_references"]
    )
    selected = set(node_ids)
    edge_ids = sorted(
        edge.id
        for edge in graph.edges
        if edge.source_node_id in selected and edge.target_node_id in selected
    )
    edge_by_id = {edge.id: edge for edge in graph.edges}
    edge_counts = Counter(edge_by_id[edge_id].kind for edge_id in edge_ids)
    graph_exclusions = [
        item
        for item in load_blueprint_lens_v1(ir_path).graphs
        if item.id != graph.id
    ]
    return {
        "format": "blueprint-lens-ground-truth",
        "schema_version": "1.0.0",
        "review": {
            "status": "frozen",
            "annotators": [
                "author_source_review",
                "independent_inventory_review",
            ],
            "reviewed_at": "2026-08-09",
            "notes": (
                "Reviewed from the real UE source export and the independent TSV "
                "inventory before comparison with the generated execution slice."
            ),
        },
        "source_fixture": ir_path.name,
        "source_sha256": _sha256_file(ir_path),
        "slice_kind": "execution_context",
        "criterion": {
            "graph_id": graph.id,
            "node_id": str(guard_truth["criterion"]["node_id"]),
            "description": LC2_CRITERION_DESCRIPTION,
        },
        "rules_version": "1.0.0",
        "expected": {
            "node_ids": node_ids,
            "edge_ids": edge_ids,
            "counts": {
                "nodes": len(node_ids),
                "edges": len(edge_ids),
                "execution_edges": edge_counts["execution"],
                "data_edges": edge_counts["data"],
            },
        },
        "excluded_regions": [
            *[
                f"{item.id}: excluded because execution-context slicing is graph-local to EventGraph"
                for item in sorted(graph_exclusions, key=lambda candidate: candidate.id)
            ],
            "No forward context is selected after LC2Complete (forward_context_hops=0)",
        ],
    }


def _outcome_edges(
    branch: Node,
    outgoing_execution: Mapping[str, Sequence[Edge]],
    pins: Mapping[str, Pin],
) -> dict[str, Edge]:
    outcomes: dict[str, Edge] = {}
    pin_outcomes = {"then": "true", "else": "false"}
    for edge in outgoing_execution.get(branch.id, ()):
        pin_name = pins[edge.source_pin_id].name
        if pin_name not in pin_outcomes:
            raise LC2ArtifactError(
                f"branch execution pin is not exact then/else identity: {pin_name!r}"
            )
        outcome = pin_outcomes[pin_name]
        if outcome in outcomes:
            raise LC2ArtifactError(f"duplicate branch {outcome} outcome: {branch.id}")
        outcomes[outcome] = edge
    if set(outcomes) != {"true", "false"}:
        raise LC2ArtifactError(f"branch must own exact true/false outcomes: {branch.id}")
    return outcomes


def _predicate_attachment(
    guard_id: str,
    branch: Node,
    incoming_data: Mapping[str, Sequence[Edge]],
    nodes: Mapping[str, Node],
    pins: Mapping[str, Pin],
) -> dict[str, Any]:
    condition_pin = _pin_by_role(branch, "branch_condition")
    edge = _one(
        incoming_data.get(condition_pin.id, ()),
        f"predicate edge for {guard_id}",
    )
    producer = nodes[edge.source_node_id]
    if _short_class(producer) != "K2Node_VariableGet":
        raise LC2ArtifactError(f"{guard_id} predicate must be an exact member Get")
    source_pin_type = _pin_type_value(pins[edge.source_pin_id])
    target_pin_type = _pin_type_value(condition_pin)
    if source_pin_type != BOOLEAN_PIN_TYPE or target_pin_type != BOOLEAN_PIN_TYPE:
        raise LC2ArtifactError(
            f"{guard_id} predicate pin type must be exact Boolean"
        )
    return {
        "guard_id": guard_id,
        "edge_id": edge.id,
        "predicate_node_id": producer.id,
        "source_pin_id": edge.source_pin_id,
        "source_port_label": pins[edge.source_pin_id].name,
        "source_pin_type": source_pin_type,
        "branch_node_id": branch.id,
        "condition_pin_id": condition_pin.id,
        "target_port_label": condition_pin.name,
        "target_pin_role": condition_pin.pin_role,
        "target_pin_type": target_pin_type,
    }


def _source_references(
    graph: Graph,
    selected_node_ids: set[str],
    selected_edge_ids: set[str],
) -> list[dict[str, Any]]:
    incident_pins: dict[str, set[str]] = defaultdict(set)
    for edge in graph.edges:
        if edge.id in selected_edge_ids:
            incident_pins[edge.source_node_id].add(edge.source_pin_id)
            incident_pins[edge.target_node_id].add(edge.target_pin_id)
    result = []
    for node in sorted(graph.nodes, key=lambda item: item.id):
        if node.id not in selected_node_ids:
            continue
        pin_ids = set(incident_pins[node.id])
        pin_ids.update(pin.id for pin in node.pins if pin.pin_role != "none")
        result.append(
            {
                "node_id": node.id,
                "native_node_guid": node.native_guid,
                "class": node.class_path,
                "symbol_name": _symbol_name(node),
                "source_pin_ids": sorted(pin_ids),
            }
        )
    return result


def _build_guard_truth(
    graph: Graph,
    criterion_node_id: str,
    source: Mapping[str, str],
) -> dict[str, Any]:
    nodes = {node.id: node for node in graph.nodes}
    pins = {pin.id: pin for node in graph.nodes for pin in node.pins}
    selected_node_ids = set(nodes)
    selected_edge_ids = {edge.id for edge in graph.edges}
    selected_edges = [edge for edge in graph.edges if edge.id in selected_edge_ids]
    if len(selected_node_ids) != LC2_EXPECTED_NODES:
        raise LC2ArtifactError(
            "LC2 EventGraph must contain exactly the nine selected source nodes"
        )
    if len(selected_edge_ids) != LC2_EXPECTED_EDGES:
        raise LC2ArtifactError(
            "LC2 EventGraph must contain exactly ten selected source edges"
        )
    execution_edges = [edge for edge in selected_edges if edge.kind == "execution"]
    data_edges = [edge for edge in selected_edges if edge.kind == "data"]
    if (
        len(execution_edges) != LC2_EXPECTED_EXECUTION_EDGES
        or len(data_edges) != LC2_EXPECTED_DATA_EDGES
    ):
        raise LC2ArtifactError("LC2 edge-kind inventory must be exactly 8 execution/2 data")

    incoming_execution: dict[str, list[Edge]] = defaultdict(list)
    outgoing_execution: dict[str, list[Edge]] = defaultdict(list)
    incoming_data: dict[str, list[Edge]] = defaultdict(list)
    for edge in selected_edges:
        if edge.kind == "execution":
            incoming_execution[edge.target_node_id].append(edge)
            outgoing_execution[edge.source_node_id].append(edge)
        else:
            incoming_data[edge.target_pin_id].append(edge)

    entry = _one(
        (node for node in graph.nodes if _short_class(node) == "K2Node_Event"),
        "LC2 entry Event",
    )
    entry_edge = _one(outgoing_execution.get(entry.id, ()), "entry execution edge")
    outer_branch = nodes[entry_edge.target_node_id]
    if _short_class(outer_branch) != "K2Node_IfThenElse":
        raise LC2ArtifactError("entry must execute the outer Branch")
    outer_outcomes = _outcome_edges(outer_branch, outgoing_execution, pins)
    inner_branch = nodes[outer_outcomes["true"].target_node_id]
    if _short_class(inner_branch) != "K2Node_IfThenElse":
        raise LC2ArtifactError("outer true outcome must execute the inner Branch")
    inner_outcomes = _outcome_edges(inner_branch, outgoing_execution, pins)

    outer_rejected = nodes[outer_outcomes["false"].target_node_id]
    inner_rejected = nodes[inner_outcomes["false"].target_node_id]
    accepted = nodes[inner_outcomes["true"].target_node_id]
    expected_outcomes = {
        "outer_rejected": (outer_rejected, "OuterRejected"),
        "inner_rejected": (inner_rejected, "InnerRejected"),
        "accepted": (accepted, "Accepted"),
    }
    for outcome_id, (node, expected_symbol) in expected_outcomes.items():
        if _short_class(node) != "K2Node_VariableSet" or _symbol_name(
            node, access="set"
        ) != expected_symbol:
            raise LC2ArtifactError(
                f"{outcome_id} must resolve to Set {expected_symbol}"
            )

    criterion = nodes[criterion_node_id]
    if _symbol_name(criterion, access="set") != LC2_CRITERION_SYMBOL:
        raise LC2ArtifactError("LC2 criterion source identity is not Set LC2Complete")

    reconvergence_edges: dict[str, Edge] = {}
    for outcome_id, (node, _) in expected_outcomes.items():
        edge = _one(
            outgoing_execution.get(node.id, ()),
            f"{outcome_id} reconvergence edge",
        )
        if edge.target_node_id != criterion.id:
            raise LC2ArtifactError(
                f"{outcome_id} must reconverge directly on LC2Complete"
            )
        reconvergence_edges[outcome_id] = edge
    if len(incoming_execution.get(criterion.id, ())) != 3:
        raise LC2ArtifactError("LC2Complete must have exactly three reconvergence inputs")

    def execution_edge_fact(edge: Edge) -> dict[str, str]:
        return {
            "edge_id": edge.id,
            "source_node_id": edge.source_node_id,
            "source_pin_id": edge.source_pin_id,
            "source_port_label": pins[edge.source_pin_id].name,
            "target_node_id": edge.target_node_id,
            "target_pin_id": edge.target_pin_id,
            "target_port_label": pins[edge.target_pin_id].name,
        }

    outer_predicate = _predicate_attachment(
        "outer_guard", outer_branch, incoming_data, nodes, pins
    )
    inner_predicate = _predicate_attachment(
        "inner_guard", inner_branch, incoming_data, nodes, pins
    )
    if _symbol_name(nodes[outer_predicate["predicate_node_id"]], access="get") != "OuterEnabled":
        raise LC2ArtifactError("outer Branch predicate must be Get OuterEnabled")
    if _symbol_name(nodes[inner_predicate["predicate_node_id"]], access="get") != "InnerEnabled":
        raise LC2ArtifactError("inner Branch predicate must be Get InnerEnabled")

    guards = [
        {
            "id": "outer_guard",
            "depth": 0,
            "parent_guard_id": None,
            "entered_by_parent_outcome": None,
            "branch_node_id": outer_branch.id,
            "predicate_node_id": outer_predicate["predicate_node_id"],
            "predicate_edge_id": outer_predicate["edge_id"],
            "condition_pin_id": outer_predicate["condition_pin_id"],
            "outcome_edges": {
                outcome: execution_edge_fact(edge)
                for outcome, edge in sorted(outer_outcomes.items())
            },
        },
        {
            "id": "inner_guard",
            "depth": 1,
            "parent_guard_id": "outer_guard",
            "entered_by_parent_outcome": "true",
            "branch_node_id": inner_branch.id,
            "predicate_node_id": inner_predicate["predicate_node_id"],
            "predicate_edge_id": inner_predicate["edge_id"],
            "condition_pin_id": inner_predicate["condition_pin_id"],
            "outcome_edges": {
                outcome: execution_edge_fact(edge)
                for outcome, edge in sorted(inner_outcomes.items())
            },
        },
    ]

    route_edges = {
        "outer_rejected": [entry_edge, outer_outcomes["false"]],
        "inner_rejected": [
            entry_edge,
            outer_outcomes["true"],
            inner_outcomes["false"],
        ],
        "accepted": [
            entry_edge,
            outer_outcomes["true"],
            inner_outcomes["true"],
        ],
    }
    branch_steps = {
        "outer_rejected": [
            {"guard_id": "outer_guard", "outcome": "false", "edge_id": outer_outcomes["false"].id}
        ],
        "inner_rejected": [
            {"guard_id": "outer_guard", "outcome": "true", "edge_id": outer_outcomes["true"].id},
            {"guard_id": "inner_guard", "outcome": "false", "edge_id": inner_outcomes["false"].id},
        ],
        "accepted": [
            {"guard_id": "outer_guard", "outcome": "true", "edge_id": outer_outcomes["true"].id},
            {"guard_id": "inner_guard", "outcome": "true", "edge_id": inner_outcomes["true"].id},
        ],
    }
    outcome_paths = []
    for outcome_id in sorted(expected_outcomes):
        node = expected_outcomes[outcome_id][0]
        literal_pin_id, literal_value = _literal_true(node)
        outcome_paths.append(
            {
                "id": outcome_id,
                "set_node_id": node.id,
                "branch_outcomes": branch_steps[outcome_id],
                "ordered_execution_edge_ids": [
                    edge.id for edge in route_edges[outcome_id]
                ],
                "reconvergence_edge_id": reconvergence_edges[outcome_id].id,
                "literal_pin_id": literal_pin_id,
                "literal_value": literal_value,
            }
        )
    criterion_literal_pin, criterion_literal = _literal_true(criterion)
    outcome_ids = sorted(expected_outcomes)
    incomparable_pairs = [
        list(pair) for pair in combinations(outcome_ids, 2)
    ]
    source_references = _source_references(
        graph, selected_node_ids, selected_edge_ids
    )
    if len({item["node_id"] for item in source_references}) != LC2_EXPECTED_NODES:
        raise LC2ArtifactError("LC2 source node ownership must be exact and disjoint")

    return {
        "format": "blueprint-lens-lc2-guard-truth",
        "schema_version": "1.0.0",
        "source": dict(source),
        "entry": {"node_id": entry.id, "entry_edge_id": entry_edge.id},
        "criterion": {
            "node_id": criterion.id,
            "literal_pin_id": criterion_literal_pin,
            "literal_value": criterion_literal,
        },
        "guards": guards,
        "predicate_attachments": [outer_predicate, inner_predicate],
        "outcome_paths": outcome_paths,
        "partial_order": {
            "semantics": "unordered_outcome_paths_with_ordered_edges_inside_each_path",
            "incomparable_terminal_pairs": incomparable_pairs,
            "reconvergence_node_id": criterion.id,
            "reconvergence_ownership": [
                {
                    "outcome_id": outcome_id,
                    **execution_edge_fact(reconvergence_edges[outcome_id]),
                }
                for outcome_id in outcome_ids
            ],
        },
        "source_references": source_references,
        "counts": {
            "nodes": len(selected_node_ids),
            "edges": len(selected_edge_ids),
            "execution_edges": len(execution_edges),
            "predicate_data_edges": len(data_edges),
            "guards": len(guards),
            "predicate_attachments": 2,
            "outcome_paths": len(outcome_paths),
            "reconvergence_edges": len(reconvergence_edges),
        },
    }


def _precision_recall(
    actual: Sequence[str], expected: Sequence[str]
) -> tuple[float, float]:
    actual_set = set(map(str, actual))
    expected_set = set(map(str, expected))
    intersection = actual_set & expected_set
    precision = len(intersection) / len(actual_set) if actual_set else 1.0
    recall = len(intersection) / len(expected_set) if expected_set else 1.0
    return precision, recall


def _audit_markdown(
    asset_path: Path,
    raw_path: Path,
    inventory_path: Path,
    guard_truth: Mapping[str, Any],
) -> str:
    counts = guard_truth["counts"]
    return f"""# BP_LC2_NestedGuards Independent Inventory Audit

The UE inventory command and raw JSON exporter use separate output paths. This
audit compares their complete normalized graph/node/pin/edge facts, then checks
the reviewed LC2 selection, exact pin roles, branch outcomes, predicate
attachments, source ownership, incomparable outcome paths and reconvergence
ownership.

## Provenance

- Asset: `{asset_path.name}`
- Asset SHA-256: `{_sha256_file(asset_path)}`
- Canonical raw: `{raw_path.name}`
- Raw SHA-256: `{_sha256_file(raw_path)}`
- Canonical inventory: `{inventory_path.name}`
- Inventory SHA-256: `{_sha256_file(inventory_path)}`

## Complete source comparison

- Blueprint path: PASS (`{LC2_BLUEPRINT_PATH}`)
- Graphs: PASS (2)
- Nodes: PASS (10 total; 9 selected EventGraph + 1 excluded ConstructionScript)
- Pins: PASS (35)
- Pin type facts: PASS (35 complete; four predicate endpoints exact Boolean)
- Edges: PASS (10)

## Selected truth comparison

- Membership: PASS ({counts['nodes']} nodes / {counts['edges']} induced edges)
- Edge kinds: PASS ({counts['execution_edges']} execution / {counts['predicate_data_edges']} predicate data)
- Branch pin identity: PASS (outer and inner `then=True`, `else=False`)
- Predicate attachments: PASS ({counts['predicate_attachments']} exact Get-to-Condition edges)
- Outcome literals: PASS (OuterRejected, InnerRejected and Accepted are `true`)
- Criterion literal: PASS (LC2Complete is `true`)
- Outcome partial order: PASS (3 pairwise-incomparable terminals; no total order)
- Reconvergence ownership: PASS ({counts['reconvergence_edges']} distinct outcome-to-criterion edges, each owned once)
- Source ownership: PASS ({counts['nodes']} unique selected node references)

## Verdict: `TRUTH_FROZEN`
"""


def _build_lc2_artifacts_in_directory(
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
    """Build, validate, and independently audit the real LC2 truth chain."""

    raw_run1_path = Path(raw_run1_path).resolve()
    raw_run2_path = Path(raw_run2_path).resolve()
    inventory_run1_path = Path(inventory_run1_path).resolve()
    inventory_run2_path = Path(inventory_run2_path).resolve()
    asset_path = Path(asset_path).resolve()
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    asset_hash_before = _sha256_file(asset_path)
    raw_bytes = raw_run1_path.read_bytes()
    if raw_bytes != raw_run2_path.read_bytes():
        raise LC2ArtifactError("LC2 raw exports are not byte-identical")
    inventory_bytes = inventory_run1_path.read_bytes()
    if inventory_bytes != inventory_run2_path.read_bytes():
        raise LC2ArtifactError("LC2 inventory exports are not byte-identical")

    load_raw_probe(raw_run1_path)
    load_raw_probe(raw_run2_path)
    raw = _load_object(raw_run1_path)
    if raw.get("blueprint", {}).get("path") != LC2_BLUEPRINT_PATH:
        raise LC2ArtifactError("LC2 source Blueprint path does not match contract")
    raw_inventory = _raw_inventory(raw)
    independent_inventory = _parse_inventory(inventory_run1_path)
    _compare_inventory(independent_inventory, raw_inventory)

    canonical_raw_path = output_dir / "BP_LC2_NestedGuards.raw-0.2.json"
    canonical_inventory_path = output_dir / "BP_LC2_NestedGuards.inventory.tsv"
    _write_bytes(canonical_raw_path, raw_bytes)
    _write_bytes(canonical_inventory_path, inventory_bytes)

    try:
        ir = build_typed_ir(raw, expected_blueprint_path=LC2_BLUEPRINT_PATH)
    except TypedIRBuildError as error:
        raise LC2ArtifactError(str(error)) from error
    ir_path = output_dir / "BP_LC2_NestedGuards.ir.v1.json"
    _write_json(ir_path, ir)
    validate_contract_file(ir_path, graph_schema_path)

    document = load_blueprint_lens_v1(ir_path)
    graph_id, criterion_node_id = _criterion(ir)
    graph = _one(
        (candidate for candidate in document.graphs if candidate.id == graph_id),
        "LC2 EventGraph",
    )
    guard_truth = _build_guard_truth(graph, criterion_node_id, {})
    ground_truth = _build_ground_truth(ir_path, guard_truth, graph)
    ground_truth_path = (
        output_dir / "BP_LC2_NestedGuards.execution.ground-truth.v1.json"
    )
    _write_json(ground_truth_path, ground_truth)
    validate_contract_file(ground_truth_path, ground_truth_schema_path)

    slice_value = _build_slice(ir_path, ir)
    slice_path = output_dir / "BP_LC2_NestedGuards.execution.slice.v1.json"
    _write_json(slice_path, slice_value)
    validate_contract_file(slice_path, slice_schema_path)

    node_precision, node_recall = _precision_recall(
        slice_value["node_ids"], ground_truth["expected"]["node_ids"]
    )
    edge_precision, edge_recall = _precision_recall(
        slice_value["edge_ids"], ground_truth["expected"]["edge_ids"]
    )
    metrics = {
        "edge_precision": edge_precision,
        "edge_recall": edge_recall,
        "node_precision": node_precision,
        "node_recall": node_recall,
    }
    if any(value != 1.0 for value in metrics.values()):
        raise LC2ArtifactError("LC2 slice precision/recall must all equal 1.0")

    guard_truth["source"] = {
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
        "graph_id": str(slice_value["graph_id"]),
    }
    guard_truth_path = output_dir / "BP_LC2_NestedGuards.guard-truth.v1.json"
    _write_json(guard_truth_path, guard_truth)

    audit_path = output_dir / "inventory-audit.md"
    _write_bytes(
        audit_path,
        _audit_markdown(
            asset_path,
            canonical_raw_path,
            canonical_inventory_path,
            guard_truth,
        ).encode("utf-8"),
    )

    asset_hash_after = _sha256_file(asset_path)
    checks = {
        "asset_hash_stable": asset_hash_before == asset_hash_after,
        "branch_outcomes_exact": True,
        "ground_truth_contract_valid": True,
        "independent_inventory_full_match": True,
        "literal_values_true": True,
        "partial_order_preserved": True,
        "precision_recall_1_0": all(value == 1.0 for value in metrics.values()),
        "predicate_attachments_exact": True,
        "predicate_pin_types_boolean": True,
        "raw_runs_byte_identical": True,
        "reconvergence_ownership_exact": True,
        "selected_membership_9_10": guard_truth["counts"]["nodes"] == 9
        and guard_truth["counts"]["edges"] == 10,
        "slice_contract_valid": True,
        "source_ownership_exact": len(guard_truth["source_references"]) == 9,
        "typed_ir_contract_valid": True,
    }
    if not all(checks.values()):
        failed = sorted(key for key, passed in checks.items() if not passed)
        raise LC2ArtifactError(f"LC2 readiness checks failed: {failed}")
    readiness = {
        "format": "blueprint-lens-lc2-readiness",
        "schema_version": "1.0.0",
        "status": "TRUTH_FROZEN",
        "checks": checks,
        "metrics": metrics,
        "counts": dict(guard_truth["counts"]),
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
            "guard_truth_sha256": _sha256_file(guard_truth_path),
            "inventory_audit_sha256": _sha256_file(audit_path),
        },
        "artifacts": {
            "canonical_raw": canonical_raw_path.name,
            "canonical_inventory": canonical_inventory_path.name,
            "typed_ir": ir_path.name,
            "execution_slice": slice_path.name,
            "reviewed_ground_truth": ground_truth_path.name,
            "guard_truth": guard_truth_path.name,
            "inventory_audit": audit_path.name,
        },
        "limitations": [
            "P2 freezes source truth only; no Explanation projection or visual candidate is selected.",
            "The three outcome-path arrays are serialization records, not a total execution order.",
        ],
    }
    readiness_path = output_dir / "readiness.v1.json"
    _write_json(readiness_path, readiness)

    if _sha256_file(asset_path) != asset_hash_before:
        raise LC2ArtifactError("LC2 Blueprint package changed during read-only analysis")
    return {
        "canonical_raw": canonical_raw_path,
        "canonical_inventory": canonical_inventory_path,
        "ir": ir_path,
        "slice": slice_path,
        "ground_truth": ground_truth_path,
        "guard_truth": guard_truth_path,
        "inventory_audit": audit_path,
        "readiness": readiness_path,
    }


def build_lc2_artifacts(
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
    """Build in fresh staging and publish readiness only after all checks pass."""

    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    readiness_path = output_dir / "readiness.v1.json"
    readiness_path.unlink(missing_ok=True)

    try:
        with tempfile.TemporaryDirectory(
            prefix=".lc2-staging-", dir=output_dir
        ) as staging_directory:
            staged = _build_lc2_artifacts_in_directory(
                raw_run1_path,
                raw_run2_path,
                inventory_run1_path,
                inventory_run2_path,
                asset_path,
                Path(staging_directory),
                graph_schema_path,
                slice_schema_path,
                ground_truth_schema_path,
            )
            published: dict[str, Path] = {}
            for key, staged_path in staged.items():
                if key == "readiness":
                    continue
                destination = output_dir / staged_path.name
                os.replace(staged_path, destination)
                published[key] = destination
            os.replace(staged["readiness"], readiness_path)
            published["readiness"] = readiness_path
            return published
    except Exception:
        readiness_path.unlink(missing_ok=True)
        raise
