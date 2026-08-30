"""Project and validate the complete LC2 nested-guard Explanation."""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
import hashlib
import json
import os
from itertools import combinations
from pathlib import Path
import tempfile
from typing import Any, Iterable, Mapping, Sequence

from .explanation_model import (
    CompleteDagRelationProjection,
    CompleteDagUnitProjection,
    ExplanationModelError,
    GROUP_PARTIAL_ORDER_SEMANTICS,
    build_complete_dag_explanation,
    canonical_explanation_bytes,
    validate_explanation_model,
)
from .lc2_artifacts import BOOLEAN_PIN_TYPE, LC2ArtifactError, _build_guard_truth
from .raw_probe import Edge, Node, Pin, ReconstructionError, load_blueprint_lens_v1


LC2_BLUEPRINT_PATH = "/Game/LensCorpus/BP_LC2_NestedGuards.BP_LC2_NestedGuards"
LC2_CRITERION_SYMBOL = "LC2Complete"
LC2_EXPECTED_UNITS = 9
LC2_EXPECTED_RELATIONS = 10
LC2_QUESTION = "Why does Set LC2Complete execute?"
LC2_FROZEN_LEDGER_FIELDS = (
    "entry",
    "criterion",
    "guards",
    "predicate_attachments",
    "outcome_paths",
    "partial_order",
    "source_references",
    "counts",
)
_LC2_SEMANTIC_LABEL_BY_PORT = {
    ("controls_execution", "then"): "condition_true",
    ("controls_execution", "else"): "condition_false",
}
_LC2_SEMANTIC_LABEL_ANY_PORT = {
    "execution_predecessor": "next_execution",
    "predicate_for": "branch_condition",
    "provides_value": "value_input",
}
_LC2_REQUIRED_TARGET_PORT = {"predicate_for": "Condition"}
_LC2_DISAMBIGUATOR_RULE = "unit.branch.from_predicate_for"


class LC2ExplanationError(ValueError):
    """Raised when LC2 cannot be projected without guessing source facts."""


@dataclass(slots=True)
class _PublishTarget:
    path: Path
    existed: bool = False
    original_payload: bytes | None = None
    staged_path: Path | None = None
    backup_path: Path | None = None
    restore_path: Path | None = None


@dataclass(frozen=True, slots=True)
class _RollbackTargetResult:
    target_path: Path
    restored: bool
    retain_transaction_files: bool
    backup_path: Path | None
    original_payload: bytes | None
    retained_paths: tuple[Path, ...]
    error: str | None


def _load_object(path: Path, label: str) -> Mapping[str, Any]:
    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC2ExplanationError(f"cannot read {label} {path}: {error}") from error
    if not isinstance(value, dict):
        raise LC2ExplanationError(f"{label} must be an object")
    return value


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(Path(path).read_bytes()).hexdigest().upper()
    except OSError as error:
        raise LC2ExplanationError(f"cannot hash {path}: {error}") from error


def _require_hash(path: Path, declared: object, label: str) -> None:
    actual = _sha256(path)
    if declared != actual:
        raise LC2ExplanationError(
            f"{label} SHA-256 mismatch: declared {declared}, actual {actual}"
        )


def _require_string_list(value: Any, label: str) -> list[str]:
    if not isinstance(value, list) or not all(
        isinstance(item, str) for item in value
    ):
        raise LC2ExplanationError(f"{label} must be an array of strings")
    return value


def _readiness_for_guard(
    guard_truth_path: Path, readiness_path: Path | None
) -> Path | None:
    if readiness_path is not None:
        return Path(readiness_path).resolve()
    sibling = guard_truth_path.parent / "readiness.v1.json"
    return sibling if sibling.exists() else None


def _validate_readiness_binding(
    guard_truth_path: Path, readiness_path: Path | None
) -> None:
    resolved = _readiness_for_guard(guard_truth_path, readiness_path)
    if resolved is None:
        return
    readiness = _load_object(resolved, "LC2 readiness")
    if (
        readiness.get("format") != "blueprint-lens-lc2-readiness"
        or readiness.get("status") != "TRUTH_FROZEN"
    ):
        raise LC2ExplanationError("LC2 readiness is not frozen")
    hashes = readiness.get("hashes")
    if not isinstance(hashes, dict):
        raise LC2ExplanationError("LC2 readiness hashes must be an object")
    _require_hash(
        guard_truth_path,
        hashes.get("guard_truth_sha256"),
        "LC2 readiness guard truth",
    )


def _raise_guard_drift(path: str, declared: Any, derived: Any) -> None:
    raise LC2ExplanationError(
        f"LC2 guard truth drift at {path}: "
        f"declared {declared!r}, derived {derived!r}"
    )


def _assert_frozen_ledger_equal(
    declared: Any, derived: Any, path: str
) -> None:
    if isinstance(derived, dict):
        if not isinstance(declared, dict):
            _raise_guard_drift(path, declared, derived)
        declared_keys = set(declared)
        derived_keys = set(derived)
        if declared_keys != derived_keys:
            _raise_guard_drift(
                f"{path} keys", sorted(declared_keys), sorted(derived_keys)
            )
        for key in sorted(derived):
            _assert_frozen_ledger_equal(
                declared[key], derived[key], f"{path}.{key}"
            )
        return
    if isinstance(derived, list):
        if not isinstance(declared, list) or len(declared) != len(derived):
            _raise_guard_drift(path, declared, derived)
        for index, item in enumerate(derived):
            _assert_frozen_ledger_equal(
                declared[index], item, f"{path}[{index}]"
            )
        return
    if declared != derived:
        _raise_guard_drift(path, declared, derived)


def _rederive_guard_ledger(
    ir_path: Path, slice_value: Mapping[str, Any]
) -> Mapping[str, Any]:
    try:
        document = load_blueprint_lens_v1(ir_path)
    except ReconstructionError as error:
        raise LC2ExplanationError(str(error)) from error
    graph_id = slice_value.get("graph_id")
    graph = next(
        (candidate for candidate in document.graphs if candidate.id == graph_id),
        None,
    )
    if graph is None:
        raise LC2ExplanationError(
            f"LC2 slice graph does not resolve in IR: {graph_id}"
        )
    criterion = slice_value.get("criterion")
    if not isinstance(criterion, dict) or not isinstance(
        criterion.get("node_id"), str
    ):
        raise LC2ExplanationError("LC2 slice criterion node is missing")
    try:
        return _build_guard_truth(graph, criterion["node_id"], source={})
    except LC2ArtifactError as error:
        raise LC2ExplanationError(
            f"LC2 guard truth cannot be rederived from IR: {error}"
        ) from error


def _validate_frozen_guard_ledger(
    guard: Mapping[str, Any], derived: Mapping[str, Any]
) -> None:
    for field in LC2_FROZEN_LEDGER_FIELDS:
        if field not in guard:
            _raise_guard_drift(field, "<missing>", derived[field])
        _assert_frozen_ledger_equal(guard[field], derived[field], field)


def _selected_entities(
    ir_path: Path,
    slice_path: Path,
) -> tuple[
    Mapping[str, Any],
    Mapping[str, Node],
    Mapping[str, Edge],
    Mapping[str, Pin],
]:
    try:
        document = load_blueprint_lens_v1(ir_path)
    except ReconstructionError as error:
        raise LC2ExplanationError(str(error)) from error
    slice_value = _load_object(slice_path, "LC2 slice")
    graph_id = slice_value.get("graph_id")
    graph = next(
        (candidate for candidate in document.graphs if candidate.id == graph_id),
        None,
    )
    if graph is None:
        raise LC2ExplanationError(
            f"LC2 slice graph does not resolve in IR: {graph_id}"
        )
    node_ids = _require_string_list(slice_value.get("node_ids"), "slice.node_ids")
    edge_ids = _require_string_list(slice_value.get("edge_ids"), "slice.edge_ids")
    graph_nodes = {node.id: node for node in graph.nodes}
    graph_edges = {edge.id: edge for edge in graph.edges}
    missing_nodes = sorted(set(node_ids) - set(graph_nodes))
    missing_edges = sorted(set(edge_ids) - set(graph_edges))
    if missing_nodes or missing_edges:
        raise LC2ExplanationError(
            "LC2 slice identities do not resolve in IR: "
            f"nodes={missing_nodes} edges={missing_edges}"
        )
    nodes = {node_id: graph_nodes[node_id] for node_id in node_ids}
    edges = {edge_id: graph_edges[edge_id] for edge_id in edge_ids}
    pins = {pin.id: pin for node in nodes.values() for pin in node.pins}
    return slice_value, nodes, edges, pins


