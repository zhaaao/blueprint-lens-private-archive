"""Adapt generic M6 Explanations to the Stage 1 composite rail.

Every valid fallback Explanation has an execution rail. Control and criterion
units become rail stations, each role=boundary unit becomes a typed cap, and
the value/predicate plus non-rail-relation remainder stays explicitly counted
until the later composite stages give it an attachment.
"""

from __future__ import annotations

from collections import Counter
from collections.abc import Mapping
import hashlib
import json
from pathlib import Path
from typing import Any


_MEASURED_ON = "2026-08-24"
_MANIFEST_RELATIVE = "fixtures/m7/m7-corpus-manifest.v1.json"
_TYPED_IR_RELATIVE = "artifacts/m7/export/run1/typed-ir"
_G7_EVIDENCE_RELATIVE = "artifacts/m7/g7/g7-evidence.v1.json"
_REPORT_RELATIVE = "artifacts/m10/lc1-explanation-adapter/lc1-explanation-adapter.v1.json"
_SCHEMA_RELATIVE = "schemas/blueprint-lens-m10-lc1-explanation-adapter-v1.schema.json"
_RAIL_UNIT_ROLES = frozenset({"criterion", "control", "boundary"})
_DEFERRED_UNIT_ROLES = frozenset({"value", "predicate"})
_UNIT_ROLES = _RAIL_UNIT_ROLES | _DEFERRED_UNIT_ROLES | frozenset({"consequence"})
_SEMANTIC_STATUSES = frozenset({"supported", "opaque", "uncertain", "unsupported"})
_CARRIED_RELATION_KINDS = frozenset({"execution_predecessor"})
_DEFERRED_RELATION_KINDS = frozenset(
    {"controls_execution", "provides_value", "predicate_for"}
)
_SEGMENT_KINDS = ("Entry", "StraightRun", "CriterionFocus")
_BOUNDARY_CAP_KIND = "BoundaryCap"
_FROZEN_LC1_ASSET_PATH = "/Game/LensCorpus/BP_LC1_LongChain.BP_LC1_LongChain"
_FROZEN_LC1_UNIT_COUNT = 14
_FROZEN_LC1_RELATION_COUNT = 13
_EXPECTED_MANIFEST_GRAPH_COUNT = 10
_EXPECTED_FALLBACK_COUNT = 249


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _is_sha256_text(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and all(character in "0123456789abcdefABCDEF" for character in value)
    )


def _ids(items: Any, key: str) -> list[str]:
    if not isinstance(items, list):
        return []
    return [str(item[key]) for item in items if isinstance(item, Mapping) and isinstance(item.get(key), str)]


def _boundary_ids(units: list[Mapping[str, Any]]) -> list[str]:
    return [
        str(unit["id"])
        for unit in units
        if isinstance(unit.get("id"), str)
        and (unit.get("role") == "boundary" or unit.get("semantic_status") != "supported")
    ]


def _unit_semantic_facts(units: list[Mapping[str, Any]]) -> list[dict[str, str]]:
    """Carry the minimum unit facts needed to audit Stage 1 locally."""

    return [
        {
            "id": str(unit["id"]),
            "role": str(unit["role"]),
            "semantic_status": str(unit["semantic_status"]),
        }
        for unit in units
        if isinstance(unit.get("id"), str)
        and isinstance(unit.get("role"), str)
        and isinstance(unit.get("semantic_status"), str)
    ]


_CAP_DISCLOSURES = {
    "opaque": (
        "Traversal stops at this opaque call; behavior beyond this cap is not "
        "claimed by the slice."
    ),
    "unsupported": (
        "Traversal stops at this unsupported node; its behavior is not claimed "
        "by the slice."
    ),
    "uncertain": (
        "Traversal stops at this uncertain dependency; the relationship beyond "
        "this cap is not claimed by the slice."
    ),
}


def _cap_disclosure(semantic_status: str) -> str:
    return _CAP_DISCLOSURES.get(
        semantic_status,
        "Traversal stops at this boundary; behavior beyond this cap is not claimed "
        "by the slice.",
    )


def _result_base(
    *,
    outcome: str,
    reason_code: str,
    reason: str,
    criterion_unit_id: str | None,
    input_unit_ids: list[str],
    input_relation_ids: list[str],
    boundary_unit_ids: list[str],
    carried_unit_ids: list[str],
    carried_relation_ids: list[str],
    segments: list[dict[str, Any]],
    not_carried_unit_ids: list[str] | None = None,
    not_carried_relation_ids: list[str] | None = None,
    input_unit_semantics: list[dict[str, str]] | None = None,
) -> dict[str, Any]:
    if not_carried_unit_ids is None:
        not_carried_unit_ids = [] if outcome == "accepted" else list(input_unit_ids)
    if not_carried_relation_ids is None:
        not_carried_relation_ids = [] if outcome == "accepted" else list(input_relation_ids)
    return {
        "outcome": outcome,
        "reason_code": reason_code,
        "reason": reason,
        "criterion_unit_id": criterion_unit_id,
        "boundary_unit_ids": list(boundary_unit_ids),
        "boundary_unit_count": len(boundary_unit_ids),
        "input_unit_ids": list(input_unit_ids),
        "input_unit_count": len(input_unit_ids),
        "input_unit_semantics": list(input_unit_semantics or []),
        "carried_unit_ids": list(carried_unit_ids),
        "carried_unit_count": len(carried_unit_ids),
        "not_carried_unit_ids": not_carried_unit_ids,
        "not_carried_unit_count": len(not_carried_unit_ids),
        "input_relation_ids": list(input_relation_ids),
        "input_relation_count": len(input_relation_ids),
        "carried_relation_ids": list(carried_relation_ids),
        "carried_relation_count": len(carried_relation_ids),
        "not_carried_relation_ids": not_carried_relation_ids,
        "not_carried_relation_count": len(not_carried_relation_ids),
        "segments": segments,
    }


def _refused(
    *,
    reason_code: str,
    reason: str,
    criterion_unit_id: str | None,
    input_unit_ids: list[str],
    input_relation_ids: list[str],
    boundary_unit_ids: list[str],
) -> dict[str, Any]:
    return _result_base(
        outcome="refused",
        reason_code=reason_code,
        reason=reason,
        criterion_unit_id=criterion_unit_id,
        input_unit_ids=input_unit_ids,
        input_relation_ids=input_relation_ids,
        boundary_unit_ids=boundary_unit_ids,
        carried_unit_ids=[],
        carried_relation_ids=[],
        segments=[],
    )


