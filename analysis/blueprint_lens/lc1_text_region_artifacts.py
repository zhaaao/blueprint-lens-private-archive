"""Generate deterministic LC1 text/region/paired-code parity artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
from typing import Any, Mapping


EXPECTED_UNIT_COUNT = 14
EXPECTED_RELATION_COUNT = 13
EXPECTED_RUN_MEMBER_COUNT = 12
VARIABLE_SET_CLASS = "/Script/BlueprintGraph.K2Node_VariableSet"
EVENT_CLASS = "/Script/BlueprintGraph.K2Node_Event"
PROJECTOR_VERSION = "BlueprintLens.LC1RegionProjector.v1"
PSEUDOCODE_PROJECTOR_VERSION = "BlueprintLens.LC1PseudocodeProjector.v1"
TARGET_PATTERN = re.compile(r"^LC1Step(0[1-9]|1[0-2])Complete$")
CONDITION_IDS = (
    "LC1_PLAIN_OUTLINE",
    "LC1_EVIDENCE_REGIONS",
    "LC1_PAIRED_PSEUDOCODE",
)


class LC1TextRegionArtifactError(ValueError):
    """Raised when LC1 parity or operation evidence cannot be sealed."""


def _fail(diagnostic: str) -> None:
    raise LC1TextRegionArtifactError(diagnostic)


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest().upper()


def _required_string(
    value: Mapping[str, Any],
    field: str,
    diagnostic: str,
    *,
    allow_empty: bool = False,
) -> str:
    result = value.get(field)
    if not isinstance(result, str) or (not allow_empty and not result):
        _fail(diagnostic)
    return result


def _ordered_explanation(
    explanation: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    units_value = explanation.get("units")
    relations_value = explanation.get("relations")
    counts = explanation.get("counts")
    if (
        not isinstance(units_value, list)
        or not isinstance(relations_value, list)
        or len(units_value) != EXPECTED_UNIT_COUNT
        or len(relations_value) != EXPECTED_RELATION_COUNT
        or not isinstance(counts, Mapping)
        or counts.get("units") != EXPECTED_UNIT_COUNT
        or counts.get("relations") != EXPECTED_RELATION_COUNT
        or counts.get("source_nodes") != EXPECTED_UNIT_COUNT
        or counts.get("source_edges") != EXPECTED_RELATION_COUNT
    ):
        _fail("LC1_REGION_MEMBERSHIP_INVALID")

    if not all(isinstance(unit, dict) for unit in units_value):
        _fail("LC1_REGION_MEMBERSHIP_INVALID")
    if not all(isinstance(relation, dict) for relation in relations_value):
        _fail("LC1_REGION_MEMBERSHIP_INVALID")
    units = list(units_value)
    relations = list(relations_value)

    units_by_id: dict[str, dict[str, Any]] = {}
    incoming: dict[str, list[dict[str, Any]]] = {}
    outgoing: dict[str, list[dict[str, Any]]] = {}
    for unit in units:
        unit_id = unit.get("id")
        if (
            not isinstance(unit_id, str)
            or not unit_id
            or unit_id in units_by_id
            or unit.get("semantic_status") != "supported"
        ):
            _fail("LC1_REGION_MEMBERSHIP_INVALID")
        units_by_id[unit_id] = unit
        incoming[unit_id] = []
        outgoing[unit_id] = []

    relation_ids: set[str] = set()
    for relation in relations:
        relation_id = relation.get("id")
        source_id = relation.get("source_unit_id")
        target_id = relation.get("target_unit_id")
        if (
            not isinstance(relation_id, str)
            or not relation_id
            or relation_id in relation_ids
            or source_id not in units_by_id
            or target_id not in units_by_id
            or relation.get("kind") != "execution_predecessor"
        ):
            _fail("LC1_REGION_RELATION_OWNERSHIP_INVALID")
        relation_ids.add(relation_id)
        outgoing[source_id].append(relation)
        incoming[target_id].append(relation)

    if any(
        len(incoming[unit_id]) > 1 or len(outgoing[unit_id]) > 1
        for unit_id in units_by_id
    ):
        _fail("LC1_REGION_MEMBERSHIP_INVALID")
    entry_ids = sorted(
        unit_id for unit_id in units_by_id if not incoming[unit_id]
    )
    criterion_id = explanation.get("criterion_unit_id")
    if (
        len(entry_ids) != 1
        or criterion_id not in units_by_id
        or outgoing[criterion_id]
    ):
        _fail("LC1_REGION_MEMBERSHIP_INVALID")

    ordered_units: list[dict[str, Any]] = []
    ordered_relations: list[dict[str, Any]] = []
    visited: set[str] = set()
    current_id = entry_ids[0]
    while True:
        if current_id in visited:
            _fail("LC1_REGION_MEMBERSHIP_INVALID")
        visited.add(current_id)
        ordered_units.append(units_by_id[current_id])
        next_relations = outgoing[current_id]
        if not next_relations:
            break
        relation = next_relations[0]
        ordered_relations.append(relation)
        current_id = str(relation["target_unit_id"])

    if (
        ordered_units[-1]["id"] != criterion_id
        or len(ordered_units) != EXPECTED_UNIT_COUNT
        or len(ordered_relations) != EXPECTED_RELATION_COUNT
    ):
        _fail("LC1_REGION_MEMBERSHIP_INVALID")
    return ordered_units, ordered_relations


def _primary_source_ids(
    ordered_units: list[dict[str, Any]],
) -> list[str]:
    source_ids: list[str] = []
    for unit in ordered_units:
        references = unit.get("source_references")
        if not isinstance(references, list):
            _fail("LC1_REGION_PRIMARY_SOURCE_INVALID")
        primary = [
            reference
            for reference in references
            if isinstance(reference, Mapping)
            and reference.get("primary") is True
        ]
        if len(primary) != 1:
            _fail("LC1_REGION_PRIMARY_SOURCE_INVALID")
        source_id = primary[0].get("source_node_id")
        if not isinstance(source_id, str) or not source_id:
            _fail("LC1_REGION_PRIMARY_SOURCE_INVALID")
        source_ids.append(source_id)
    if len(set(source_ids)) != EXPECTED_UNIT_COUNT:
        _fail("LC1_REGION_PRIMARY_SOURCE_INVALID")
    return source_ids


def _load_ir_facts(
    ir_path: Path,
    expected_sha256: str,
) -> tuple[
    str,
    dict[str, dict[str, str]],
    dict[str, dict[str, str]],
]:
    try:
        payload = ir_path.read_bytes()
    except OSError:
        _fail("LC1_IR_FILE_UNREADABLE")
    actual_sha256 = _sha256(payload)
    if (
        not isinstance(expected_sha256, str)
        or not expected_sha256
        or actual_sha256.casefold() != expected_sha256.casefold()
    ):
        _fail("LC1_IR_HASH_MISMATCH")
    try:
        root = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        _fail("LC1_IR_ROOT_MALFORMED")
    if not isinstance(root, Mapping):
        _fail("LC1_IR_ROOT_MALFORMED")
    blueprint = root.get("blueprint")
    if not isinstance(blueprint, Mapping):
        _fail("LC1_IR_ROOT_MALFORMED")
    graphs = blueprint.get("graphs")
    if not isinstance(graphs, list):
        _fail("LC1_IR_GRAPH_MALFORMED")

    node_facts: dict[str, dict[str, str]] = {}
    facts: dict[str, dict[str, str]] = {}
    for graph in graphs:
        if not isinstance(graph, Mapping):
            _fail("LC1_IR_GRAPH_MALFORMED")
        nodes = graph.get("nodes")
        if not isinstance(nodes, list):
            _fail("LC1_IR_GRAPH_MALFORMED")
        for node in nodes:
            if not isinstance(node, Mapping):
                _fail("LC1_IR_NODE_MALFORMED")
            source_node_id = _required_string(
                node, "id", "LC1_IR_NODE_ID_MISSING"
            )
            operation_class = _required_string(
                node, "class", "LC1_IR_NODE_CLASS_MISSING"
            )
            native_title = _required_string(
                node, "title", "LC1_IR_NODE_TITLE_MISSING"
            )
            if source_node_id in node_facts:
                _fail("LC1_IR_NODE_ID_DUPLICATE")
            node_facts[source_node_id] = {
                "source_node_id": source_node_id,
                "node_class": operation_class,
                "native_title": native_title,
            }
            pins = node.get("pins")
            if not isinstance(pins, list):
                _fail("LC1_IR_PIN_MALFORMED")

            value_pins: list[Mapping[str, Any]] = []
            for pin in pins:
                if not isinstance(pin, Mapping):
                    _fail("LC1_IR_PIN_MALFORMED")
                pin_role = _required_string(
                    pin,
                    "pin_role",
                    "LC1_IR_PIN_MALFORMED",
                    allow_empty=True,
                )
                direction = _required_string(
                    pin,
                    "direction",
                    "LC1_IR_PIN_MALFORMED",
                    allow_empty=True,
                )
                kind = _required_string(
                    pin,
                    "kind",
                    "LC1_IR_PIN_MALFORMED",
                    allow_empty=True,
                )
                if (
                    pin_role == "variable_set_value"
                    and direction == "input"
                    and kind == "data"
                ):
                    value_pins.append(pin)

            if operation_class != VARIABLE_SET_CLASS:
                continue
            if len(value_pins) != 1:
                _fail("LC1_IR_AMBIGUOUS_VALUE_PIN")
            value_pin = value_pins[0]
            value_pin_id = _required_string(
                value_pin, "id", "LC1_IR_PIN_MALFORMED"
            )
            variable_target = _required_string(
                value_pin,
                "name",
                "LC1_IR_PIN_MALFORMED",
                allow_empty=True,
            )
            type_value = value_pin.get("type")
            default = value_pin.get("default")
            if not isinstance(type_value, Mapping) or not isinstance(
                default, Mapping
            ):
                _fail("LC1_IR_PIN_MALFORMED")
            value_type = _required_string(
                type_value,
                "category",
                "LC1_IR_PIN_MALFORMED",
                allow_empty=True,
            )
            literal_value = _required_string(
                default,
                "value",
                "LC1_IR_PIN_MALFORMED",
                allow_empty=True,
            )
            if source_node_id in facts:
                _fail("LC1_IR_NODE_ID_DUPLICATE")
            facts[source_node_id] = {
                "source_node_id": source_node_id,
                "operation_class": operation_class,
                "value_pin_id": value_pin_id,
                "variable_target": variable_target,
                "value_type": value_type,
                "literal_value": literal_value,
            }
    return actual_sha256, facts, node_facts


def _append_value(label: str, value: str) -> str:
    return f"{label}[{len(value)}:{value}]"


def _append_ids(label: str, values: list[str]) -> str:
    body = "".join(f"{len(value)}:{value};" for value in values)
    return f"{label}[{body}]"


def _md5(value: str) -> str:
    return hashlib.md5(value.encode("utf-8")).hexdigest()


def _build_region(
    ordered_units: list[dict[str, Any]],
    ordered_relations: list[dict[str, Any]],
    source_ids: list[str],
    source_ir_sha256: str,
    facts: Mapping[str, Mapping[str, str]],
) -> dict[str, Any]:
    member_units = ordered_units[1:-1]
    member_unit_ids = [str(unit["id"]) for unit in member_units]
    member_source_ids = source_ids[1:-1]
    internal_relation_ids = [
        str(relation["id"]) for relation in ordered_relations[1:-1]
    ]
    incoming_relation_ids = [str(ordered_relations[0]["id"])]
    outgoing_relation_ids = [str(ordered_relations[-1]["id"])]
    if len(member_unit_ids) != EXPECTED_RUN_MEMBER_COUNT:
        _fail("LC1_REGION_MEMBERSHIP_INVALID")
    if (
        len(internal_relation_ids) != 11
        or len(incoming_relation_ids) != 1
        or len(outgoing_relation_ids) != 1
        or len(
            set(
                internal_relation_ids
                + incoming_relation_ids
                + outgoing_relation_ids
            )
        )
        != EXPECTED_RELATION_COUNT
    ):
        _fail("LC1_REGION_RELATION_OWNERSHIP_INVALID")

    ordered_facts = [facts.get(source_id) for source_id in member_source_ids]
    if any(fact is None for fact in ordered_facts):
        _fail("LC1_REGION_OPERATION_CLASS_UNAVAILABLE")
    bound_facts = [fact for fact in ordered_facts if fact is not None]
    if any(
        fact["operation_class"] != VARIABLE_SET_CLASS for fact in bound_facts
    ):
        _fail("LC1_REGION_OPERATION_CLASS_UNAVAILABLE")
    expected_targets = [
        f"LC1Step{index:02d}Complete"
        for index in range(1, EXPECTED_RUN_MEMBER_COUNT + 1)
    ]
    actual_targets = [fact["variable_target"] for fact in bound_facts]
    if any(TARGET_PATTERN.fullmatch(target) is None for target in actual_targets):
        _fail("LC1_REGION_TARGET_FAMILY_UNAVAILABLE")
    if actual_targets != expected_targets:
        _fail("LC1_REGION_TARGET_FAMILY_UNAVAILABLE")
    if any(fact["value_type"] != "bool" for fact in bound_facts):
        _fail("LC1_REGION_BOOLEAN_TYPE_UNAVAILABLE")
    if any(fact["literal_value"] != "true" for fact in bound_facts):
        _fail("LC1_REGION_TRUE_LITERAL_UNAVAILABLE")

    layout_segment_id = (
        f"segment.straight-run.{member_unit_ids[0]}."
        f"{member_unit_ids[-1]}"
    )
    identity = "".join(
        (
            _append_ids("members", member_unit_ids),
            _append_ids("internal", internal_relation_ids),
            _append_ids("incoming", incoming_relation_ids),
            _append_ids("outgoing", outgoing_relation_ids),
        )
    )
    region_id = f"lc1.region.{_md5(identity)}"
    source_ledger = ",".join(member_source_ids)
    relation_ledger = ",".join(internal_relation_ids)
    first_member_id = member_unit_ids[0]
    last_member_id = member_unit_ids[-1]
    sequence = f"{first_member_id}->{last_member_id}"
    claim_evidence = [
        {
            "claim_part": "operation",
            "fact_owner": "typed_ir.operation_class",
            "source_id": source_ledger,
            "value": VARIABLE_SET_CLASS,
        },
        {
            "claim_part": "count",
            "fact_owner": "layout.straight_run_members",
            "source_id": layout_segment_id,
            "value": "12",
        },
        {
            "claim_part": "target_family",
            "fact_owner": "typed_ir.variable_set_value.name",
            "source_id": source_ledger,
            "value": "LC1Step01Complete..LC1Step12Complete",
        },
        {
            "claim_part": "literal_value",
            "fact_owner": "typed_ir.variable_set_value.default",
            "source_id": source_ledger,
            "value": "true",
        },
        {
            "claim_part": "sequence",
            "fact_owner": "layout.execution_relations",
            "source_id": relation_ledger,
            "value": sequence,
        },
    ]
    summary_arguments = [
        "12",
        actual_targets[0],
        actual_targets[-1],
        "true",
    ]
    diagnostic_code = "LC1_REGION_COMPLETE"
    integrity = "".join(
        (
            _append_value("version", PROJECTOR_VERSION),
            _append_value("source-ir", source_ir_sha256),
            _append_value("region-id", region_id),
            _append_value("region-kind", "operation_region"),
            _append_ids("members", member_unit_ids),
            _append_ids("internal", internal_relation_ids),
            _append_ids("incoming", incoming_relation_ids),
            _append_ids("outgoing", outgoing_relation_ids),
            _append_value("first-member", first_member_id),
            _append_value("last-member", last_member_id),
            _append_value("status", "0"),
            _append_value(
                "template", "set_completion_flags_true_in_sequence"
            ),
            _append_ids("arguments", summary_arguments),
            *(
                "".join(
                    (
                        _append_value(
                            "claim-part", evidence["claim_part"]
                        ),
                        _append_value(
                            "fact-owner", evidence["fact_owner"]
                        ),
                        _append_value("source-id", evidence["source_id"]),
                        _append_value("value", evidence["value"]),
                    )
                )
                for evidence in claim_evidence
            ),
            _append_value("diagnostic", diagnostic_code),
        )
    )
    return {
        "claim_evidence": claim_evidence,
        "diagnostic_code": diagnostic_code,
        "first_member_unit_id": first_member_id,
        "incoming_relation_count": len(incoming_relation_ids),
        "incoming_relation_ids": incoming_relation_ids,
        "internal_relation_count": len(internal_relation_ids),
        "internal_relation_ids": internal_relation_ids,
        "last_member_unit_id": last_member_id,
        "layout_segment_id": layout_segment_id,
        "ordered_member_count": len(member_unit_ids),
        "ordered_member_source_node_ids": member_source_ids,
        "ordered_member_unit_ids": member_unit_ids,
        "outgoing_relation_count": len(outgoing_relation_ids),
        "outgoing_relation_ids": outgoing_relation_ids,
        "projection_integrity_hash": _md5(integrity),
        "projection_status": "complete_operation_region",
        "projector_version": PROJECTOR_VERSION,
        "region_id": region_id,
        "region_kind": "operation_region",
        "source_ir_sha256": source_ir_sha256,
        "summary_arguments": summary_arguments,
        "summary_template_id": "set_completion_flags_true_in_sequence",
    }


def _build_pseudocode(
    ordered_units: list[dict[str, Any]],
    ordered_relations: list[dict[str, Any]],
    source_ids: list[str],
    source_ir_sha256: str,
    facts: Mapping[str, Mapping[str, str]],
    node_facts: Mapping[str, Mapping[str, str]],
) -> dict[str, Any]:
    if (
        len(ordered_units) != EXPECTED_UNIT_COUNT
        or len(ordered_relations) != EXPECTED_RELATION_COUNT
        or len(source_ids) != EXPECTED_UNIT_COUNT
    ):
        _fail("LC1_PSEUDOCODE_PROFILE_INVALID")

    lines: list[dict[str, Any]] = []
    for index, (unit, source_id) in enumerate(
        zip(ordered_units, source_ids, strict=True)
    ):
        references = unit.get("source_references")
        primary = [
            reference
            for reference in references
            if isinstance(reference, Mapping)
            and reference.get("primary") is True
        ] if isinstance(references, list) else []
        if len(primary) != 1:
            _fail("LC1_PSEUDOCODE_PRIMARY_SOURCE_INVALID")
        source_pin_ids = primary[0].get("source_pin_ids")
        if (
            not isinstance(source_pin_ids, list)
            or not all(isinstance(pin_id, str) for pin_id in source_pin_ids)
        ):
            _fail("LC1_PSEUDOCODE_PRIMARY_SOURCE_INVALID")
        following_relation_id = (
            str(ordered_relations[index]["id"])
            if index < EXPECTED_RELATION_COUNT
            else ""
        )
        role = unit.get("role")
        if role not in {"control", "criterion"}:
            _fail("LC1_PSEUDOCODE_UNIT_UNSUPPORTED")

        if index == 0:
            node_fact = node_facts.get(source_id)
            if (
                node_fact is None
                or node_fact["node_class"] != EVENT_CLASS
                or "beginplay" not in node_fact["native_title"].casefold()
            ):
                _fail("LC1_PSEUDOCODE_ENTRY_UNSUPPORTED")
            code_text = "event BeginPlay"
            fact_owner = "typed_ir.node.class+title"
            line_diagnostic = "LC1_CODE_EVENT"
        else:
            fact = facts.get(source_id)
            if fact is None or fact["operation_class"] != VARIABLE_SET_CLASS:
                _fail("LC1_PSEUDOCODE_OPERATION_UNSUPPORTED")
            expected_target = (
                "LC1Ready"
                if index == 13
                else f"LC1Step{index:02d}Complete"
            )
            if fact["variable_target"] != expected_target:
                _fail("LC1_PSEUDOCODE_TARGET_UNEXPECTED")
            if fact["value_type"] != "bool" or fact["literal_value"] != "true":
                _fail("LC1_PSEUDOCODE_LITERAL_UNSUPPORTED")
            code_text = f"    {fact['variable_target']} = true;"
            fact_owner = "typed_ir.variable_set_value"
            line_diagnostic = (
                "LC1_CODE_CRITERION_ASSIGNMENT"
                if index == 13
                else "LC1_CODE_REGION_ASSIGNMENT"
            )

        lines.append(
            {
                "code_text": code_text,
                "fact_owner": fact_owner,
                "following_relation_id": following_relation_id,
                "line_id": f"lc1.code.{index + 1:02d}",
                "line_number": index + 1,
                "projection_diagnostic": line_diagnostic,
                "role": role,
                "semantic_status": "supported",
                "source_node_id": source_id,
                "source_pin_ids": list(source_pin_ids),
                "unit_id": str(unit["id"]),
            }
        )

    if len({line["unit_id"] for line in lines}) != EXPECTED_UNIT_COUNT:
        _fail("LC1_PSEUDOCODE_UNIT_COVERAGE_INVALID")
    if len({line["source_node_id"] for line in lines}) != EXPECTED_UNIT_COUNT:
        _fail("LC1_PSEUDOCODE_SOURCE_COVERAGE_INVALID")
    if (
        len(
            {
                line["following_relation_id"]
                for line in lines
                if line["following_relation_id"]
            }
        )
        != EXPECTED_RELATION_COUNT
    ):
        _fail("LC1_PSEUDOCODE_RELATION_COVERAGE_INVALID")

    diagnostic = "LC1_PSEUDOCODE_COMPLETE"
    integrity_parts = [
        _append_value("version", PSEUDOCODE_PROJECTOR_VERSION),
        _append_value("source-ir", source_ir_sha256),
        _append_value("status", "0"),
    ]
    role_values = {"criterion": "0", "control": "1"}
    for line in lines:
        integrity_parts.extend(
            (
                _append_value("line-id", line["line_id"]),
                _append_value("line-number", str(line["line_number"])),
                _append_value("code", line["code_text"]),
                _append_value("role", role_values[line["role"]]),
                _append_value("semantic-status", "0"),
                _append_value("unit", line["unit_id"]),
                _append_value(
                    "following-relation", line["following_relation_id"]
                ),
                _append_value("source-node", line["source_node_id"]),
                _append_ids("source-pins", line["source_pin_ids"]),
                _append_value("fact-owner", line["fact_owner"]),
                _append_value(
                    "diagnostic", line["projection_diagnostic"]
                ),
            )
        )
    integrity_parts.append(_append_value("diagnostic", diagnostic))
    return {
        "diagnostic_code": diagnostic,
        "line_count": len(lines),
        "lines": lines,
        "projection_integrity_hash": _md5("".join(integrity_parts)),
        "projection_status": "complete",
        "projector_version": PSEUDOCODE_PROJECTOR_VERSION,
        "relation_transition_count": EXPECTED_RELATION_COUNT,
        "source_ir_sha256": source_ir_sha256,
    }


def _portable_fixture_path(
    explanation_path: Path,
    source_ir_path: str,
) -> str:
    if source_ir_path:
        return Path(source_ir_path).with_name(explanation_path.name).as_posix()
    return explanation_path.as_posix()


def build_lc1_text_region_payloads(
    explanation: Mapping[str, Any],
    *,
    explanation_path: Path,
    ir_path: Path,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Build validated manifest and parity values without writing files."""

    explanation_path = Path(explanation_path)
    ir_path = Path(ir_path)
    ordered_units, ordered_relations = _ordered_explanation(explanation)
    source_ids = _primary_source_ids(ordered_units)
    source = explanation.get("source")
    if not isinstance(source, Mapping):
        _fail("LC1_IR_FILE_UNREADABLE")
    expected_ir_sha256 = source.get("ir_sha256")
    actual_ir_sha256, facts, node_facts = _load_ir_facts(
        ir_path,
        expected_ir_sha256 if isinstance(expected_ir_sha256, str) else "",
    )
    region = _build_region(
        ordered_units,
        ordered_relations,
        source_ids,
        actual_ir_sha256,
        facts,
    )
    pseudocode = _build_pseudocode(
        ordered_units,
        ordered_relations,
        source_ids,
        actual_ir_sha256,
        facts,
        node_facts,
    )

    unit_ids = [str(unit["id"]) for unit in ordered_units]
    relation_ids = [
        str(relation["id"]) for relation in ordered_relations
    ]
    common = {
        "adds_source_facts": False,
        "boundary_visible": True,
        "displayed_relation_ids": relation_ids,
        "displayed_unit_ids": unit_ids,
        "source_navigation": True,
        "status_visible": True,
    }
    manifest = {
        "conditions": [
            {
                **common,
                "id": CONDITION_IDS[0],
                "label": "Plain Ordered Outline",
                "primary_topology": "complete_text_ordered_outline",
            },
            {
                **common,
                "id": CONDITION_IDS[1],
                "label": "Evidence-Backed Operation Regions",
                "primary_topology": (
                    "deterministic_operation_region_with_complete_text_detail"
                ),
                "region_projection": region["region_id"],
            },
            {
                **common,
                "id": CONDITION_IDS[2],
                "label": "Paired Pseudocode",
                "primary_topology": (
                    "evidence_region_with_source_linked_structured_pseudocode"
                ),
                "pseudocode_projection_hash": pseudocode[
                    "projection_integrity_hash"
                ],
                "pseudocode_projector_version": pseudocode[
                    "projector_version"
                ],
                "region_projection": region["region_id"],
            },
        ],
        "default_condition_id": None,
        "fixture": {
            "ir_path": str(source.get("ir_path", "")),
            "ir_sha256": actual_ir_sha256,
            "path": _portable_fixture_path(
                explanation_path, str(source.get("ir_path", ""))
            ),
            "relation_ids": relation_ids,
            "source_edges": EXPECTED_RELATION_COUNT,
            "source_node_ids": source_ids,
            "source_nodes": EXPECTED_UNIT_COUNT,
            "unit_ids": unit_ids,
        },
        "schema_version": "1.0.0",
        "selection_status": "unselected",
    }
    parity = {
        "condition_ids": list(CONDITION_IDS),
        "format": "blueprint-lens-lc1-text-region-parity",
        "information_matched": True,
        "information_matching": {
            "adds_source_facts": False,
            "boundary_visibility_equal": True,
            "relation_coverage_equal": True,
            "selection_status": "unselected",
            "source_navigation_equal": True,
            "status_visibility_equal": True,
            "unit_coverage_equal": True,
        },
        "region": region,
        "pseudocode": pseudocode,
        "schema_version": "3.0.0",
        "verdict": "PASS",
    }
    return manifest, parity


