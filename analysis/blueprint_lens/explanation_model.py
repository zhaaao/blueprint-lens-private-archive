"""Build and validate the presentation-independent probe explanation model."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from heapq import heappop, heappush
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence, TypeAlias

from .raw_probe import (
    BlueprintDocument,
    Edge,
    Node,
    ReconstructionError,
    load_blueprint_lens_v1,
)
from .schema_validation import SchemaValidationError, validate_instance


class ExplanationModelError(ValueError):
    """Raised when an explanation model violates its schema or semantics."""


ExplanationModel: TypeAlias = dict[str, Any]


@dataclass(frozen=True, slots=True)
class CompleteDagUnitProjection:
    """Presentation-independent classification for one selected DAG node."""

    role: str
    kind: str
    title: str
    expression: str = ""


@dataclass(frozen=True, slots=True)
class CompleteDagRelationProjection:
    """Presentation-independent classification for one selected DAG edge."""

    kind: str
    label: str


LANE_ORDER = (
    "criterion",
    "control",
    "predicate",
    "value",
    "consequence",
    "boundary",
)

GROUP_PARTIAL_ORDER_SEMANTICS = "no execution order is proven between these groups"

UNIT_SOURCE_GUIDS = {
    "unit.criterion.set-health": (
        "63180c9c-4c18-1a57-ca71-60b47521033c",
    ),
    "unit.control.event-begin-play": (
        "0a170a17-4a0e-e306-76df-ac876e3d5f01",
    ),
    "unit.control.sequence": (
        "ff067901-4f51-b8c2-4b11-cda9a6bde21e",
    ),
    "unit.control.branch": (
        "cbbd5a81-42fa-5ea1-6363-c09cebb1efb2",
    ),
    "unit.predicate.health-positive": (
        "b65b6930-4042-ba18-ea89-79b906d351ec",
        "d609619c-44bc-d2f0-ef2a-aba7f13d1ea7",
    ),
    "unit.value.health-minus-ten": (
        "fd6fa7a3-4434-6992-629e-15bf05340f5a",
        "f84a6e45-473e-5019-1800-7f884b5e7b80",
    ),
}

UNIT_COPY = {
    "unit.criterion.set-health": ("criterion", "node", "Set Health", ""),
    "unit.control.event-begin-play": (
        "control",
        "node",
        "Event BeginPlay",
        "",
    ),
    "unit.control.sequence": ("control", "node", "Sequence", ""),
    "unit.control.branch": ("control", "node", "Branch", ""),
    "unit.predicate.health-positive": (
        "predicate",
        "expression",
        "Health is positive",
        "Health > 0",
    ),
    "unit.value.health-minus-ten": (
        "value",
        "expression",
        "Assigned value",
        "Health - 10",
    ),
}

RELATION_RULES = (
    (
        "relation.execution.begin-play-to-sequence",
        "unit.control.event-begin-play",
        "unit.control.sequence",
        "execution_predecessor",
        "then",
    ),
    (
        "relation.execution.sequence-to-branch",
        "unit.control.sequence",
        "unit.control.branch",
        "execution_predecessor",
        "then 0",
    ),
    (
        "relation.control.branch-to-set-health",
        "unit.control.branch",
        "unit.criterion.set-health",
        "controls_execution",
        "true branch",
    ),
    (
        "relation.predicate.health-positive-to-branch",
        "unit.predicate.health-positive",
        "unit.control.branch",
        "predicate_for",
        "condition",
    ),
    (
        "relation.value.health-minus-ten-to-set-health",
        "unit.value.health-minus-ten",
        "unit.criterion.set-health",
        "provides_value",
        "Health input",
    ),
)

_QUERY = (
    "Why does Set Health execute, and where does its assigned value come from?"
)
_EMPTY_PREDICATE_MESSAGE = "No predicate facts in this explanation"
_EMPTY_VALUE_MESSAGE = "No value facts in this explanation"
_CONSEQUENCE_MESSAGE = "Not enabled in this backward-only query"
_BOUNDARY_MESSAGE = "All selected constructs supported"
_SEMANTIC_STATUS_ORDER = {
    "supported": 0,
    "opaque": 1,
    "uncertain": 2,
    "unsupported": 3,
}
_RELATION_EDGE_KIND = {
    "execution_predecessor": "execution",
    "controls_execution": "execution",
    "predicate_for": "data",
    "provides_value": "data",
}

# v1.1 semantic extension. Every table below is frozen: an unlisted combination
# is a validation failure, never a fallback. See
# Explanation semantic-extension contract v1.
_SEMANTIC_LABEL_BY_PORT = {
    ("controls_execution", "then"): "condition_true",
    ("controls_execution", "else"): "condition_false",
}
_SEMANTIC_LABEL_ANY_PORT = {
    "execution_predecessor": "next_execution",
    "predicate_for": "branch_condition",
    "provides_value": "value_input",
}
# `port_label` is always the exported *source* pin. For `predicate_for` that is
# the producer's variable pin, so the meaning is carried by the consumed target
# port instead and is required explicitly.
_REQUIRED_TARGET_PORT = {"predicate_for": "Condition"}
_DISAMBIGUATOR_RULES = {"unit.branch.from_predicate_for"}
_ORDERED_EXECUTION_KINDS = {"execution_predecessor", "controls_execution"}


def _read_json(path: Path, label: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ExplanationModelError(f"cannot read {label} {path}: {error}") from error


def _require_mapping(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise ExplanationModelError(f"{label} must be an object")
    return value


def _require_string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not all(
        isinstance(item, str) for item in value
    ):
        raise ExplanationModelError(f"{label} must be an array of strings")
    return value


def _sha256(path: Path, label: str) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest().upper()
    except OSError as error:
        raise ExplanationModelError(f"cannot read {label} {path}: {error}") from error


def _require_sha256(path: Path, declared: object, label: str) -> None:
    actual = _sha256(path, label)
    if declared != actual:
        raise ExplanationModelError(
            f"{label} SHA-256 mismatch: declared {declared}, actual {actual}"
        )


def _load_document(path: Path) -> BlueprintDocument:
    try:
        return load_blueprint_lens_v1(path)
    except ReconstructionError as error:
        raise ExplanationModelError(str(error)) from error


def _graph_and_slice_entities(
    document: BlueprintDocument,
    slice_value: Mapping[str, Any],
) -> tuple[
    Mapping[str, Node],
    Mapping[str, Edge],
    list[str],
    list[str],
]:
    graph_id = slice_value.get("graph_id")
    graph = next(
        (candidate for candidate in document.graphs if candidate.id == graph_id),
        None,
    )
    if graph is None:
        raise ExplanationModelError(f"slice graph does not resolve in IR: {graph_id}")

    node_ids = _require_string_list(slice_value.get("node_ids"), "slice.node_ids")
    edge_ids = _require_string_list(slice_value.get("edge_ids"), "slice.edge_ids")
    all_nodes = {node.id: node for node in graph.nodes}
    all_edges = {edge.id: edge for edge in graph.edges}
    missing_nodes = sorted(set(node_ids) - set(all_nodes))
    missing_edges = sorted(set(edge_ids) - set(all_edges))
    if missing_nodes:
        raise ExplanationModelError(
            f"slice node identities do not resolve in IR: {missing_nodes}"
        )
    if missing_edges:
        raise ExplanationModelError(
            f"slice edge identities do not resolve in IR: {missing_edges}"
        )
    return (
        {node_id: all_nodes[node_id] for node_id in node_ids},
        {edge_id: all_edges[edge_id] for edge_id in edge_ids},
        node_ids,
        edge_ids,
    )


def _nodes_by_guid(nodes: Mapping[str, Node]) -> Mapping[str, Node]:
    result: dict[str, Node] = {}
    for node in nodes.values():
        if node.native_guid in result:
            raise ExplanationModelError(
                f"selected native GUID is ambiguous: {node.native_guid}"
            )
        result[node.native_guid] = node
    return result


def _stable_union(values: Iterable[Iterable[str]]) -> list[str]:
    seen: set[str] = set()
    result: list[str] = []
    for group in values:
        for value in group:
            if value not in seen:
                seen.add(value)
                result.append(value)
    return result


def _unit_status(nodes: Iterable[Node]) -> str:
    statuses = [node.semantic_status for node in nodes]
    unknown = sorted(set(statuses) - set(_SEMANTIC_STATUS_ORDER))
    if unknown:
        raise ExplanationModelError(f"unknown semantic statuses: {unknown}")
    if not statuses:
        raise ExplanationModelError("unit must resolve at least one source node")
    return max(statuses, key=_SEMANTIC_STATUS_ORDER.__getitem__)


def _selected_source_pins(
    edges: Mapping[str, Edge],
    edge_order: Iterable[str],
) -> Mapping[str, list[str]]:
    pins: dict[str, list[str]] = {}
    for edge_id in edge_order:
        edge = edges[edge_id]
        for node_id, pin_id in (
            (edge.source_node_id, edge.source_pin_id),
            (edge.target_node_id, edge.target_pin_id),
        ):
            node_pins = pins.setdefault(node_id, [])
            if pin_id not in node_pins:
                node_pins.append(pin_id)
    return pins


def _slice_reasons(
    slice_value: Mapping[str, Any],
    node_id: str,
) -> list[str]:
    reasons_value = _require_mapping(
        slice_value.get("inclusion_reasons"),
        "slice.inclusion_reasons",
    )
    return _require_string_list(
        reasons_value.get(node_id),
        f"slice.inclusion_reasons[{node_id!r}]",
    )


def _classify_relations(
    edges: Mapping[str, Edge],
    edge_order: list[str],
    unit_node_ids: Mapping[str, set[str]],
) -> list[dict[str, Any]]:
    owned_by_rule: list[list[str]] = [[] for _ in RELATION_RULES]
    for edge_id in edge_order:
        edge = edges[edge_id]
        owners: list[int] = []
        for index, (_, source_unit_id, target_unit_id, kind, _) in enumerate(
            RELATION_RULES
        ):
            if edge.kind != _RELATION_EDGE_KIND[kind]:
                continue
            participating = (
                unit_node_ids[source_unit_id] | unit_node_ids[target_unit_id]
            )
            if (
                edge.source_node_id in participating
                and edge.target_node_id in participating
            ):
                owners.append(index)
        if len(owners) != 1:
            raise ExplanationModelError(
                f"selected edge must be owned once, got {len(owners)}: {edge_id}"
            )
        owned_by_rule[owners[0]].append(edge_id)

    if any(not edge_ids for edge_ids in owned_by_rule):
        missing = [
            RELATION_RULES[index][0]
            for index, edge_ids in enumerate(owned_by_rule)
            if not edge_ids
        ]
        raise ExplanationModelError(
            f"relation rules did not own selected edges: {missing}"
        )

    return [
        {
            "id": relation_id,
            "source_unit_id": source_unit_id,
            "target_unit_id": target_unit_id,
            "kind": kind,
            "label": label,
            "source_edge_ids": owned_by_rule[index],
        }
        for index, (
            relation_id,
            source_unit_id,
            target_unit_id,
            kind,
            label,
        ) in enumerate(RELATION_RULES)
    ]


def build_probe_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
) -> dict[str, Any]:
    """Build the fixed six-lane explanation for the canonical probe slice."""

    ir_path = Path(ir_path)
    slice_path = Path(slice_path)
    asset_file = Path(asset_file)
    document = _load_document(ir_path)
    slice_value = _require_mapping(_read_json(slice_path, "slice"), "slice")
    nodes, edges, node_order, edge_order = _graph_and_slice_entities(
        document, slice_value
    )
    nodes_by_guid = _nodes_by_guid(nodes)
    selected_pins = _selected_source_pins(edges, edge_order)

    expected_guids = {
        guid for unit_guids in UNIT_SOURCE_GUIDS.values() for guid in unit_guids
    }
    if set(nodes_by_guid) != expected_guids:
        raise ExplanationModelError(
            "fixed unit source GUIDs must partition the selected slice nodes"
        )

    units: list[dict[str, Any]] = []
    unit_node_ids: dict[str, set[str]] = {}
    for unit_id, guids in UNIT_SOURCE_GUIDS.items():
        role, kind, fixed_title, expression = UNIT_COPY[unit_id]
        source_nodes = [nodes_by_guid[guid] for guid in guids]
        primary_guid = guids[-1] if kind == "expression" else guids[0]
        source_references = [
            {
                "blueprint_asset_path": document.blueprint_path,
                "graph_id": node.graph_id,
                "source_node_id": node.id,
                "native_node_guid": node.native_guid,
                "source_pin_ids": selected_pins.get(node.id, []),
                "primary": node.native_guid == primary_guid,
            }
            for node in source_nodes
        ]
        inclusion_reasons = _stable_union(
            _slice_reasons(slice_value, node.id) for node in source_nodes
        )
        title = fixed_title or next(
            (node.title for node in source_nodes if node.title),
            unit_id,
        )
        units.append(
            {
                "id": unit_id,
                "role": role,
                "kind": kind,
                "title": title,
                "expression": expression,
                "semantic_status": _unit_status(source_nodes),
                "inclusion_reasons": inclusion_reasons,
                "source_references": source_references,
            }
        )
        unit_node_ids[unit_id] = {node.id for node in source_nodes}

    relations = _classify_relations(edges, edge_order, unit_node_ids)
    lane_units = {
        role: [unit["id"] for unit in units if unit["role"] == role]
        for role in LANE_ORDER
    }
    lanes = [
        {
            "role": role,
            "state": (
                "not_enabled"
                if role == "consequence"
                else "empty"
                if role == "boundary"
                else "populated"
            ),
            "unit_ids": lane_units[role],
            "empty_message": (
                _CONSEQUENCE_MESSAGE
                if role == "consequence"
                else _BOUNDARY_MESSAGE
                if role == "boundary"
                else ""
            ),
        }
        for role in LANE_ORDER
    ]

    criterion = _require_mapping(slice_value.get("criterion"), "slice.criterion")
    criterion_node_id = criterion.get("node_id")
    if not isinstance(criterion_node_id, str) or not criterion_node_id:
        raise ExplanationModelError("slice.criterion.node_id must be a nonempty string")

    return {
        "format": "blueprint-lens-explanation",
        "schema_version": "1.0.0",
        "rules_version": str(slice_value.get("rules_version", "")),
        "source": {
            "ir_path": ir_path.as_posix(),
            "ir_sha256": _sha256(ir_path, "IR"),
            "slice_path": slice_path.as_posix(),
            "slice_sha256": _sha256(slice_path, "slice"),
            "blueprint_asset_path": document.blueprint_path,
            "blueprint_package_sha256": _sha256(
                asset_file, "Blueprint package"
            ),
            "graph_id": str(slice_value.get("graph_id", "")),
        },
        "query": {
            "question": _QUERY,
            "direction": "backward_only",
            "criterion_source_node_id": criterion_node_id,
        },
        "criterion_unit_id": "unit.criterion.set-health",
        "lanes": lanes,
        "units": units,
        "relations": relations,
        "counts": {
            "lanes": len(lanes),
            "units": len(units),
            "relations": len(relations),
            "source_nodes": len(node_order),
            "source_edges": len(edge_order),
        },
    }


def _linear_execution_order(
    nodes: Mapping[str, Node],
    edges: Mapping[str, Edge],
    edge_order: list[str],
    criterion_node_id: str,
) -> tuple[list[str], list[str]]:
    if criterion_node_id not in nodes:
        raise ExplanationModelError(
            "linear projection criterion does not resolve in selected nodes"
        )
    if len(nodes) < 3:
        raise ExplanationModelError(
            "linear projection requires entry, intermediate, and criterion nodes"
        )

    non_supported = sorted(
        node.id
        for node in nodes.values()
        if node.semantic_status != "supported"
    )
    if non_supported:
        raise ExplanationModelError(
            "linear projection requires every selected node to be supported: "
            f"{non_supported}"
        )

    non_execution = sorted(
        edge_id for edge_id, edge in edges.items() if edge.kind != "execution"
    )
    if non_execution:
        raise ExplanationModelError(
            "linear projection does not accept data attachments: "
            f"{non_execution}"
        )

    incoming: dict[str, list[str]] = {node_id: [] for node_id in nodes}
    outgoing: dict[str, list[str]] = {node_id: [] for node_id in nodes}
    for edge_id in edge_order:
        edge = edges[edge_id]
        incoming[edge.target_node_id].append(edge_id)
        outgoing[edge.source_node_id].append(edge_id)

    fan_out = sorted(
        node_id for node_id, edge_ids in outgoing.items() if len(edge_ids) > 1
    )
    if fan_out:
        raise ExplanationModelError(
            f"linear projection does not accept fan-out: {fan_out}"
        )
    reconvergence = sorted(
        node_id for node_id, edge_ids in incoming.items() if len(edge_ids) > 1
    )
    if reconvergence:
        raise ExplanationModelError(
            "linear projection does not accept reconvergence: "
            f"{reconvergence}"
        )

    entries = sorted(
        node_id for node_id, edge_ids in incoming.items() if not edge_ids
    )
    if len(entries) != 1:
        raise ExplanationModelError(
            f"linear projection requires one entry, found {len(entries)}"
        )
    if outgoing[criterion_node_id]:
        raise ExplanationModelError(
            "linear projection criterion must terminate the selected chain"
        )

    ordered_nodes: list[str] = []
    ordered_edges: list[str] = []
    visited: set[str] = set()
    current = entries[0]
    while True:
        if current in visited:
            raise ExplanationModelError("linear projection contains a cycle")
        visited.add(current)
        ordered_nodes.append(current)
        next_edges = outgoing[current]
        if not next_edges:
            break
        edge_id = next_edges[0]
        ordered_edges.append(edge_id)
        current = edges[edge_id].target_node_id

    if current != criterion_node_id:
        raise ExplanationModelError(
            "linear projection chain does not terminate at the criterion"
        )
    if set(ordered_nodes) != set(nodes) or set(ordered_edges) != set(edges):
        raise ExplanationModelError(
            "linear projection requires one connected chain covering every "
            "selected node and edge"
        )
    return ordered_nodes, ordered_edges


def _linear_unit_id(node: Node, criterion_node_id: str) -> str:
    role = "criterion" if node.id == criterion_node_id else "control"
    return f"unit.{role}.{node.native_guid.lower()}"


def build_linear_execution_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    *,
    question: str | None = None,
) -> dict[str, Any]:
    """Project one complete supported execution chain without guessing facts."""

    ir_path = Path(ir_path)
    slice_path = Path(slice_path)
    asset_file = Path(asset_file)
    document = _load_document(ir_path)
    slice_value = _require_mapping(_read_json(slice_path, "slice"), "slice")
    nodes, edges, node_order, edge_order = _graph_and_slice_entities(
        document, slice_value
    )
    criterion = _require_mapping(slice_value.get("criterion"), "slice.criterion")
    criterion_node_id = criterion.get("node_id")
    if not isinstance(criterion_node_id, str) or not criterion_node_id:
        raise ExplanationModelError(
            "slice.criterion.node_id must be a nonempty string"
        )

    ordered_node_ids, ordered_edge_ids = _linear_execution_order(
        nodes,
        edges,
        edge_order,
        criterion_node_id,
    )
    selected_pins = _selected_source_pins(edges, ordered_edge_ids)
    unit_ids_by_node = {
        node_id: _linear_unit_id(nodes[node_id], criterion_node_id)
        for node_id in ordered_node_ids
    }

    units = []
    for node_id in ordered_node_ids:
        node = nodes[node_id]
        role = "criterion" if node_id == criterion_node_id else "control"
        units.append(
            {
                "id": unit_ids_by_node[node_id],
                "role": role,
                "kind": "node",
                "title": node.title or node.native_guid,
                "expression": "",
                "semantic_status": node.semantic_status,
                "inclusion_reasons": _slice_reasons(slice_value, node_id),
                "source_references": [
                    {
                        "blueprint_asset_path": document.blueprint_path,
                        "graph_id": node.graph_id,
                        "source_node_id": node.id,
                        "native_node_guid": node.native_guid,
                        "source_pin_ids": selected_pins.get(node.id, []),
                        "primary": True,
                    }
                ],
            }
        )

    relations = []
    for edge_id in ordered_edge_ids:
        edge = edges[edge_id]
        source_node = nodes[edge.source_node_id]
        source_pin = next(
            (pin for pin in source_node.pins if pin.id == edge.source_pin_id),
            None,
        )
        if source_pin is None:
            raise ExplanationModelError(
                f"linear projection source pin does not resolve: "
                f"{edge.source_pin_id}"
            )
        relation_suffix = hashlib.sha256(edge.id.encode("utf-8")).hexdigest()[:16]
        relations.append(
            {
                "id": f"relation.execution.{relation_suffix}",
                "source_unit_id": unit_ids_by_node[edge.source_node_id],
                "target_unit_id": unit_ids_by_node[edge.target_node_id],
                "kind": "execution_predecessor",
                "label": source_pin.name or "then",
                "source_edge_ids": [edge.id],
            }
        )

    criterion_unit_id = unit_ids_by_node[criterion_node_id]
    criterion_title = nodes[criterion_node_id].title or "criterion"
    lane_units = {
        "criterion": [criterion_unit_id],
        "control": [
            unit_ids_by_node[node_id]
            for node_id in ordered_node_ids
            if node_id != criterion_node_id
        ],
    }
    lanes = [
        {
            "role": "criterion",
            "state": "populated",
            "unit_ids": lane_units["criterion"],
            "empty_message": "",
        },
        {
            "role": "control",
            "state": "populated",
            "unit_ids": lane_units["control"],
            "empty_message": "",
        },
        {
            "role": "predicate",
            "state": "empty",
            "unit_ids": [],
            "empty_message": _EMPTY_PREDICATE_MESSAGE,
        },
        {
            "role": "value",
            "state": "empty",
            "unit_ids": [],
            "empty_message": _EMPTY_VALUE_MESSAGE,
        },
        {
            "role": "consequence",
            "state": "not_enabled",
            "unit_ids": [],
            "empty_message": _CONSEQUENCE_MESSAGE,
        },
        {
            "role": "boundary",
            "state": "empty",
            "unit_ids": [],
            "empty_message": _BOUNDARY_MESSAGE,
        },
    ]

    return {
        "format": "blueprint-lens-explanation",
        "schema_version": "1.0.0",
        "rules_version": str(slice_value.get("rules_version", "")),
        "source": {
            "ir_path": ir_path.as_posix(),
            "ir_sha256": _sha256(ir_path, "IR"),
            "slice_path": slice_path.as_posix(),
            "slice_sha256": _sha256(slice_path, "slice"),
            "blueprint_asset_path": document.blueprint_path,
            "blueprint_package_sha256": _sha256(
                asset_file, "Blueprint package"
            ),
            "graph_id": str(slice_value.get("graph_id", "")),
        },
        "query": {
            "question": question or f"Why does {criterion_title} execute?",
            "direction": "backward_only",
            "criterion_source_node_id": criterion_node_id,
        },
        "criterion_unit_id": criterion_unit_id,
        "lanes": lanes,
        "units": units,
        "relations": relations,
        "counts": {
            "lanes": len(lanes),
            "units": len(node_order),
            "relations": len(edge_order),
            "source_nodes": len(node_order),
            "source_edges": len(edge_order),
        },
    }


def _complete_dag_order(
    nodes: Mapping[str, Node],
    edges: Mapping[str, Edge],
    criterion_node_ids: Sequence[str],
) -> None:
    """Require one acyclic selected closure reaching a criterion write."""

    outgoing: dict[str, list[str]] = {node_id: [] for node_id in nodes}
    incoming_count = {node_id: 0 for node_id in nodes}
    reverse: dict[str, list[str]] = {node_id: [] for node_id in nodes}
    for edge in edges.values():
        outgoing[edge.source_node_id].append(edge.target_node_id)
        reverse[edge.target_node_id].append(edge.source_node_id)
        incoming_count[edge.target_node_id] += 1
    for values in outgoing.values():
        values.sort()
    for values in reverse.values():
        values.sort()

    ready: list[str] = []
    for node_id, count in incoming_count.items():
        if count == 0:
            heappush(ready, node_id)
    visited_count = 0
    while ready:
        node_id = heappop(ready)
        visited_count += 1
        for target_node_id in outgoing[node_id]:
            incoming_count[target_node_id] -= 1
            if incoming_count[target_node_id] == 0:
                heappush(ready, target_node_id)
    if visited_count != len(nodes):
        raise ExplanationModelError("complete DAG projection contains a cycle")

    _require_complete_graph_reachability(nodes, edges, criterion_node_ids)


def _require_complete_graph_reachability(
    nodes: Mapping[str, Node],
    edges: Mapping[str, Edge],
    criterion_node_ids: Sequence[str],
) -> None:
    """Require every selected node to reach a criterion, allowing cycles."""

    reverse: dict[str, list[str]] = {node_id: [] for node_id in nodes}
    for edge in edges.values():
        reverse[edge.target_node_id].append(edge.source_node_id)
    for values in reverse.values():
        values.sort()
    reaches_criterion = set(criterion_node_ids)
    frontier = list(criterion_node_ids)
    while frontier:
        target_node_id = frontier.pop()
        for source_node_id in reverse[target_node_id]:
            if source_node_id not in reaches_criterion:
                reaches_criterion.add(source_node_id)
                frontier.append(source_node_id)
    missing = sorted(set(nodes) - reaches_criterion)
    if missing:
        raise ExplanationModelError(
            "complete DAG projection requires every selected node to reach "
            f"a criterion: {missing}"
        )


def _slice_criterion_node_ids(
    slice_value: Mapping[str, Any],
    nodes: Mapping[str, Node],
) -> tuple[str, frozenset[str]]:
    """Resolve criterion membership and the deterministic v1 render anchor."""

    criterion = _require_mapping(slice_value.get("criterion"), "slice.criterion")
    criterion_node_id = criterion.get("node_id")
    if isinstance(criterion_node_id, str) and criterion_node_id:
        return criterion_node_id, frozenset({criterion_node_id})

    member_guid = criterion.get("member_guid")
    member_name = criterion.get("member_name")
    if (
        slice_value.get("slice_kind") != "member_variable_data_dependency"
        or not isinstance(member_guid, str)
        or not member_guid
        or not isinstance(member_name, str)
        or not member_name
    ):
        raise ExplanationModelError(
            "slice.criterion.node_id must be a nonempty string"
        )
    member_matches = frozenset(
        node_id
        for node_id, node in nodes.items()
        if node.symbol
        and node.symbol.get("kind") == "variable"
        and node.symbol.get("guid") == member_guid
        and node.symbol.get("name") == member_name
        and node.symbol.get("access") == "set"
    )
    if not member_matches:
        raise ExplanationModelError(
            "member slice criterion must resolve at least one Set node by "
            "member GUID and name"
        )
    # Explanation v1 retains singular criterion fields for compatibility.
    # They name a deterministic rendering anchor; criterion membership is the
    # complete Set collection carried by the criterion lane and unit roles.
    return min(member_matches), member_matches


def _complete_dag_unit_id(node: Node, role: str) -> str:
    return f"unit.{role}.{node.native_guid.lower()}"


def _build_complete_graph_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    unit_projections: Mapping[str, CompleteDagUnitProjection],
    relation_projections: Mapping[str, CompleteDagRelationProjection],
    *,
    question: str,
    require_acyclic: bool,
    groups: Sequence[Mapping[str, Any]] | None,
) -> dict[str, Any]:
    """Project one complete node/relation cover from adapter-classified facts."""

    ir_path = Path(ir_path)
    slice_path = Path(slice_path)
    asset_file = Path(asset_file)
    document = _load_document(ir_path)
    slice_value = _require_mapping(_read_json(slice_path, "slice"), "slice")
    nodes, edges, _, _ = _graph_and_slice_entities(document, slice_value)
    criterion_node_id, criterion_node_ids = _slice_criterion_node_ids(
        slice_value, nodes
    )
    if set(unit_projections) != set(nodes):
        missing = sorted(set(nodes) - set(unit_projections))
        extra = sorted(set(unit_projections) - set(nodes))
        raise ExplanationModelError(
            "complete DAG unit classifications must bijectively match "
            f"selected nodes: missing={missing} extra={extra}"
        )
    if set(relation_projections) != set(edges):
        missing = sorted(set(edges) - set(relation_projections))
        extra = sorted(set(relation_projections) - set(edges))
        raise ExplanationModelError(
            "complete DAG relation classifications must bijectively match "
            f"selected edges: missing={missing} extra={extra}"
        )
    if require_acyclic:
        _complete_dag_order(nodes, edges, sorted(criterion_node_ids))
    else:
        _require_complete_graph_reachability(
            nodes, edges, sorted(criterion_node_ids)
        )
    _nodes_by_guid(nodes)

    allowed_kinds = {"node", "expression", "summary"}
    criterion_projection_ids = []
    for node_id, projection in unit_projections.items():
        if projection.role not in LANE_ORDER:
            raise ExplanationModelError(
                f"complete DAG unit has unknown role: {node_id}={projection.role}"
            )
        if projection.kind not in allowed_kinds:
            raise ExplanationModelError(
                f"complete DAG unit has unknown kind: {node_id}={projection.kind}"
            )
        if not projection.title:
            raise ExplanationModelError(
                f"complete DAG unit title must not be empty: {node_id}"
            )
        if projection.role == "criterion":
            criterion_projection_ids.append(node_id)
    if set(criterion_projection_ids) != set(criterion_node_ids):
        raise ExplanationModelError(
            "complete DAG must classify every and only member Set criterion"
        )

    sorted_edge_ids = sorted(edges)
    selected_pins = _selected_source_pins(edges, sorted_edge_ids)
    unit_ids_by_node = {
        node_id: _complete_dag_unit_id(nodes[node_id], projection.role)
        for node_id, projection in unit_projections.items()
    }
    sorted_node_ids = sorted(
        nodes,
        key=lambda node_id: (
            LANE_ORDER.index(unit_projections[node_id].role),
            unit_ids_by_node[node_id],
        ),
    )

    units: list[dict[str, Any]] = []
    for node_id in sorted_node_ids:
        node = nodes[node_id]
        projection = unit_projections[node_id]
        units.append(
            {
                "id": unit_ids_by_node[node_id],
                "role": projection.role,
                "kind": projection.kind,
                "title": projection.title,
                "expression": projection.expression,
                "semantic_status": node.semantic_status,
                "inclusion_reasons": sorted(
                    set(_slice_reasons(slice_value, node_id))
                ),
                "source_references": [
                    {
                        "blueprint_asset_path": document.blueprint_path,
                        "graph_id": node.graph_id,
                        "source_node_id": node.id,
                        "native_node_guid": node.native_guid,
                        "source_pin_ids": sorted(selected_pins.get(node.id, [])),
                        "primary": True,
                    }
                ],
            }
        )

    pins_by_id = {
        pin.id: pin for node in nodes.values() for pin in node.pins
    }
    relations: list[dict[str, Any]] = []
    for edge_id in sorted_edge_ids:
        edge = edges[edge_id]
        projection = relation_projections[edge_id]
        expected_edge_kind = _RELATION_EDGE_KIND.get(projection.kind)
        if expected_edge_kind != edge.kind:
            raise ExplanationModelError(
                "complete DAG relation kind disagrees with selected edge: "
                f"{edge_id}"
            )
        if not projection.label:
            raise ExplanationModelError(
                f"complete DAG relation label must not be empty: {edge_id}"
            )
        source_pin = pins_by_id.get(edge.source_pin_id)
        target_pin = pins_by_id.get(edge.target_pin_id)
        if source_pin is None or target_pin is None:
            raise ExplanationModelError(
                f"complete DAG relation pin does not resolve: {edge_id}"
            )
        relation_suffix = hashlib.sha256(edge.id.encode("utf-8")).hexdigest()[:16]
        relations.append(
            {
                "id": f"relation.{projection.kind}.{relation_suffix}",
                "source_unit_id": unit_ids_by_node[edge.source_node_id],
                "target_unit_id": unit_ids_by_node[edge.target_node_id],
                "kind": projection.kind,
                "label": projection.label,
                "source_edge_ids": [edge.id],
                "source_edge_endpoints": [
                    {
                        "source_edge_id": edge.id,
                        "source_node_id": edge.source_node_id,
                        "source_pin_id": edge.source_pin_id,
                        "source_port_label": source_pin.name,
                        "target_node_id": edge.target_node_id,
                        "target_pin_id": edge.target_pin_id,
                        "target_port_label": target_pin.name,
                    }
                ],
            }
        )

    lane_units = {
        role: [unit["id"] for unit in units if unit["role"] == role]
        for role in LANE_ORDER
    }
    lanes = []
    for role in LANE_ORDER:
        unit_ids = lane_units[role]
        if unit_ids:
            state = "populated"
            empty_message = ""
        elif role == "consequence":
            state = "not_enabled"
            empty_message = _CONSEQUENCE_MESSAGE
        elif role == "predicate":
            state = "empty"
            empty_message = _EMPTY_PREDICATE_MESSAGE
        elif role == "value":
            state = "empty"
            empty_message = _EMPTY_VALUE_MESSAGE
        elif role == "boundary":
            state = "empty"
            empty_message = _BOUNDARY_MESSAGE
        else:
            raise ExplanationModelError(
                f"complete DAG requires a populated {role} lane"
            )
        lanes.append(
            {
                "role": role,
                "state": state,
                "unit_ids": unit_ids,
                "empty_message": empty_message,
            }
        )

    model: dict[str, Any] = {
        "format": "blueprint-lens-explanation",
        "schema_version": "1.0.0",
        "rules_version": str(slice_value.get("rules_version", "")),
        "source": {
            "ir_path": ir_path.as_posix(),
            "ir_sha256": _sha256(ir_path, "IR"),
            "slice_path": slice_path.as_posix(),
            "slice_sha256": _sha256(slice_path, "slice"),
            "blueprint_asset_path": document.blueprint_path,
            "blueprint_package_sha256": _sha256(
                asset_file, "Blueprint package"
            ),
            "graph_id": str(slice_value.get("graph_id", "")),
        },
        "query": {
            "question": question,
            "direction": "backward_only",
            "criterion_source_node_id": criterion_node_id,
        },
        "criterion_unit_id": unit_ids_by_node[criterion_node_id],
        "lanes": lanes,
        "units": units,
        "relations": relations,
        "counts": {
            "lanes": len(lanes),
            "units": len(units),
            "relations": len(relations),
            "source_nodes": len(nodes),
            "source_edges": len(edges),
        },
    }
    if groups is not None:
        if not groups:
            raise ExplanationModelError(
                "complete cyclic projection requires at least one group"
            )
        copied_groups = [dict(group) for group in groups]
        if any(not isinstance(group.get("id"), str) or not group["id"] for group in copied_groups):
            raise ExplanationModelError(
                "complete cyclic projection group ID must be a nonempty string"
            )
        model["groups"] = sorted(copied_groups, key=lambda group: group["id"])
        try:
            _validate_groups(model)
        except KeyError as error:
            raise ExplanationModelError(
                f"complete cyclic projection group field is missing: {error.args[0]}"
            ) from error
    return model


def build_complete_dag_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    unit_projections: Mapping[str, CompleteDagUnitProjection],
    relation_projections: Mapping[str, CompleteDagRelationProjection],
    *,
    question: str,
) -> dict[str, Any]:
    """Project a complete one-node-per-unit DAG from adapter-classified facts."""

    return _build_complete_graph_explanation(
        ir_path,
        slice_path,
        asset_file,
        unit_projections,
        relation_projections,
        question=question,
        require_acyclic=True,
        groups=None,
    )


def build_complete_cyclic_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    unit_projections: Mapping[str, CompleteDagUnitProjection],
    relation_projections: Mapping[str, CompleteDagRelationProjection],
    *,
    question: str,
    groups: Sequence[Mapping[str, Any]],
) -> dict[str, Any]:
    """Project a complete graph while retaining criterion reachability."""

    return _build_complete_graph_explanation(
        ir_path,
        slice_path,
        asset_file,
        unit_projections,
        relation_projections,
        question=question,
        require_acyclic=False,
        groups=groups,
    )


def _validate_relation_labels(model: Mapping[str, Any]) -> None:
    """Require port labels to be source facts and semantic labels to be admissible."""

    for relation in model["relations"]:
        has_port = "port_label" in relation
        has_semantic = "semantic_label" in relation
        if not has_port and not has_semantic:
            continue
        if has_semantic and not has_port:
            raise ExplanationModelError(
                f"relation semantic_label requires port_label: {relation['id']}"
            )
        endpoints = relation.get("source_edge_endpoints")
        if not endpoints:
            raise ExplanationModelError(
                "relation port_label requires source_edge_endpoints: "
                f"{relation['id']}"
            )
        port_label = relation["port_label"]
        for endpoint in endpoints:
            if endpoint["source_port_label"] != port_label:
                raise ExplanationModelError(
                    "relation port_label disagrees with endpoint provenance: "
                    f"{relation['id']}"
                )
        if not has_semantic:
            continue
        kind = relation["kind"]
        required_target = _REQUIRED_TARGET_PORT.get(kind)
        if required_target is not None and any(
            endpoint["target_port_label"] != required_target
            for endpoint in endpoints
        ):
            raise ExplanationModelError(
                f"relation {kind} must consume the {required_target!r} port: "
                f"{relation['id']}"
            )
        expected = _SEMANTIC_LABEL_ANY_PORT.get(kind)
        if expected is None:
            expected = _SEMANTIC_LABEL_BY_PORT.get((kind, port_label))
        if expected is None:
            raise ExplanationModelError(
                f"relation ({kind}, {port_label!r}) has no admissible semantic "
                f"label: {relation['id']}"
            )
        if relation["semantic_label"] != expected:
            raise ExplanationModelError(
                f"relation semantic_label must be {expected!r}: {relation['id']}"
            )


def _validate_disambiguators(model: Mapping[str, Any]) -> None:
    """Require every disambiguator to be derived, not asserted."""

    relation_ids = {relation["id"] for relation in model["relations"]}
    predicate_for_by_target: dict[str, list[Mapping[str, Any]]] = {}
    for relation in model["relations"]:
        if relation["kind"] == "predicate_for":
            predicate_for_by_target.setdefault(
                relation["target_unit_id"], []
            ).append(relation)

    for unit in model["units"]:
        disambiguator = unit.get("disambiguator")
        if disambiguator is None:
            continue
        if disambiguator["rule_id"] not in _DISAMBIGUATOR_RULES:
            raise ExplanationModelError(
                f"unknown disambiguator rule: {disambiguator['rule_id']}"
            )
        if (
            disambiguator["rule_id"] == "unit.branch.from_predicate_for"
            and unit["role"] != "control"
        ):
            raise ExplanationModelError(
                "disambiguator rule requires control role: "
                f"{unit['id']}"
            )
        evidence_ids = disambiguator["evidence_relation_ids"]
        missing = sorted(set(evidence_ids) - relation_ids)
        if missing:
            raise ExplanationModelError(
                f"disambiguator evidence does not resolve: {missing}"
            )
        candidates = predicate_for_by_target.get(unit["id"], [])
        if len(candidates) != 1:
            raise ExplanationModelError(
                "disambiguator rule requires exactly one predicate_for relation, "
                f"got {len(candidates)}: {unit['id']}"
            )
        relation = candidates[0]
        if evidence_ids != [relation["id"]]:
            raise ExplanationModelError(
                f"disambiguator evidence must name its deriving relation: {unit['id']}"
            )
        endpoints = relation.get("source_edge_endpoints")
        if not endpoints or len(endpoints) != 1:
            raise ExplanationModelError(
                f"disambiguator requires exactly one endpoint ledger: {unit['id']}"
            )
        expected_text = endpoints[0]["source_port_label"]
        if disambiguator["text"] != expected_text:
            raise ExplanationModelError(
                "disambiguator text must equal the exported source port label "
                f"{expected_text!r}: {unit['id']}"
            )


def _reaches(
    adjacency: Mapping[str, list[str]], source: str, target: str
) -> bool:
    frontier = list(adjacency.get(source, ()))
    visited: set[str] = set()
    while frontier:
        unit_id = frontier.pop()
        if unit_id == target:
            return True
        if unit_id not in visited:
            visited.add(unit_id)
            frontier.extend(adjacency.get(unit_id, ()))
    return False


def _validate_group_status(group: Mapping[str, Any]) -> None:
    group_id = group["id"]
    status = group["projection_status"]
    title = group["title"]
    evidence = group["claim_evidence"]
    diagnostic = group["diagnostic_code"]
    if status == "COMPLETE":
        if not title or not evidence or diagnostic:
            raise ExplanationModelError(
                "COMPLETE group requires a title and claim evidence and no "
                f"diagnostic: {group_id}"
            )
    elif status == "STRUCTURAL_ONLY":
        if title or evidence:
            raise ExplanationModelError(
                "STRUCTURAL_ONLY group requires an empty title and empty "
                f"claim_evidence: {group_id}"
            )
    elif title or evidence or not diagnostic:
        raise ExplanationModelError(
            f"ABSTAINED group requires a diagnostic and no title or claim: {group_id}"
        )


def _validate_complete_group_claim_coverage(
    model: Mapping[str, Any],
    group: Mapping[str, Any],
) -> None:
    components = {entry["component"] for entry in group["claim_evidence"]}
    members = set(group["ordered_unit_ids"])
    branch_unit_ids = {
        relation["source_unit_id"]
        for relation in model["relations"]
        if relation["kind"] == "controls_execution"
        and relation["source_unit_id"] in members
    }

    if group["kind"] == "guard_nest":
        predicate_relation_count = sum(
            relation["kind"] == "predicate_for"
            and relation["target_unit_id"] == group["entry_unit_id"]
            for relation in model["relations"]
        )
        if predicate_relation_count and not any(
            component.startswith("predicate_label.") for component in components
        ):
            raise ExplanationModelError(
                "COMPLETE group claim evidence does not cover stated claim "
                f"component 'predicate_label': {group['id']}"
            )
        return

    if group["kind"] == "outcome_path" and branch_unit_ids:
        predicate_components = {
            component
            for component in components
            if component.startswith("predicate_label.")
        }
        outcome_components = {
            component
            for component in components
            if component.startswith("branch_outcome.")
        }
        if len(predicate_components) < len(branch_unit_ids):
            raise ExplanationModelError(
                "COMPLETE group claim evidence does not cover "
                f"predicate_label prefix: {group['id']}"
            )
        if len(outcome_components) < len(branch_unit_ids):
            raise ExplanationModelError(
                "COMPLETE group claim evidence does not cover stated claim "
                f"component 'branch_outcome': {group['id']}"
            )
        predicate_suffixes = {
            component.removeprefix("predicate_label.")
            for component in predicate_components
        }
        outcome_suffixes = {
            component.removeprefix("branch_outcome.")
            for component in outcome_components
        }
        if predicate_suffixes != outcome_suffixes:
            missing = sorted(predicate_suffixes - outcome_suffixes)
            missing_component = (
                f"branch_outcome.{missing[0]}"
                if missing
                else "predicate_label"
            )
            raise ExplanationModelError(
                "COMPLETE group claim evidence does not cover stated claim "
                f"component {missing_component!r}: {group['id']}"
            )
        return

    if "title" not in components:
        raise ExplanationModelError(
            "COMPLETE group claim evidence does not cover stated claim "
            f"component 'title': {group['id']}"
        )


def _validate_groups(model: Mapping[str, Any]) -> None:
    """Validate the optional overlapping semantic cover.

    Groups are deliberately not a partition: the LC2 entry relation belongs to
    every outcome path at once, so membership is checked for resolution and
    provenance rather than exhaustiveness.
    """

    groups = model.get("groups")
    partial_order = model.get("group_partial_order")
    if groups is None:
        if partial_order is not None:
            raise ExplanationModelError("group_partial_order requires groups")
        return

    unit_ids = {unit["id"] for unit in model["units"]}
    relations_by_id = {relation["id"]: relation for relation in model["relations"]}
    groups_by_id: dict[str, Mapping[str, Any]] = {}
    for group in groups:
        if group["id"] in groups_by_id:
            raise ExplanationModelError(f"group IDs must be unique: {group['id']}")
        groups_by_id[group["id"]] = group

    for group in groups:
        group_id = group["id"]
        members = group["ordered_unit_ids"]
        if len(members) != len(set(members)):
            raise ExplanationModelError(f"group members must be unique: {group_id}")
        missing_units = sorted(set(members) - unit_ids)
        if missing_units:
            raise ExplanationModelError(
                f"group unit does not resolve: {group_id}: {missing_units}"
            )
        member_relations = group["ordered_relation_ids"]
        missing_relations = sorted(set(member_relations) - set(relations_by_id))
        if missing_relations:
            raise ExplanationModelError(
                f"group relation does not resolve: {group_id}: {missing_relations}"
            )
        if group["member_count"] != len(members):
            raise ExplanationModelError(
                f"group member_count does not match members: {group_id}"
            )
        if group["entry_unit_id"] not in members:
            raise ExplanationModelError(
                f"group entry_unit_id must be a member: {group_id}"
            )
        has_exit = "exit_unit_id" in group
        if group["kind"] == "guard_nest" and has_exit:
            raise ExplanationModelError(
                f"guard_nest must not declare exit_unit_id: {group_id}"
            )
        if group["kind"] in {"outcome_path", "scc"} and not has_exit:
            raise ExplanationModelError(
                f"{group['kind']} requires exit_unit_id: {group_id}"
            )
        if has_exit and group["exit_unit_id"] not in members:
            raise ExplanationModelError(
                f"group exit_unit_id must be a member: {group_id}"
            )
        if (
            group["kind"] == "outcome_path"
            and group["exit_unit_id"] == model["criterion_unit_id"]
        ):
            raise ExplanationModelError(
                "outcome_path exit_unit_id must not equal criterion_unit_id: "
                f"{group_id}"
            )

        if group["kind"] == "outcome_path":
            if len(member_relations) != len(members) - 1:
                raise ExplanationModelError(
                    f"outcome_path must chain every member exactly once: {group_id}"
                )
            for index, relation_id in enumerate(member_relations):
                relation = relations_by_id[relation_id]
                if (
                    relation["source_unit_id"] != members[index]
                    or relation["target_unit_id"] != members[index + 1]
                ):
                    raise ExplanationModelError(
                        f"outcome_path relation order is inconsistent: {group_id}"
                    )
            if (
                group["entry_unit_id"] != members[0]
                or group["exit_unit_id"] != members[-1]
            ):
                raise ExplanationModelError(
                    f"outcome_path entry/exit must be its chain endpoints: {group_id}"
                )
        else:
            member_set = set(members)
            for relation_id in member_relations:
                relation = relations_by_id[relation_id]
                if (
                    relation["source_unit_id"] not in member_set
                    or relation["target_unit_id"] not in member_set
                ):
                    raise ExplanationModelError(
                        f"group relation leaves its members: {group_id}"
                    )

        _validate_group_status(group)
        if group["projection_status"] == "COMPLETE":
            _validate_complete_group_claim_coverage(
                model, group
            )

    # Structure before semantics: an unresolvable or cyclic parent chain is
    # reported ahead of the entering-relation rules below.
    for group in groups:
        parent_id = group["parent_group_id"]
        if parent_id is not None and parent_id not in groups_by_id:
            raise ExplanationModelError(f"group parent does not resolve: {group['id']}")

    for group in groups:
        seen: set[str] = set()
        current: str | None = group["id"]
        while current is not None:
            if current in seen:
                raise ExplanationModelError(
                    f"group parent chain contains a cycle: {group['id']}"
                )
            seen.add(current)
            current = groups_by_id[current]["parent_group_id"]

    for group in groups:
        parent_id = group["parent_group_id"]
        entered_by = group["entered_by"]
        if parent_id is None:
            if entered_by is not None:
                raise ExplanationModelError(
                    "group without a parent must not declare entered_by: "
                    f"{group['id']}"
                )
            continue
        if entered_by is None:
            raise ExplanationModelError(
                f"nested group must declare entered_by: {group['id']}"
            )
        members = set(group["ordered_unit_ids"])
        outside_parent = set(groups_by_id[parent_id]["ordered_unit_ids"]) - members
        entering = [
            relation
            for relation in model["relations"]
            if relation.get("semantic_label") == entered_by
            and relation["target_unit_id"] in members
            and relation["source_unit_id"] in outside_parent
        ]
        if not entering:
            raise ExplanationModelError(
                "group entered_by has no entering relation from its parent: "
                f"{group['id']}"
            )

    if partial_order is None:
        return
    if partial_order.get("semantics") != GROUP_PARTIAL_ORDER_SEMANTICS:
        raise ExplanationModelError(
            "group_partial_order semantics does not match frozen constant"
        )

    adjacency: dict[str, list[str]] = {}
    for relation in model["relations"]:
        if relation["kind"] in _ORDERED_EXECUTION_KINDS:
            adjacency.setdefault(relation["source_unit_id"], []).append(
                relation["target_unit_id"]
            )
    seen_incomparable_pairs: set[tuple[str, str]] = set()
    for pair in partial_order["incomparable_group_ids"]:
        first, second = pair
        if first == second:
            raise ExplanationModelError(
                f"incomparable pair must name two distinct groups: {first}"
            )
        for group_id in pair:
            if group_id not in groups_by_id:
                raise ExplanationModelError(
                    f"incomparable group does not resolve: {group_id}"
                )
        canonical_pair = tuple(sorted((first, second)))
        if canonical_pair in seen_incomparable_pairs:
            raise ExplanationModelError(
                "incomparable pair is duplicated: "
                f"{canonical_pair[0]}/{canonical_pair[1]}"
            )
        seen_incomparable_pairs.add(canonical_pair)
        first_exit = groups_by_id[first].get("exit_unit_id")
        second_exit = groups_by_id[second].get("exit_unit_id")
        if first_exit is None or second_exit is None or first_exit == second_exit:
            raise ExplanationModelError(
                "incomparable groups must have distinct exit_unit_id values: "
                f"{first}/{second}"
            )
        if _reaches(adjacency, first_exit, second_exit) or _reaches(
            adjacency, second_exit, first_exit
        ):
            raise ExplanationModelError(
                f"declared incomparable groups are ordered: {first}/{second}"
            )


def validate_explanation_model(
    model: Mapping[str, Any],
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    schema_path: Path,
) -> None:
    """Validate schema shape, source freshness, and semantic provenance."""

    ir_path = Path(ir_path)
    slice_path = Path(slice_path)
    asset_file = Path(asset_file)
    schema_path = Path(schema_path)
    schema = _require_mapping(_read_json(schema_path, "schema"), "schema")
    try:
        validate_instance(model, schema)
    except (SchemaValidationError, ReconstructionError) as error:
        raise ExplanationModelError(str(error)) from error

    source = model["source"]
    _require_sha256(ir_path, source["ir_sha256"], "IR")
    _require_sha256(slice_path, source["slice_sha256"], "slice")
    _require_sha256(
        asset_file,
        source["blueprint_package_sha256"],
        "Blueprint package",
    )

    document = _load_document(ir_path)
    slice_value = _require_mapping(_read_json(slice_path, "slice"), "slice")
    nodes, edges, _, _ = _graph_and_slice_entities(document, slice_value)
    slice_node_ids = _require_string_list(
        slice_value.get("node_ids"), "slice.node_ids"
    )
    slice_edge_ids = _require_string_list(
        slice_value.get("edge_ids"), "slice.edge_ids"
    )

    expected_lane_order = list(LANE_ORDER)
    actual_lane_order = [lane["role"] for lane in model["lanes"]]
    if actual_lane_order != expected_lane_order:
        raise ExplanationModelError(
            f"lane order must be {expected_lane_order}, got {actual_lane_order}"
        )

    for lane in model["lanes"]:
        role = lane["role"]
        if role == "criterion":
            if (
                lane["state"] != "populated"
                or not lane["unit_ids"]
                or lane["empty_message"] != ""
            ):
                raise ExplanationModelError(
                    f"{role} lane must be populated with units "
                    "and no empty message"
                )
        elif role == "control":
            is_populated = (
                lane["state"] == "populated"
                and bool(lane["unit_ids"])
                and lane["empty_message"] == ""
            )
            is_empty = (
                lane["state"] == "empty"
                and not lane["unit_ids"]
                and bool(lane["empty_message"])
            )
            if not (is_populated or is_empty):
                raise ExplanationModelError(
                    "control lane must be populated with units and no empty "
                    "message, or explicitly empty with a message"
                )
        elif role in {"predicate", "value"}:
            expected_empty_message = (
                _EMPTY_PREDICATE_MESSAGE
                if role == "predicate"
                else _EMPTY_VALUE_MESSAGE
            )
            is_populated = (
                lane["state"] == "populated"
                and bool(lane["unit_ids"])
                and lane["empty_message"] == ""
            )
            is_empty = (
                lane["state"] == "empty"
                and not lane["unit_ids"]
                and lane["empty_message"] == expected_empty_message
            )
            if not (is_populated or is_empty):
                raise ExplanationModelError(
                    f"{role} lane must be populated with units or explicitly "
                    "empty with its no-facts message"
                )
        elif role == "consequence":
            if (
                lane["state"] != "not_enabled"
                or lane["unit_ids"]
                or lane["empty_message"] != _CONSEQUENCE_MESSAGE
            ):
                raise ExplanationModelError(
                    "consequence lane must be not_enabled with no units "
                    "and the backward-only message"
                )
        else:
            is_populated = (
                lane["state"] == "populated"
                and bool(lane["unit_ids"])
                and lane["empty_message"] == ""
            )
            is_empty = (
                lane["state"] == "empty"
                and not lane["unit_ids"]
                and lane["empty_message"] == _BOUNDARY_MESSAGE
            )
            if not (is_populated or is_empty):
                raise ExplanationModelError(
                    "boundary lane must be empty with no units "
                    "and the supported-constructs message, or populated "
                    "with boundary units"
                )

    unit_ids = [unit["id"] for unit in model["units"]]
    if len(unit_ids) != len(set(unit_ids)):
        raise ExplanationModelError("unit IDs must be unique")

    relation_ids = [relation["id"] for relation in model["relations"]]
    if len(relation_ids) != len(set(relation_ids)):
        raise ExplanationModelError("relation IDs must be unique")

    source_node_ids = [
        reference["source_node_id"]
        for unit in model["units"]
        for reference in unit["source_references"]
    ]
    if len(source_node_ids) != len(set(source_node_ids)):
        raise ExplanationModelError("source node ownership must not overlap")
    if set(source_node_ids) != set(slice_node_ids):
        raise ExplanationModelError("source references must partition slice node_ids")

    source_edge_ids = [
        edge_id
        for relation in model["relations"]
        for edge_id in relation["source_edge_ids"]
    ]
    if len(source_edge_ids) != len(set(source_edge_ids)):
        raise ExplanationModelError("source edge ownership must not overlap")
    if set(source_edge_ids) != set(slice_edge_ids):
        raise ExplanationModelError("relations must partition slice edge_ids")

    lane_unit_ids = [
        unit_id for lane in model["lanes"] for unit_id in lane["unit_ids"]
    ]
    if len(lane_unit_ids) != len(set(lane_unit_ids)):
        raise ExplanationModelError("lane unit ownership must not overlap")
    if set(lane_unit_ids) != set(unit_ids):
        raise ExplanationModelError("lane unit_ids must partition all unit IDs")

    units_by_id = {unit["id"]: unit for unit in model["units"]}
    for lane in model["lanes"]:
        for unit_id in lane["unit_ids"]:
            if units_by_id[unit_id]["role"] != lane["role"]:
                raise ExplanationModelError(
                    f"lane role disagrees with unit role: {unit_id}"
                )

    criterion_node_id, criterion_node_ids = _slice_criterion_node_ids(
        slice_value, nodes
    )
    criterion_unit_id = model["criterion_unit_id"]
    criterion_unit = units_by_id.get(criterion_unit_id)
    if criterion_unit is None:
        raise ExplanationModelError("criterion unit does not resolve")
    if criterion_unit["role"] != "criterion":
        raise ExplanationModelError("criterion unit must have criterion role")
    if model["query"]["criterion_source_node_id"] != criterion_node_id:
        raise ExplanationModelError(
            "query criterion must resolve to the slice criterion"
        )
    if criterion_node_id not in {
        reference["source_node_id"]
        for reference in criterion_unit["source_references"]
    }:
        raise ExplanationModelError(
            "criterion unit must resolve to the slice criterion"
        )
    projected_criterion_node_ids = {
        reference["source_node_id"]
        for unit in model["units"]
        if unit["role"] == "criterion"
        for reference in unit["source_references"]
    }
    if projected_criterion_node_ids != set(criterion_node_ids):
        raise ExplanationModelError(
            "criterion units must resolve every and only member Set criterion"
        )

    relation_endpoints = [
        endpoint
        for relation in model["relations"]
        for endpoint in (
            relation["source_unit_id"],
            relation["target_unit_id"],
        )
    ]
    missing_endpoints = sorted(set(relation_endpoints) - set(unit_ids))
    if missing_endpoints:
        raise ExplanationModelError(
            f"relation endpoint does not resolve: {missing_endpoints}"
        )

    if source["blueprint_asset_path"] != document.blueprint_path:
        raise ExplanationModelError("source Blueprint asset path does not match IR")
    if source["graph_id"] != slice_value.get("graph_id"):
        raise ExplanationModelError("source graph ID does not match slice")
    if model["rules_version"] != slice_value.get("rules_version"):
        raise ExplanationModelError("rules version does not match slice")

    selected_pins = _selected_source_pins(edges, slice_edge_ids)
    pins_by_id = {
        pin.id: pin
        for node in nodes.values()
        for pin in node.pins
    }
    for unit in model["units"]:
        source_nodes: list[Node] = []
        primary_count = 0
        for reference in unit["source_references"]:
            node = nodes.get(reference["source_node_id"])
            if node is None:
                raise ExplanationModelError(
                    f"source reference does not resolve: "
                    f"{reference['source_node_id']}"
                )
            source_nodes.append(node)
            if reference["blueprint_asset_path"] != document.blueprint_path:
                raise ExplanationModelError(
                    f"source reference asset path mismatch: {node.id}"
                )
            if reference["graph_id"] != node.graph_id:
                raise ExplanationModelError(
                    f"source reference graph ID mismatch: {node.id}"
                )
            if reference["native_node_guid"] != node.native_guid:
                raise ExplanationModelError(
                    f"source reference native GUID mismatch: {node.id}"
                )
            if sorted(reference["source_pin_ids"]) != sorted(
                selected_pins.get(node.id, [])
            ):
                raise ExplanationModelError(
                    f"source reference pin IDs mismatch: {node.id}"
                )
            primary_count += bool(reference["primary"])
        if primary_count != 1:
            raise ExplanationModelError(
                f"unit must have exactly one primary source: {unit['id']}"
            )
        expected_status = _unit_status(source_nodes)
        if unit["semantic_status"] != expected_status:
            raise ExplanationModelError(
                f"unit semantic status does not match source nodes: {unit['id']}"
            )
        expected_reasons = _stable_union(
            _slice_reasons(slice_value, node.id) for node in source_nodes
        )
        if unit["inclusion_reasons"] != expected_reasons:
            raise ExplanationModelError(
                f"unit inclusion reasons do not match slice: {unit['id']}"
            )

    for relation in model["relations"]:
        source_nodes = {
            reference["source_node_id"]
            for reference in units_by_id[relation["source_unit_id"]][
                "source_references"
            ]
        }
        target_nodes = {
            reference["source_node_id"]
            for reference in units_by_id[relation["target_unit_id"]][
                "source_references"
            ]
        }
        participating = source_nodes | target_nodes
        expected_edge_kind = _RELATION_EDGE_KIND[relation["kind"]]
        # One-edge relations (mandatory for LC2) therefore require the exact
        # source-unit -> target-unit direction. Legacy grouped-expression
        # relations may additionally own internal evidence edges, but still
        # require a directed bridge between their declared units.
        has_directed_bridge = False
        for edge_id in relation["source_edge_ids"]:
            edge = edges[edge_id]
            if (
                edge.kind != expected_edge_kind
                or edge.source_node_id not in participating
                or edge.target_node_id not in participating
            ):
                raise ExplanationModelError(
                    f"relation source edge disagrees with endpoints: {edge_id}"
                )
            has_directed_bridge |= (
                edge.source_node_id in source_nodes
                and edge.target_node_id in target_nodes
            )
        if not has_directed_bridge:
            raise ExplanationModelError(
                "relation source edge disagrees with endpoints: "
                f"{relation['source_edge_ids'][0]}"
            )
        if "source_edge_endpoints" in relation:
            endpoint_entries = relation["source_edge_endpoints"]
            endpoint_edge_ids = [
                endpoint["source_edge_id"]
                for endpoint in endpoint_entries
            ]
            if (
                len(endpoint_edge_ids) != len(set(endpoint_edge_ids))
                or set(endpoint_edge_ids) != set(relation["source_edge_ids"])
            ):
                raise ExplanationModelError(
                    "relation source_edge_endpoints must bijectively match "
                    f"source_edge_ids: {relation['id']}"
                )
            for endpoint in endpoint_entries:
                edge_id = endpoint["source_edge_id"]
                edge = edges[edge_id]
                source_node_id = endpoint["source_node_id"]
                target_node_id = endpoint["target_node_id"]
                source_pin_id = endpoint["source_pin_id"]
                target_pin_id = endpoint["target_pin_id"]
                if source_node_id not in nodes:
                    raise ExplanationModelError(
                        "relation endpoint source node does not resolve in IR: "
                        f"{source_node_id}"
                    )
                if target_node_id not in nodes:
                    raise ExplanationModelError(
                        "relation endpoint target node does not resolve in IR: "
                        f"{target_node_id}"
                    )
                if source_pin_id not in pins_by_id:
                    raise ExplanationModelError(
                        "relation endpoint source pin does not resolve in IR: "
                        f"{source_pin_id}"
                    )
                if target_pin_id not in pins_by_id:
                    raise ExplanationModelError(
                        "relation endpoint target pin does not resolve in IR: "
                        f"{target_pin_id}"
                    )
                if (
                    source_node_id != edge.source_node_id
                    or source_pin_id != edge.source_pin_id
                    or target_node_id != edge.target_node_id
                    or target_pin_id != edge.target_pin_id
                ):
                    raise ExplanationModelError(
                        "relation endpoint provenance disagrees with IR edge: "
                        f"{edge_id}"
                    )
                if (
                    endpoint["source_port_label"]
                    != pins_by_id[source_pin_id].name
                    or endpoint["target_port_label"]
                    != pins_by_id[target_pin_id].name
                ):
                    raise ExplanationModelError(
                        "relation endpoint port label disagrees with IR pin: "
                        f"{edge_id}"
                    )

    # v1.1 optional semantic extension. Each check is a no-op when its field is
    # absent, so frozen v1.0 artifacts remain valid and byte-identical.
    _validate_relation_labels(model)
    _validate_disambiguators(model)
    _validate_groups(model)

    expected_counts = {
        "lanes": len(model["lanes"]),
        "units": len(model["units"]),
        "relations": len(model["relations"]),
        "source_nodes": len(source_node_ids),
        "source_edges": len(source_edge_ids),
    }
    if model["counts"] != expected_counts:
        raise ExplanationModelError(
            f"declared counts do not match derived values: "
            f"{model['counts']} != {expected_counts}"
        )


def canonical_explanation_bytes(model: Mapping[str, Any]) -> bytes:
    """Encode an explanation model as stable canonical UTF-8 JSON."""

    return (
        json.dumps(
            model,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")