def _refused_from_shape(
    explanation: Any,
    reason_code: str,
    reason: str,
) -> dict[str, Any]:
    if not isinstance(explanation, Mapping):
        return _refused(
            reason_code=reason_code,
            reason=reason,
            criterion_unit_id=None,
            input_unit_ids=[],
            input_relation_ids=[],
            boundary_unit_ids=[],
        )
    raw_units = explanation.get("units")
    raw_relations = explanation.get("relations")
    units = [item for item in raw_units if isinstance(item, Mapping)] if isinstance(raw_units, list) else []
    return _refused(
        reason_code=reason_code,
        reason=reason,
        criterion_unit_id=(
            str(explanation["criterion_unit_id"])
            if isinstance(explanation.get("criterion_unit_id"), str)
            else None
        ),
        input_unit_ids=_ids(raw_units, "id"),
        input_relation_ids=_ids(raw_relations, "id"),
        boundary_unit_ids=_boundary_ids(units),
    )


def _rail_order(
    unit_ids: list[str],
    criterion_unit_id: str,
    relations: list[Mapping[str, Any]],
) -> list[str]:
    """Order rail members from their far end towards the criterion.

    An execution slice may merge several predecessors and may contain an SCC.
    Stage 1 neither drops those members nor claims to fold them: it performs a
    deterministic backwards walk, then reserves folding for Stage 3.
    """

    order = {unit_id: index for index, unit_id in enumerate(unit_ids)}
    predecessors: dict[str, list[str]] = {unit_id: [] for unit_id in unit_ids}
    for relation in relations:
        source = relation["source_unit_id"]
        target = relation["target_unit_id"]
        predecessors[target].append(source)
    for values in predecessors.values():
        values.sort(key=order.__getitem__)

    state = {unit_id: 0 for unit_id in unit_ids}
    ordered: list[str] = []

    def visit(unit_id: str) -> None:
        if state[unit_id] == 2:
            return
        if state[unit_id] == 1:
            return
        state[unit_id] = 1
        for predecessor in predecessors[unit_id]:
            visit(predecessor)
        state[unit_id] = 2
        ordered.append(unit_id)

    visit(criterion_unit_id)
    for unit_id in unit_ids:
        visit(unit_id)
    ordered.remove(criterion_unit_id)
    ordered.append(criterion_unit_id)
    return ordered