def _short_class(node: Node) -> str:
    return node.class_path.rsplit(".", 1)[-1]


def _symbol(node: Node, access: str) -> str:
    symbol = node.symbol or {}
    if symbol.get("kind") != "variable" or symbol.get("access") != access:
        return ""
    return str(symbol.get("name", ""))


def _one(values: Iterable[Any], label: str) -> Any:
    matches = list(values)
    if len(matches) != 1:
        raise LC2ExplanationError(
            f"{label} must resolve exactly once; found {len(matches)}"
        )
    return matches[0]


def _validate_freshness(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    guard_truth_path: Path,
    readiness_path: Path | None = None,
    *,
    validate_ledger: bool = True,
) -> tuple[Mapping[str, Any], Mapping[str, Any]]:
    slice_value = _load_object(slice_path, "LC2 slice")
    guard = _load_object(guard_truth_path, "LC2 guard truth")
    if guard.get("format") != "blueprint-lens-lc2-guard-truth":
        raise LC2ExplanationError("LC2 guard truth format is not supported")
    source = guard.get("source")
    if not isinstance(source, dict):
        raise LC2ExplanationError("LC2 guard truth source must be an object")
    _require_hash(ir_path, source.get("ir_sha256"), "LC2 guard truth IR")
    _require_hash(slice_path, source.get("slice_sha256"), "LC2 guard truth slice")
    _require_hash(asset_file, source.get("asset_sha256"), "LC2 guard truth asset")
    _require_hash(ir_path, slice_value.get("source_sha256"), "LC2 slice source IR")
    if guard.get("counts") != {
        "nodes": 9,
        "edges": 10,
        "execution_edges": 8,
        "predicate_data_edges": 2,
        "guards": 2,
        "predicate_attachments": 2,
        "outcome_paths": 3,
        "reconvergence_edges": 3,
    }:
        raise LC2ExplanationError("LC2 guard truth frozen counts do not match 9/10")
    if source.get("graph_id") != slice_value.get("graph_id"):
        raise LC2ExplanationError("LC2 guard truth graph does not match slice")
    _validate_readiness_binding(guard_truth_path, readiness_path)
    if validate_ledger:
        derived = _rederive_guard_ledger(ir_path, slice_value)
        _validate_frozen_guard_ledger(guard, derived)
    return slice_value, guard


def _lc2_projection_specs(
    ir_path: Path,
    slice_path: Path,
) -> tuple[
    Mapping[str, CompleteDagUnitProjection],
    Mapping[str, CompleteDagRelationProjection],
]:
    slice_value, nodes, edges, pins = _selected_entities(ir_path, slice_path)
    criterion = slice_value.get("criterion")
    if not isinstance(criterion, dict) or not isinstance(
        criterion.get("node_id"), str
    ):
        raise LC2ExplanationError("LC2 slice criterion node is missing")
    criterion_node_id = criterion["node_id"]
    outgoing_execution: dict[str, list[Edge]] = defaultdict(list)
    outgoing_data: dict[str, list[Edge]] = defaultdict(list)
    for edge in edges.values():
        if edge.kind == "execution":
            outgoing_execution[edge.source_node_id].append(edge)
        elif edge.kind == "data":
            outgoing_data[edge.source_node_id].append(edge)

    unit_specs: dict[str, CompleteDagUnitProjection] = {}
    for node_id in sorted(nodes):
        node = nodes[node_id]
        node_class = _short_class(node)
        if node.semantic_status != "supported":
            raise LC2ExplanationError(
                f"selected node is unknown or unsupported: {node_id}"
            )
        if node_class == "K2Node_Event":
            if "BeginPlay" not in node.title:
                raise LC2ExplanationError(
                    f"selected node is unknown or unsupported: {node_id}"
                )
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="control", kind="node", title="BeginPlay"
            )
        elif node_class == "K2Node_IfThenElse":
            outcome_names = [
                pins[edge.source_pin_id].name
                for edge in outgoing_execution.get(node_id, ())
            ]
            if sorted(outcome_names) != ["else", "then"]:
                raise LC2ExplanationError(
                    f"branch outcome ownership is ambiguous: {node_id}"
                )
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="control", kind="node", title="Branch"
            )
        elif node_class == "K2Node_VariableSet":
            name = _symbol(node, "set")
            if not name:
                raise LC2ExplanationError(
                    f"selected node is unknown or unsupported: {node_id}"
                )
            is_criterion = node_id == criterion_node_id
            if is_criterion and name != LC2_CRITERION_SYMBOL:
                raise LC2ExplanationError(
                    "LC2 criterion source identity is not Set LC2Complete"
                )
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="criterion" if is_criterion else "control",
                kind="node",
                title=f"Set {name}",
            )
        elif node_class == "K2Node_VariableGet":
            name = _symbol(node, "get")
            attachment = _one(
                outgoing_data.get(node_id, ()),
                f"predicate attachment for {node_id}",
            )
            target_node = nodes[attachment.target_node_id]
            target_pin = pins[attachment.target_pin_id]
            if (
                not name
                or _short_class(target_node) != "K2Node_IfThenElse"
                or target_pin.pin_role != "branch_condition"
                or target_pin.name != "Condition"
            ):
                raise LC2ExplanationError(
                    "predicate attachment must target the exact Condition port: "
                    f"{attachment.id}"
                )
            if (
                dict(pins[attachment.source_pin_id].type) != BOOLEAN_PIN_TYPE
                or dict(target_pin.type) != BOOLEAN_PIN_TYPE
            ):
                raise LC2ExplanationError(
                    "predicate attachment pins must be exact Boolean: "
                    f"{attachment.id}"
                )
            unit_specs[node_id] = CompleteDagUnitProjection(
                role="predicate",
                kind="node",
                title=f"Get {name}",
            )
        else:
            raise LC2ExplanationError(
                f"selected node is unknown or unsupported: {node_id}"
            )

    relation_specs: dict[str, CompleteDagRelationProjection] = {}
    for edge_id in sorted(edges):
        edge = edges[edge_id]
        source_node = nodes[edge.source_node_id]
        target_node = nodes[edge.target_node_id]
        source_pin = pins[edge.source_pin_id]
        target_pin = pins[edge.target_pin_id]
        if edge.kind == "execution":
            if _short_class(source_node) == "K2Node_IfThenElse":
                if source_pin.name not in {"then", "else"}:
                    raise LC2ExplanationError(
                        f"branch outcome ownership is ambiguous: {edge_id}"
                    )
                relation_specs[edge_id] = CompleteDagRelationProjection(
                    kind="controls_execution", label=source_pin.name
                )
            else:
                relation_specs[edge_id] = CompleteDagRelationProjection(
                    kind="execution_predecessor", label=source_pin.name
                )
        elif (
            edge.kind == "data"
            and _short_class(source_node) == "K2Node_VariableGet"
            and _short_class(target_node) == "K2Node_IfThenElse"
        ):
            if target_pin.pin_role != "branch_condition" or target_pin.name != "Condition":
                raise LC2ExplanationError(
                    "predicate attachment must target the exact Condition port: "
                    f"{edge_id}"
                )
            relation_specs[edge_id] = CompleteDagRelationProjection(
                kind="predicate_for", label=target_pin.name
            )
        else:
            raise LC2ExplanationError(
                f"selected edge is unknown or unsupported: {edge_id}"
            )
    if len(unit_specs) != LC2_EXPECTED_UNITS or len(relation_specs) != LC2_EXPECTED_RELATIONS:
        raise LC2ExplanationError("LC2 complete projection must contain exactly 9/10")
    return unit_specs, relation_specs


