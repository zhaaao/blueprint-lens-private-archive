"""Frozen M7 corpus and G7 evidence packet construction and verification."""

from __future__ import annotations

import json
from pathlib import Path
import re
from typing import Any, Mapping

from ..digests import file_sha256
from ..schema_validation import validate_instance


_CONDITION_IDS = tuple(f"G7-C{index}" for index in range(1, 8))
_DISPOSITIONS = ("met", "met_vacuously", "met_with_recorded_gap", "not_met")
_SATISFIED = frozenset(_DISPOSITIONS[:3])
_SCHEMA_NAME = "blueprint-lens-m7-g7-evidence"
_SCHEMA_VERSION = "1.0.0"
_ROOT = Path(__file__).resolve().parents[3]


def _sha256(path: Path) -> str:
    return file_sha256(path)


def _json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _portable(root: Path, path: Path) -> str:
    return path.resolve().relative_to(root.resolve()).as_posix()


def _schema(root: Path) -> Mapping[str, Any]:
    path = root / "schemas" / "blueprint-lens-m7-g7-evidence-v1.schema.json"
    if not path.is_file():
        path = _ROOT / "schemas" / "blueprint-lens-m7-g7-evidence-v1.schema.json"
    return _json(path)


def _freeze_paths(root: Path) -> list[str]:
    paths: set[str] = set()
    for base in (root / "artifacts" / "m7", root / "fixtures" / "m7"):
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if not path.is_file() or "__pycache__" in path.parts:
                continue
            relative = _portable(root, path)
            if relative.startswith("artifacts/m7/g7/"):
                continue
            paths.add(relative)
    registry_path = root / "fixtures" / "m7" / "m7-truth-registry.v1.json"
    if registry_path.is_file():
        for entry in _json(registry_path).get("entries", []):
            if isinstance(entry, Mapping) and isinstance(entry.get("path"), str):
                paths.add(entry["path"])

    claim_audit_path = root / "artifacts/m7/claim-audit/claim-audit.v1.json"
    if claim_audit_path.is_file():
        for claim in _json(claim_audit_path).get("claims", []):
            for evidence in claim.get("evidence", []):
                if isinstance(evidence, Mapping) and isinstance(evidence.get("path"), str):
                    paths.add(evidence["path"])

    coverage_path = root / "artifacts/m7/coverage/coverage-matrix.v1.json"
    if coverage_path.is_file():
        for binding in _json(coverage_path).get("lc_bindings", []):
            if isinstance(binding, Mapping) and isinstance(binding.get("truth_artefact"), str):
                paths.add(binding["truth_artefact"])

    measurement_path = root / "artifacts/m7/measurement/measurement-report.v1.json"
    if measurement_path.is_file():
        measurement = _json(measurement_path)
        for metric in measurement.get("layout_metrics", []):
            for source in metric.get("sources", []):
                if isinstance(source, str):
                    paths.add(source)
        for telemetry in measurement.get("telemetry", []):
            if isinstance(telemetry, Mapping) and isinstance(telemetry.get("path"), str):
                paths.add(telemetry["path"])
    return sorted(paths)


def frozen_dataset(root: Path) -> tuple[Mapping[str, Any], ...]:
    """Return the rule-derived frozen path/digest rows in sorted path order."""

    return tuple(
        {"path": relative, "sha256": _sha256(root / relative)}
        for relative in _freeze_paths(root)
        if (root / relative).is_file()
    )


def _typed_product_paths(root: Path) -> tuple[str, ...]:
    return tuple(
        _portable(root, path)
        for path in sorted((root / "artifacts/m7").rglob("*.blueprint-lens-v1.json"))
    )


def _plan_statements(root: Path) -> dict[str, str]:
    plan_path = root / "idea-stage" / "docs" / "development_plan.md"
    if not plan_path.is_file():
        plan_path = _ROOT / "idea-stage" / "docs" / "development_plan.md"
    text = plan_path.read_text(
        encoding="utf-8"
    )
    match = re.search(r"^### G7 验收条件\s*$([\s\S]*?)(?=^## |\Z)", text, re.MULTILINE)
    if match is None:
        raise ValueError("G7 acceptance section is missing")
    lines = [line[2:].strip() for line in match.group(1).splitlines() if line.startswith("- ")]
    if len(lines) != 7:
        raise ValueError(f"expected seven G7 statements, found {len(lines)}")
    return dict(zip(_CONDITION_IDS, lines))


