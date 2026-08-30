"""Build and fail-closed publish the real LC4 Sequence truth artifacts."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import tempfile
from typing import Any, Callable, Mapping

from .contract_validation import validate_contract_file
from .execution_slice import compute_execution_slice
from .lc4_sequence import (
    BINDING_MISMATCH,
    CANONICAL_IDENTITY_DUPLICATED,
    COUNT_MISMATCH,
    CRITERION_MEMBERSHIP_MISMATCH,
    LC4SequenceError,
    ORDINAL_INVALID,
    OUTPUT_COVERAGE_MISMATCH,
    PATH_MEMBERSHIP_MISMATCH,
    RECONVERGENCE_KIND_INVALID,
    ROOT_INVALID,
    SOURCE_COMPILER_ORDER_MISMATCH,
    SOURCE_PIN_MISMATCH,
    UNSUPPORTED_BOUNDARY_UNDECLARED,
    build_sequence_profile,
    parse_compiler_audit,
    validate_sequence_profile,
)
from .raw_probe import load_blueprint_lens_v1, load_raw_probe
from .schema_validation import validate_json_file
from .typed_ir import TypedIRBuildError, build_typed_ir


LC4_BLUEPRINT_PATH = (
    "/Game/LensCorpus/BP_LC4_SequenceDisclosure.BP_LC4_SequenceDisclosure"
)
LC4_CRITERION_SYMBOL = "LC4Complete"
LC4_CRITERION_DESCRIPTION = (
    "Set LC4Complete after the criterion-reaching Sequence outputs reconverge"
)
LC4_STEM = "BP_LC4_SequenceDisclosure"


class LC4ArtifactError(ValueError):
    """Raised when the LC4 evidence chain cannot be frozen or published."""


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


def _load_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC4ArtifactError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, dict):
        raise LC4ArtifactError(f"JSON root must be an object: {path}")
    return value


def _one(values: list[Any], description: str) -> Any:
    if len(values) != 1:
        raise LC4ArtifactError(
            f"{description} must resolve exactly once; found {len(values)}"
        )
    return values[0]


def _criterion(ir: Mapping[str, Any]) -> tuple[str, str]:
    matches: list[tuple[str, str]] = []
    blueprint = ir.get("blueprint")
    if not isinstance(blueprint, Mapping):
        raise LC4ArtifactError("typed IR blueprint must be an object")
    graphs = blueprint.get("graphs")
    if not isinstance(graphs, list):
        raise LC4ArtifactError("typed IR graphs must be an array")
    for graph in graphs:
        if not isinstance(graph, Mapping):
            continue
        nodes = graph.get("nodes")
        if not isinstance(nodes, list):
            continue
        for node in nodes:
            if not isinstance(node, Mapping):
                continue
            symbol = node.get("symbol") or {}
            if (
                isinstance(symbol, Mapping)
                and symbol.get("kind") == "variable"
                and symbol.get("access") == "set"
                and symbol.get("name") == LC4_CRITERION_SYMBOL
            ):
                matches.append((str(graph.get("id", "")), str(node.get("id", ""))))
    return _one(matches, "LC4 criterion symbol")


def _build_slice(ir_path: Path, ir: Mapping[str, Any]) -> dict[str, Any]:
    graph_id, criterion_node_id = _criterion(ir)
    document = load_blueprint_lens_v1(ir_path)
    result = compute_execution_slice(document, criterion_node_id)
    if result.graph_id != graph_id:
        raise LC4ArtifactError("LC4 criterion graph changed during slicing")
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
        "slice_kind": "execution_context",
        "criterion": {
            "graph_id": graph_id,
            "node_id": criterion_node_id,
            "description": LC4_CRITERION_DESCRIPTION,
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


def _validate_reviewed_ground_truth(
    ground_truth: Mapping[str, Any],
    slice_value: Mapping[str, Any],
) -> None:
    review = ground_truth.get("review")
    if not isinstance(review, Mapping) or review.get("status") != "frozen":
        raise LC4ArtifactError("LC4 reviewed ground truth must have frozen status")
    if ground_truth.get("slice_kind") != "execution_context":
        raise LC4ArtifactError("LC4 ground truth must describe an execution slice")
    if ground_truth.get("criterion") != slice_value.get("criterion"):
        raise LC4ArtifactError("LC4 ground-truth criterion differs from generated slice")
    expected = ground_truth.get("expected")
    if not isinstance(expected, Mapping):
        raise LC4ArtifactError("LC4 ground truth expected record is missing")
    expected_node_ids = expected.get("node_ids")
    expected_edge_ids = expected.get("edge_ids")
    slice_node_ids = slice_value.get("node_ids")
    slice_edge_ids = slice_value.get("edge_ids")
    if not all(
        isinstance(values, list)
        for values in (
            expected_node_ids,
            expected_edge_ids,
            slice_node_ids,
            slice_edge_ids,
        )
    ) or set(expected_node_ids) != set(slice_node_ids) or set(
        expected_edge_ids
    ) != set(slice_edge_ids):
        raise LC4ArtifactError(
            "LC4 generated slice differs from independently reviewed membership"
        )


def _expect_diagnostic(
    name: str,
    expected_code: str,
    operation: Callable[[], None],
) -> dict[str, Any]:
    try:
        operation()
    except LC4SequenceError as error:
        if error.code != expected_code:
            raise LC4ArtifactError(
                f"mutation {name} emitted {error.code}, expected {expected_code}"
            ) from error
        return {
            "name": name,
            "expected_diagnostic": expected_code,
            "actual_diagnostic": error.code,
            "passed": True,
        }
    raise LC4ArtifactError(f"mutation {name} was accepted")


def run_sequence_mutations(
    profile: Mapping[str, Any],
    ir: Mapping[str, Any],
    slice_value: Mapping[str, Any],
    sequence_source: Mapping[str, Any],
    compiler_audit: str,
    source_binding: Mapping[str, Any],
) -> dict[str, Any]:
    """Run the frozen adversarial matrix against one valid LC4 profile."""

    cases: list[dict[str, Any]] = []

    changed_source = deepcopy(dict(sequence_source))
    changed_source["sequence_node_id"] = "missing-sequence-root"
    cases.append(
        _expect_diagnostic(
            "missing_root",
            ROOT_INVALID,
            lambda: build_sequence_profile(
                ir, slice_value, changed_source, compiler_audit, source_binding
            ),
        )
    )

    changed_profile = deepcopy(dict(profile))
    changed_profile["source"]["raw_sha256"] = (
        "1" * 64
        if changed_profile["source"]["raw_sha256"] == "0" * 64
        else "0" * 64
    )
    cases.append(
        _expect_diagnostic(
            "corrupt_bound_hash",
            BINDING_MISMATCH,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    changed_profile = deepcopy(dict(profile))
    changed_profile["outputs"].pop()
    cases.append(
        _expect_diagnostic(
            "missing_output",
            OUTPUT_COVERAGE_MISMATCH,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    changed_profile = deepcopy(dict(profile))
    changed_profile["outputs"][1]["ordinal"] = changed_profile["outputs"][0][
        "ordinal"
    ]
    cases.append(
        _expect_diagnostic(
            "duplicate_ordinal",
            ORDINAL_INVALID,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    changed_profile = deepcopy(dict(profile))
    changed_profile["outputs"][0]["source_pin_name"] += "_forged"
    cases.append(
        _expect_diagnostic(
            "forged_source_pin",
            SOURCE_PIN_MISMATCH,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    audit_lines = compiler_audit.splitlines()
    output_indices = [
        index for index, line in enumerate(audit_lines) if line.startswith("OUTPUT\t")
    ]
    if len(output_indices) < 2:
        raise LC4ArtifactError("LC4 mutation audit requires two connected outputs")
    first_fields = audit_lines[output_indices[0]].split("\t")
    second_fields = audit_lines[output_indices[1]].split("\t")
    audit_lines[output_indices[0]] = "\t".join([first_fields[0], first_fields[1], *second_fields[2:]])
    audit_lines[output_indices[1]] = "\t".join([second_fields[0], second_fields[1], *first_fields[2:]])
    changed_audit = "\n".join(audit_lines) + "\n"
    cases.append(
        _expect_diagnostic(
            "source_compiler_order_swap",
            SOURCE_COMPILER_ORDER_MISMATCH,
            lambda: build_sequence_profile(
                ir, slice_value, sequence_source, changed_audit, source_binding
            ),
        )
    )

    changed_profile = deepcopy(dict(profile))
    membership_output = next(
        item for item in changed_profile["outputs"] if item["criterion_relation"] != "included"
    )
    membership_output["criterion_relation"] = "included"
    membership_output["criterion_reason"] = "selected_execution_edge"
    cases.append(
        _expect_diagnostic(
            "reverse_criterion_membership",
            CRITERION_MEMBERSHIP_MISMATCH,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    changed_profile = deepcopy(dict(profile))
    path_output = next(
        item for item in changed_profile["outputs"] if item["reachable_edge_ids"]
    )
    path_output["reachable_edge_ids"].pop()
    cases.append(
        _expect_diagnostic(
            "remove_reachable_edge",
            PATH_MEMBERSHIP_MISMATCH,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    if not profile.get("reconvergences"):
        raise LC4ArtifactError("LC4 fixture must expose an ordinary reconvergence")
    changed_profile = deepcopy(dict(profile))
    changed_profile["reconvergences"][0]["kind"] = "and_barrier"
    cases.append(
        _expect_diagnostic(
            "false_barrier_kind",
            RECONVERGENCE_KIND_INVALID,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    for count_name in sorted(profile["counts"]):
        changed_profile = deepcopy(dict(profile))
        changed_profile["counts"][count_name] += 1
        cases.append(
            _expect_diagnostic(
                f"corrupt_count_{count_name}",
                COUNT_MISMATCH,
                lambda changed_profile=changed_profile: validate_sequence_profile(
                    changed_profile,
                    ir,
                    slice_value,
                    sequence_source,
                    compiler_audit,
                    source_binding,
                ),
            )
        )

    changed_profile = deepcopy(dict(profile))
    identity_output = next(
        item for item in changed_profile["outputs"] if item["reachable_node_ids"]
    )
    identity_output["reachable_node_ids"].append(
        identity_output["reachable_node_ids"][0]
    )
    cases.append(
        _expect_diagnostic(
            "duplicate_canonical_identity",
            CANONICAL_IDENTITY_DUPLICATED,
            lambda: validate_sequence_profile(
                changed_profile,
                ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    changed_ir = deepcopy(dict(ir))
    candidate_output = next(
        item
        for item in profile["outputs"]
        if item["connection_state"] == "connected"
        and item["criterion_relation"] == "outside"
        and item["reachable_node_ids"]
    )
    boundary_node_id = candidate_output["reachable_node_ids"][0]
    changed_node = _one(
        [
            node
            for graph in changed_ir["blueprint"]["graphs"]
            for node in graph["nodes"]
            if node["id"] == boundary_node_id
        ],
        "LC4 unsupported mutation target",
    )
    changed_node["semantic_status"] = "unsupported"
    changed_node["semantic_reason"] = "mutation_boundary"
    cases.append(
        _expect_diagnostic(
            "suppress_unsupported_boundary",
            UNSUPPORTED_BOUNDARY_UNDECLARED,
            lambda: validate_sequence_profile(
                profile,
                changed_ir,
                slice_value,
                sequence_source,
                compiler_audit,
                source_binding,
            ),
        )
    )

    return {
        "format": "blueprint-lens-lc4-sequence-mutations",
        "schema_version": "1.0.0",
        "status": "PASS",
        "case_count": len(cases),
        "cases": cases,
    }


def _verification_markdown(
    asset_path: Path,
    readiness: Mapping[str, Any],
) -> str:
    checks = readiness["checks"]
    counts = readiness["counts"]
    return f"""# LC4 Sequence source-truth verification