def _relation_by_edge(model: Mapping[str, Any]) -> Mapping[str, Mapping[str, Any]]:
    result: dict[str, Mapping[str, Any]] = {}
    for relation in model["relations"]:
        edge_ids = relation["source_edge_ids"]
        endpoints = relation.get("source_edge_endpoints")
        if len(edge_ids) != 1 or not isinstance(endpoints, list) or len(endpoints) != 1:
            raise LC2ExplanationError(
                "every LC2 relation must own exactly one source edge and endpoint"
            )
        edge_id = edge_ids[0]
        if edge_id in result:
            raise LC2ExplanationError(f"LC2 relation edge ownership overlaps: {edge_id}")
        result[edge_id] = relation
    return result


def _assert_relation_fact(
    relation: Mapping[str, Any],
    fact: Mapping[str, Any],
    *,
    kind: str,
    label: str,
) -> None:
    edge_id = str(fact["edge_id"])
    if relation["kind"] != kind or relation["label"] != label:
        raise LC2ExplanationError(
            f"LC2 guard truth relation classification mismatch: {edge_id}"
        )
    endpoint = relation["source_edge_endpoints"][0]
    for field in (
        "source_node_id",
        "source_pin_id",
        "source_port_label",
        "target_node_id",
        "target_pin_id",
        "target_port_label",
    ):
        if endpoint[field] != fact[field]:
            raise LC2ExplanationError(
                f"LC2 guard truth endpoint mismatch: {edge_id}.{field}"
            )


def _is_reachable(
    adjacency: Mapping[str, Sequence[str]], source: str, target: str
) -> bool:
    frontier = list(adjacency.get(source, ()))
    visited: set[str] = set()
    while frontier:
        node_id = frontier.pop()
        if node_id == target:
            return True
        if node_id not in visited:
            visited.add(node_id)
            frontier.extend(adjacency.get(node_id, ()))
    return False


def _unit_ids_by_source_node(model: Mapping[str, Any]) -> Mapping[str, str]:
    result: dict[str, str] = {}
    for unit in model["units"]:
        references = unit.get("source_references")
        if not isinstance(references, list) or len(references) != 1:
            raise LC2ExplanationError(
                "LC2 semantic projection requires one source reference per unit"
            )
        node_id = references[0].get("source_node_id")
        if not isinstance(node_id, str) or node_id in result:
            raise LC2ExplanationError(
                "LC2 semantic projection source node ownership is ambiguous"
            )
        result[node_id] = unit["id"]
    return result


def _semantic_label_for_relation(
    relation: Mapping[str, Any], endpoints: Sequence[Mapping[str, Any]]
) -> str:
    kind = relation["kind"]
    port_label = relation["port_label"]
    required_target_port = _LC2_REQUIRED_TARGET_PORT.get(kind)
    if required_target_port is not None and any(
        endpoint["target_port_label"] != required_target_port
        for endpoint in endpoints
    ):
        raise LC2ExplanationError(
            f"LC2 {kind} relation must consume the {required_target_port!r} port"
        )
    semantic_label = _LC2_SEMANTIC_LABEL_ANY_PORT.get(kind)
    if semantic_label is None:
        semantic_label = _LC2_SEMANTIC_LABEL_BY_PORT.get((kind, port_label))
    if semantic_label is None:
        raise LC2ExplanationError(
            f"LC2 relation ({kind}, {port_label!r}) has no frozen semantic label"
        )
    return semantic_label


def _populate_lc2_relation_semantics(model: dict[str, Any]) -> None:
    for relation in model["relations"]:
        endpoints = relation.get("source_edge_endpoints")
        if not isinstance(endpoints, list) or not endpoints:
            raise LC2ExplanationError(
                f"LC2 relation endpoint provenance is missing: {relation['id']}"
            )
        port_label = endpoints[0].get("source_port_label")
        if not isinstance(port_label, str):
            raise LC2ExplanationError(
                f"LC2 relation source port label is missing: {relation['id']}"
            )
        if any(endpoint.get("source_port_label") != port_label for endpoint in endpoints):
            raise LC2ExplanationError(
                f"LC2 relation source port labels disagree: {relation['id']}"
            )
        relation["port_label"] = port_label
        relation["semantic_label"] = _semantic_label_for_relation(
            relation, endpoints
        )


def _populate_lc2_disambiguators(model: dict[str, Any]) -> None:
    units_by_id = {unit["id"]: unit for unit in model["units"]}
    for unit in model["units"]:
        unit.pop("disambiguator", None)
    predicate_for_by_target: dict[str, list[Mapping[str, Any]]] = defaultdict(list)
    for relation in model["relations"]:
        if relation["kind"] == "predicate_for":
            predicate_for_by_target[relation["target_unit_id"]].append(relation)
    for unit in model["units"]:
        if unit["role"] != "control":
            continue
        candidates = predicate_for_by_target.get(unit["id"], [])
        if len(candidates) != 1:
            continue
        relation = candidates[0]
        source_unit = units_by_id.get(relation["source_unit_id"])
        endpoints = relation.get("source_edge_endpoints")
        if source_unit is None or not isinstance(endpoints, list) or len(endpoints) != 1:
            raise LC2ExplanationError(
                f"LC2 disambiguator evidence is incomplete: {unit['id']}"
            )
        source_port_label = endpoints[0].get("source_port_label")
        if not isinstance(source_port_label, str) or not source_port_label:
            raise LC2ExplanationError(
                f"LC2 disambiguator source title is missing: {unit['id']}"
            )
        unit["disambiguator"] = {
            "text": source_port_label,
            "rule_id": _LC2_DISAMBIGUATOR_RULE,
            "evidence_relation_ids": [relation["id"]],
        }


def _claim_evidence(
    component: str, fact_owner: str, source: object
) -> dict[str, str]:
    return {
        "component": component,
        "fact_owner": fact_owner,
        "source": str(source),
    }


def _outcome_path_title(
    path: Mapping[str, Any], predicate_labels: Mapping[str, str]
) -> str:
    false_predicates = [
        predicate_labels[str(step["guard_id"])]
        for step in path["branch_outcomes"]
        if step["outcome"] == "false"
    ]
    if len(false_predicates) == 1:
        return f"{false_predicates[0]} was false"
    if not false_predicates and len(path["branch_outcomes"]) == 2:
        return "Both guards passed"
    raise LC2ExplanationError(
        f"LC2 outcome path title cannot be derived without guessing: {path['id']}"
    )


def _outcome_path_claim_evidence(
    path: Mapping[str, Any], predicate_attachments: Mapping[str, Mapping[str, Any]]
) -> list[dict[str, str]]:
    evidence = [
        _claim_evidence(
            "outcome_id", "guard_truth.outcome_paths", path["id"]
        ),
        _claim_evidence(
            "set_node_id", "guard_truth.outcome_paths", path["set_node_id"]
        ),
        _claim_evidence(
            "literal_pin_id", "guard_truth.outcome_paths", path["literal_pin_id"]
        ),
        _claim_evidence(
            "literal_value", "guard_truth.outcome_paths", path["literal_value"]
        ),
        _claim_evidence(
            "reconvergence_edge_outside_path",
            "guard_truth.outcome_paths",
            path["reconvergence_edge_id"],
        ),
    ]
    for step in path["branch_outcomes"]:
        guard_id = str(step["guard_id"])
        attachment = predicate_attachments.get(guard_id)
        if attachment is None:
            raise LC2ExplanationError(
                f"LC2 outcome path predicate attachment is missing: {guard_id}"
            )
        evidence.append(
            _claim_evidence(
                f"predicate_label.{guard_id}",
                "guard_truth.predicate_attachments",
                attachment["edge_id"],
            )
        )
        evidence.append(
            _claim_evidence(
                f"branch_outcome.{guard_id}",
                "guard_truth.outcome_paths",
                step["edge_id"],
            )
        )
    for index, edge_id in enumerate(path["ordered_execution_edge_ids"]):
        evidence.append(
            _claim_evidence(
                f"ordered_execution_edge.{index}",
                "guard_truth.outcome_paths",
                edge_id,
            )
        )
    return evidence


