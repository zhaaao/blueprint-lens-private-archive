"""Independent exact-file verifier for the bounded M6/G6 evidence contract."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any, Mapping, NoReturn, cast

from ..m6_errors import M6Error
from ..schema_validation import validate_instance
from .session_contracts import canonical_json_bytes, load_controlled_scenarios
from .session_products import PACKET_FILES, validate_session_packet
from .session_telemetry import validate_telemetry_record


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas/blueprint-lens-m6-g6-evidence-v1.schema.json"
_SCENARIO_IDS = ("M6-E01", "M6-D01")
_BASELINES = ("A", "B", "C")
_GENERATIONS = (1, 2)
_LOG_NAMES = (
    "editor-visible.log",
    "python-focused.log",
    "python-full.log",
    "ue-full.log",
    "ue-m6.log",
)
_NEGATIVE_CODES = {
    "N1": "M6_PRECONDITION_DIRTY_SOURCE",
    "N2": "M6_PRECONDITION_COMPILE_FAILED",
    "N3": "M6_RUNNER_NONZERO_EXIT",
    "N4": "M6_RUNNER_TIMEOUT",
    "N5": "M6_RUNNER_CANCELLED",
    "N6": "M6_PACKET_VERSION_UNSUPPORTED",
    "N7": "M6_PACKET_HASH_MISMATCH",
    "N8": "M6_PACKET_SOURCE_STALE",
    "N9": "M6_VIEW_SOURCE_NAVIGATION_FAILED",
}


def _retained_paths() -> tuple[str, ...]:
    paths = [
        f"semantic/{scenario_id}/run{generation}/{name}"
        for scenario_id in _SCENARIO_IDS
        for generation in _GENERATIONS
        for name in PACKET_FILES
    ]
    paths.extend(
        f"telemetry/{scenario_id}-run{generation}.telemetry.v1.jsonl"
        for scenario_id in _SCENARIO_IDS
        for generation in _GENERATIONS
    )
    paths.extend(
        f"visible/{scenario_id}-{baseline}.png"
        for scenario_id in _SCENARIO_IDS
        for baseline in _BASELINES
    )
    paths.append("visible/ue-visible-review.v1.json")
    paths.extend(f"logs/{name}" for name in _LOG_NAMES)
    paths.append("g6-evidence.v1.json")
    return tuple(sorted(paths))


G6_RETAINED_PATHS = _retained_paths()
_NON_REPORT_PATHS = tuple(
    path for path in G6_RETAINED_PATHS if path != "g6-evidence.v1.json"
)


def _fail(message: str, *, cause: Exception | None = None) -> NoReturn:
    raise M6Error(
        "M6_PACKET_REFERENCE_INVALID",
        message,
        phase="evidence",
        retryable=False,
        cause=cause,
    )


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(f"cannot hash retained evidence {path.name}: {error}", cause=error)


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        _fail(f"{context} must be an object")
    return cast(dict[str, Any], value)


def _canonical_object(path: Path, context: str) -> dict[str, Any]:
    try:
        payload = path.read_bytes()
        value = _object(json.loads(payload.decode("utf-8")), context)
    except M6Error:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read {context}: {error}", cause=error)
    if payload != canonical_json_bytes(value):
        _fail(f"{context} is not canonical JSON")
    return value


def _portable_path(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(f"{context} path must be non-empty")
    posix = PurePosixPath(value)
    windows = PureWindowsPath(value)
    if (
        "\\" in value
        or posix.is_absolute()
        or windows.is_absolute()
        or bool(windows.drive)
        or any(part in {"", ".", ".."} for part in posix.parts)
    ):
        _fail(f"{context} path must be portable and relative")
    return value


def _resolve(root: Path, stored: Any, context: str) -> Path:
    relative = _portable_path(stored, context)
    path = root.joinpath(*PurePosixPath(relative).parts).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        _fail(f"{context} path escapes evidence root", cause=error)
    if not path.is_file():
        _fail(f"{context} path does not resolve to a file")
    return path


def _enumerate(root: Path) -> set[str]:
    try:
        return {
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file()
        }
    except OSError as error:
        _fail(f"cannot enumerate G6 evidence: {error}", cause=error)


def _project_relative(path: Path) -> str:
    try:
        return path.resolve().relative_to(_ROOT).as_posix()
    except ValueError as error:
        _fail("controlled registry must be project-relative", cause=error)


def _load_visible_review(root: Path) -> dict[str, Any]:
    review_path = root / "visible/ue-visible-review.v1.json"
    review = _canonical_object(review_path, "UE visible review")
    if set(review) != {
        "format",
        "schema_version",
        "conditions",
        "scenario_interactions",
        "negative_matrix",
    }:
        _fail("UE visible review fields are not exact")
    if (
        review["format"] != "blueprint-lens-m6-visible-review"
        or review["schema_version"] != "1.0.0"
    ):
        _fail("UE visible review version is unsupported")
    conditions = review["conditions"]
    if not isinstance(conditions, list):
        _fail("visible conditions must be an array")
    expected_pairs = {
        (scenario_id, baseline)
        for scenario_id in _SCENARIO_IDS
        for baseline in _BASELINES
    }
    actual_pairs = {
        (row.get("scenario_id"), row.get("baseline_id"))
        for row in conditions
        if isinstance(row, Mapping)
    }
    if len(conditions) != 6 or actual_pairs != expected_pairs:
        _fail("visible review must contain exactly six unique primary conditions")
    for row in conditions:
        condition = _object(row, "visible condition")
        scenario_id = condition["scenario_id"]
        baseline = condition["baseline_id"]
        expected_path = f"visible/{scenario_id}-{baseline}.png"
        if condition.get("observed") is not True:
            _fail(f"primary visible condition was not observed: {scenario_id}/{baseline}")
        if condition.get("screenshot_path") != expected_path:
            _fail(f"visible screenshot path mismatch: {scenario_id}/{baseline}")
        screenshot = _resolve(root, expected_path, "visible screenshot")
        try:
            signature = screenshot.read_bytes()[:8]
        except OSError as error:
            _fail(f"cannot read visible screenshot: {error}", cause=error)
        if signature != b"\x89PNG\r\n\x1a\n" or condition.get(
            "screenshot_sha256"
        ) != _sha256(screenshot):
            _fail(f"visible screenshot binding is invalid: {scenario_id}/{baseline}")

    interactions = review["scenario_interactions"]
    if not isinstance(interactions, list) or len(interactions) != 2:
        _fail("visible review must contain two scenario interaction records")
    if {row.get("scenario_id") for row in interactions} != set(_SCENARIO_IDS):
        _fail("scenario interaction identities are incomplete")
    interaction_flags = {
        "selection_sync",
        "source_jump",
        "outside_session",
        "pending_stale",
        "controlled_failure",
        "reset",
    }
    for row in interactions:
        interaction = _object(row, "scenario interaction")
        if set(interaction) != {"scenario_id", *interaction_flags} or any(
            interaction[flag] is not True for flag in interaction_flags
        ):
            _fail("scenario interaction evidence is incomplete")

    negatives = review["negative_matrix"]
    if not isinstance(negatives, list) or len(negatives) != 9:
        _fail("negative matrix must contain exactly nine rows")
    actual_negatives = {
        row.get("case_id"): row
        for row in negatives
        if isinstance(row, Mapping)
    }
    if set(actual_negatives) != set(_NEGATIVE_CODES):
        _fail("negative matrix case identities are incomplete or duplicated")
    for case_id, code in _NEGATIVE_CODES.items():
        row = actual_negatives[case_id]
        if row.get("code") != code or row.get("passed") is not True:
            _fail(f"negative matrix binding failed: {case_id}")
    return review


def _condition_index(review: Mapping[str, Any]) -> dict[tuple[str, str], Mapping[str, Any]]:
    return {
        (row["scenario_id"], row["baseline_id"]): row
        for row in review["conditions"]
    }


def _missing_packet_counts(
    *,
    slice_value: Mapping[str, Any],
    explanation: Mapping[str, Any],
    facts: Mapping[str, Any],
) -> dict[str, int]:
    units = explanation.get("units")
    entities = facts.get("entities")
    relations = facts.get("relations")
    boundaries = facts.get("boundaries")
    if not isinstance(units, list):
        units = []
    if not isinstance(entities, list):
        entities = []
    if not isinstance(relations, list):
        relations = []
    if not isinstance(boundaries, list):
        boundaries = []
    selected_node_ids = set(slice_value.get("node_ids", []))
    selected_edge_ids = set(slice_value.get("edge_ids", []))
    fact_node_ids = {
        entity.get("id") for entity in entities if isinstance(entity, Mapping)
    }
    fact_edge_ids = {
        relation.get("id") for relation in relations if isinstance(relation, Mapping)
    }
    expected_boundary_ids = {
        entity.get("id")
        for entity in entities
        if isinstance(entity, Mapping)
        and entity.get("semantic_status") in {"opaque", "uncertain", "unsupported"}
    }
    actual_boundary_ids = {
        boundary.get("node_id")
        for boundary in boundaries
        if isinstance(boundary, Mapping)
    }
    return {
        "source_references": sum(
            not isinstance(unit, Mapping)
            or not isinstance(unit.get("source_references"), list)
            or not unit["source_references"]
            for unit in units
        ),
        "facts": len(selected_node_ids - fact_node_ids)
        + len(selected_edge_ids - fact_edge_ids),
        "reasons": sum(
            not isinstance(entity, Mapping)
            or not isinstance(entity.get("inclusion_reasons"), list)
            or not entity["inclusion_reasons"]
            for entity in entities
        ),
        "boundaries": len(expected_boundary_ids - actual_boundary_ids),
    }


def _assert_condition_parity(
    condition: Mapping[str, Any], facts: Mapping[str, Any]
) -> None:
    entities = facts["entities"]
    relations = facts["relations"]
    expected_entities = [entity["id"] for entity in entities]
    expected_relations = [relation["id"] for relation in relations]
    expected_labels = {entity["id"]: entity["label"] for entity in entities}
    expected_statuses = {
        entity["id"]: entity["presentation_status"] for entity in entities
    }
    expected_reasons = {
        entity["id"]: entity["semantic_reason"] for entity in entities
    }
    if (
        condition.get("session_entity_ids") != expected_entities
        or condition.get("session_relation_ids") != expected_relations
        or condition.get("labels") != expected_labels
        or condition.get("statuses") != expected_statuses
        or condition.get("reasons") != expected_reasons
        or condition.get("boundaries") != facts["boundaries"]
    ):
        _fail(
            "visible A/B/C facts disagree with the immutable baseline facts: "
            f"{condition.get('scenario_id')}/{condition.get('baseline_id')}"
        )


def _schema() -> Mapping[str, Any]:
    try:
        value = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read G6 evidence schema: {error}", cause=error)
    return _object(value, "G6 evidence schema")


def build_g6_evidence(
    artifact_root: Path,
    controlled_registry_path: Path,
) -> Mapping[str, object]:
    """Reopen the bounded candidate set and derive its exact G6 evidence report."""

    root = Path(artifact_root).resolve()
    registry_path = Path(controlled_registry_path).resolve()
    actual = _enumerate(root)
    allowed_sets = {frozenset(_NON_REPORT_PATHS), frozenset(G6_RETAINED_PATHS)}
    if frozenset(actual) not in allowed_sets:
        missing = sorted(set(_NON_REPORT_PATHS) - actual)
        extra = sorted(actual - set(G6_RETAINED_PATHS))
        _fail(f"retained file allowlist mismatch: missing={missing}, extra={extra}")
    scenarios = load_controlled_scenarios(registry_path)
    scenarios_by_id = {scenario.scenario_id: scenario for scenario in scenarios}
    if set(scenarios_by_id) != set(_SCENARIO_IDS) or len(scenarios) != 2:
        _fail("controlled registry scenario identity is not exact")
    scenarios = tuple(scenarios_by_id[scenario_id] for scenario_id in _SCENARIO_IDS)
    review = _load_visible_review(root)
    conditions = _condition_index(review)
    scenario_reports: list[dict[str, Any]] = []
    parity_rows: list[dict[str, Any]] = []
    missing = {
        "source_references": 0,
        "facts": 0,
        "reasons": 0,
        "boundaries": 0,
    }
    for scenario in scenarios:
        generation_reports: list[dict[str, Any]] = []
        semantic_hashes: list[str] = []
        baseline_values: list[dict[str, Any]] = []
        for generation in _GENERATIONS:
            packet_relative = (
                f"semantic/{scenario.scenario_id}/run{generation}"
            )
            packet = root.joinpath(*PurePosixPath(packet_relative).parts)
            result = validate_session_packet(
                packet,
                expected_source_fingerprint=scenario.request.source_fingerprint,
            )
            telemetry_relative = (
                f"telemetry/{scenario.scenario_id}-run{generation}.telemetry.v1.jsonl"
            )
            telemetry = _resolve(root, telemetry_relative, "telemetry record")
            validate_telemetry_record(telemetry, result.semantic_sha256)
            facts = _canonical_object(
                packet / "baseline-facts.json",
                "baseline facts",
            )
            slice_value = _canonical_object(packet / "slice.json", "slice")
            explanation = _canonical_object(
                packet / "explanation.json",
                "Explanation",
            )
            packet_missing = _missing_packet_counts(
                slice_value=slice_value,
                explanation=explanation,
                facts=facts,
            )
            for key, value in packet_missing.items():
                missing[key] += value
            expected_counts = {
                "entities": scenario.expected_node_count,
                "relations": scenario.expected_relation_count,
            }
            if (
                facts["counts"]["entities"] != expected_counts["entities"]
                or facts["counts"]["relations"] != expected_counts["relations"]
            ):
                _fail(f"controlled counts disagree: {scenario.scenario_id}")
            for baseline in _BASELINES:
                condition = conditions[(scenario.scenario_id, baseline)]
                _assert_condition_parity(condition, facts)
                parity_rows.append(
                    {
                        "scenario_id": scenario.scenario_id,
                        "generation": generation,
                        "baseline_id": baseline,
                        "passed": True,
                    }
                )
            semantic_hashes.append(result.semantic_sha256)
            baseline_values.append(facts)
            generation_reports.append(
                {
                    "generation": generation,
                    "packet_path": packet_relative,
                    "semantic_sha256": result.semantic_sha256,
                    "telemetry_path": telemetry_relative,
                    "telemetry_sha256": _sha256(telemetry),
                }
            )
        if len(set(semantic_hashes)) != 1 or baseline_values[0] != baseline_values[1]:
            _fail(f"independent generation disagreement: {scenario.scenario_id}")
        scenario_reports.append(
            {
                "scenario_id": scenario.scenario_id,
                "query_kind": scenario.request.query_kind,
                "truth_path": scenario.truth_path,
                "truth_sha256": scenario.truth_sha256,
                "expected_node_count": scenario.expected_node_count,
                "expected_relation_count": scenario.expected_relation_count,
                "semantic_agreement": True,
                "generations": generation_reports,
            }
        )
    if any(missing.values()):
        _fail(f"reopened packet has missing products: {missing}")
    retained_records = [
        {"path": relative, "sha256": _sha256(_resolve(root, relative, "retained file"))}
        for relative in _NON_REPORT_PATHS
    ]
    report: dict[str, Any] = {
        "format": "blueprint-lens-m6-g6-evidence",
        "schema_version": "1.0.0",
        "registry": {
            "path": _project_relative(registry_path),
            "sha256": _sha256(registry_path),
        },
        "scenarios": scenario_reports,
        "baseline_parity": parity_rows,
        "visible_review": {
            "path": "visible/ue-visible-review.v1.json",
            "sha256": _sha256(root / "visible/ue-visible-review.v1.json"),
        },
        "negative_matrix": {"passed": 9, "total": 9},
        "denominators": {
            "execution_generations_passed": 2,
            "execution_generations_total": 2,
            "data_generations_passed": 2,
            "data_generations_total": 2,
            "execution_semantic_agreement_passed": 1,
            "execution_semantic_agreement_total": 1,
            "data_semantic_agreement_passed": 1,
            "data_semantic_agreement_total": 1,
            "aggregate_agreement_passed": 2,
            "aggregate_agreement_total": 2,
            "baseline_parity_passed": len(parity_rows),
            "baseline_parity_total": 12,
            "primary_visible_passed": 6,
            "primary_visible_total": 6,
        },
        "missing": missing,
        "retained_files": retained_records,
        "counts": {"retained_files": 45},
    }
    try:
        validate_instance(report, _schema())
    except M6Error:
        raise
    except Exception as error:
        _fail(f"G6 evidence report violates its schema: {error}", cause=error)
    return report


def verify_g6_evidence(
    artifact_root: Path,
    controlled_registry_path: Path,
) -> None:
    """Independently reopen, rehash, rebuild and compare an exact 45-file packet."""

    root = Path(artifact_root).resolve()
    actual = _enumerate(root)
    if actual != set(G6_RETAINED_PATHS):
        _fail(
            "G6 retained set is not the exact 45-file allowlist: "
            f"missing={sorted(set(G6_RETAINED_PATHS) - actual)}, "
            f"extra={sorted(actual - set(G6_RETAINED_PATHS))}"
        )
    evidence_path = root / "g6-evidence.v1.json"
    evidence = _canonical_object(evidence_path, "G6 evidence report")
    try:
        validate_instance(evidence, _schema())
    except M6Error:
        raise
    except Exception as error:
        _fail(f"G6 evidence report violates its schema: {error}", cause=error)
    records = evidence.get("retained_files")
    if not isinstance(records, list):
        _fail("retained_files must be an array")
    if [record.get("path") for record in records] != list(_NON_REPORT_PATHS):
        _fail("retained_files paths are not the exact ordered 44-file binding")
    for record in records:
        path = _resolve(root, record["path"], "retained file")
        if record.get("sha256") != _sha256(path):
            _fail(f"retained file hash mismatch: {record['path']}")
    expected = build_g6_evidence(root, Path(controlled_registry_path))
    if evidence != expected:
        _fail("retained G6 evidence differs from independent rebuild")