def _pointer(root: Path, relative: str) -> dict[str, str]:
    path = root / relative
    return {"path": relative, "sha256": _sha256(path)}


def _basis(root: Path, *relative: str) -> list[dict[str, str]]:
    return [_pointer(root, path) for path in relative]


def _condition_facts(root: Path) -> dict[str, Any]:
    manifest = _json(root / "fixtures/m7/m7-corpus-manifest.v1.json")
    statuses: set[str] = set()
    uncertain_nodes: dict[tuple[str, str], str] = {}
    for path in (root / "artifacts/m7").rglob("*.blueprint-lens-v1.json"):
        try:
            document = _json(path)
        except (OSError, UnicodeError, json.JSONDecodeError):
            continue
        for graph in document.get("blueprint", {}).get("graphs", []):
            for node in graph.get("nodes", []):
                if isinstance(node, Mapping) and isinstance(node.get("semantic_status"), str):
                    statuses.add(node["semantic_status"])
                    if node["semantic_status"] == "uncertain":
                        uncertain_nodes[(graph.get("id", ""), node.get("id", ""))] = node.get(
                            "class", "unknown"
                        )

    correctness = _json(root / "artifacts/m7/correctness/correctness-report.v1.json")
    rows = correctness.get("rows", [])
    exact = sum(
        1
        for row in rows
        if all(
            row.get(side, {}).get(field) == 0
            for side in ("nodes", "edges")
            for field in ("false_positive", "false_negative")
        )
    )
    adjudication = _json(root / "artifacts/m7/truth/adjudication.v1.json")
    coverage = _json(root / "artifacts/m7/coverage/coverage-matrix.v1.json")
    coverage_gaps = tuple(coverage.get("gaps", []))
    measurement = _json(root / "artifacts/m7/measurement/measurement-report.v1.json")
    taxonomy = _json(root / "artifacts/m7/failure-taxonomy/failure-taxonomy.v1.json")
    audit = _json(root / "artifacts/m7/claim-audit/claim-audit.v1.json")

    c1_ok = statuses.issubset({"supported", "opaque", "unsupported"}) and bool(statuses)
    false_negative_nodes = sum(row.get("nodes", {}).get("false_negative", 0) for row in rows)
    false_negative_edges = sum(row.get("edges", {}).get("false_negative", 0) for row in rows)
    repaired_entries = tuple(
        entry
        for entry in taxonomy.get("entries", [])
        if isinstance(entry, Mapping)
        and isinstance(entry.get("repaired"), bool)
    )
    unrepaired_entries = tuple(
        entry for entry in repaired_entries if entry["repaired"] is False
    )
    c4_ok = (
        false_negative_nodes == 0
        and false_negative_edges == 0
        and bool(repaired_entries)
        and not unrepaired_entries
    )
    limitation_ids = {
        row.get("id") for row in measurement.get("limitations", []) if isinstance(row, Mapping)
    }
    layout_not_recorded = sum(
        1
        for metric in measurement.get("layout_metrics", [])
        if isinstance(metric.get("value"), Mapping)
        and metric["value"].get("status") == "not_recorded"
    )
    candidate_graph_ids = {
        row.get("graph_id")
        for row in manifest.get("candidate_graphs", [])
        if isinstance(row, Mapping)
    }
    telemetry_scopes = [
        row.get("scope", "")
        for row in measurement.get("telemetry", [])
        if isinstance(row, Mapping)
    ]
    covered_graph_ids = {
        graph_id
        for graph_id in candidate_graph_ids
        if graph_id and any(graph_id in scope for scope in telemetry_scopes)
    }
    layout_sources = [
        source
        for metric in measurement.get("layout_metrics", [])
        for source in metric.get("sources", [])
    ]
    layout_on_corpus = bool(layout_sources) and all(source.startswith("artifacts/m7/") for source in layout_sources)
    c6_ok = (
        layout_not_recorded == 0
        and layout_on_corpus
        and covered_graph_ids == candidate_graph_ids
    )
    frozen_index = {row["path"]: row["sha256"] for row in frozen_dataset(root)}
    established_claims_have_evidence = all(
        claim.get("status") != "established_bounded" or bool(claim.get("evidence"))
        for claim in audit.get("claims", [])
    )
    established_claims_are_frozen = all(
        claim.get("status") != "established_bounded"
        or all(
            evidence.get("path") in frozen_index
            and frozen_index[evidence["path"]] == evidence.get("sha256")
            for evidence in claim.get("evidence", [])
        )
        for claim in audit.get("claims", [])
    )
    c7_ok = established_claims_have_evidence and established_claims_are_frozen and all(
        isinstance(claim, Mapping)
        and isinstance(claim.get("boundaries"), list)
        and bool(claim["boundaries"])
        for claim in audit.get("claims", [])
    )
    admitted_graph_ids = {
        row.get("graph_id")
        for row in manifest.get("candidate_graphs", [])
        if isinstance(row, Mapping)
    }
    uncertain_admitted = {
        key: class_path
        for key, class_path in uncertain_nodes.items()
        if key[0] in admitted_graph_ids
    }
    return {
        "c1_ok": c1_ok,
        "statuses": tuple(sorted(statuses)),
        "uncertain_total": len(uncertain_nodes),
        "uncertain_admitted": len(uncertain_admitted),
        "uncertain_classes": tuple(sorted(set(uncertain_nodes.values()))),
        "c2": {"numerator": exact, "denominator": len(rows)},
        "c3_vacuous": adjudication.get("counts", {}).get("disagreed") == 0
        and adjudication.get("counts", {}).get("unresolved") == 0,
        "c4_ok": c4_ok,
        "c4_unrepaired_count": len(unrepaired_entries),
        "c4_repair_field_present": bool(repaired_entries),
        "c4_false_negative_nodes": false_negative_nodes,
        "c4_false_negative_edges": false_negative_edges,
        "c5_gap_count": len(coverage_gaps),
        "c5_dimension_gap_count": sum(
            str(gap).startswith("M7_COVERAGE_DIMENSION_") for gap in coverage_gaps
        ),
        "c5_lc_binding_gap_count": sum(
            str(gap).startswith("M7_COVERAGE_LC_") for gap in coverage_gaps
        ),
        "c6_ok": c6_ok,
        "c6_limitation_ids": tuple(sorted(limitation_ids)),
        "c6_layout_not_recorded": layout_not_recorded,
        "c6_layout_on_corpus": layout_on_corpus,
        "c6_covered_graph_count": len(covered_graph_ids),
        "c6_corpus_graph_count": len(candidate_graph_ids),
        "c7_ok": c7_ok,
        "c7_established_claims_have_evidence": established_claims_have_evidence,
        "c7_established_claims_are_frozen": established_claims_are_frozen,
        "c5_exhibited_dimension_count": len({
            cell.get("dimension")
            for cell in coverage.get("cells", [])
            if isinstance(cell, Mapping) and cell.get("exhibited") is True
        }),
        "candidate_graph_count": len(manifest.get("candidate_graphs", [])),
        "observed_max_node_count": max(
            row["measured"]["node_count"] for row in manifest.get("candidate_graphs", [])
        ),
        "failure_count": len(taxonomy.get("entries", [])),
    }