def _guard_nest_claim_evidence(
    guard: Mapping[str, Any],
    predicate_attachment: Mapping[str, Any],
) -> list[dict[str, str]]:
    evidence = [
        _claim_evidence(
            f"predicate_label.{guard['id']}",
            "guard_truth.predicate_attachments",
            predicate_attachment["edge_id"],
        ),
        _claim_evidence("guard_id", "guard_truth.guards", guard["id"]),
        _claim_evidence("depth", "guard_truth.guards", guard["depth"]),
        _claim_evidence(
            "branch_node_id", "guard_truth.guards", guard["branch_node_id"]
        ),
        _claim_evidence(
            "predicate_edge_id", "guard_truth.guards", guard["predicate_edge_id"]
        ),
        _claim_evidence(
            "parent_guard_id",
            "guard_truth.guards",
            guard["parent_guard_id"] or "none",
        ),
        _claim_evidence(
            "entered_by_parent_outcome",
            "guard_truth.guards",
            guard["entered_by_parent_outcome"] or "none",
        ),
    ]
    for outcome in sorted(guard["outcome_edges"]):
        evidence.append(
            _claim_evidence(
                f"outcome_edge.{outcome}",
                "guard_truth.guards",
                guard["outcome_edges"][outcome]["edge_id"],
            )
        )
    return evidence


def _lc2_path_execution_key(
    path: Mapping[str, Any],
    guards_by_id: Mapping[str, Mapping[str, Any]],
) -> tuple[Any, ...]:
    branch_outcome_keys: list[tuple[tuple[int, ...], int]] = []
    for branch_outcome in path["branch_outcomes"]:
        guard_id = str(branch_outcome["guard_id"])
        current_guard_id: str | None = guard_id
        depth_chain: list[int] = []
        seen_guard_ids: set[str] = set()
        while current_guard_id is not None:
            if current_guard_id in seen_guard_ids:
                raise LC2ExplanationError(
                    f"LC2 guard parent chain contains a cycle: {guard_id}"
                )
            seen_guard_ids.add(current_guard_id)
            current_guard = guards_by_id.get(current_guard_id)
            if current_guard is None:
                raise LC2ExplanationError(
                    f"LC2 outcome path guard does not resolve: {guard_id}"
                )
            depth_chain.append(int(current_guard["depth"]))
            parent_guard_id = current_guard["parent_guard_id"]
            current_guard_id = (
                str(parent_guard_id) if parent_guard_id is not None else None
            )
        branch_outcome_keys.append(
            (
                tuple(reversed(depth_chain)),
                0 if branch_outcome["outcome"] == "true" else 1,
            )
        )
    return (
        tuple(sorted(branch_outcome_keys)),
        tuple(str(edge_id) for edge_id in path["ordered_execution_edge_ids"]),
    )


def _ordered_lc2_path_facts(
    path: Mapping[str, Any],
    relations_by_edge: Mapping[str, Mapping[str, Any]],
    unit_ids_by_node: Mapping[str, str],
) -> tuple[list[str], list[str], str]:
    ordered_relation_ids: list[str] = []
    ordered_unit_ids: list[str] = []
    previous_target: str | None = None
    for edge_id in path["ordered_execution_edge_ids"]:
        relation = relations_by_edge.get(str(edge_id))
        if relation is None:
            raise LC2ExplanationError(
                f"LC2 outcome path relation does not resolve: {edge_id}"
            )
        source_unit_id = relation["source_unit_id"]
        target_unit_id = relation["target_unit_id"]
        if previous_target is not None and source_unit_id != previous_target:
            raise LC2ExplanationError(
                f"LC2 outcome path relation order is inconsistent: {path['id']}"
            )
        if not ordered_unit_ids:
            ordered_unit_ids.append(source_unit_id)
        ordered_unit_ids.append(target_unit_id)
        ordered_relation_ids.append(relation["id"])
        previous_target = target_unit_id
    if not ordered_unit_ids:
        raise LC2ExplanationError(
            f"LC2 outcome path has no ordered execution edges: {path['id']}"
        )
    expected_exit = unit_ids_by_node.get(str(path["set_node_id"]))
    if expected_exit != ordered_unit_ids[-1]:
        raise LC2ExplanationError(
            f"LC2 outcome path exit does not resolve to its Set node: {path['id']}"
        )
    return ordered_unit_ids, ordered_relation_ids, expected_exit


def _lc2_descendants_by_guard(
    guards: Sequence[Mapping[str, Any]],
) -> dict[str, set[str]]:
    descendants_by_guard: dict[str, set[str]] = defaultdict(set)
    for candidate in guards:
        parent_id = candidate["parent_guard_id"]
        if parent_id is not None:
            descendants_by_guard[str(parent_id)].add(str(candidate["id"]))
    return descendants_by_guard


def _ordered_lc2_guard_facts(
    candidate: Mapping[str, Any],
    ordered_paths: Sequence[Mapping[str, Any]],
    path_projections: Mapping[str, Mapping[str, Any]],
    descendants_by_guard: Mapping[str, set[str]],
    relations_by_id: Mapping[str, Mapping[str, Any]],
    unit_ids_by_node: Mapping[str, str],
) -> tuple[list[str], list[str]]:
    guard_id = str(candidate["id"])
    frontier = list(descendants_by_guard.get(guard_id, set()))
    descendant_ids = {guard_id}
    while frontier:
        descendant_id = frontier.pop()
        if descendant_id in descendant_ids:
            continue
        descendant_ids.add(descendant_id)
        frontier.extend(descendants_by_guard.get(descendant_id, set()))

    branch_unit_id = unit_ids_by_node.get(str(candidate["branch_node_id"]))
    if branch_unit_id is None:
        raise LC2ExplanationError(
            f"LC2 guard branch unit does not resolve: {guard_id}"
        )

    ordered_unit_ids: list[str] = []
    relevant_paths: list[Mapping[str, Any]] = []
    for path in ordered_paths:
        path_guard_ids = {
            str(step["guard_id"]) for step in path["branch_outcomes"]
        }
        if not path_guard_ids & descendant_ids:
            continue
        projection = path_projections[str(path["id"])]
        path_units = projection["ordered_unit_ids"]
        if branch_unit_id not in path_units:
            continue
        relevant_paths.append(projection)
        start_index = path_units.index(branch_unit_id)
        for unit_id in path_units[start_index:]:
            if unit_id not in ordered_unit_ids:
                ordered_unit_ids.append(unit_id)

    if branch_unit_id not in ordered_unit_ids:
        ordered_unit_ids.insert(0, branch_unit_id)
    member_set = set(ordered_unit_ids)
    ordered_relation_ids: list[str] = []
    for projection in relevant_paths:
        for relation_id in projection["ordered_relation_ids"]:
            relation = relations_by_id[relation_id]
            if (
                relation["source_unit_id"] in member_set
                and relation["target_unit_id"] in member_set
                and relation_id not in ordered_relation_ids
            ):
                ordered_relation_ids.append(relation_id)
    return ordered_unit_ids, ordered_relation_ids