Status: `TRUTH_FROZEN`

This record freezes source truth only. It does not select a visual grammar,
change the Explanation schema, implement Slate, or provide human-comprehension
evidence.

## Provenance

- Asset: `{asset_path.name}`
- Asset SHA-256: `{readiness['hashes']['asset_sha256_before']}`
- Sequence profile: `{readiness['artifacts']['sequence_profile']}`

## Gate checks

- Repeated raw export: {'PASS' if checks['raw_runs_byte_identical'] else 'FAIL'}
- Repeated Sequence API inventory: {'PASS' if checks['sequence_source_runs_byte_identical'] else 'FAIL'}
- Repeated compiler-order audit: {'PASS' if checks['compiler_audit_runs_byte_identical'] else 'FAIL'}
- API/compiler connected order: {'PASS' if checks['source_compiler_order_match'] else 'FAIL'}
- Reviewed criterion truth: {'PASS' if checks['reviewed_ground_truth_exact'] else 'FAIL'}
- Adversarial mutations: {'PASS' if checks['all_mutations_rejected'] else 'FAIL'}
- Package hash stability: {'PASS' if checks['asset_hash_stable'] else 'FAIL'}

## Discovered counts

- Declared outputs: {counts['declared_output_count']}
- Connected outputs: {counts['connected_output_count']}
- Unconnected outputs: {counts['unconnected_output_count']}
- Criterion-included outputs: {counts['criterion_included_output_count']}
- Outside-criterion connected outputs: {counts['outside_criterion_connected_output_count']}
- Indeterminate outputs: {counts['indeterminate_output_count']}