def _condition_rows(root: Path) -> list[dict[str, Any]]:
    statements = _plan_statements(root)
    facts = _condition_facts(root)
    rows = [
        {
            "id": "G7-C1",
            "statement": statements["G7-C1"],
            "disposition": "met" if facts["c1_ok"] else "not_met",
            "basis": _basis(root, *_typed_product_paths(root)),
            "derivation": (
                "Reopened every retained typed-IR product and checked every node's "
                "semantic_status against supported, opaque and unsupported. The retained "
                f"products contain {facts['uncertain_total']} distinct uncertain nodes "
                f"({facts['uncertain_admitted']} on admitted candidates), including "
                + ", ".join(facts["uncertain_classes"])
                + "; observed statuses were "
                + ", ".join(facts["statuses"])
                + "."
            ),
            "gap": None if facts["c1_ok"] else "Four distinct uncertain nodes remain in retained typed products, including two on admitted candidate graphs; uncertain is outside the G7 vocabulary.",
            "measured": None,
        },
        {
            "id": "G7-C2",
            "statement": statements["G7-C2"],
            "disposition": "met" if facts["c2"]["numerator"] == facts["c2"]["denominator"] else "not_met",
            "basis": _basis(root, "artifacts/m7/correctness/correctness-report.v1.json"),
            "derivation": "Recomputed exact node and edge agreement from each correctness row, rather than using the report aggregate.",
            "gap": None if facts["c2"]["numerator"] == facts["c2"]["denominator"] else "Some correctness rows have a node or edge disagreement.",
            "measured": facts["c2"],
        },
        {
            "id": "G7-C3",
            "statement": statements["G7-C3"],
            "disposition": "met_vacuously" if facts["c3_vacuous"] else "met_with_recorded_gap",
            "basis": _basis(root, "artifacts/m7/truth/adjudication.v1.json", "artifacts/m7/failure-taxonomy/failure-taxonomy.v1.json"),
            "derivation": "The adjudication record has an empty disagreement and unresolved denominator; M7-F19 records that agreement does not exercise the ambiguous specification regions.",
            "gap": "No disagreement was available to adjudicate; the empty denominator is not evidence that ambiguous cases are resolved.",
            "measured": {"numerator": 0, "denominator": facts["c2"]["denominator"]},
        },
        {
            "id": "G7-C4",
            "statement": statements["G7-C4"],
            "disposition": "met" if facts["c4_ok"] else "not_met",
            "basis": _basis(root, "artifacts/m7/correctness/correctness-report.v1.json", "artifacts/m7/failure-taxonomy/failure-taxonomy.v1.json"),
            "derivation": (
                "Read the correctness rows for omitted content (false negatives are "
                f"{facts['c4_false_negative_nodes']} of nodes and {facts['c4_false_negative_edges']} "
                "of edges), then inspected the module-defined boolean taxonomy field "
                "repaired for unrepaired entries "
                f"({facts['c4_unrepaired_count']}); repaired: bool is present="
                f"{facts['c4_repair_field_present']}. Other taxonomy fields are not repair "
                "evidence. The conservative "
                "silent-omission reading treats either a measured omission or a known "
                "unrepaired omission as failing."
            ),
            "gap": None if facts["c4_ok"] else "The correctness rows show no slice false negatives, but the taxonomy has no repaired: bool field from which absence of a known unrepaired omission can be established.",
            "measured": None,
        },
        {
            "id": "G7-C5",
            "statement": statements["G7-C5"],
            "disposition": "met_with_recorded_gap" if facts["c5_gap_count"] else "met",
            "basis": _basis(root, "artifacts/m7/coverage/coverage-matrix.v1.json"),
            "derivation": f"Counted distinct dimensions with at least one exhibited=true coverage cell: {facts['c5_exhibited_dimension_count']} of 8. The remaining dimension is explicitly declared_unverified; the matrix separately records {facts['c5_dimension_gap_count']} unrepresented dimension and {facts['c5_lc_binding_gap_count']} LC/binding gaps.",
            "gap": None if not facts["c5_gap_count"] else (
                f"The coverage matrix records {facts['c5_dimension_gap_count']} "
                f"unrepresented dimension and {facts['c5_lc_binding_gap_count']} "
                "LC/binding gaps."
            ),
            "measured": {"numerator": facts["c5_exhibited_dimension_count"], "denominator": 8},
        },
        {
            "id": "G7-C6",
            "statement": statements["G7-C6"],
            "disposition": "met" if facts["c6_ok"] else "not_met",
            "basis": _basis(root, "artifacts/m7/measurement/measurement-report.v1.json", "fixtures/m7/m7-corpus-manifest.v1.json"),
            "derivation": (
                "Derived from measurement content rather than the required limitation id: "
                f"{facts['c6_layout_not_recorded']} layout metrics are not_recorded; "
                f"layout sources are M7-corpus={facts['c6_layout_on_corpus']}; render timing "
                f"covers {facts['c6_covered_graph_count']} of {facts['c6_corpus_graph_count']} "
                "manifest graphs."
            ),
            "gap": None if facts["c6_ok"] else (
                f"{facts['c6_layout_not_recorded']} layout metrics are not_recorded; "
                "their sources are R1 geometry oracles rather than M7 corpus graphs; "
                f"render timing covers {facts['c6_covered_graph_count']} of "
                f"{facts['c6_corpus_graph_count']} corpus graphs."
            ),
            "measured": None,
        },
        {
            "id": "G7-C7",
            "statement": statements["G7-C7"],
            "disposition": "met" if facts["c7_ok"] else "not_met",
            "basis": _basis(root, "artifacts/m7/claim-audit/claim-audit.v1.json", "artifacts/m7/correctness/correctness-report.v1.json"),
            "derivation": "For every established_bounded claim, required non-empty evidence and matched each evidence digest to the freshly derived frozen dataset; also required a non-empty boundary on every claim. not_established claims remain basis-empty.",
            "gap": None if facts["c7_ok"] else (
                "The claim audit has an established_bounded claim without frozen evidence "
                "or a claim without its required boundary."
            ),
            "measured": None,
        },
    ]
    return rows