def _populate_lc2_groups(model: dict[str, Any], guard: Mapping[str, Any]) -> None:
    unit_ids_by_node = _unit_ids_by_source_node(model)
    relations_by_edge = _relation_by_edge(model)
    relations_by_id = {relation["id"]: relation for relation in model["relations"]}
    guards_by_id = {
        str(candidate["id"]): candidate for candidate in guard["guards"]
    }
    ordered_paths = sorted(
        guard["outcome_paths"],
        key=lambda path: _lc2_path_execution_key(path, guards_by_id),
    )
    guard_order_by_id: dict[str, tuple[int, int]] = {}
    for path_index, path in enumerate(ordered_paths):
        for branch_index, branch_outcome in enumerate(path["branch_outcomes"]):
            guard_order_by_id.setdefault(
                str(branch_outcome["guard_id"]), (path_index, branch_index)
            )
    predicate_attachments = {
        str(attachment["guard_id"]): attachment
        for attachment in guard["predicate_attachments"]
    }
    predicate_labels = {
        guard_id: str(attachment["source_port_label"])
        for guard_id, attachment in predicate_attachments.items()
    }

    path_projections: dict[str, dict[str, Any]] = {}
    for path in ordered_paths:
        ordered_unit_ids, ordered_relation_ids, expected_exit = (
            _ordered_lc2_path_facts(path, relations_by_edge, unit_ids_by_node)
        )
        reconvergence = relations_by_edge.get(str(path["reconvergence_edge_id"]))
        if reconvergence is None:
            raise LC2ExplanationError(
                f"LC2 outcome path reconvergence relation does not resolve: {path['id']}"
            )
        if reconvergence["id"] in ordered_relation_ids:
            raise LC2ExplanationError(
                f"LC2 outcome path owns its reconvergence relation: {path['id']}"
            )
        group_id = f"group.outcome_path.{path['id']}"
        path_projections[str(path["id"])] = {
            "id": group_id,
            "kind": "outcome_path",
            "title": _outcome_path_title(path, predicate_labels),
            "ordered_unit_ids": ordered_unit_ids,
            "ordered_relation_ids": ordered_relation_ids,
            "entry_unit_id": ordered_unit_ids[0],
            "exit_unit_id": expected_exit,
            "parent_group_id": None,
            "entered_by": None,
            "member_count": len(ordered_unit_ids),
            "projection_status": "COMPLETE",
            "diagnostic_code": "",
            "claim_evidence": _outcome_path_claim_evidence(
                path, predicate_attachments
            ),
        }

    guards = sorted(
        guard["guards"],
        key=lambda candidate: (
            int(candidate["depth"]),
            guard_order_by_id[str(candidate["id"])],
        ),
    )
    descendants_by_guard = _lc2_descendants_by_guard(guards)
    for candidate in guards:
        guard_id = str(candidate["id"])
        branch_unit_id = unit_ids_by_node.get(str(candidate["branch_node_id"]))
        if branch_unit_id is None:
            raise LC2ExplanationError(
                f"LC2 guard branch unit does not resolve: {guard_id}"
            )
        ordered_unit_ids, ordered_relation_ids = _ordered_lc2_guard_facts(
            candidate,
            ordered_paths,
            path_projections,
            descendants_by_guard,
            relations_by_id,
            unit_ids_by_node,
        )
        parent_id = candidate["parent_guard_id"]
        parent_group_id = (
            f"group.guard_nest.{parent_id}" if parent_id is not None else None
        )
        entered_by = candidate["entered_by_parent_outcome"]
        if entered_by is not None:
            entered_by = {
                "true": "condition_true",
                "false": "condition_false",
            }.get(str(entered_by))
            if entered_by is None:
                raise LC2ExplanationError(
                    f"LC2 guard parent outcome is not a frozen branch outcome: {guard_id}"
                )
        predicate_attachment = predicate_attachments.get(guard_id)
        if predicate_attachment is None:
            raise LC2ExplanationError(
                f"LC2 guard predicate attachment does not resolve: {guard_id}"
            )
        group_id = f"group.guard_nest.{guard_id}"
        path_projections[group_id] = {
            "id": group_id,
            "kind": "guard_nest",
            "title": f"{predicate_attachment['source_port_label']} guard",
            "ordered_unit_ids": ordered_unit_ids,
            "ordered_relation_ids": ordered_relation_ids,
            "entry_unit_id": branch_unit_id,
            "parent_group_id": parent_group_id,
            "entered_by": entered_by,
            "member_count": len(ordered_unit_ids),
            "projection_status": "COMPLETE",
            "diagnostic_code": "",
            "claim_evidence": _guard_nest_claim_evidence(
                candidate,
                predicate_attachment,
            ),
        }

    model["groups"] = [
        path_projections[str(path["id"])]
        for path in ordered_paths
    ] + [
        path_projections[f"group.guard_nest.{candidate['id']}"]
        for candidate in guards
    ]
    incomparable_pairs = {
        frozenset((str(first), str(second)))
        for first, second in guard["partial_order"][
            "incomparable_terminal_pairs"
        ]
    }
    ordered_path_ids = [str(path["id"]) for path in ordered_paths]
    incomparable_group_ids = [
        [
            f"group.outcome_path.{first}",
            f"group.outcome_path.{second}",
        ]
        for first, second in combinations(ordered_path_ids, 2)
        if frozenset((first, second)) in incomparable_pairs
    ]
    model["group_partial_order"] = {
        "incomparable_group_ids": incomparable_group_ids,
        "semantics": GROUP_PARTIAL_ORDER_SEMANTICS,
    }


def _populate_lc2_semantics(model: dict[str, Any], guard: Mapping[str, Any]) -> None:
    _populate_lc2_relation_semantics(model)
    _populate_lc2_disambiguators(model)
    _populate_lc2_groups(model, guard)


def _validate_lc2_group_claim_evidence(
    group: Mapping[str, Any],
    expected_evidence: Sequence[Mapping[str, str]],
) -> None:
    expected_by_component = {
        entry["component"]: (entry["fact_owner"], entry["source"])
        for entry in expected_evidence
    }
    actual_evidence = group.get("claim_evidence")
    if not isinstance(actual_evidence, list):
        raise LC2ExplanationError(
            f"LC2 group claim evidence is not an array: {group.get('id')}"
        )
    actual_by_component: dict[str, tuple[Any, Any]] = {}
    for entry in actual_evidence:
        if not isinstance(entry, Mapping):
            raise LC2ExplanationError(
                "LC2 group claim evidence entry is not an object: "
                f"{group.get('id')}"
            )
        component = entry.get("component")
        if not isinstance(component, str):
            raise LC2ExplanationError(
                "LC2 group claim evidence component is not a string: "
                f"{group.get('id')}"
            )
        if component in actual_by_component:
            raise LC2ExplanationError(
                "LC2 group claim evidence component is duplicated: "
                f"{group.get('id')}.{component}"
            )
        if component not in expected_by_component:
            raise LC2ExplanationError(
                "LC2 group claim evidence component does not resolve against "
                f"guard truth: {group.get('id')}.{component}"
            )
        actual_fact = (entry.get("fact_owner"), entry.get("source"))
        expected_fact = expected_by_component[component]
        if actual_fact != expected_fact:
            raise LC2ExplanationError(
                "LC2 group claim evidence does not resolve against guard truth: "
                f"{group.get('id')}.{component} declared "
                f"fact_owner/source={actual_fact!r}, expected={expected_fact!r}"
            )
        actual_by_component[component] = actual_fact
    missing_components = sorted(
        set(expected_by_component) - set(actual_by_component)
    )
    if missing_components:
        raise LC2ExplanationError(
            "LC2 group claim evidence is incomplete against guard truth: "
            f"{group.get('id')}: missing {missing_components}"
        )