def adapt_explanation_to_lc1(explanation: Mapping[str, Any]) -> dict[str, Any]:
    """Adapt a generic Explanation to the Stage 1 composite rail.

    Every valid Explanation gets the execution rail.  Control and criterion
    units are rail stations; boundary units are typed caps; Stage 2/3 content
    remains explicitly counted rather than turning one feature into a refusal
    of the whole slice.
    """

    if not isinstance(explanation, Mapping):
        return _refused_from_shape(
            explanation,
            "M10_LC1_ADAPTER_EXPLANATION_SHAPE_INVALID",
            "Explanation must be an object with units, relations and a criterion.",
        )
    raw_units = explanation.get("units")
    raw_relations = explanation.get("relations")
    input_unit_ids = _ids(raw_units, "id")
    input_relation_ids = _ids(raw_relations, "id")
    raw_unit_values = raw_units if isinstance(raw_units, list) else []
    raw_relation_values = raw_relations if isinstance(raw_relations, list) else []
    units = [item for item in raw_unit_values if isinstance(item, Mapping)]
    boundary_ids = _boundary_ids(units)
    criterion_value = explanation.get("criterion_unit_id")
    criterion_id = criterion_value if isinstance(criterion_value, str) and criterion_value else None

    if not isinstance(raw_units, list) or not isinstance(raw_relations, list):
        return _refused(
            reason_code="M10_LC1_ADAPTER_EXPLANATION_SHAPE_INVALID",
            reason="Explanation units and relations must both be arrays.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=input_relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    if len(units) != len(raw_units) or any(
        not isinstance(unit.get("id"), str) or not unit.get("id")
        for unit in units
    ):
        return _refused(
            reason_code="M10_LC1_ADAPTER_UNIT_SHAPE_INVALID",
            reason="Every Explanation unit must be an object with a non-empty id.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=input_relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    if len(input_unit_ids) != len(set(input_unit_ids)):
        return _refused(
            reason_code="M10_LC1_ADAPTER_UNIT_SHAPE_INVALID",
            reason="Explanation unit ids must be unique before LC1 can account for them.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=input_relation_ids,
            boundary_unit_ids=boundary_ids,
        )

    if any(
        not isinstance(unit.get("role"), str)
        or not isinstance(unit.get("title"), str)
        or not unit["title"].strip()
        or not isinstance(unit.get("semantic_status"), str)
        or not unit["semantic_status"].strip()
        for unit in units
    ):
        return _refused(
            reason_code="M10_LC1_ADAPTER_UNIT_SHAPE_INVALID",
            reason=(
                "Every Explanation unit must have string role and semantic status "
                "fields plus a non-empty title."
            ),
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=input_relation_ids,
            boundary_unit_ids=boundary_ids,
        )

    relation_ids = []
    if any(not isinstance(relation, Mapping) for relation in raw_relation_values):
        return _refused(
            reason_code="M10_LC1_ADAPTER_RELATION_SHAPE_INVALID",
            reason="Every Explanation relation must be an object with an id and endpoints.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=input_relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    for relation in raw_relation_values:
        relation_id = relation.get("id")
        if not isinstance(relation_id, str) or not relation_id:
            return _refused(
                reason_code="M10_LC1_ADAPTER_RELATION_SHAPE_INVALID",
                reason="Every Explanation relation must have a non-empty id.",
                criterion_unit_id=criterion_id,
                input_unit_ids=input_unit_ids,
                input_relation_ids=input_relation_ids,
                boundary_unit_ids=boundary_ids,
            )
        relation_ids.append(relation_id)
    if len(relation_ids) != len(set(relation_ids)):
        return _refused(
            reason_code="M10_LC1_ADAPTER_RELATION_SHAPE_INVALID",
            reason="Explanation relation ids must be unique before LC1 can account for them.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )

    source = explanation.get("source")
    if not isinstance(source, Mapping) or not _is_sha256_text(source.get("ir_sha256")):
        return _refused(
            reason_code="M10_LC1_ADAPTER_SOURCE_SHAPE_INVALID",
            reason="LC1 requires a non-empty SHA-256 integrity value in source.ir_sha256.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    if source.get("blueprint_asset_path") == _FROZEN_LC1_ASSET_PATH and (
        len(input_unit_ids) != _FROZEN_LC1_UNIT_COUNT
        or len(relation_ids) != _FROZEN_LC1_RELATION_COUNT
    ):
        return _refused(
            reason_code="M10_LC1_ADAPTER_FROZEN_SOURCE_COVERAGE_INVALID",
            reason=(
                "The frozen LC1 LongChain source requires exactly 14 units and "
                "13 relations."
            ),
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )

    if criterion_id is None or criterion_id not in set(input_unit_ids):
        return _refused(
            reason_code="M10_LC1_ADAPTER_CRITERION_MISSING",
            reason="The Explanation criterion_unit_id does not identify a carried unit.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    units_by_id = {str(unit["id"]): unit for unit in units}
    criterion = units_by_id[criterion_id]
    if criterion.get("role") != "criterion":
        return _refused(
            reason_code="M10_LC1_ADAPTER_CRITERION_ROLE_INVALID",
            reason="The identified criterion unit does not have criterion role.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )

    relations: list[Mapping[str, Any]] = []
    for relation in raw_relation_values:
        source = relation.get("source_unit_id")
        target = relation.get("target_unit_id")
        if (
            not isinstance(source, str)
            or not isinstance(target, str)
            or source not in units_by_id
            or target not in units_by_id
        ):
            return _refused(
                reason_code="M10_LC1_ADAPTER_RELATION_DANGLING",
                reason="An Explanation relation endpoint is outside the unit inventory.",
                criterion_unit_id=criterion_id,
                input_unit_ids=input_unit_ids,
                input_relation_ids=relation_ids,
                boundary_unit_ids=boundary_ids,
            )
        relation_kind = relation.get("kind")
        if not isinstance(relation_kind, str) or relation_kind not in (
            _CARRIED_RELATION_KINDS | _DEFERRED_RELATION_KINDS
        ):
            return _refused(
                reason_code="M10_LC1_ADAPTER_RELATION_KIND_UNSUPPORTED",
                reason=(
                    "Stage 1 recognises execution, value, predicate and guard "
                    "relations before it can account for them."
                ),
                criterion_unit_id=criterion_id,
                input_unit_ids=input_unit_ids,
                input_relation_ids=relation_ids,
                boundary_unit_ids=boundary_ids,
            )
        relations.append(relation)

    unsupported_roles = sorted(
        {
            str(unit.get("role"))
            for unit in units
            if unit.get("role") not in (_RAIL_UNIT_ROLES | _DEFERRED_UNIT_ROLES)
        }
    )
    if unsupported_roles:
        return _refused(
            reason_code="M10_LC1_ADAPTER_ROLE_UNSUPPORTED",
            reason=(
                "Stage 1 has no counted disposition for roles other than rail, "
                "value or predicate: "
                + ", ".join(unsupported_roles)
                + "."
            ),
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    if len(input_unit_ids) < 3:
        return _refused(
            reason_code="M10_LC1_ADAPTER_STRAIGHT_RUN_REQUIRED",
            reason=(
                "Stage 1 requires at least three input units so the rail retains "
                "a non-empty straight run without duplicating a station."
            ),
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    carried_unit_ids = [
        unit_id for unit_id in input_unit_ids if units_by_id[unit_id]["role"] in _RAIL_UNIT_ROLES
    ]
    not_carried_unit_ids = [
        unit_id
        for unit_id in input_unit_ids
        if units_by_id[unit_id]["role"] in _DEFERRED_UNIT_ROLES
    ]
    execution_relations = [
        relation
        for relation in relations
        if relation["kind"] in _CARRIED_RELATION_KINDS
    ]
    carried_relation_ids = [str(relation["id"]) for relation in execution_relations]
    not_carried_relation_ids = [
        str(relation["id"])
        for relation in relations
        if relation["kind"] in _DEFERRED_RELATION_KINDS
    ]
    carried_unit_id_set = set(carried_unit_ids)
    if any(
        relation["source_unit_id"] not in carried_unit_id_set
        or relation["target_unit_id"] not in carried_unit_id_set
        for relation in execution_relations
    ):
        return _refused(
            reason_code="M10_LC1_ADAPTER_EXECUTION_ENDPOINT_ROLE_INVALID",
            reason="Every Stage 1 execution relation must join rail-carrying units.",
            criterion_unit_id=criterion_id,
            input_unit_ids=input_unit_ids,
            input_relation_ids=relation_ids,
            boundary_unit_ids=boundary_ids,
        )
    ordered_unit_ids = _rail_order(
        carried_unit_ids,
        criterion_id,
        execution_relations,
    )
    control_unit_ids = [
        unit_id for unit_id in ordered_unit_ids if units_by_id[unit_id]["role"] == "control"
    ]
    cap_segments = [
        {
            "kind": _BOUNDARY_CAP_KIND,
            "unit_id": unit_id,
            "semantic_status": units_by_id[unit_id]["semantic_status"],
            "title": units_by_id[unit_id]["title"],
            "disclosure": _cap_disclosure(units_by_id[unit_id]["semantic_status"]),
        }
        for unit_id in ordered_unit_ids
        if units_by_id[unit_id]["role"] == "boundary"
    ]
    segments = cap_segments + [
        {
            "kind": "Entry",
            "member_unit_ids": control_unit_ids[:1],
            "member_relation_ids": [],
            "incoming_relation_ids": [],
            "outgoing_relation_ids": [],
        },
        {
            "kind": "StraightRun",
            "member_unit_ids": control_unit_ids[1:],
            "member_relation_ids": carried_relation_ids,
            "incoming_relation_ids": [],
            "outgoing_relation_ids": [],
        },
        {
            "kind": "CriterionFocus",
            "member_unit_ids": [criterion_id],
            "member_relation_ids": [],
            "incoming_relation_ids": [],
            "outgoing_relation_ids": [],
        },
    ]
    return _result_base(
        outcome="accepted",
        reason_code="M10_LC1_ADAPTER_ACCEPTED",
        reason=(
            "The Explanation is conserved as a composite rail with typed boundary "
            "caps and counted Stage 2/3 remainder."
        ),
        criterion_unit_id=criterion_id,
        input_unit_ids=input_unit_ids,
        input_relation_ids=relation_ids,
        boundary_unit_ids=boundary_ids,
        carried_unit_ids=carried_unit_ids,
        carried_relation_ids=carried_relation_ids,
        segments=segments,
        not_carried_unit_ids=not_carried_unit_ids,
        not_carried_relation_ids=not_carried_relation_ids,
        input_unit_semantics=_unit_semantic_facts(units),
    )


def _lane_occupancy(explanation: Mapping[str, Any]) -> dict[str, int]:
    occupancy = {role: 0 for role in ("criterion", "control", "predicate", "value", "consequence", "boundary")}
    lanes = explanation.get("lanes")
    if isinstance(lanes, list):
        for lane in lanes:
            if isinstance(lane, Mapping) and isinstance(lane.get("role"), str):
                unit_ids = lane.get("unit_ids")
                if isinstance(unit_ids, list) and lane["role"] in occupancy:
                    occupancy[lane["role"]] = len(unit_ids)
    return occupancy


def _strict_control_only(explanation: Mapping[str, Any]) -> bool:
    occupancy = _lane_occupancy(explanation)
    return (
        occupancy["criterion"] > 0
        and occupancy["control"] > 0
        and occupancy["predicate"] == 0
        and occupancy["value"] == 0
    )


def _g7_typed_ir_inventory(root: Path) -> tuple[dict[str, str], ...]:
    """Read the accepted run-1 typed-IR inventory from the frozen G7 packet."""

    evidence = json.loads((root / _G7_EVIDENCE_RELATIVE).read_text(encoding="utf-8"))
    conditions = evidence.get("conditions")
    if not isinstance(conditions, list):
        raise ValueError("G7 conditions are missing")
    condition = next(
        (
            item
            for item in conditions
            if isinstance(item, Mapping) and item.get("id") == "G7-C1"
        ),
        None,
    )
    if not isinstance(condition, Mapping) or not isinstance(condition.get("basis"), list):
        raise ValueError("G7-C1 typed-IR basis is missing")
    prefix = f"{_TYPED_IR_RELATIVE}/"
    inventory = [
        {"path": item["path"], "sha256": item["sha256"]}
        for item in condition["basis"]
        if isinstance(item, Mapping)
        and isinstance(item.get("path"), str)
        and item["path"].startswith(prefix)
        and isinstance(item.get("sha256"), str)
    ]
    if len(inventory) != 12 or len({item["path"] for item in inventory}) != len(inventory):
        raise ValueError("G7-C1 does not bind exactly the run-1 typed-IR inventory")
    return tuple(sorted(inventory, key=lambda item: item["path"]))


def _g7_manifest_sha256(root: Path) -> str:
    evidence = json.loads((root / _G7_EVIDENCE_RELATIVE).read_text(encoding="utf-8"))
    dataset = evidence.get("dataset")
    if not isinstance(dataset, Mapping) or not isinstance(dataset.get("files"), list):
        raise ValueError("G7 frozen dataset is missing")
    for item in dataset["files"]:
        if isinstance(item, Mapping) and item.get("path") == _MANIFEST_RELATIVE:
            digest = item.get("sha256")
            if isinstance(digest, str):
                return digest
    raise ValueError("G7 frozen dataset does not bind the M7 manifest")


def _corpus_inputs(
    root: Path,
) -> tuple[dict[str, Any], list[str], tuple[dict[str, str], ...], dict[str, dict[str, str]]]:
    """Validate the frozen M7 population inputs before deriving any rows."""

    manifest_path = root / _MANIFEST_RELATIVE
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    candidate_graphs = manifest.get("candidate_graphs")
    if not isinstance(candidate_graphs, list):
        raise ValueError("M7 manifest candidate_graphs is missing")
    graph_ids = [
        row["graph_id"]
        for row in candidate_graphs
        if isinstance(row, Mapping) and isinstance(row.get("graph_id"), str)
    ]
    if (
        len(graph_ids) != _EXPECTED_MANIFEST_GRAPH_COUNT
        or len(set(graph_ids)) != _EXPECTED_MANIFEST_GRAPH_COUNT
    ):
        raise ValueError("M7 manifest must contain exactly ten unique candidate graphs")
    if _sha256(manifest_path) != _g7_manifest_sha256(root):
        raise ValueError("M7 manifest digest does not match the accepted G7 packet")

    expected_inventory = _g7_typed_ir_inventory(root)
    directory = root / _TYPED_IR_RELATIVE
    paths = sorted(directory.glob("*.blueprint-lens-v1.json"))
    actual_inventory = tuple(
        {
            "path": path.relative_to(root).as_posix(),
            "sha256": _sha256(path),
        }
        for path in paths
    )
    if actual_inventory != expected_inventory:
        raise ValueError("M7 typed-IR inventory does not match the accepted G7 packet")

    from ..raw_probe import load_blueprint_lens_v1

    graph_sources: dict[str, dict[str, str]] = {}
    for path in paths:
        typed_document = load_blueprint_lens_v1(path)
        source = {
            "path": path.relative_to(root).as_posix(),
            "sha256": _sha256(path),
        }
        for graph in typed_document.graphs:
            if graph.id in graph_sources:
                raise ValueError(f"duplicate typed-IR graph identity: {graph.id}")
            graph_sources[graph.id] = source
    if set(graph_ids) - set(graph_sources):
        raise ValueError("M7 manifest graph is absent from typed-IR inventory")
    return manifest, sorted(graph_ids), expected_inventory, graph_sources


def _fallback_explanations(
    root: Path,
    manifest_graph_ids: list[str] | None = None,
) -> dict[str, dict[str, Any]]:
    """Rebuild the exact current generic-fallback population from M4/M6."""

    from .execution_products import build_execution_slice_value
    from ..execution_slice import compute_execution_slice
    from ..raw_probe import load_blueprint_lens_v1
    from .session_explanation import build_session_explanation

    if manifest_graph_ids is None:
        manifest_path = root / _MANIFEST_RELATIVE
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        manifest_graph_ids = sorted(
            row["graph_id"]
            for row in manifest.get("candidate_graphs", [])
            if isinstance(row, Mapping) and isinstance(row.get("graph_id"), str)
        )
    manifest_graph_id_set = set(manifest_graph_ids)
    directory = root / _TYPED_IR_RELATIVE
    explanations: dict[str, dict[str, Any]] = {}
    for typed_path in sorted(directory.glob("*.blueprint-lens-v1.json")):
        typed_document = load_blueprint_lens_v1(typed_path)
        typed_value = json.loads(typed_path.read_text(encoding="utf-8"))
        typed_value["_m6_source_fingerprint"] = "0" * 64
        source_sha256 = _sha256(typed_path)
        source_fixture = typed_path.relative_to(root).as_posix()
        for graph in typed_document.graphs:
            if graph.id not in manifest_graph_id_set:
                continue
            for node in sorted(graph.nodes, key=lambda item: item.id):
                try:
                    execution_slice = compute_execution_slice(typed_document, node.id)
                except Exception as error:
                    if getattr(error, "code", "") == "M4_CRITERION_INVALID":
                        continue
                    raise
                slice_value = build_execution_slice_value(
                    typed_document,
                    execution_slice,
                    source_fixture=source_fixture,
                    source_sha256=source_sha256,
                    description=f"Why does {node.id} execute?",
                )
                explanation = build_session_explanation(
                    typed_value,
                    slice_value,
                    query_kind="execution",
                    renderer_id="R1_GENERIC_FRAME_FLOW_V1",
                ).explanation
                if len(explanation["units"]) < 3:
                    continue
                if _lane_occupancy(explanation)["criterion"] > 0 and all(
                    _lane_occupancy(explanation)[role] > 0
                    for role in ("control", "predicate", "value")
                ):
                    continue
                identity = f"{graph.id}\n{node.id}"
                if identity in explanations:
                    raise ValueError(f"duplicate Explanation identity: {identity}")
                explanations[identity] = dict(explanation)
    return explanations


def _build_report(root: Path) -> dict[str, Any]:
    manifest, graph_ids, typed_inventory, graph_sources = _corpus_inputs(root)
    manifest_path = root / _MANIFEST_RELATIVE
    explanations = _fallback_explanations(root, graph_ids)
    if len(explanations) != _EXPECTED_FALLBACK_COUNT:
        raise ValueError(
            f"M7 fallback population changed: expected {_EXPECTED_FALLBACK_COUNT}, "
            f"found {len(explanations)}"
        )
    rows: list[dict[str, Any]] = []
    not_carried_unit_role_counts: Counter[str] = Counter()
    not_carried_relation_kind_counts: Counter[str] = Counter()
    for identity, explanation in sorted(explanations.items()):
        graph_id, criterion_node_id = identity.split("\n", 1)
        source = graph_sources.get(graph_id)
        if source is None:
            raise ValueError(f"missing typed-IR source for graph: {graph_id}")
        result = adapt_explanation_to_lc1(explanation)
        units_by_id = {
            str(unit["id"]): unit
            for unit in explanation["units"]
            if isinstance(unit, Mapping) and isinstance(unit.get("id"), str)
        }
        relations_by_id = {
            str(relation["id"]): relation
            for relation in explanation["relations"]
            if isinstance(relation, Mapping) and isinstance(relation.get("id"), str)
        }
        not_carried_unit_role_counts.update(
            str(units_by_id[unit_id]["role"])
            for unit_id in result["not_carried_unit_ids"]
        )
        not_carried_relation_kind_counts.update(
            str(relations_by_id[relation_id]["kind"])
            for relation_id in result["not_carried_relation_ids"]
        )
        rows.append(
            {
                "source_graph_id": graph_id,
                "source_criterion_node_id": criterion_node_id,
                "source_typed_ir_path": source["path"],
                "source_typed_ir_sha256": source["sha256"],
                "source_explanation_sha256": hashlib.sha256(_canonical(explanation)).hexdigest(),
                "refusal_causes": [],
                "result": result,
            }
        )
    if {row["source_graph_id"] for row in rows} != set(graph_ids):
        raise ValueError("fallback population does not cover exactly the ten manifest graphs")
    strict_count = sum(_strict_control_only(explanation) for explanation in explanations.values())
    outcomes = Counter(row["result"]["outcome"] for row in rows)
    refusal_classes = Counter(
        row["result"]["reason_code"]
        for row in rows
        if row["result"]["outcome"] == "refused"
    )
    independent_refusal_causes = Counter(
        cause
        for row in rows
        for cause in row["refusal_causes"]
    )
    identities = [
        f"{row['source_graph_id']}\n{row['source_criterion_node_id']}"
        for row in rows
    ]
    return {
        "schema_name": "blueprint-lens-m10-lc1-explanation-adapter",
        "schema_version": "1.0.0",
        "measured_on": _MEASURED_ON,
        "corpus": {
            "manifest_path": _MANIFEST_RELATIVE,
            "manifest_sha256": _sha256(manifest_path),
            "typed_ir_path": _TYPED_IR_RELATIVE,
            "graph_count": len(graph_ids),
            "manifest_graph_ids": graph_ids,
            "typed_ir_inventory": list(typed_inventory),
            "population_identities": identities,
            "fallback_definition": (
                "Accepted M4 execution slices projected by M6 Explanation; retain "
                "units >= 3 and exclude only slices using the current four-lane "
                "specialized route."
            ),
        },
        "rows": rows,
        "summary": {
            "candidate_count": len(rows),
            "strict_control_only_count": strict_count,
            "other_fallback_shape_count": len(rows) - strict_count,
            "accepted_count": outcomes["accepted"],
            "refused_count": outcomes["refused"],
            "refusal_classes": dict(sorted(refusal_classes.items())),
            "independent_refusal_causes": dict(sorted(independent_refusal_causes.items())),
            "not_carried_unit_role_counts": dict(sorted(not_carried_unit_role_counts.items())),
            "not_carried_relation_kind_counts": dict(
                sorted(not_carried_relation_kind_counts.items())
            ),
        },
        "limitations": [
            {
                "id": "NO_PANEL_ROUTING",
                "statement": "This artifact measures the Stage 1 adapter only; panel routing remains outside its scope.",
            },
            {
                "id": "STAGE_ONE_REMAINDER",
                "statement": "Value and predicate units plus non-execution relations are counted remainder until later composite attachments exist.",
            },
            {
                "id": "NO_CAPACITY_OR_COMPREHENSION_CLAIM",
                "statement": "Acceptance establishes translation and conservation only; it does not establish capacity, rendering, or comprehension.",
            },
        ],
    }


def _unique(errors: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(errors)))


def _has_duplicates(values: list[Any]) -> bool:
    """Check JSON values without assuming they are hashable strings."""

    return any(value == previous for index, value in enumerate(values) for previous in values[:index])


def _identifier_kind(identifier: str, prefix: str) -> str | None:
    """Read the role or relation kind encoded by canonical Explanation IDs."""

    parts = identifier.split(".", 2)
    if len(parts) != 3 or parts[0] != prefix or not parts[1] or not parts[2]:
        return None
    return parts[1]


def _stage_one_errors(document: Any) -> tuple[str, ...]:
    """Check the relationships JSON Schema cannot express for the composite rail."""

    if not isinstance(document, Mapping):
        return ("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: root must be an object",)
    required = {
        "schema_name", "schema_version", "measured_on", "corpus", "rows", "summary", "limitations"
    }
    errors: list[str] = []
    missing = sorted(required - set(document))
    if missing:
        return ("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: missing " + ",".join(missing),)
    if (
        document.get("schema_name") != "blueprint-lens-m10-lc1-explanation-adapter"
        or document.get("schema_version") != "1.0.0"
    ):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: schema identity")
    corpus = document.get("corpus")
    rows = document.get("rows")
    summary = document.get("summary")
    if not isinstance(corpus, Mapping):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: corpus")
    if not isinstance(rows, list):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows")
    if not isinstance(summary, Mapping):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: summary")
    if not isinstance(document.get("limitations"), list):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: limitations")
    if errors:
        return _unique(errors)

    graph_ids = corpus.get("manifest_graph_ids")
    population_ids = corpus.get("population_identities")
    if not isinstance(graph_ids, list) or any(not isinstance(value, str) or not value for value in graph_ids):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: corpus.manifest_graph_ids")
        graph_ids = []
    if not isinstance(population_ids, list) or any(
        not isinstance(value, str) or not value for value in population_ids
    ):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: corpus.population_identities")
        population_ids = []
    if (
        corpus.get("graph_count") != _EXPECTED_MANIFEST_GRAPH_COUNT
        or len(graph_ids) != _EXPECTED_MANIFEST_GRAPH_COUNT
        or graph_ids != sorted(graph_ids)
        or len(set(graph_ids)) != len(graph_ids)
    ):
        errors.append("M10_LC1_ADAPTER_POPULATION_INVALID: manifest graph inventory")
    if (
        len(population_ids) != _EXPECTED_FALLBACK_COUNT
        or population_ids != sorted(population_ids)
        or len(set(population_ids)) != len(population_ids)
    ):
        errors.append("M10_LC1_ADAPTER_POPULATION_INVALID: population identity count")

    row_keys: list[str] = []
    outcomes: Counter[str] = Counter()
    refusal_classes: Counter[str] = Counter()
    independent_causes: Counter[str] = Counter()
    not_carried_unit_role_counts: Counter[str] = Counter()
    not_carried_relation_kind_counts: Counter[str] = Counter()
    for row_index, row in enumerate(rows):
        if not isinstance(row, Mapping):
            errors.append(f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}]")
            continue
        row_required = {
            "source_graph_id", "source_criterion_node_id", "source_typed_ir_path",
            "source_typed_ir_sha256", "source_explanation_sha256", "refusal_causes", "result",
        }
        row_missing = sorted(row_required - set(row))
        if row_missing:
            errors.append(
                f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}] missing "
                + ",".join(row_missing)
            )
            continue
        graph_id = row.get("source_graph_id")
        criterion_source_id = row.get("source_criterion_node_id")
        if not isinstance(graph_id, str) or not isinstance(criterion_source_id, str):
            errors.append(f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}].identity")
            continue
        row_keys.append(f"{graph_id}\n{criterion_source_id}")
        causes = row.get("refusal_causes")
        if not isinstance(causes, list) or any(not isinstance(cause, str) for cause in causes):
            errors.append(f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}].refusal_causes")
            causes = []
        if causes or _has_duplicates(causes):
            errors.append(f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].refusal_causes")
        independent_causes.update(causes)
        result = row.get("result")
        if not isinstance(result, Mapping):
            errors.append(f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}].result")
            continue
        result_required = {
            "outcome", "reason_code", "reason", "criterion_unit_id", "boundary_unit_ids",
            "boundary_unit_count", "input_unit_semantics",
            "input_unit_ids", "input_unit_count", "carried_unit_ids", "carried_unit_count",
            "not_carried_unit_ids", "not_carried_unit_count", "input_relation_ids",
            "input_relation_count", "carried_relation_ids", "carried_relation_count",
            "not_carried_relation_ids", "not_carried_relation_count", "segments",
        }
        result_missing = sorted(result_required - set(result))
        if result_missing:
            errors.append(
                f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}].result missing "
                + ",".join(result_missing)
            )
            continue
        outcome = result.get("outcome")
        if outcome != "accepted":
            errors.append(f"M10_LC1_ADAPTER_OUTCOME_INVALID: rows[{row_index}].not_accepted")
            if isinstance(outcome, str):
                outcomes[outcome] += 1
            continue
        outcomes["accepted"] += 1
        if result.get("reason_code") != "M10_LC1_ADAPTER_ACCEPTED" or not isinstance(
            result.get("reason"), str
        ) or not result["reason"].strip():
            errors.append(f"M10_LC1_ADAPTER_OUTCOME_INVALID: rows[{row_index}].accepted_reason")

        dispositions: dict[str, list[str]] = {}
        for noun in ("unit", "relation"):
            fields = (
                f"input_{noun}_ids", f"carried_{noun}_ids", f"not_carried_{noun}_ids"
            )
            values: list[list[str]] = []
            for field in fields:
                value = result.get(field)
                if not isinstance(value, list) or any(
                    not isinstance(item, str) or not item for item in value
                ) or _has_duplicates(value):
                    errors.append(
                        f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].{field}"
                    )
                    value = []
                values.append(value)
            input_ids, carried_ids, not_carried_ids = values
            dispositions[noun] = carried_ids
            if set(carried_ids) & set(not_carried_ids) or (
                set(carried_ids) | set(not_carried_ids) != set(input_ids)
            ):
                errors.append(f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].{noun}_partition")
            for field, value in zip(fields, values, strict=True):
                count_field = field.replace("_ids", "_count")
                if result.get(count_field) != len(value):
                    errors.append(f"M10_LC1_ADAPTER_COUNT_MISMATCH: rows[{row_index}].{count_field}")
            if noun == "unit":
                for unit_id in carried_ids:
                    role = _identifier_kind(unit_id, "unit")
                    if role not in _RAIL_UNIT_ROLES:
                        errors.append(
                            f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].carried_unit_role"
                        )
                for unit_id in not_carried_ids:
                    role = _identifier_kind(unit_id, "unit")
                    if role not in _DEFERRED_UNIT_ROLES:
                        errors.append(
                            f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].not_carried_unit_role"
                        )
                    else:
                        not_carried_unit_role_counts[role] += 1
            else:
                for relation_id in carried_ids:
                    relation_kind = _identifier_kind(relation_id, "relation")
                    if relation_kind not in _CARRIED_RELATION_KINDS:
                        errors.append(
                            f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].carried_relation_kind"
                        )
                for relation_id in not_carried_ids:
                    relation_kind = _identifier_kind(relation_id, "relation")
                    if relation_kind not in _DEFERRED_RELATION_KINDS:
                        errors.append(
                            f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].not_carried_relation_kind"
                        )
                    else:
                        not_carried_relation_kind_counts[relation_kind] += 1

        carried_units = dispositions["unit"]
        carried_relations = dispositions["relation"]
        input_units = result.get("input_unit_ids")
        unit_semantics = result.get("input_unit_semantics")
        semantic_ids: list[str] = []
        semantic_status_by_unit_id: dict[str, str] = {}
        expected_boundary_ids: list[str] = []
        expected_cap_ids: list[str] = []
        if not isinstance(unit_semantics, list) or any(
            not isinstance(fact, Mapping) for fact in unit_semantics
        ):
            errors.append(
                f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: rows[{row_index}].input_unit_semantics"
            )
            unit_semantics = []
        for fact in unit_semantics:
            unit_id = fact.get("id")
            role = fact.get("role")
            status = fact.get("semantic_status")
            if (
                not isinstance(unit_id, str)
                or not unit_id
                or not isinstance(role, str)
                or role not in _UNIT_ROLES
                or not isinstance(status, str)
                or status not in _SEMANTIC_STATUSES
            ):
                errors.append(
                    f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].unit_semantic_fact"
                )
                continue
            if _identifier_kind(unit_id, "unit") != role:
                errors.append(
                    f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].unit_semantic_role"
                )
            semantic_ids.append(unit_id)
            semantic_status_by_unit_id[unit_id] = status
            if role == "boundary":
                expected_cap_ids.append(unit_id)
            if role == "boundary" or status != "supported":
                expected_boundary_ids.append(unit_id)
        if (
            _has_duplicates(semantic_ids)
            or not isinstance(input_units, list)
            or semantic_ids != input_units
        ):
            errors.append(
                f"M10_LC1_ADAPTER_DISPOSITION_INVALID: rows[{row_index}].unit_semantic_inventory"
            )
        criterion_id = result.get("criterion_unit_id")
        if not isinstance(criterion_id, str) or criterion_id not in carried_units:
            errors.append(f"M10_LC1_ADAPTER_CRITERION_INVALID: rows[{row_index}].accepted_criterion")
        boundary_ids = result.get("boundary_unit_ids")
        if not isinstance(boundary_ids, list) or any(
            not isinstance(value, str) or not value for value in boundary_ids
        ) or _has_duplicates(boundary_ids):
            errors.append(f"M10_LC1_ADAPTER_BOUNDARY_INVALID: rows[{row_index}].boundary_ids")
            boundary_ids = []
        if (
            not isinstance(result.get("boundary_unit_count"), int)
            or isinstance(result.get("boundary_unit_count"), bool)
            or result.get("boundary_unit_count") != len(boundary_ids)
        ):
            errors.append(f"M10_LC1_ADAPTER_COUNT_MISMATCH: rows[{row_index}].boundary_unit_count")
        if boundary_ids != expected_boundary_ids:
            errors.append(
                f"M10_LC1_ADAPTER_BOUNDARY_INVALID: rows[{row_index}].boundary_recomputation"
            )
        segments = result.get("segments")
        if not isinstance(segments, list) or len(segments) < 3:
            errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].shape")
            continue
        cap_segments = segments[:-3]
        rail_segments = segments[-3:]
        if [segment.get("kind") if isinstance(segment, Mapping) else None for segment in rail_segments] != list(_SEGMENT_KINDS):
            errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].rail_order")
            continue
        cap_ids: list[str] = []
        for cap in cap_segments:
            if not isinstance(cap, Mapping) or cap.get("kind") != _BOUNDARY_CAP_KIND:
                errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].cap_shape")
                continue
            unit_id = cap.get("unit_id")
            status = cap.get("semantic_status")
            title = cap.get("title")
            disclosure = cap.get("disclosure")
            if not isinstance(unit_id, str) or unit_id not in carried_units:
                errors.append(f"M10_LC1_ADAPTER_BOUNDARY_INVALID: rows[{row_index}].cap_unit")
                continue
            if not isinstance(status, str) or not isinstance(title, str) or not title.strip() or (
                disclosure != _cap_disclosure(status)
            ):
                errors.append(f"M10_LC1_ADAPTER_BOUNDARY_INVALID: rows[{row_index}].cap_disclosure")
            if status != semantic_status_by_unit_id.get(unit_id):
                errors.append(
                    f"M10_LC1_ADAPTER_BOUNDARY_INVALID: rows[{row_index}].cap_semantic_status"
                )
            cap_ids.append(unit_id)
        if (
            _has_duplicates(cap_ids)
            or set(cap_ids) != set(expected_cap_ids)
            or not set(cap_ids).issubset(boundary_ids)
            or criterion_id in cap_ids
        ):
            errors.append(f"M10_LC1_ADAPTER_BOUNDARY_INVALID: rows[{row_index}].cap_ownership")
        station_ids: list[str] = []
        mentioned_relations: list[str] = []
        for segment in rail_segments:
            if not isinstance(segment, Mapping):
                continue
            members = segment.get("member_unit_ids")
            if not isinstance(members, list) or any(not isinstance(value, str) for value in members):
                errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].member_units")
            else:
                station_ids.extend(members)
            for field in ("member_relation_ids", "incoming_relation_ids", "outgoing_relation_ids"):
                values = segment.get(field)
                if not isinstance(values, list) or any(
                    not isinstance(value, str) or value not in carried_relations for value in values
                ):
                    errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].{field}")
                else:
                    mentioned_relations.extend(values)
        if (
            _has_duplicates(station_ids)
            or set(station_ids) | set(cap_ids) != set(carried_units)
            or set(station_ids) & set(cap_ids)
        ):
            errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].unit_ownership")
        if set(mentioned_relations) != set(carried_relations):
            errors.append(f"M10_LC1_ADAPTER_SEGMENT_INVALID: rows[{row_index}].relation_ownership")

    if row_keys != population_ids or len(row_keys) != _EXPECTED_FALLBACK_COUNT:
        errors.append("M10_LC1_ADAPTER_POPULATION_INVALID: row identities")
    summary_required = {
        "candidate_count", "strict_control_only_count", "other_fallback_shape_count",
        "accepted_count", "refused_count", "refusal_classes", "independent_refusal_causes",
        "not_carried_unit_role_counts", "not_carried_relation_kind_counts",
    }
    missing_summary = sorted(summary_required - set(summary))
    if missing_summary:
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: summary missing " + ",".join(missing_summary))
    elif any(
        not isinstance(summary.get(field), int) or isinstance(summary.get(field), bool)
        for field in (
            "candidate_count", "strict_control_only_count", "other_fallback_shape_count",
            "accepted_count", "refused_count",
        )
    ):
        errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: summary scalar")
    else:
        summary_unit_counts = summary.get("not_carried_unit_role_counts")
        summary_relation_counts = summary.get("not_carried_relation_kind_counts")
        if not isinstance(summary_unit_counts, Mapping) or any(
            not isinstance(key, str)
            or key not in _DEFERRED_UNIT_ROLES
            or not isinstance(value, int)
            or isinstance(value, bool)
            or value < 0
            for key, value in summary_unit_counts.items()
        ):
            errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: summary.not_carried_unit_role_counts")
        elif dict(sorted(summary_unit_counts.items())) != dict(sorted(not_carried_unit_role_counts.items())):
            errors.append("M10_LC1_ADAPTER_DRIFT: summary.not_carried_unit_role_counts")
        if not isinstance(summary_relation_counts, Mapping) or any(
            not isinstance(key, str)
            or key not in _DEFERRED_RELATION_KINDS
            or not isinstance(value, int)
            or isinstance(value, bool)
            or value < 0
            for key, value in summary_relation_counts.items()
        ):
            errors.append("M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: summary.not_carried_relation_kind_counts")
        elif dict(sorted(summary_relation_counts.items())) != dict(
            sorted(not_carried_relation_kind_counts.items())
        ):
            errors.append("M10_LC1_ADAPTER_DRIFT: summary.not_carried_relation_kind_counts")
    if (
        summary.get("candidate_count") != len(rows)
        or summary.get("accepted_count") != outcomes["accepted"]
        or summary.get("refused_count") != outcomes["refused"]
        or summary.get("refusal_classes") != dict(sorted(refusal_classes.items()))
        or summary.get("independent_refusal_causes") != dict(sorted(independent_causes.items()))
    ):
        errors.append("M10_LC1_ADAPTER_COUNT_MISMATCH: summary")
    return _unique(errors)