def _claims(root: Path) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for claim in _json(root / "artifacts/m7/claim-audit/claim-audit.v1.json").get("claims", []):
        status = claim["status"]
        result.append(
            {
                "claim_id": claim["claim_id"],
                "status": status,
                "basis": []
                if status == "not_established"
                else [
                    {"path": evidence["path"], "sha256": evidence["sha256"]}
                    for evidence in claim.get("evidence", [])
                ],
                "boundaries": list(claim["boundaries"]),
            }
        )
    return result


def build_g7_evidence(root: Path) -> dict[str, Any]:
    """Build the complete packet from reopened retained artefacts."""

    root = Path(root)
    files = list(frozen_dataset(root))
    manifest = _json(root / "fixtures/m7/m7-corpus-manifest.v1.json")
    conditions = _condition_rows(root)
    counts = {name: sum(row["disposition"] == name for row in conditions) for name in _DISPOSITIONS}
    return {
        "boundary": (
            "M7 establishes bounded engineering evidence only. It does not establish "
            "human comprehension, preference, superiority, usability, or a product default. "
            "The packet under artifacts/m7/g7/ is excluded from the freeze because a "
            "report cannot hash itself."
        ),
        "claims": _claims(root),
        "condition_counts": counts,
        "conditions": conditions,
        "dataset": {
            "candidate_graph_count": len(manifest["candidate_graphs"]),
            "file_count": len(files),
            "files": files,
            "observed_max_node_count": max(row["measured"]["node_count"] for row in manifest["candidate_graphs"]),
        },
        "gate_disposition": (
            "G7_BLOCKED_PENDING_OWNER_DECISION"
            if any(row["disposition"] == "not_met" for row in conditions)
            else "G7_READY_FOR_OWNER_ACCEPTANCE"
        ),
        "observed_failure_count": len(_json(root / "artifacts/m7/failure-taxonomy/failure-taxonomy.v1.json")["entries"]),
        "schema_name": _SCHEMA_NAME,
        "schema_version": _SCHEMA_VERSION,
    }