def _validate_lc2_groups_against_guard(
    model: Mapping[str, Any],
    guard: Mapping[str, Any],
    units_by_node: Mapping[str, str],
    relations_by_edge: Mapping[str, Mapping[str, Any]],
) -> None:
    groups = model.get("groups")
    if groups is None:
        return
    if not isinstance(groups, list):
        raise LC2ExplanationError("LC2 groups must be an array")

    groups_by_id: dict[str, Mapping[str, Any]] = {}
    for group in groups:
        if not isinstance(group, Mapping) or not isinstance(group.get("id"), str):
            raise LC2ExplanationError("LC2 groups must have string IDs")
        group_id = group["id"]
        if group_id in groups_by_id:
            raise LC2ExplanationError(
                f"LC2 group IDs must be unique: {group_id}"
            )
        groups_by_id[group_id] = group

    expected_path_ids = {
        f"group.outcome_path.{path['id']}" for path in guard["outcome_paths"]
    }
    expected_guard_ids = {
        f"group.guard_nest.{candidate['id']}"
        for candidate in guard["guards"]
    }
    expected_group_ids = expected_path_ids | expected_guard_ids
    if len(groups) != len(expected_group_ids):
        raise LC2ExplanationError(
            "LC2 groups must contain exactly three outcome_path and two "
            f"guard_nest groups: found {len(groups)}"
        )
    if set(groups_by_id) != expected_group_ids:
        raise LC2ExplanationError(
            "LC2 group IDs do not match guard truth: "
            f"declared={sorted(groups_by_id)}, expected={sorted(expected_group_ids)}"
        )

    reconvergence_relation_ids = {
        str(fact["edge_id"])
        for fact in guard["partial_order"]["reconvergence_ownership"]
    }
    for group in groups:
        overlap = sorted(
            set(group.get("ordered_relation_ids", ()))
            & reconvergence_relation_ids
        )
        if overlap:
            raise LC2ExplanationError(
                "LC2 reconvergence relations must stay outside every group: "
                f"{group['id']} owns {overlap}"
            )

    predicate_attachments = {
        str(attachment["guard_id"]): attachment
        for attachment in guard["predicate_attachments"]
    }
    predicate_labels = {
        guard_id: str(attachment["source_port_label"])
        for guard_id, attachment in predicate_attachments.items()
    }
    guards_by_id = {
        str(candidate["id"]): candidate for candidate in guard["guards"]
    }
    ordered_paths = sorted(
        guard["outcome_paths"],
        key=lambda path: _lc2_path_execution_key(path, guards_by_id),
    )
    path_projections: dict[str, dict[str, list[str]]] = {}
    for path in ordered_paths:
        expected_unit_ids, expected_relation_ids, _ = _ordered_lc2_path_facts(
            path, relations_by_edge, units_by_node
        )
        path_projections[str(path["id"])] = {
            "ordered_unit_ids": expected_unit_ids,
            "ordered_relation_ids": expected_relation_ids,
        }

    for path in ordered_paths:
        group_id = f"group.outcome_path.{path['id']}"
        group = groups_by_id[group_id]
        if group.get("kind") != "outcome_path":
            raise LC2ExplanationError(
                f"LC2 group kind does not match guard truth: {group_id}"
            )
        if group.get("parent_group_id") is not None:
            raise LC2ExplanationError(
                "LC2 outcome_path must not declare parent_group_id: "
                f"{group_id}"
            )
        if group.get("entered_by") is not None:
            raise LC2ExplanationError(
                "LC2 outcome_path must not declare entered_by: "
                f"{group_id}"
            )
        expected_unit_ids, expected_relation_ids, expected_exit = (
            _ordered_lc2_path_facts(path, relations_by_edge, units_by_node)
        )
        if group.get("ordered_unit_ids") != expected_unit_ids:
            raise LC2ExplanationError(
                "LC2 outcome_path group members do not match guard truth: "
                f"{group_id}"
            )
        if group.get("ordered_relation_ids") != expected_relation_ids:
            raise LC2ExplanationError(
                "LC2 outcome_path group relation order does not match guard "
                f"truth: {group_id}"
            )
        if group.get("member_count") != len(expected_unit_ids):
            raise LC2ExplanationError(
                "LC2 outcome_path member_count does not match guard truth: "
                f"{group_id}"
            )
        if group.get("entry_unit_id") != expected_unit_ids[0]:
            raise LC2ExplanationError(
                "LC2 outcome_path entry_unit_id does not match guard truth: "
                f"{group_id}"
            )
        expected_title = _outcome_path_title(path, predicate_labels)
        if group.get("title") != expected_title:
            raise LC2ExplanationError(
                "LC2 outcome_path group title does not match guard truth: "
                f"{group_id}"
            )
        if group.get("exit_unit_id") != expected_exit:
            raise LC2ExplanationError(
                "LC2 outcome_path exit_unit_id does not match its ledger "
                f"set_node_id: {group_id}"
            )
        reconvergence = relations_by_edge.get(str(path["reconvergence_edge_id"]))
        if reconvergence is None:
            raise LC2ExplanationError(
                "LC2 outcome_path reconvergence relation does not resolve: "
                f"{group_id}"
            )
        _validate_lc2_group_claim_evidence(
            group,
            _outcome_path_claim_evidence(path, predicate_attachments),
        )

    expected_entered_by = {"true": "condition_true", "false": "condition_false"}
    descendants_by_guard = _lc2_descendants_by_guard(guard["guards"])
    relations_by_id = {relation["id"]: relation for relation in model["relations"]}
    for declared_guard in guard["guards"]:
        guard_id = str(declared_guard["id"])
        group_id = f"group.guard_nest.{guard_id}"
        group = groups_by_id[group_id]
        if group.get("kind") != "guard_nest":
            raise LC2ExplanationError(
                f"LC2 group kind does not match guard truth: {group_id}"
            )
        expected_entry = units_by_node.get(str(declared_guard["branch_node_id"]))
        if group.get("entry_unit_id") != expected_entry:
            raise LC2ExplanationError(
                "LC2 guard_nest entry_unit_id does not match its ledger "
                f"branch_node_id: {group_id}"
            )
        parent_guard_id = declared_guard["parent_guard_id"]
        expected_parent = (
            f"group.guard_nest.{parent_guard_id}"
            if parent_guard_id is not None
            else None
        )
        if group.get("parent_group_id") != expected_parent:
            raise LC2ExplanationError(
                "LC2 guard_nest parent_group_id does not match guard truth: "
                f"{group_id}"
            )
        entered_by_parent_outcome = declared_guard["entered_by_parent_outcome"]
        expected_entered = (
            expected_entered_by.get(str(entered_by_parent_outcome))
            if entered_by_parent_outcome is not None
            else None
        )
        if group.get("entered_by") != expected_entered:
            raise LC2ExplanationError(
                "LC2 guard_nest entered_by does not match guard truth: "
                f"{group_id}"
            )
        expected_unit_ids, expected_relation_ids = _ordered_lc2_guard_facts(
            declared_guard,
            ordered_paths,
            path_projections,
            descendants_by_guard,
            relations_by_id,
            units_by_node,
        )
        if group.get("ordered_unit_ids") != expected_unit_ids:
            raise LC2ExplanationError(
                "LC2 guard_nest group members do not match guard truth: "
                f"{group_id}"
            )
        if group.get("ordered_relation_ids") != expected_relation_ids:
            raise LC2ExplanationError(
                "LC2 guard_nest group relation order does not match guard truth: "
                f"{group_id}"
            )
        if group.get("member_count") != len(expected_unit_ids):
            raise LC2ExplanationError(
                "LC2 guard_nest member_count does not match guard truth: "
                f"{group_id}"
            )
        predicate_attachment = predicate_attachments.get(guard_id)
        if predicate_attachment is None:
            raise LC2ExplanationError(
                f"LC2 guard predicate attachment does not resolve: {guard_id}"
            )
        expected_title = f"{predicate_attachment['source_port_label']} guard"
        if group.get("title") != expected_title:
            raise LC2ExplanationError(
                "LC2 guard_nest title does not match guard truth: "
                f"{group_id}"
            )
        _validate_lc2_group_claim_evidence(
            group,
            _guard_nest_claim_evidence(declared_guard, predicate_attachment),
        )

    partial_order = model.get("group_partial_order")
    if not isinstance(partial_order, Mapping):
        raise LC2ExplanationError("LC2 group_partial_order is missing")
    expected_pairs = Counter(
        tuple(
            sorted(
                (
                    f"group.outcome_path.{first}",
                    f"group.outcome_path.{second}",
                )
            )
        )
        for first, second in guard["partial_order"][
            "incomparable_terminal_pairs"
        ]
    )
    actual_pairs = Counter(
        tuple(sorted((str(pair[0]), str(pair[1]))))
        for pair in partial_order.get("incomparable_group_ids", ())
    )
    if actual_pairs != expected_pairs:
        raise LC2ExplanationError(
            "LC2 group_partial_order does not match guard truth: "
            "incomparable_group_ids"
        )


