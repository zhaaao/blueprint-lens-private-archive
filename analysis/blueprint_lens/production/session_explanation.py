"""Generic M6 Explanation projection and information-matched shared facts."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
from typing import Any, Literal, Mapping, NoReturn, Sequence, cast

from ..explanation_model import ExplanationModel, LANE_ORDER
from ..m6_errors import M6Error
from ..schema_validation import validate_instance


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas/blueprint-lens-m6-baseline-facts-v1.schema.json"
_RENDERER_ID = "R1_GENERIC_FRAME_FLOW_V1"
_SEMANTIC_STATUSES = frozenset(
    {"supported", "opaque", "uncertain", "unsupported"}
)
_BOUNDARY_STATUSES = frozenset({"opaque", "uncertain", "unsupported"})
_STATUS_ORDER = {"supported": 0, "opaque": 1, "uncertain": 2, "unsupported": 3}
_EMPTY_MESSAGES = {
    "control": "No control facts in this explanation",
    "predicate": "No predicate facts in this explanation",
    "value": "No value facts in this explanation",
    "boundary": "All selected constructs supported",
}


@dataclass(frozen=True, slots=True)
class SessionExplanationProducts:
    explanation: ExplanationModel
    baseline_facts: Mapping[str, object]


def derive_presentation_visibility(
    entities: Sequence[Mapping[str, Any]],
    relations: Sequence[Mapping[str, Any]],
    *,
    criterion_entity_id: str,
    max_visible_entities: int,
) -> tuple[list[str], frozenset[str], int]:
    """Derive the canonical depth order and visible relation count."""

    entity_ids = [str(entity["id"]) for entity in entities]
    adjacency: dict[str, set[str]] = {entity_id: set() for entity_id in entity_ids}
    for relation in relations:
        source_id = str(relation["source_entity_id"])
        target_id = str(relation["target_entity_id"])
        adjacency[source_id].add(target_id)
        adjacency[target_id].add(source_id)
    depths: dict[str, int] = {criterion_entity_id: 0}
    frontier = [criterion_entity_id]
    while frontier:
        current = frontier.pop(0)
        for neighbour in sorted(adjacency[current]):
            if neighbour not in depths:
                depths[neighbour] = depths[current] + 1
                frontier.append(neighbour)
    ordered_entity_ids = sorted(
        entity_ids,
        key=lambda entity_id: (depths.get(entity_id, float("inf")), entity_id),
    )
    visible_ids = frozenset(ordered_entity_ids[:max_visible_entities])
    visible_relation_count = sum(
        str(relation["source_entity_id"]) in visible_ids
        and str(relation["target_entity_id"]) in visible_ids
        for relation in relations
    )
    return ordered_entity_ids, visible_ids, visible_relation_count


def _fail(
    code: str,
    message: str,
    *,
    diagnostics: Mapping[str, object] | None = None,
    cause: Exception | None = None,
) -> NoReturn:
    raise M6Error(
        code,
        message,
        phase="pipeline",
        retryable=False,
        diagnostics=diagnostics,
        cause=cause,
    )


def _mapping(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        _fail("M6_PIPELINE_EXPLANATION_FAILED", f"{context} must be an object")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"{context} must be a non-empty string",
        )
    return value


def _string_list(value: Any, context: str) -> list[str]:
    if not isinstance(value, list) or any(
        not isinstance(item, str) or not item for item in value
    ):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"{context} must be an array of non-empty strings",
        )
    return cast(list[str], value)


def _canonical_product_bytes(value: Mapping[str, Any]) -> bytes:
    clean = {key: item for key, item in value.items() if not key.startswith("_m6_")}
    try:
        return (
            json.dumps(
                clean,
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
                allow_nan=False,
            )
            + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"product cannot be canonically serialized: {error}",
            cause=error,
        )


def _graph(
    typed_document: Mapping[str, Any], graph_id: str
) -> tuple[Mapping[str, Any], str]:
    blueprint = _mapping(typed_document.get("blueprint"), "typed blueprint")
    asset_path = _string(blueprint.get("path"), "typed blueprint.path")
    graphs = blueprint.get("graphs")
    if not isinstance(graphs, list):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "typed blueprint.graphs must be an array",
        )
    matches = [
        item
        for item in graphs
        if isinstance(item, Mapping) and item.get("id") == graph_id
    ]
    if len(matches) != 1:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"slice graph must resolve exactly once: {graph_id}",
        )
    return matches[0], asset_path


def _criterion_node_ids(
    slice_value: Mapping[str, Any],
    nodes: Mapping[str, Mapping[str, Any]],
    query_kind: str,
) -> tuple[str, frozenset[str]]:
    criterion = _mapping(slice_value.get("criterion"), "slice criterion")
    if query_kind == "execution":
        node_id = _string(
            criterion.get("node_id"), "execution criterion.node_id"
        )
        return node_id, frozenset({node_id})
    member_guid = _string(criterion.get("member_guid"), "data criterion.member_guid")
    member_name = _string(criterion.get("member_name"), "data criterion.member_name")
    matches = []
    for node_id, node in nodes.items():
        symbol = node.get("symbol")
        if not isinstance(symbol, Mapping):
            continue
        if (
            symbol.get("kind") == "variable"
            and symbol.get("access") == "set"
            and symbol.get("guid") == member_guid
            and symbol.get("name") == member_name
        ):
            matches.append(node_id)
    if not matches:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "data criterion must resolve at least one Set node by GUID and name",
        )
    criterion_ids = frozenset(matches)
    # The query remains member-level.  The singular v1 criterion fields are a
    # deterministic rendering anchor only; every matching Set remains a
    # criterion unit/entity and retains its own source facts and relations.
    return min(criterion_ids), criterion_ids


def _pin_index(
    nodes: Mapping[str, Mapping[str, Any]],
) -> dict[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    for node in nodes.values():
        pins = node.get("pins")
        if not isinstance(pins, list):
            _fail(
                "M6_PIPELINE_EXPLANATION_FAILED",
                f"node pins must be an array: {node.get('id')}",
            )
        for raw_pin in pins:
            pin = _mapping(raw_pin, "typed pin")
            pin_id = _string(pin.get("id"), "typed pin.id")
            if pin_id in result:
                _fail(
                    "M6_PIPELINE_EXPLANATION_FAILED",
                    f"duplicate typed pin identity: {pin_id}",
                )
            result[pin_id] = pin
    return result


def _role_for_node(
    node_id: str,
    node: Mapping[str, Any],
    criterion_ids: frozenset[str],
    selected_edges: Mapping[str, Mapping[str, Any]],
    pins: Mapping[str, Mapping[str, Any]],
) -> str:
    if node_id in criterion_ids:
        return "criterion"
    if node.get("semantic_status") != "supported":
        return "boundary"
    node_pins = node.get("pins", [])
    if isinstance(node_pins, list) and any(
        isinstance(pin, Mapping) and pin.get("kind") == "execution"
        for pin in node_pins
    ):
        return "control"
    for edge in selected_edges.values():
        if edge.get("source_node_id") != node_id:
            continue
        target_pin = pins.get(str(edge.get("target_pin_id")))
        if target_pin is not None and target_pin.get("pin_role") == "branch_condition":
            return "predicate"
    return "value"


def _relation_projection(
    edge: Mapping[str, Any],
    nodes: Mapping[str, Mapping[str, Any]],
    pins: Mapping[str, Mapping[str, Any]],
) -> tuple[str, str, str, str, str]:
    source_pin_id = _string(edge.get("source_pin_id"), "edge.source_pin_id")
    target_pin_id = _string(edge.get("target_pin_id"), "edge.target_pin_id")
    source_pin = pins.get(source_pin_id)
    target_pin = pins.get(target_pin_id)
    if source_pin is None or target_pin is None:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"edge pin identity does not resolve: {edge.get('id')}",
        )
    source_label = _string(source_pin.get("name"), "source pin.name")
    target_label = _string(target_pin.get("name"), "target pin.name")
    edge_kind = edge.get("kind")
    if edge_kind == "data":
        if target_pin.get("pin_role") == "branch_condition":
            return (
                "predicate_for",
                target_label,
                "branch_condition",
                source_label,
                target_label,
            )
        return (
            "provides_value",
            target_label,
            "value_input",
            source_label,
            target_label,
        )
    if edge_kind != "execution":
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"unsupported typed edge kind: {edge_kind}",
        )
    source_node = nodes[_string(edge.get("source_node_id"), "edge.source_node_id")]
    if str(source_node.get("class", "")).endswith("K2Node_IfThenElse"):
        semantic_label = {
            "then": "condition_true",
            "else": "condition_false",
        }.get(source_label)
        if semantic_label is not None:
            return (
                "controls_execution",
                source_label,
                semantic_label,
                source_label,
                target_label,
            )
    return (
        "execution_predecessor",
        source_label,
        "next_execution",
        source_label,
        target_label,
    )


def _status_and_reason(
    node: Mapping[str, Any], node_id: str
) -> tuple[str, str]:
    status = _string(node.get("semantic_status"), f"node status {node_id}")
    if status not in _SEMANTIC_STATUSES:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"unknown semantic status for {node_id}: {status}",
        )
    raw_reason = node.get("semantic_reason")
    if not isinstance(raw_reason, str):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"semantic reason must be a string: {node_id}",
        )
    if status != "supported" and not raw_reason:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"non-supported node is missing a semantic reason: {node_id}",
        )
    return status, raw_reason


def build_session_explanation(
    typed_document: Mapping[str, object],
    slice_value: Mapping[str, object],
    *,
    query_kind: Literal["execution", "data"],
    renderer_id: str,
    presentation_budget: tuple[int, int] | None = None,
) -> SessionExplanationProducts:
    """Project one accepted slice into generic Explanation and shared facts."""

    if renderer_id != _RENDERER_ID:
        _fail(
            "M6_VIEW_PROFILE_UNSUPPORTED",
            f"unsupported renderer_id: {renderer_id}",
        )
    expected_slice_kind = {
        "execution": "execution_context",
        "data": "member_variable_data_dependency",
    }[query_kind]
    if slice_value.get("slice_kind") != expected_slice_kind:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "query kind disagrees with the frozen slice kind",
        )
    graph_id = _string(slice_value.get("graph_id"), "slice.graph_id")
    graph, asset_path = _graph(typed_document, graph_id)
    raw_nodes = graph.get("nodes")
    raw_edges = graph.get("edges")
    if not isinstance(raw_nodes, list) or not isinstance(raw_edges, list):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "typed graph nodes and edges must be arrays",
        )
    all_nodes: dict[str, Mapping[str, Any]] = {}
    for raw_node in raw_nodes:
        node = _mapping(raw_node, "typed node")
        node_id = _string(node.get("id"), "typed node.id")
        if node_id in all_nodes:
            _fail(
                "M6_PIPELINE_EXPLANATION_FAILED",
                f"duplicate typed node identity: {node_id}",
            )
        all_nodes[node_id] = node
    all_edges: dict[str, Mapping[str, Any]] = {}
    for raw_edge in raw_edges:
        edge = _mapping(raw_edge, "typed edge")
        edge_id = _string(edge.get("id"), "typed edge.id")
        if edge_id in all_edges:
            _fail(
                "M6_PIPELINE_EXPLANATION_FAILED",
                f"duplicate typed edge identity: {edge_id}",
            )
        all_edges[edge_id] = edge

    node_ids = _string_list(slice_value.get("node_ids"), "slice.node_ids")
    edge_ids = _string_list(slice_value.get("edge_ids"), "slice.edge_ids")
    if node_ids != sorted(node_ids) or len(node_ids) != len(set(node_ids)):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "slice node identities must be unique and sorted",
        )
    if edge_ids != sorted(edge_ids) or len(edge_ids) != len(set(edge_ids)):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "slice edge identities must be unique and sorted",
        )
    missing_nodes = sorted(set(node_ids) - set(all_nodes))
    missing_edges = sorted(set(edge_ids) - set(all_edges))
    if missing_nodes or missing_edges:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            f"slice has dangling identities: nodes={missing_nodes}, edges={missing_edges}",
        )
    selected_nodes = {node_id: all_nodes[node_id] for node_id in node_ids}
    selected_edges = {edge_id: all_edges[edge_id] for edge_id in edge_ids}
    for edge_id, edge in selected_edges.items():
        if (
            edge.get("source_node_id") not in selected_nodes
            or edge.get("target_node_id") not in selected_nodes
        ):
            _fail(
                "M6_PIPELINE_EXPLANATION_FAILED",
                f"selected edge leaves selected membership: {edge_id}",
            )
    counts = _mapping(slice_value.get("counts"), "slice.counts")
    if counts.get("nodes") != len(node_ids) or counts.get("edges") != len(edge_ids):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "slice counts disagree with selected membership",
        )
    raw_reasons = _mapping(
        slice_value.get("inclusion_reasons"), "slice.inclusion_reasons"
    )
    if set(raw_reasons) != set(node_ids):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "slice inclusion reasons must exactly cover selected nodes",
        )
    reasons = {
        node_id: _string_list(raw_reasons[node_id], f"reasons for {node_id}")
        for node_id in node_ids
    }
    if any(not values or len(values) != len(set(values)) for values in reasons.values()):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "each selected node requires unique inclusion reasons",
        )

    pins = _pin_index(selected_nodes)
    criterion_id, criterion_ids = _criterion_node_ids(
        slice_value, selected_nodes, query_kind
    )
    if not criterion_ids.issubset(selected_nodes):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "criterion nodes are not all selected",
        )
    selected_pin_ids: dict[str, set[str]] = {node_id: set() for node_id in node_ids}
    for edge in selected_edges.values():
        selected_pin_ids[str(edge["source_node_id"])].add(str(edge["source_pin_id"]))
        selected_pin_ids[str(edge["target_node_id"])].add(str(edge["target_pin_id"]))

    statuses = {
        node_id: _status_and_reason(node, node_id)
        for node_id, node in selected_nodes.items()
    }
    raw_boundaries = slice_value.get("boundaries")
    if not isinstance(raw_boundaries, list):
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "slice boundaries must be an array",
        )
    boundaries: list[dict[str, str]] = []
    for raw_boundary in raw_boundaries:
        boundary = _mapping(raw_boundary, "slice boundary")
        boundaries.append(
            {
                "node_id": _string(boundary.get("node_id"), "boundary.node_id"),
                "status": _string(boundary.get("status"), "boundary.status"),
                "reason": _string(boundary.get("reason"), "boundary.reason"),
            }
        )
    boundaries.sort(key=lambda item: item["node_id"])
    expected_boundaries = [
        {"node_id": node_id, "status": status, "reason": reason}
        for node_id, (status, reason) in statuses.items()
        if status in _BOUNDARY_STATUSES
    ]
    expected_boundaries.sort(key=lambda item: item["node_id"])
    if boundaries != expected_boundaries:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "slice boundaries disagree with typed semantic states/reasons",
        )

    roles = {
        node_id: _role_for_node(
            node_id,
            node,
            criterion_ids,
            selected_edges,
            pins,
        )
        for node_id, node in selected_nodes.items()
    }
    unit_id_by_node = {
        node_id: f"unit.{roles[node_id]}.{selected_nodes[node_id]['native_guid']}"
        for node_id in node_ids
    }
    units: list[dict[str, Any]] = []
    entities: list[dict[str, Any]] = []
    for node_id in node_ids:
        node = selected_nodes[node_id]
        role = roles[node_id]
        status, reason = statuses[node_id]
        title = _string(node.get("title") or node.get("class"), f"node title {node_id}")
        native_guid = _string(node.get("native_guid"), f"node native_guid {node_id}")
        source_pin_ids = sorted(selected_pin_ids[node_id])
        unit = {
            "id": unit_id_by_node[node_id],
            "role": role,
            "kind": "node",
            "title": title,
            "expression": "",
            "semantic_status": status,
            "inclusion_reasons": list(reasons[node_id]),
            "source_references": [
                {
                    "blueprint_asset_path": asset_path,
                    "graph_id": graph_id,
                    "source_node_id": node_id,
                    "native_node_guid": native_guid,
                    "source_pin_ids": source_pin_ids,
                    "primary": True,
                }
            ],
        }
        units.append(unit)
        symbol = node.get("symbol")
        entities.append(
            {
                "id": node_id,
                "label": title,
                "role": role,
                "semantic_status": status,
                "semantic_reason": reason,
                "presentation_status": status,
                "presentation_reason": "",
                "inclusion_reasons": list(reasons[node_id]),
                "analysis": {
                    "class_path": _string(node.get("class"), f"node class {node_id}"),
                    "position_x": node.get("position_x"),
                    "position_y": node.get("position_y"),
                    "symbol": dict(symbol) if isinstance(symbol, Mapping) else None,
                },
                "source": {
                    "asset_path": asset_path,
                    "graph_id": graph_id,
                    "node_id": node_id,
                    "native_node_guid": native_guid,
                    "pin_ids": source_pin_ids,
                },
            }
        )

    explanation_relations: list[dict[str, Any]] = []
    fact_relations: list[dict[str, Any]] = []
    for edge_id in edge_ids:
        edge = selected_edges[edge_id]
        source_node_id = _string(edge.get("source_node_id"), "edge.source_node_id")
        target_node_id = _string(edge.get("target_node_id"), "edge.target_node_id")
        kind, label, semantic_label, source_port, target_port = _relation_projection(
            edge, selected_nodes, pins
        )
        relation_id = f"relation.{kind}.{hashlib.sha256(edge_id.encode()).hexdigest()[:16]}"
        endpoint = {
            "source_edge_id": edge_id,
            "source_node_id": source_node_id,
            "source_pin_id": str(edge["source_pin_id"]),
            "source_port_label": source_port,
            "target_node_id": target_node_id,
            "target_pin_id": str(edge["target_pin_id"]),
            "target_port_label": target_port,
        }
        explanation_relations.append(
            {
                "id": relation_id,
                "source_unit_id": unit_id_by_node[source_node_id],
                "target_unit_id": unit_id_by_node[target_node_id],
                "kind": kind,
                "label": label,
                "source_edge_ids": [edge_id],
                "source_edge_endpoints": [endpoint],
                "port_label": source_port,
                "semantic_label": semantic_label,
            }
        )
        endpoint_statuses = [statuses[source_node_id], statuses[target_node_id]]
        relation_status = max(endpoint_statuses, key=lambda item: _STATUS_ORDER[item[0]])
        relation_reason = "; ".join(
            dict.fromkeys(reason for _, reason in endpoint_statuses if reason)
        )
        fact_relations.append(
            {
                "id": edge_id,
                "label": label,
                "kind": kind,
                "semantic_label": semantic_label,
                "semantic_status": relation_status[0],
                "semantic_reason": relation_reason,
                "source_entity_id": source_node_id,
                "target_entity_id": target_node_id,
                "source": {
                    "edge_id": edge_id,
                    "source_node_id": source_node_id,
                    "source_pin_id": str(edge["source_pin_id"]),
                    "source_port_label": source_port,
                    "target_node_id": target_node_id,
                    "target_pin_id": str(edge["target_pin_id"]),
                    "target_port_label": target_port,
                },
            }
        )

    if presentation_budget is not None:
        max_visible_entities, max_visible_relations = presentation_budget
        _, visible_ids, visible_relation_count = derive_presentation_visibility(
            entities,
            fact_relations,
            criterion_entity_id=criterion_id,
            max_visible_entities=max_visible_entities,
        )
        for entity in entities:
            if entity["id"] in visible_ids:
                entity["presentation_status"] = entity["semantic_status"]
                entity["presentation_reason"] = ""
            else:
                entity["presentation_status"] = "truncated"
                entity["presentation_reason"] = "presentation_budget_exhausted"
        if visible_relation_count > max_visible_relations:
            _fail(
                "M6_PIPELINE_BUDGET_EXCEEDED",
                "visible relations exceed the presentation budget",
                diagnostics={
                    "dimension": "visible_relations",
                    "observed": visible_relation_count,
                    "declared": max_visible_relations,
                },
            )

    units.sort(key=lambda item: item["id"])
    explanation_relations.sort(key=lambda item: item["id"])
    lane_units = {
        role: sorted(unit["id"] for unit in units if unit["role"] == role)
        for role in LANE_ORDER
    }
    lanes = []
    for role in LANE_ORDER:
        ids = lane_units[role]
        if ids:
            state = "populated"
            message = ""
        elif role == "consequence":
            state = "not_enabled"
            message = "Not enabled in this backward-only query"
        else:
            state = "empty"
            message = _EMPTY_MESSAGES.get(role, "No criterion fact resolved")
        lanes.append(
            {
                "role": role,
                "state": state,
                "unit_ids": ids,
                "empty_message": message,
            }
        )

    source_fingerprint = typed_document.get("_m6_source_fingerprint", "0" * 64)
    if not isinstance(source_fingerprint, str) or len(source_fingerprint) != 64:
        _fail(
            "M6_PIPELINE_EXPLANATION_FAILED",
            "M6 source fingerprint must be a SHA-256 string",
        )
    question_key = "description" if query_kind == "execution" else "question"
    criterion = _mapping(slice_value.get("criterion"), "slice criterion")
    explanation: ExplanationModel = {
        "format": "blueprint-lens-explanation",
        "schema_version": "1.0.0",
        "rules_version": _string(slice_value.get("rules_version"), "slice.rules_version"),
        "source": {
            "ir_path": "typed-source.json",
            "ir_sha256": hashlib.sha256(_canonical_product_bytes(typed_document)).hexdigest().upper(),
            "slice_path": "slice.json",
            "slice_sha256": hashlib.sha256(_canonical_product_bytes(slice_value)).hexdigest().upper(),
            "blueprint_asset_path": asset_path,
            "blueprint_package_sha256": source_fingerprint.upper(),
            "graph_id": graph_id,
        },
        "query": {
            "question": _string(criterion.get(question_key), f"criterion.{question_key}"),
            "direction": "backward_only",
            "criterion_source_node_id": criterion_id,
        },
        "criterion_unit_id": unit_id_by_node[criterion_id],
        "lanes": lanes,
        "units": units,
        "relations": explanation_relations,
        "counts": {
            "lanes": len(lanes),
            "units": len(units),
            "relations": len(explanation_relations),
            "source_nodes": len(node_ids),
            "source_edges": len(edge_ids),
        },
    }
    entities.sort(key=lambda item: item["id"])
    fact_relations.sort(key=lambda item: item["id"])
    baseline_facts: dict[str, Any] = {
        "format": "blueprint-lens-m6-baseline-facts",
        "schema_version": "1.0.0",
        "renderer_id": renderer_id,
        "graph_id": graph_id,
        "criterion_entity_id": criterion_id,
        "entities": entities,
        "relations": fact_relations,
        "boundaries": boundaries,
        "entity_lookup": {
            entity["id"]: index for index, entity in enumerate(entities)
        },
        "relation_lookup": {
            relation["id"]: index for index, relation in enumerate(fact_relations)
        },
        "counts": {
            "entities": len(entities),
            "relations": len(fact_relations),
            "boundaries": len(boundaries),
            "truncated": sum(
                entity["presentation_status"] == "truncated" for entity in entities
            ),
        },
    }
    validate_baseline_facts(
        baseline_facts,
        typed_document=typed_document,
        slice_value=slice_value,
        explanation=explanation,
    )
    return SessionExplanationProducts(explanation, baseline_facts)


def validate_baseline_facts(
    value: Mapping[str, object],
    *,
    typed_document: Mapping[str, object],
    slice_value: Mapping[str, object],
    explanation: ExplanationModel,
) -> None:
    """Validate schema and complete Typed/Slice/Explanation cross-product parity."""

    try:
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(value, schema)
        graph_id = slice_value["graph_id"]
        if value["renderer_id"] != _RENDERER_ID or value["graph_id"] != graph_id:
            raise ValueError("renderer or graph binding mismatch")
        graph, _ = _graph(typed_document, str(graph_id))
        graph_nodes = {node["id"]: node for node in graph["nodes"]}
        graph_edges = {edge["id"]: edge for edge in graph["edges"]}
        selected_nodes = list(slice_value["node_ids"])
        selected_edges = list(slice_value["edge_ids"])
        entities = list(value["entities"])
        relations = list(value["relations"])
        entity_ids = [entity["id"] for entity in entities]
        relation_ids = [relation["source"]["edge_id"] for relation in relations]
        if entity_ids != sorted(selected_nodes) or relation_ids != sorted(selected_edges):
            raise ValueError("shared facts membership/order disagrees with slice")
        if len(entity_ids) != len(set(entity_ids)) or len(relation_ids) != len(
            set(relation_ids)
        ):
            raise ValueError("shared facts contain duplicate identities")
        if value["entity_lookup"] != {
            entity_id: index for index, entity_id in enumerate(entity_ids)
        }:
            raise ValueError("entity lookup is incomplete or incorrect")
        if value["relation_lookup"] != {
            relation["id"]: index for index, relation in enumerate(relations)
        }:
            raise ValueError("relation lookup is incomplete or incorrect")
        explanation_units = {
            reference["source_node_id"]: unit
            for unit in explanation["units"]
            for reference in unit["source_references"]
        }
        explanation_relations = {
            edge_id: relation
            for relation in explanation["relations"]
            for edge_id in relation["source_edge_ids"]
        }
        if set(explanation_units) != set(selected_nodes) or set(
            explanation_relations
        ) != set(selected_edges):
            raise ValueError("Explanation membership disagrees with slice")
        for entity in entities:
            entity_id = entity["id"]
            node = graph_nodes[entity_id]
            unit = explanation_units[entity_id]
            if entity["label"] != (node.get("title") or node["class"]):
                raise ValueError(f"entity label disagrees with Typed IR: {entity_id}")
            if (
                entity["role"] != unit["role"]
                or entity["semantic_status"] != unit["semantic_status"]
                or entity["inclusion_reasons"] != unit["inclusion_reasons"]
            ):
                raise ValueError(f"entity facts disagree with Explanation: {entity_id}")
            presentation_status = entity["presentation_status"]
            presentation_reason = entity["presentation_reason"]
            if (presentation_status == "truncated") != bool(presentation_reason):
                raise ValueError(
                    f"truncated presentation state/reason mismatch: {entity_id}"
                )
            source = entity["source"]
            if (
                source["node_id"] != entity_id
                or source["native_node_guid"] != node["native_guid"]
                or source["graph_id"] != graph_id
            ):
                raise ValueError(f"entity source identity mismatch: {entity_id}")
        for relation in relations:
            edge_id = relation["source"]["edge_id"]
            edge = graph_edges[edge_id]
            projected = explanation_relations[edge_id]
            source_unit = explanation_units[edge["source_node_id"]]["id"]
            target_unit = explanation_units[edge["target_node_id"]]["id"]
            if (
                relation["id"] != edge_id
                or relation["source_entity_id"] != edge["source_node_id"]
                or relation["target_entity_id"] != edge["target_node_id"]
                or projected["source_unit_id"] != source_unit
                or projected["target_unit_id"] != target_unit
                or relation["label"] != projected["label"]
                or relation["kind"] != projected["kind"]
                or relation["semantic_label"] != projected["semantic_label"]
            ):
                raise ValueError(f"relation facts disagree with source: {edge_id}")
        expected_boundaries = sorted(
            [dict(item) for item in slice_value["boundaries"]],
            key=lambda item: item["node_id"],
        )
        if value["boundaries"] != expected_boundaries:
            raise ValueError("shared boundaries disagree with slice")
        boundary_ids = [item["node_id"] for item in value["boundaries"]]
        expected_boundary_ids = [
            entity["id"]
            for entity in entities
            if entity["semantic_status"] in _BOUNDARY_STATUSES
        ]
        if boundary_ids != sorted(expected_boundary_ids):
            raise ValueError("non-supported entities do not map one-to-one to boundaries")
        counts = value["counts"]
        expected_counts = {
            "entities": len(entities),
            "relations": len(relations),
            "boundaries": len(value["boundaries"]),
            "truncated": sum(
                entity["presentation_status"] == "truncated" for entity in entities
            ),
        }
        if counts != expected_counts:
            raise ValueError("shared-facts counts disagree with content")
    except M6Error:
        raise
    except Exception as error:
        _fail(
            "M6_PIPELINE_SHARED_FACTS_INVALID",
            f"shared baseline facts are invalid: {error}",
            cause=error,
        )