def _shape_errors(document: Any, root: Path) -> tuple[str, ...]:
    try:
        validate_instance(document, _schema(root))
    except Exception as error:
        return (f"M7_G7_DOCUMENT_SHAPE_INVALID: {error}",)
    return ()


def g7_evidence_errors(document: Any) -> tuple[str, ...]:
    """Check packet-internal coherence without consulting external artefacts."""

    root = Path(__file__).resolve().parents[3]
    shape = _shape_errors(document, root)
    if shape:
        return shape
    errors: list[str] = []
    conditions = document["conditions"]
    if tuple(row["id"] for row in conditions) != _CONDITION_IDS:
        errors.append("M7_G7_CONDITION_SET_INVALID: condition IDs/order differ")
    if document["dataset"]["file_count"] != len(document["dataset"]["files"]):
        errors.append("M7_G7_COUNT_MISMATCH: dataset file_count differs from rows")
    if len({row["path"] for row in document["dataset"]["files"]}) != len(document["dataset"]["files"]):
        errors.append("M7_G7_COUNT_MISMATCH: dataset paths are not unique")
    for row in conditions:
        if row["disposition"] in _SATISFIED and not row["basis"]:
            errors.append(f"M7_G7_CONDITION_BASIS_EMPTY: {row['id']}")
        if row["disposition"] == "met" and row["gap"] is not None:
            errors.append(f"M7_G7_GAP_MISSING: {row['id']} is met but has a gap")
        if row["disposition"] != "met" and (not isinstance(row["gap"], str) or not row["gap"].strip()):
            errors.append(f"M7_G7_GAP_MISSING: {row['id']}")
    expected_counts = {name: sum(row["disposition"] == name for row in conditions) for name in _DISPOSITIONS}
    if document["condition_counts"] != expected_counts:
        errors.append("M7_G7_COUNT_MISMATCH: condition_counts differs from rows")
    if any(row["disposition"] == "not_met" for row in conditions) and document["gate_disposition"] != "G7_BLOCKED_PENDING_OWNER_DECISION":
        errors.append("M7_G7_READINESS_OVERSTATED: unmet condition is reported ready")
    if document["gate_disposition"] == "G7_READY_FOR_OWNER_ACCEPTANCE" and any(row["disposition"] == "not_met" for row in conditions):
        errors.append("M7_G7_READINESS_OVERSTATED: readiness conflicts with condition rows")
    return tuple(sorted(set(errors)))