def validate_lc2_guard_explanation(
    model: Mapping[str, Any],
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    guard_truth_path: Path,
    *,
    readiness_path: Path | None = None,
) -> None:
    """Cross-check the projected Explanation against frozen LC2 guard truth."""

    _, guard = _validate_freshness(
        Path(ir_path),
        Path(slice_path),
        Path(asset_file),
        Path(guard_truth_path),
        readiness_path,
    )
    if model.get("counts") != {
        "lanes": 6,
        "units": 9,
        "relations": 10,
        "source_nodes": 9,
        "source_edges": 10,
    }:
        raise LC2ExplanationError("LC2 Explanation declared counts do not match 9/10")
    model_source = model.get("source")
    if not isinstance(model_source, dict):
        raise LC2ExplanationError("LC2 Explanation source must be an object")
    if (
        model_source.get("ir_sha256") != _sha256(ir_path)
        or model_source.get("slice_sha256") != _sha256(slice_path)
        or model_source.get("blueprint_package_sha256") != _sha256(asset_file)
        or model_source.get("blueprint_asset_path") != LC2_BLUEPRINT_PATH
        or model_source.get("graph_id") != guard["source"]["graph_id"]
    ):
        raise LC2ExplanationError(
            "LC2 Explanation source provenance disagrees with frozen inputs"
        )
    relations_by_edge = _relation_by_edge(model)
    guard_node_ids = {
        str(reference["node_id"]) for reference in guard["source_references"]
    }
    units_by_node: dict[str, str] = {}
    for unit in model["units"]:
        references = unit["source_references"]
        if len(references) != 1:
            raise LC2ExplanationError("LC2 units must own disjoint single source nodes")
        node_id = references[0]["source_node_id"]
        if node_id in units_by_node:
            raise LC2ExplanationError("LC2 source node ownership must be disjoint")
        units_by_node[node_id] = unit["id"]
    if set(units_by_node) != guard_node_ids:
        raise LC2ExplanationError("LC2 source node ownership disagrees with guard truth")

    accounted_edges: list[str] = []
    for declared_guard in guard["guards"]:
        branch_node_id = str(declared_guard["branch_node_id"])
        for outcome, fact in declared_guard["outcome_edges"].items():
            edge_id = str(fact["edge_id"])
            relation = relations_by_edge.get(edge_id)
            if relation is None:
                raise LC2ExplanationError(
                    f"LC2 guard truth outcome edge is missing: {edge_id}"
                )
            _assert_relation_fact(
                relation,
                fact,
                kind="controls_execution",
                label="then" if outcome == "true" else "else",
            )
            if relation["source_unit_id"] != units_by_node[branch_node_id]:
                raise LC2ExplanationError(
                    f"LC2 guard truth outcome source mismatch: {edge_id}"
                )
            accounted_edges.append(edge_id)

    for attachment in guard["predicate_attachments"]:
        edge_id = str(attachment["edge_id"])
        relation = relations_by_edge.get(edge_id)
        if relation is None:
            raise LC2ExplanationError(
                f"LC2 guard truth predicate edge is missing: {edge_id}"
            )
        fact = {
            "edge_id": edge_id,
            "source_node_id": attachment["predicate_node_id"],
            "source_pin_id": attachment["source_pin_id"],
            "source_port_label": attachment["source_port_label"],
            "target_node_id": attachment["branch_node_id"],
            "target_pin_id": attachment["condition_pin_id"],
            "target_port_label": attachment["target_port_label"],
        }
        _assert_relation_fact(
            relation, fact, kind="predicate_for", label="Condition"
        )
        if (
            relation["source_unit_id"]
            != units_by_node[str(attachment["predicate_node_id"])]
            or relation["target_unit_id"]
            != units_by_node[str(attachment["branch_node_id"])]
        ):
            raise LC2ExplanationError(
                f"LC2 guard truth predicate attachment mismatch: {edge_id}"
            )
        accounted_edges.append(edge_id)

    criterion_node_id = str(guard["criterion"]["node_id"])
    for fact in guard["partial_order"]["reconvergence_ownership"]:
        edge_id = str(fact["edge_id"])
        relation = relations_by_edge.get(edge_id)
        if relation is None:
            raise LC2ExplanationError(
                f"LC2 guard truth reconvergence edge is missing: {edge_id}"
            )
        _assert_relation_fact(
            relation, fact, kind="execution_predecessor", label=fact["source_port_label"]
        )
        if relation["target_unit_id"] != units_by_node[criterion_node_id]:
            raise LC2ExplanationError(
                f"LC2 guard truth reconvergence target mismatch: {edge_id}"
            )
        accounted_edges.append(edge_id)

    entry_edge_id = str(guard["entry"]["entry_edge_id"])
    entry_relation = relations_by_edge.get(entry_edge_id)
    if entry_relation is None or entry_relation["kind"] != "execution_predecessor":
        raise LC2ExplanationError("LC2 guard truth entry edge is missing or misclassified")
    if (
        entry_relation["source_unit_id"]
        != units_by_node[str(guard["entry"]["node_id"])]
        or entry_relation["target_unit_id"]
        != units_by_node[str(guard["guards"][0]["branch_node_id"])]
    ):
        raise LC2ExplanationError("LC2 guard truth entry edge direction mismatch")
    accounted_edges.append(entry_edge_id)

    if len(accounted_edges) != len(set(accounted_edges)) or set(accounted_edges) != set(
        relations_by_edge
    ):
        raise LC2ExplanationError("LC2 guard truth must account for every edge exactly once")
    if Counter(relation["kind"] for relation in model["relations"]) != Counter(
        {
            "controls_execution": 4,
            "execution_predecessor": 4,
            "predicate_for": 2,
        }
    ):
        raise LC2ExplanationError("LC2 Explanation relation-kind counts are not exact")

    outcome_nodes = {
        str(path["id"]): str(path["set_node_id"])
        for path in guard["outcome_paths"]
    }
    expected_pairs = {
        frozenset(pair) for pair in combinations(outcome_nodes, 2)
    }
    actual_pairs = {
        frozenset(pair)
        for pair in guard["partial_order"].get(
            "incomparable_terminal_pairs", ()
        )
    }
    if actual_pairs != expected_pairs:
        raise LC2ExplanationError("LC2 guard truth incomparable outcome pairs are incomplete")
    adjacency: dict[str, list[str]] = defaultdict(list)
    for relation in model["relations"]:
        if relation["kind"] in {"execution_predecessor", "controls_execution"}:
            adjacency[relation["source_unit_id"]].append(relation["target_unit_id"])
    for pair in expected_pairs:
        first, second = tuple(pair)
        first_unit = units_by_node[outcome_nodes[first]]
        second_unit = units_by_node[outcome_nodes[second]]
        if _is_reachable(adjacency, first_unit, second_unit) or _is_reachable(
            adjacency, second_unit, first_unit
        ):
            raise LC2ExplanationError(
                f"LC2 outcome terminals must remain incomparable: {first}/{second}"
            )
    _validate_lc2_groups_against_guard(
        model,
        guard,
        units_by_node,
        relations_by_edge,
    )


def build_lc2_explanation(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    guard_truth_path: Path,
    *,
    readiness_path: Path | None = None,
) -> dict[str, Any]:
    """Build the complete LC2 Explanation from validated source truth."""

    ir_path = Path(ir_path).resolve()
    slice_path = Path(slice_path).resolve()
    asset_file = Path(asset_file).resolve()
    guard_truth_path = Path(guard_truth_path).resolve()
    if readiness_path is not None:
        readiness_path = Path(readiness_path).resolve()
    _, guard = _validate_freshness(
        ir_path,
        slice_path,
        asset_file,
        guard_truth_path,
        readiness_path,
        validate_ledger=False,
    )
    unit_specs, relation_specs = _lc2_projection_specs(ir_path, slice_path)
    try:
        model = build_complete_dag_explanation(
            ir_path,
            slice_path,
            asset_file,
            unit_specs,
            relation_specs,
            question=LC2_QUESTION,
        )
    except ExplanationModelError as error:
        raise LC2ExplanationError(str(error)) from error
    validate_lc2_guard_explanation(
        model,
        ir_path,
        slice_path,
        asset_file,
        guard_truth_path,
        readiness_path=readiness_path,
    )
    _populate_lc2_semantics(model, guard)
    validate_lc2_guard_explanation(
        model,
        ir_path,
        slice_path,
        asset_file,
        guard_truth_path,
        readiness_path=readiness_path,
    )
    return model


def _ensure_publish_parent(target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)


def _write_publish_temporary(
    target: Path, payload: bytes, marker: str
) -> Path:
    descriptor, temporary_name = tempfile.mkstemp(
        dir=target.parent,
        prefix=f".{target.name}.{marker}-",
        suffix=".tmp",
    )
    temporary_path = Path(temporary_name)
    stream_opened = False
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream_opened = True
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        if temporary_path.read_bytes() != payload:
            raise LC2ExplanationError(
                f"LC2 {marker} verification failed for {target}"
            )
        return temporary_path
    except Exception:
        if not stream_opened:
            os.close(descriptor)
        temporary_path.unlink(missing_ok=True)
        raise