## Verdict

`TRUTH_FROZEN`; the separate visual-grammar decision is the next Gate.
"""


def _build_lc4_artifacts_in_directory(
    raw_run1_path: Path,
    raw_run2_path: Path,
    sequence_source_run1_path: Path,
    sequence_source_run2_path: Path,
    compiler_audit_run1_path: Path,
    compiler_audit_run2_path: Path,
    reviewed_ground_truth_path: Path,
    asset_path: Path,
    output_dir: Path,
    graph_schema_path: Path,
    slice_schema_path: Path,
    ground_truth_schema_path: Path,
    profile_schema_path: Path,
) -> dict[str, Path]:
    asset_hash_before = _sha256_file(asset_path)
    raw_bytes = raw_run1_path.read_bytes()
    source_bytes = sequence_source_run1_path.read_bytes()
    audit_bytes = compiler_audit_run1_path.read_bytes()
    if raw_bytes != raw_run2_path.read_bytes():
        raise LC4ArtifactError("LC4 raw exports are not byte-identical")
    if source_bytes != sequence_source_run2_path.read_bytes():
        raise LC4ArtifactError("LC4 Sequence source exports are not byte-identical")
    if audit_bytes != compiler_audit_run2_path.read_bytes():
        raise LC4ArtifactError("LC4 compiler-order audits are not byte-identical")

    load_raw_probe(raw_run1_path)
    load_raw_probe(raw_run2_path)
    raw = _load_object(raw_run1_path)
    if raw.get("blueprint", {}).get("path") != LC4_BLUEPRINT_PATH:
        raise LC4ArtifactError("LC4 source Blueprint path does not match contract")
    source_value = _load_object(sequence_source_run1_path)
    audit_text = compiler_audit_run1_path.read_text(encoding="utf-8")
    parse_compiler_audit(audit_text)

    canonical_raw_path = output_dir / f"{LC4_STEM}.raw-0.2.json"
    canonical_source_path = output_dir / f"{LC4_STEM}.sequence-source.json"
    canonical_audit_path = output_dir / f"{LC4_STEM}.sequence-compiler-order.tsv"
    _write_bytes(canonical_raw_path, raw_bytes)
    _write_bytes(canonical_source_path, source_bytes)
    _write_bytes(canonical_audit_path, audit_bytes)

    try:
        ir = build_typed_ir(raw, expected_blueprint_path=LC4_BLUEPRINT_PATH)
    except TypedIRBuildError as error:
        raise LC4ArtifactError(str(error)) from error
    ir_path = output_dir / f"{LC4_STEM}.ir.v1.json"
    _write_json(ir_path, ir)
    validate_contract_file(ir_path, graph_schema_path)

    slice_value = _build_slice(ir_path, ir)
    slice_path = output_dir / f"{LC4_STEM}.execution.slice.v1.json"
    _write_json(slice_path, slice_value)
    validate_contract_file(slice_path, slice_schema_path)

    ground_truth = _load_object(reviewed_ground_truth_path)
    ground_truth_path = output_dir / f"{LC4_STEM}.execution.ground-truth.v1.json"
    _write_json(ground_truth_path, ground_truth)
    validate_contract_file(ground_truth_path, ground_truth_schema_path)
    _validate_reviewed_ground_truth(ground_truth, slice_value)

    graph_id, criterion_node_id = _criterion(ir)
    sequence_node_id = str(source_value.get("sequence_node_id", ""))
    source_binding = {
        "blueprint_asset_path": LC4_BLUEPRINT_PATH,
        "graph_id": graph_id,
        "sequence_node_id": sequence_node_id,
        "criterion_node_id": criterion_node_id,
        "asset_file": asset_path.name,
        "asset_sha256": asset_hash_before,
        "raw_file": canonical_raw_path.name,
        "raw_sha256": _sha256_file(canonical_raw_path),
        "sequence_source_file": canonical_source_path.name,
        "sequence_source_sha256": _sha256_file(canonical_source_path),
        "compiler_audit_file": canonical_audit_path.name,
        "compiler_audit_sha256": _sha256_file(canonical_audit_path),
        "ir_file": ir_path.name,
        "ir_sha256": _sha256_file(ir_path),
        "slice_file": slice_path.name,
        "slice_sha256": _sha256_file(slice_path),
    }
    profile = build_sequence_profile(
        ir, slice_value, source_value, audit_text, source_binding
    )
    profile_path = output_dir / f"{LC4_STEM}.sequence-profile.v1.json"
    _write_json(profile_path, profile)
    validate_json_file(profile_path, profile_schema_path)
    validate_sequence_profile(
        profile, ir, slice_value, source_value, audit_text, source_binding
    )

    mutation_report = run_sequence_mutations(
        profile, ir, slice_value, source_value, audit_text, source_binding
    )
    mutation_path = output_dir / "mutation-report.json"
    _write_json(mutation_path, mutation_report)

    asset_hash_after = _sha256_file(asset_path)
    checks = {
        "all_mutations_rejected": mutation_report["status"] == "PASS",
        "asset_hash_stable": asset_hash_before == asset_hash_after,
        "compiler_audit_runs_byte_identical": True,
        "profile_schema_valid": True,
        "profile_semantic_valid": True,
        "raw_runs_byte_identical": True,
        "reviewed_ground_truth_exact": True,
        "sequence_source_runs_byte_identical": True,
        "slice_contract_valid": True,
        "source_compiler_order_match": True,
        "typed_ir_contract_valid": True,
    }
    if not all(checks.values()):
        failed = sorted(name for name, passed in checks.items() if not passed)
        raise LC4ArtifactError(f"LC4 readiness checks failed: {failed}")
    readiness = {
        "format": "blueprint-lens-lc4-sequence-readiness",
        "schema_version": "1.0.0",
        "status": "TRUTH_FROZEN",
        "checks": checks,
        "counts": dict(profile["counts"]),
        "hashes": {
            "asset_sha256_before": asset_hash_before,
            "asset_sha256_after": asset_hash_after,
            "raw_run1_sha256": _sha256_file(raw_run1_path),
            "raw_run2_sha256": _sha256_file(raw_run2_path),
            "sequence_source_run1_sha256": _sha256_file(sequence_source_run1_path),
            "sequence_source_run2_sha256": _sha256_file(sequence_source_run2_path),
            "compiler_audit_run1_sha256": _sha256_file(compiler_audit_run1_path),
            "compiler_audit_run2_sha256": _sha256_file(compiler_audit_run2_path),
            "typed_ir_sha256": _sha256_file(ir_path),
            "slice_sha256": _sha256_file(slice_path),
            "ground_truth_sha256": _sha256_file(ground_truth_path),
            "sequence_profile_sha256": _sha256_file(profile_path),
            "mutation_report_sha256": _sha256_file(mutation_path),
        },
        "artifacts": {
            "canonical_raw": canonical_raw_path.name,
            "sequence_source": canonical_source_path.name,
            "compiler_audit": canonical_audit_path.name,
            "typed_ir": ir_path.name,
            "criterion_slice": slice_path.name,
            "reviewed_ground_truth": ground_truth_path.name,
            "sequence_profile": profile_path.name,
            "mutation_report": mutation_path.name,
            "verification": "verification.md",
        },
        "limitations": [
            "This Gate freezes LC4-SEQ source truth only; no visual grammar or Slate surface is selected.",
            "Ordinary reconvergence is not an AND barrier and carries no single-fire claim.",
            "LC4-ASYNC remains outside core-v1 and deferred at its declared frontier.",
        ],
    }
    verification_path = output_dir / "verification.md"
    _write_bytes(
        verification_path,
        _verification_markdown(asset_path, readiness).encode("utf-8"),
    )
    readiness["hashes"]["verification_sha256"] = _sha256_file(verification_path)
    readiness_path = output_dir / "readiness.json"
    _write_json(readiness_path, readiness)

    if _sha256_file(asset_path) != asset_hash_before:
        raise LC4ArtifactError("LC4 Blueprint package changed during analysis")
    return {
        "canonical_raw": canonical_raw_path,
        "sequence_source": canonical_source_path,
        "compiler_audit": canonical_audit_path,
        "ir": ir_path,
        "slice": slice_path,
        "ground_truth": ground_truth_path,
        "profile": profile_path,
        "mutations": mutation_path,
        "verification": verification_path,
        "readiness": readiness_path,
    }


def build_lc4_sequence_artifacts(
    raw_run1_path: Path,
    raw_run2_path: Path,
    sequence_source_run1_path: Path,
    sequence_source_run2_path: Path,
    compiler_audit_run1_path: Path,
    compiler_audit_run2_path: Path,
    reviewed_ground_truth_path: Path,
    asset_path: Path,
    output_dir: Path,
    graph_schema_path: Path,
    slice_schema_path: Path,
    ground_truth_schema_path: Path,
    profile_schema_path: Path,
) -> dict[str, Path]:
    """Build in staging and publish readiness only after every Gate check passes."""

    inputs = [
        raw_run1_path,
        raw_run2_path,
        sequence_source_run1_path,
        sequence_source_run2_path,
        compiler_audit_run1_path,
        compiler_audit_run2_path,
        reviewed_ground_truth_path,
        asset_path,
    ]
    resolved_inputs = [Path(path).resolve() for path in inputs]
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    readiness_path = output_dir / "readiness.json"
    readiness_path.unlink(missing_ok=True)

    try:
        with tempfile.TemporaryDirectory(
            prefix=".lc4-sequence-staging-", dir=output_dir
        ) as staging_directory:
            staged = _build_lc4_artifacts_in_directory(
                *resolved_inputs,
                Path(staging_directory),
                Path(graph_schema_path).resolve(),
                Path(slice_schema_path).resolve(),
                Path(ground_truth_schema_path).resolve(),
                Path(profile_schema_path).resolve(),
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