def verify_g7_evidence(document: Any, root: Path) -> tuple[str, ...]:
    """Verify internal coherence and fresh filesystem-derived evidence."""

    root = Path(root)
    errors = list(_shape_errors(document, root))
    if errors:
        return tuple(errors)
    errors.extend(g7_evidence_errors(document))
    expected_rows = list(frozen_dataset(root))
    actual_rows = document["dataset"]["files"]
    expected_paths = {row["path"] for row in expected_rows}
    actual_paths = {row["path"] for row in actual_rows}
    if expected_paths - actual_paths:
        errors.append(f"M7_G7_DATASET_INCOMPLETE: missing={sorted(expected_paths - actual_paths)!r}")
    if actual_paths - expected_paths:
        errors.append(f"M7_G7_DATASET_FOREIGN_FILE: extra={sorted(actual_paths - expected_paths)!r}")
    actual_by_path = {row["path"]: row for row in actual_rows}
    for expected in expected_rows:
        actual = actual_by_path.get(expected["path"])
        if actual is None:
            continue
        if actual["sha256"] != expected["sha256"]:
            errors.append(f"M7_G7_DATASET_HASH_DRIFT: {expected['path']}")
    frozen_index = {row["path"]: row["sha256"] for row in expected_rows}
    for condition in document["conditions"]:
        for pointer in condition["basis"]:
            if pointer["path"] not in frozen_index:
                errors.append(f"M7_G7_CONDITION_BASIS_UNFROZEN: {condition['id']} {pointer['path']}")
            elif pointer["sha256"] != frozen_index[pointer["path"]]:
                errors.append(f"M7_G7_DATASET_HASH_DRIFT: condition basis {pointer['path']}")
    audit = _json(root / "artifacts/m7/claim-audit/claim-audit.v1.json")
    audited = {claim["claim_id"]: claim for claim in audit["claims"]}
    for source in audit["claims"]:
        for pointer in source.get("evidence", []):
            path = pointer.get("path")
            digest = pointer.get("sha256")
            if path not in frozen_index:
                errors.append(f"M7_G7_DATASET_INCOMPLETE: claim evidence {path}")
            elif frozen_index[path] != digest:
                errors.append(f"M7_G7_DATASET_HASH_DRIFT: claim evidence {path}")
    for claim in document["claims"]:
        source = audited.get(claim["claim_id"])
        if source is None or claim["status"] != source["status"] or claim["boundaries"] != source["boundaries"]:
            errors.append(f"M7_G7_CLAIM_EXCEEDS_AUDIT: {claim['claim_id']}")
        if source is not None and source["status"] == "not_established" and claim["basis"]:
            errors.append(f"M7_G7_CLAIM_EXCEEDS_AUDIT: {claim['claim_id']} cites refused evidence")
    expected = build_g7_evidence(root)
    for field in ("dataset", "conditions", "condition_counts", "gate_disposition", "claims", "observed_failure_count", "boundary"):
        if document.get(field) != expected[field]:
            code = "M7_G7_CONDITION_DISPOSITION_DRIFT" if field in {"conditions", "gate_disposition"} else "M7_G7_DATASET_INCOMPLETE" if field == "dataset" else "M7_G7_CLAIM_EXCEEDS_AUDIT" if field == "claims" else "M7_G7_COUNT_MISMATCH"
            errors.append(f"{code}: {field} differs from fresh derivation")
    return tuple(sorted(set(errors)))


__all__ = ["build_g7_evidence", "frozen_dataset", "g7_evidence_errors", "verify_g7_evidence"]