def _write_staged_payload(target: Path, payload: bytes) -> Path:
    return _write_publish_temporary(target, payload, "stage")


def _write_backup_payload(target: Path, payload: bytes) -> Path:
    return _write_publish_temporary(target, payload, "backup")


def _replace_for_publish(source: Path, target: Path) -> None:
    os.replace(source, target)


def _rollback_publication(
    states: Sequence[_PublishTarget],
) -> list[_RollbackTargetResult]:
    results: list[_RollbackTargetResult] = []
    for state in reversed(states):
        restored = False
        error_text: str | None = None
        try:
            if state.existed:
                try:
                    target_is_original = (
                        state.path.exists()
                        and state.path.read_bytes() == state.original_payload
                    )
                except OSError:
                    target_is_original = False
                if not target_is_original:
                    if state.backup_path is None or not state.backup_path.exists():
                        raise OSError("verified backup is unavailable")
                    backup_payload = state.backup_path.read_bytes()
                    if backup_payload != state.original_payload:
                        raise OSError(
                            "verified backup no longer matches the original target"
                        )
                    state.restore_path = _write_publish_temporary(
                        state.path, backup_payload, "stage-restore"
                    )
                    os.replace(state.restore_path, state.path)
                if state.path.read_bytes() != state.original_payload:
                    raise OSError("restored bytes do not match the original target")
                restored = True
            else:
                state.path.unlink(missing_ok=True)
                if state.path.exists():
                    raise OSError("new target could not be removed")
                restored = True
        except (OSError, LC2ExplanationError) as error:
            error_text = str(error)
            try:
                restored = (
                    state.path.exists()
                    and state.path.read_bytes() == state.original_payload
                    if state.existed
                    else not state.path.exists()
                )
            except OSError:
                restored = False
        transaction_paths = tuple(
            path
            for path in (state.staged_path, state.backup_path, state.restore_path)
            if path is not None and path.exists()
        )
        results.append(
            _RollbackTargetResult(
                target_path=state.path,
                restored=restored,
                retain_transaction_files=not restored,
                backup_path=state.backup_path,
                original_payload=state.original_payload,
                retained_paths=transaction_paths if not restored else (),
                error=error_text,
            )
        )
    return results


def _cleanup_publication(
    states: Sequence[_PublishTarget],
    *,
    cleanup_targets: set[Path] | None = None,
) -> list[str]:
    errors: list[str] = []
    for state in states:
        if cleanup_targets is not None and state.path not in cleanup_targets:
            continue
        for temporary_path in (
            state.staged_path,
            state.backup_path,
            state.restore_path,
        ):
            if temporary_path is None:
                continue
            try:
                temporary_path.unlink(missing_ok=True)
            except OSError as error:
                errors.append(f"{temporary_path}: {error}")
    return errors


def _rollback_diagnostic(results: Sequence[_RollbackTargetResult]) -> str:
    failures = [result for result in results if not result.restored]
    if not failures:
        return ""
    details = ["rollback incomplete"]
    for result in failures:
        recovery = f"recovery required: target={result.target_path}"
        backup_path = result.backup_path
        if backup_path is not None and backup_path.exists():
            try:
                backup_payload = backup_path.read_bytes()
            except OSError as error:
                recovery += (
                    f"; retained_backup={backup_path}; backup_read_error={error}"
                )
            else:
                backup_hash = hashlib.sha256(backup_payload).hexdigest().upper()
                if backup_payload == result.original_payload:
                    recovery += (
                        f"; verified_old_backup={backup_path}; "
                        f"sha256={backup_hash}"
                    )
                else:
                    recovery += (
                        f"; retained_backup={backup_path}; sha256={backup_hash}; "
                        "backup_verification=does_not_match_original"
                    )
        else:
            recovery += "; verified_old_backup=unavailable"
        if result.error:
            recovery += f"; rollback_error={result.error}"
        if result.retained_paths:
            recovery += "; retained_transaction_files=" + ",".join(
                str(path) for path in result.retained_paths
            )
        details.append(recovery)
    return "; ".join(details)


def _publish_validated_payload(payload: bytes, targets: Sequence[Path]) -> None:
    resolved_targets = [Path(target).resolve() for target in targets]
    if len(resolved_targets) != len(set(resolved_targets)):
        raise LC2ExplanationError("LC2 publication targets must be distinct")
    states = [_PublishTarget(path=target) for target in resolved_targets]
    try:
        for state in states:
            state.existed = state.path.exists()
            if state.existed:
                state.original_payload = state.path.read_bytes()
        for state in states:
            _ensure_publish_parent(state.path)
        for state in states:
            state.staged_path = _write_staged_payload(state.path, payload)
        for state in states:
            if state.existed:
                assert state.original_payload is not None
                state.backup_path = _write_backup_payload(
                    state.path, state.original_payload
                )
        for state in states:
            if state.existed and state.path.read_bytes() != state.original_payload:
                raise OSError(f"publication target changed before replace: {state.path}")
        for state in states:
            assert state.staged_path is not None
            _replace_for_publish(state.staged_path, state.path)
        for state in states:
            if state.path.read_bytes() != payload:
                raise OSError(f"published bytes failed verification: {state.path}")
    except (OSError, LC2ExplanationError) as error:
        rollback_results = _rollback_publication(states)
        restored_targets = {
            result.target_path
            for result in rollback_results
            if not result.retain_transaction_files
        }
        cleanup_errors = _cleanup_publication(
            states, cleanup_targets=restored_targets
        )
        detail = f"LC2 publication failed: {error}"
        rollback_detail = _rollback_diagnostic(rollback_results)
        if rollback_detail:
            detail += "; " + rollback_detail
        if cleanup_errors:
            detail += "; cleanup residue: " + "; ".join(cleanup_errors)
        raise LC2ExplanationError(detail) from error

    # Both targets matching the payload is the transaction commit point. After
    # it, backups are maintenance-only and can no longer be used for rollback:
    # cleanup may already have removed only a subset of them.
    cleanup_errors = _cleanup_publication(states)
    if cleanup_errors:
        residue = sorted(
            str(temporary_path)
            for state in states
            for temporary_path in (
                state.staged_path,
                state.backup_path,
                state.restore_path,
            )
            if temporary_path is not None and temporary_path.exists()
        )
        raise LC2ExplanationError(
            "LC2 publication committed with verified byte-identical targets; "
            "cleanup residue: "
            + (", ".join(residue) if residue else "none remains")
            + "; cleanup errors: "
            + "; ".join(cleanup_errors)
        )


def build_lc2_explanation_artifacts(
    ir_path: Path,
    slice_path: Path,
    asset_file: Path,
    guard_truth_path: Path,
    explanation_schema_path: Path,
    output_path: Path,
    *,
    renderer_copy_path: Path | None = None,
    readiness_path: Path | None = None,
) -> dict[str, Path]:
    """Validate all LC2 truth contracts before publishing either copy."""

    ir_path = Path(ir_path).resolve()
    slice_path = Path(slice_path).resolve()
    asset_file = Path(asset_file).resolve()
    guard_truth_path = Path(guard_truth_path).resolve()
    output_path = Path(output_path).resolve()
    model = build_lc2_explanation(
        ir_path,
        slice_path,
        asset_file,
        guard_truth_path,
        readiness_path=readiness_path,
    )
    try:
        validate_explanation_model(
            model,
            ir_path,
            slice_path,
            asset_file,
            Path(explanation_schema_path),
        )
    except ExplanationModelError as error:
        raise LC2ExplanationError(str(error)) from error
    validate_lc2_guard_explanation(
        model,
        ir_path,
        slice_path,
        asset_file,
        guard_truth_path,
        readiness_path=readiness_path,
    )
    payload = canonical_explanation_bytes(model)

    outputs = {"explanation": output_path}
    targets = [output_path]
    if renderer_copy_path is not None:
        renderer_copy = Path(renderer_copy_path).resolve()
        targets.append(renderer_copy)
        outputs["renderer_copy"] = renderer_copy
    _publish_validated_payload(payload, targets)
    return outputs