def lc1_explanation_adapter_errors(document: Any) -> tuple[str, ...]:
    """Return stable Stage 1 coherence errors without ever raising."""

    return _stage_one_errors(document)

def verify_lc1_explanation_adapter(document: Any, root: Path) -> tuple[str, ...]:
    """Verify a report against both its internal contract and the live root.

    The report carries provenance for auditability, but those fields are not
    authorities.  Rebuilding from the frozen G7 inputs is the durable check:
    coordinated edits to every embedded digest still disagree with this
    independently derived document.
    """

    try:
        coherence_errors = lc1_explanation_adapter_errors(document)
    except Exception as error:  # pragma: no cover - defensive boundary
        return (f"M10_LC1_ADAPTER_DOCUMENT_SHAPE_INVALID: coherence raised {type(error).__name__}",)
    if coherence_errors:
        return coherence_errors

    try:
        root = Path(root).resolve()
        schema_path = root / _SCHEMA_RELATIVE
        from ..schema_validation import validate_instance

        validate_instance(document, json.loads(schema_path.read_text(encoding="utf-8")))
    except Exception as error:
        return (f"M10_LC1_ADAPTER_DOCUMENT_SCHEMA_INVALID: {type(error).__name__}",)

    try:
        expected = _build_report(root)
        expected_errors = lc1_explanation_adapter_errors(expected)
        if expected_errors:
            return ("M10_LC1_ADAPTER_PROVENANCE_INVALID: root-derived report is incoherent",)
        if document != expected:
            return ("M10_LC1_ADAPTER_PROVENANCE_INVALID: report differs from root-derived sources",)
    except Exception as error:
        return (f"M10_LC1_ADAPTER_PROVENANCE_INVALID: {type(error).__name__}",)
    return ()


def build_lc1_explanation_adapter(root: Path, out: Path | None = None) -> int:
    """Build the canonical report, writing only to ``out`` when supplied."""

    root = Path(root).resolve()
    target = Path(out) if out is not None else root / _REPORT_RELATIVE
    try:
        document = _build_report(root)
        errors = verify_lc1_explanation_adapter(document, root)
        if errors:
            return 1
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(_canonical(document))
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError, ImportError):
        return 1
    return 0


__all__ = [
    "adapt_explanation_to_lc1",
    "build_lc1_explanation_adapter",
    "lc1_explanation_adapter_errors",
    "verify_lc1_explanation_adapter",
]