def _resolve_ir_path(
    explanation_path: Path,
    source: Mapping[str, Any],
    explicit_ir_path: Path | None,
) -> Path:
    if explicit_ir_path is not None:
        return Path(explicit_ir_path).resolve()
    source_path_value = source.get("ir_path")
    if isinstance(source_path_value, str) and source_path_value:
        companion = explanation_path.parent / Path(source_path_value).name
        if companion.is_file():
            return companion.resolve()
        return Path(source_path_value).resolve()
    _fail("LC1_IR_FILE_UNREADABLE")


def build_lc1_text_region_artifacts(
    explanation_path: Path,
    output_dir: Path,
    *,
    ir_path: Path | None = None,
) -> dict[str, Path]:
    """Validate LC1 facts and write the v3 manifest/parity artifacts."""

    explanation_path = Path(explanation_path).resolve()
    output_dir = Path(output_dir).resolve()
    try:
        explanation = json.loads(
            explanation_path.read_text(encoding="utf-8")
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        _fail("LC1_EXPLANATION_UNREADABLE")
    if not isinstance(explanation, Mapping):
        _fail("LC1_EXPLANATION_UNREADABLE")
    source = explanation.get("source")
    if not isinstance(source, Mapping):
        _fail("LC1_IR_FILE_UNREADABLE")
    resolved_ir_path = _resolve_ir_path(
        explanation_path, source, ir_path
    )
    manifest, parity = build_lc1_text_region_payloads(
        explanation,
        explanation_path=explanation_path,
        ir_path=resolved_ir_path,
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = output_dir / "manifest.v3.json"
    parity_path = output_dir / "parity.v3.json"
    manifest_path.write_bytes(_canonical_json_bytes(manifest))
    parity_path.write_bytes(_canonical_json_bytes(parity))
    return {"manifest": manifest_path, "parity": parity_path}


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate deterministic LC1 paired-code parity artifacts."
    )
    parser.add_argument("--explanation", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    outputs = build_lc1_text_region_artifacts(
        args.explanation, args.output
    )
    for name, path in outputs.items():
        print(f"{name}={path}")


if __name__ == "__main__":
    main()
