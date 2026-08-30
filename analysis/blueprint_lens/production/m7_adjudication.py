"""M7 Task 7 comparison and adjudication record.

The controller and independent artefacts are compared as sets of identities.
This module records the absence of disagreements without treating that absence
as an independent confirmation of either derivation's correctness.
"""

from __future__ import annotations

from collections.abc import Mapping
import json
from pathlib import Path
from typing import Any

from ..schema_validation import validate_instance
from .m7_truth import audit_truth_registry


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas" / "blueprint-lens-m7-adjudication-v1.schema.json"
_CONTROLLER_PRODUCER = "controller_round_b"
_ADJUDICATOR_NOTE = (
    "The controller produced Round B and also adjudicates. Because all 38 node "
    "sets and all 38 edge sets agreed, no pair turned on this role; this record "
    "therefore records an absence of disagreements, not a confirmation that the "
    "shared derivation is correct. Had any disagreement existed, this party "
    "relationship would have been disqualifying for the adjudication."
)


def _unique_sorted(values: list[str]) -> tuple[str, ...]:
    return tuple(sorted(set(values)))


def _schema_errors(document: Any) -> tuple[str, ...]:
    try:
        schema = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
        validate_instance(document, schema)
    except Exception as error:  # pragma: no cover - fail-closed boundary
        return (f"M7_ADJUDICATION_DOCUMENT_SHAPE_INVALID: {error}",)
    return ()


def _registry_pair_specs(registry: Any) -> list[tuple[Mapping[str, Any], Mapping[str, Any]]]:
    if not isinstance(registry, Mapping):
        raise ValueError("truth registry must be an object")
    entries = registry.get("entries")
    if not isinstance(entries, list):
        raise ValueError("truth registry entries must be an array")

    by_id: dict[str, Mapping[str, Any]] = {}
    for raw_entry in entries:
        if isinstance(raw_entry, Mapping):
            entry_id = raw_entry.get("entry_id")
            if isinstance(entry_id, str) and entry_id:
                by_id[entry_id] = raw_entry

    rederivations = [
        entry
        for entry in entries
        if isinstance(entry, Mapping)
        and entry.get("independence_model") == "independent_rederivation"
    ]
    pairs: list[tuple[Mapping[str, Any], Mapping[str, Any]]] = []
    seen: set[frozenset[str]] = set()
    for entry in rederivations:
        entry_id = entry.get("entry_id")
        counterpart_id = entry.get("counterpart_entry_id")
        if not isinstance(entry_id, str) or not isinstance(counterpart_id, str):
            raise ValueError("every rederivation entry must have an entry and counterpart id")
        counterpart = by_id.get(counterpart_id)
        if counterpart is None:
            raise ValueError(f"rederivation counterpart is not registered: {entry_id}")
        if counterpart.get("independence_model") != "independent_rederivation":
            raise ValueError(f"rederivation counterpart is not a rederivation: {entry_id}")
        if counterpart.get("counterpart_entry_id") != entry_id:
            raise ValueError(f"rederivation counterpart is not mutual: {entry_id}")
        pair_key = frozenset((entry_id, counterpart_id))
        if len(pair_key) != 2:
            raise ValueError(f"rederivation entry is paired with itself: {entry_id}")
        if pair_key in seen:
            continue

        if entry.get("producer") == _CONTROLLER_PRODUCER:
            controller, independent = entry, counterpart
        elif counterpart.get("producer") == _CONTROLLER_PRODUCER:
            controller, independent = counterpart, entry
        else:
            raise ValueError(f"pair has no controller Round B entry: {entry_id}")
        pairs.append((controller, independent))
        seen.add(pair_key)

    if len(rederivations) != len(pairs) * 2:
        raise ValueError("every rederivation entry must belong to exactly one pair")
    return sorted(
        pairs,
        key=lambda pair: (
            str(pair[0].get("entry_id", "")),
            str(pair[1].get("entry_id", "")),
        ),
    )


def _role_relative_path(entry: Mapping[str, Any], role: str) -> Path:
    raw_path = entry.get("path")
    if not isinstance(raw_path, str) or not raw_path:
        raise ValueError(f"{role} entry path is invalid")
    parts = Path(raw_path).parts
    try:
        marker = parts.index(role)
    except ValueError as error:
        raise ValueError(f"{role} entry path does not name its artefact root") from error
    relative = parts[marker + 1 :]
    if not relative:
        raise ValueError(f"{role} entry path has no artefact name")
    return Path(*relative)


def _load_json(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError(f"could not read artefact {path}: {error}") from error
    if not isinstance(value, Mapping):
        raise ValueError(f"artefact {path} must be an object")
    return value


def _identity_set(value: Any, label: str) -> set[str]:
    if not isinstance(value, list):
        raise ValueError(f"{label} must be an array")
    if any(not isinstance(item, str) or not item for item in value):
        raise ValueError(f"{label} must contain non-empty identities")
    return set(value)


def _artefact_sets(
    entry: Mapping[str, Any], root: Path, role: str
) -> tuple[str | None, set[str], set[str]]:
    document = _load_json(root / _role_relative_path(entry, role))
    if role == "controller":
        expected = document.get("expected")
        if not isinstance(expected, Mapping):
            raise ValueError("controller artefact expected must be an object")
        nodes = _identity_set(expected.get("node_ids"), "controller expected.node_ids")
        edges = _identity_set(expected.get("edge_ids"), "controller expected.edge_ids")
    else:
        nodes = _identity_set(document.get("node_ids"), "independent node_ids")
        edges = _identity_set(document.get("edge_ids"), "independent edge_ids")
    query_id = document.get("query_id")
    if role == "independent" and (not isinstance(query_id, str) or not query_id):
        raise ValueError("independent artefact query_id is invalid")
    if role == "controller":
        query_id = None
    return query_id, nodes, edges


def _comparison_row(
    controller: Mapping[str, Any],
    independent: Mapping[str, Any],
    controller_dir: Path,
    independent_dir: Path,
) -> dict[str, Any]:
    _, controller_nodes, controller_edges = _artefact_sets(
        controller, controller_dir, "controller"
    )
    independent_query_id, independent_nodes, independent_edges = _artefact_sets(
        independent, independent_dir, "independent"
    )
    node_agreement = controller_nodes == independent_nodes
    edge_agreement = controller_edges == independent_edges
    disagreement = None
    if not node_agreement or not edge_agreement:
        disagreement = {
            "controller_only_nodes": sorted(controller_nodes - independent_nodes),
            "independent_only_nodes": sorted(independent_nodes - controller_nodes),
            "controller_only_edges": sorted(controller_edges - independent_edges),
            "independent_only_edges": sorted(independent_edges - controller_edges),
        }
    return {
        "query_id": independent_query_id,
        "controller_entry_id": controller["entry_id"],
        "independent_entry_id": independent["entry_id"],
        "node_agreement": node_agreement,
        "edge_agreement": edge_agreement,
        "disagreement": disagreement,
        "resolution": None,
    }


def _validated_registry(registry: Any) -> None:
    audit = audit_truth_registry(registry)
    if audit.errors:
        raise ValueError("truth registry audit failed: " + "; ".join(audit.errors))


def build_adjudication(
    registry: Mapping[str, Any], controller_dir: str | Path, independent_dir: str | Path
) -> dict[str, Any]:
    """Build the canonical comparison record from the registered artefacts."""

    _validated_registry(registry)
    pair_specs = _registry_pair_specs(registry)
    rows = [
        _comparison_row(
            controller,
            independent,
            Path(controller_dir),
            Path(independent_dir),
        )
        for controller, independent in pair_specs
    ]
    rows.sort(key=lambda row: row["query_id"])
    agreed = sum(
        1
        for row in rows
        if row["node_agreement"] and row["edge_agreement"]
    )
    disagreed = len(rows) - agreed
    unresolved = sum(
        1
        for row in rows
        if not (row["node_agreement"] and row["edge_agreement"])
        and row["resolution"] is None
    )
    return {
        "schema_name": "blueprint-lens-m7-adjudication",
        "schema_version": "1.0.0",
        "adjudicated_on": "2026-08-22",
        "adjudicator": {
            "identity": "controller",
            "is_party_to_a_derivation": True,
            "note": _ADJUDICATOR_NOTE,
        },
        "counts": {
            "pairs": len(rows),
            "agreed": agreed,
            "disagreed": disagreed,
            "unresolved": unresolved,
        },
        "agreement_rate": {
            "numerator": agreed,
            "denominator": len(rows),
        },
        "pairs": rows,
    }


def _row_key(row: Mapping[str, Any]) -> tuple[Any, Any]:
    return row.get("controller_entry_id"), row.get("independent_entry_id")


def _source_view(row: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "query_id": row["query_id"],
        "controller_entry_id": row["controller_entry_id"],
        "independent_entry_id": row["independent_entry_id"],
        "node_agreement": row["node_agreement"],
        "edge_agreement": row["edge_agreement"],
        "disagreement": row["disagreement"],
    }


def adjudication_errors(document: Any) -> tuple[str, ...]:
    """Return internal document errors without reading external artefacts."""

    shape_errors = _schema_errors(document)
    if shape_errors:
        return shape_errors
    try:
        rows = document["pairs"]
        agreed = 0
        unresolved = 0
        errors: list[str] = []
        for index, row in enumerate(rows):
            row_agrees = row["node_agreement"] and row["edge_agreement"]
            if row_agrees:
                agreed += 1
                continue
            if row["resolution"] is None:
                unresolved += 1
                errors.append(
                    "M7_ADJUDICATION_UNRESOLVED_DISAGREEMENT: "
                    f"pairs[{index}] {row['query_id']} has no resolution"
                )
            resolution = row["resolution"]
            if resolution is not None and not resolution["reasoning"].strip():
                errors.append(
                    "M7_ADJUDICATION_RESOLUTION_WITHOUT_REASONING: "
                    f"pairs[{index}] {row['query_id']} has empty reasoning"
                )

        expected_counts = {
            "pairs": len(rows),
            "agreed": agreed,
            "disagreed": len(rows) - agreed,
            "unresolved": unresolved,
        }
        expected_rate = {"numerator": agreed, "denominator": len(rows)}
        if document["counts"] != expected_counts or document["agreement_rate"] != expected_rate:
            errors.append(
                "M7_ADJUDICATION_RATE_MISMATCH: counts or agreement_rate "
                "does not match the pair rows"
            )
        return _unique_sorted(errors)
    except Exception as error:  # pragma: no cover - schema is the normal guard
        return (f"M7_ADJUDICATION_DOCUMENT_SHAPE_INVALID: {error}",)


def adjudication_gaps(document: Any) -> tuple[str, ...]:
    """Return limitations recorded by an otherwise well-shaped document."""

    if _schema_errors(document):
        return ()
    try:
        gaps: list[str] = []
        adjudicator = document["adjudicator"]
        if adjudicator["is_party_to_a_derivation"]:
            gaps.append(
                "M7_ADJUDICATION_ADJUDICATOR_IS_A_PARTY: "
                "the controller produced Round B and also adjudicates; any "
                "disagreement would make this adjudicator disqualifying"
            )
        for row in document["pairs"]:
            resolution = row["resolution"]
            if resolution is not None and resolution["decided_for"] == "slicer_output":
                gaps.append(
                    "M7_ADJUDICATION_RESOLVED_TOWARD_SLICER: "
                    f"{row['query_id']} was resolved toward the slicer's output"
                )
        return _unique_sorted(gaps)
    except Exception:  # pragma: no cover - schema is the normal guard
        return ()


def verify_adjudication(
    document: Any,
    registry: Mapping[str, Any],
    controller_dir: str | Path,
    independent_dir: str | Path,
) -> tuple[str, ...]:
    """Verify the document against a fresh registry-backed artefact comparison."""

    shape_errors = _schema_errors(document)
    if shape_errors:
        return shape_errors
    try:
        _validated_registry(registry)
        expected = build_adjudication(registry, controller_dir, independent_dir)

        actual_rows = document["pairs"]
        expected_rows = expected["pairs"]
        actual_keys = [_row_key(row) for row in actual_rows]
        expected_keys = [_row_key(row) for row in expected_rows]
        actual_key_set = set(actual_keys)
        expected_key_set = set(expected_keys)
        if (
            len(actual_keys) != len(actual_key_set)
            or actual_key_set != expected_key_set
        ):
            missing = sorted(expected_key_set - actual_key_set)
            extra = sorted(actual_key_set - expected_key_set)
            return (
                "M7_ADJUDICATION_PAIR_MISSING: document pair set differs from "
                f"the registry (missing={missing!r}, extra={extra!r})",
            )

        actual_by_key = {_row_key(row): row for row in actual_rows}
        for expected_row in expected_rows:
            key = _row_key(expected_row)
            if _source_view(actual_by_key[key]) != _source_view(expected_row):
                return (
                    "M7_ADJUDICATION_DRIFT: "
                    f"{expected_row['query_id']} differs from the fresh "
                    "controller/independent artefact comparison",
                )

        return adjudication_errors(document)
    except Exception as error:  # pragma: no cover - external content boundary
        return (f"M7_ADJUDICATION_DRIFT: fresh comparison failed: {error}",)


__all__ = [
    "adjudication_errors",
    "adjudication_gaps",
    "build_adjudication",
    "verify_adjudication",
]
