"""Independent readiness audit for the bounded LC5 intra-BP pure-call profile."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any, Mapping

from .intra_bp_pure import (
    PROFILE_ID,
    FrozenProjectDocumentProvider,
    canonical_resolution_bytes,
    resolve_intra_bp_pure_call,
    run_intra_bp_pure_mutations,
    validate_resolution_product,
)
from ..raw_probe import load_blueprint_lens_v1
from ..schema_validation import validate_instance


COMMIT_BINDING_INVALID = "LC5_READINESS_COMMIT_BINDING_INVALID"
GATE_INVALID = "LC5_READINESS_GATE_INVALID"
MUTATION_EVIDENCE_INVALID = "LC5_READINESS_MUTATION_EVIDENCE_INVALID"
PRODUCT_HASH_INVALID = "LC5_READINESS_PRODUCT_HASH_INVALID"
SCOPE_INVALID = "LC5_READINESS_SCOPE_INVALID"
SEMANTIC_INVALID = "LC5_READINESS_SEMANTIC_INVALID"

_READINESS_NAME = "readiness.json"
_CONTEXTUAL_NAME = "BP_SlicingProbe.contextual-slice.v1.json"
_MUTATION_NAME = "mutation-report.json"
_GATE_NAME = "schema-gate.json"
_GROUND_TRUTH_NAME = "reviewed-ground-truth.v1.json"
_SOURCE_NAME = "BP_SlicingProbe.intra-bp-pure-source.json"
_AUDIT_NAME = "BP_SlicingProbe.intra-bp-pure-audit.tsv"
_EXPECTED_MUTATIONS = {
    "stale_compile_state",
    "missing_target",
    "ambiguous_target",
    "non_self_context",
    "impure_call",
    "latent_call",
    "cross_blueprint_target",
    "recursive_call_context",
    "depth_budget_exhausted",
    "function_reference_guid_mismatch",
    "source_audit_target_mismatch",
    "formal_pin_identity_mismatch",
    "pin_container_mismatch",
    "duplicate_occurrence",
    "cross_context_internal_relation",
}


class LC5IntraBpPureReadinessError(ValueError):
    """Stable fail-closed diagnostic from the independent LC5 readiness audit."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


def _fail(code: str, message: str) -> None:
    raise LC5IntraBpPureReadinessError(code, message)


def _load_object(path: Path, code: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(code, f"cannot load {path.name}: {error}")
    if not isinstance(value, dict):
        _fail(code, f"{path.name} must contain one JSON object")
    return value


def _read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        _fail(PRODUCT_HASH_INVALID, f"cannot read {path.name}: {error}")


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(PRODUCT_HASH_INVALID, f"cannot hash {path}: {error}")


def _canonical_bytes(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode(
        "utf-8"
    )


def _git(root: Path, arguments: list[str], context: str) -> str:
    completed = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        _fail(
            COMMIT_BINDING_INVALID,
            f"{context}: {completed.stderr.strip() or completed.stdout.strip()}",
        )
    return completed.stdout.strip()


def _audit_commit_tree(
    evidence: Path,
    fixture: Path,
    asset: Path,
    raw: Path,
    source_schema: Path,
    contextual_schema: Path,
    schema_gate_commit: str,
) -> None:
    root = Path(
        _git(asset.parent, ["rev-parse", "--show-toplevel"], "cannot resolve Git root")
    ).resolve()
    _git(
        root,
        ["cat-file", "-e", f"{schema_gate_commit}^{{commit}}"],
        "schema Gate object is not a commit",
    )
    canonical_evidence = Path("artifacts/r1/lc5-intra-bp-pure-truth")
    targets = [
        (fixture, fixture.relative_to(root)),
        (asset, asset.relative_to(root)),
        (raw, raw.relative_to(root)),
        (source_schema, source_schema.relative_to(root)),
        (contextual_schema, contextual_schema.relative_to(root)),
        (evidence / _CONTEXTUAL_NAME, canonical_evidence / _CONTEXTUAL_NAME),
        (evidence / _MUTATION_NAME, canonical_evidence / _MUTATION_NAME),
        (evidence / _GATE_NAME, canonical_evidence / _GATE_NAME),
        (evidence / _GROUND_TRUTH_NAME, canonical_evidence / _GROUND_TRUTH_NAME),
    ]
    for run in ("run1", "run2"):
        for name in (_SOURCE_NAME, _AUDIT_NAME):
            targets.append((evidence / run / name, canonical_evidence / run / name))
    for local, target in targets:
        if not local.is_file():
            _fail(PRODUCT_HASH_INVALID, f"required product is missing: {local.name}")
        target_text = target.as_posix()
        committed_oid = _git(
            root,
            ["rev-parse", f"{schema_gate_commit}:{target_text}"],
            f"schema Gate commit lacks {target_text}",
        )
        working_oid = _git(
            root,
            ["hash-object", f"--path={target_text}", str(local)],
            f"cannot hash {target_text} with Git attributes",
        )
        if committed_oid != working_oid:
            _fail(COMMIT_BINDING_INVALID, f"schema Gate commit bytes differ for {target_text}")


def _audit_gate(gate: Mapping[str, Any]) -> None:
    if gate.get("profile_id") != PROFILE_ID:
        _fail(SCOPE_INVALID, "schema Gate scope expanded beyond the bounded profile")
    if (
        gate.get("format") != "blueprint-lens-lc5-intra-bp-pure-schema-gate"
        or gate.get("schema_version") != "1.0.0"
        or gate.get("status")
        != "SCHEMA_VALIDATOR_MUTATIONS_VERIFIED__COMMIT_BOUND_READINESS_NEXT"
        or gate.get("next_gate")
        != "commit-bound independent readiness audit and TRUTH_FROZEN decision"
    ):
        _fail(GATE_INVALID, "schema Gate identity or status differs")
    checks = gate.get("checks")
    if not isinstance(checks, dict) or not checks or not all(
        value is True for value in checks.values()
    ):
        _fail(GATE_INVALID, "schema Gate contains a failed or malformed check")


def _audit_mutations(report: Mapping[str, Any]) -> None:
    cases = report.get("cases")
    if (
        report.get("format") != "blueprint-lens-lc5-intra-bp-pure-mutations"
        or report.get("schema_version") != "1.0.0"
        or report.get("status") != "PASS"
        or report.get("case_count") != 15
        or not isinstance(cases, list)
        or len(cases) != 15
    ):
        _fail(MUTATION_EVIDENCE_INVALID, "mutation report header or count differs")
    names = [case.get("name") for case in cases if isinstance(case, dict)]
    if set(names) != _EXPECTED_MUTATIONS or len(names) != len(set(names)):
        _fail(MUTATION_EVIDENCE_INVALID, "mutation coverage differs")
    if any(
        case.get("passed") is not True
        or case.get("actual_outcome") != case.get("expected_outcome")
        for case in cases
    ):
        _fail(MUTATION_EVIDENCE_INVALID, "a mutation did not fail closed")


def _audit_core_boundary(fixture: Mapping[str, Any], call_site_node_id: str) -> None:
    matches = []
    blueprint = fixture.get("blueprint")
    graphs = blueprint.get("graphs", []) if isinstance(blueprint, dict) else []
    for graph in graphs:
        if not isinstance(graph, dict):
            continue
        matches.extend(
            node
            for node in graph.get("nodes", [])
            if isinstance(node, dict) and node.get("id") == call_site_node_id
        )
    if len(matches) != 1:
        _fail(SEMANTIC_INVALID, "core-v1 call site identity is not unique")
    call = matches[0]
    if (
        call.get("semantic_status") != "opaque"
        or call.get("semantic_reason") != "function_body_not_expanded"
    ):
        _fail(SEMANTIC_INVALID, "core-v1 function-call boundary is no longer opaque")


def audit_intra_bp_pure_readiness(
    evidence_dir: str | Path,
    fixture_path: str | Path,
    asset_path: str | Path,
    raw_path: str | Path,
    source_schema_path: str | Path,
    contextual_schema_path: str | Path,
    schema_gate_commit: str,
) -> dict[str, Any]:
    """Re-open every frozen input and independently decide bounded readiness."""

    evidence = Path(evidence_dir).resolve()
    fixture_path = Path(fixture_path).resolve()
    asset = Path(asset_path).resolve()
    raw = Path(raw_path).resolve()
    source_schema_path = Path(source_schema_path).resolve()
    contextual_schema_path = Path(contextual_schema_path).resolve()
    if not isinstance(schema_gate_commit, str) or len(schema_gate_commit) != 40:
        _fail(SCOPE_INVALID, "schema Gate commit must be a full Git object id")

    required = [
        fixture_path,
        asset,
        raw,
        source_schema_path,
        contextual_schema_path,
        evidence / _CONTEXTUAL_NAME,
        evidence / _MUTATION_NAME,
        evidence / _GATE_NAME,
        evidence / _GROUND_TRUTH_NAME,
    ]
    for run in ("run1", "run2"):
        required.extend((evidence / run / _SOURCE_NAME, evidence / run / _AUDIT_NAME))
    for path in required:
        if not path.is_file():
            _fail(PRODUCT_HASH_INVALID, f"required readiness input is missing: {path}")

    gate_path = evidence / _GATE_NAME
    contextual_path = evidence / _CONTEXTUAL_NAME
    mutation_path = evidence / _MUTATION_NAME
    ground_truth_path = evidence / _GROUND_TRUTH_NAME
    gate = _load_object(gate_path, GATE_INVALID)
    contextual = _load_object(contextual_path, PRODUCT_HASH_INVALID)
    mutation_report = _load_object(mutation_path, MUTATION_EVIDENCE_INVALID)
    reviewed = _load_object(ground_truth_path, PRODUCT_HASH_INVALID)
    source_schema = _load_object(source_schema_path, PRODUCT_HASH_INVALID)
    contextual_schema = _load_object(contextual_schema_path, PRODUCT_HASH_INVALID)
    fixture_object = _load_object(fixture_path, PRODUCT_HASH_INVALID)

    _audit_gate(gate)
    _audit_mutations(mutation_report)
    _audit_commit_tree(
        evidence,
        fixture_path,
        asset,
        raw,
        source_schema_path,
        contextual_schema_path,
        schema_gate_commit,
    )
    hashes = gate.get("hashes")
    if not isinstance(hashes, dict):
        _fail(GATE_INVALID, "schema Gate hashes are missing")
    expected_hashes = {
        "asset_sha256": asset,
        "raw_sha256": raw,
        "call_resolution_schema_sha256": source_schema_path,
        "contextual_slice_schema_sha256": contextual_schema_path,
        "contextual_slice_sha256": contextual_path,
        "mutation_report_sha256": mutation_path,
        "reviewed_ground_truth_sha256": ground_truth_path,
    }
    for field, path in expected_hashes.items():
        if hashes.get(field) != _sha256(path):
            _fail(PRODUCT_HASH_INVALID, f"schema Gate hash differs for {field}")

    sources = [
        _load_object(evidence / run / _SOURCE_NAME, PRODUCT_HASH_INVALID)
        for run in ("run1", "run2")
    ]
    audits = [_read_text(evidence / run / _AUDIT_NAME) for run in ("run1", "run2")]
    source_bytes = [(evidence / run / _SOURCE_NAME).read_bytes() for run in ("run1", "run2")]
    audit_bytes = [(evidence / run / _AUDIT_NAME).read_bytes() for run in ("run1", "run2")]
    if source_bytes[0] != source_bytes[1] or audit_bytes[0] != audit_bytes[1]:
        _fail(PRODUCT_HASH_INVALID, "native repeated products are not byte-identical")
    if hashes.get("source_sha256") != hashlib.sha256(source_bytes[0]).hexdigest():
        _fail(PRODUCT_HASH_INVALID, "native source hash differs")
    if hashes.get("audit_sha256") != hashlib.sha256(audit_bytes[0]).hexdigest():
        _fail(PRODUCT_HASH_INVALID, "native audit hash differs")

    source = sources[0]
    call_site = source.get("call_site")
    if not isinstance(call_site, dict) or not isinstance(call_site.get("node_id"), str):
        _fail(SEMANTIC_INVALID, "native call-site identity is absent")
    provider = FrozenProjectDocumentProvider(
        document=load_blueprint_lens_v1(fixture_path),
        asset_sha256=str(source.get("asset_sha256", "")),
        raw_sha256=str(source.get("raw_sha256", "")),
        compile_provenance=source.get("compile_provenance", {}),
    )
    try:
        rebuilt = resolve_intra_bp_pure_call(
            provider,
            source,
            audits[0],
            call_site_node_id=call_site["node_id"],
        )
        validate_resolution_product(rebuilt)
        validate_instance(source, source_schema)
        validate_instance(rebuilt, contextual_schema)
    except ValueError as error:
        _fail(SEMANTIC_INVALID, f"independent semantic rebuild failed: {error}")
    if _canonical_bytes(rebuilt) != contextual_path.read_bytes():
        _fail(SEMANTIC_INVALID, "contextual slice differs from a fresh semantic rebuild")
    if canonical_resolution_bytes(rebuilt) != canonical_resolution_bytes(contextual):
        _fail(SEMANTIC_INVALID, "contextual slice semantic bytes differ")

    fresh_mutations = run_intra_bp_pure_mutations(
        provider,
        source,
        audits[0],
        call_site_node_id=call_site["node_id"],
    )
    if _canonical_bytes(fresh_mutations) != mutation_path.read_bytes():
        _fail(MUTATION_EVIDENCE_INVALID, "mutation matrix differs from a fresh rebuild")
    _audit_core_boundary(fixture_object, call_site["node_id"])

    review = reviewed.get("review")
    if (
        not isinstance(review, dict)
        or review.get("status") != "reviewed_for_schema_gate"
        or "runtime" not in " ".join(review.get("limitations", [])).lower()
    ):
        _fail(SEMANTIC_INVALID, "reviewed truth does not preserve the static-only boundary")
    reviewed_truth = reviewed.get("source_truth")
    adjudication = reviewed.get("product_adjudication")
    target = source.get("targets", [{}])[0]
    reviewed_bindings = (
        reviewed_truth.get("bindings", []) if isinstance(reviewed_truth, dict) else []
    )
    source_bindings = source.get("bindings", [])
    binding_projection = [
        {
            "ordinal": binding.get("ordinal"),
            "kind": binding.get("kind"),
            "name": binding.get("property", {}).get("name"),
            "cpp_type": binding.get("property", {}).get("cpp_type"),
            "pin_category": binding.get("property", {})
            .get("pin_type", {})
            .get("category"),
            "container": binding.get("property", {})
            .get("pin_type", {})
            .get("container"),
        }
        for binding in source_bindings
        if isinstance(binding, dict)
    ]
    reviewed_projection = {
        "profile_id": PROFILE_ID,
        "asset_path": source.get("blueprint_asset_path"),
        "asset_sha256": source.get("asset_sha256"),
        "raw_sha256": source.get("raw_sha256"),
        "package_guid": source.get("compile_provenance", {}).get("package_guid"),
        "generated_class_path": source.get("compile_provenance", {}).get(
            "generated_class_path"
        ),
        "call_graph_id": call_site.get("graph_id"),
        "call_site_node_id": call_site.get("node_id"),
        "function_name": target.get("name"),
        "function_guid": target.get("guid"),
        "function_path": target.get("function_path"),
        "function_graph_id": target.get("graph_id"),
        "function_graph_guid": target.get("graph_guid"),
        "entry_node_id": target.get("entry_node_id"),
        "result_node_id": target.get("result_node_id"),
        "body_node_count": len(rebuilt.get("occurrences", [])) - 1,
        "body_edge_count": len(rebuilt.get("internal_relations", [])),
        "bindings": binding_projection,
    }
    if reviewed_truth != reviewed_projection or reviewed_bindings != binding_projection:
        _fail(SEMANTIC_INVALID, "reviewed source truth differs from the fresh rebuild")
    expected_adjudication = {
        "run1_run2_source_byte_identical": True,
        "run1_run2_audit_byte_identical": True,
        "source_sha256": hashes.get("source_sha256"),
        "audit_sha256": hashes.get("audit_sha256"),
        "candidate_count": len(source.get("targets", [])),
        "binding_count": len(source_bindings),
        "contextual_occurrence_count": len(rebuilt.get("occurrences", [])),
        "internal_relation_count": len(rebuilt.get("internal_relations", [])),
        "max_call_depth": rebuilt.get("max_call_depth"),
    }
    if adjudication != expected_adjudication:
        _fail(SEMANTIC_INVALID, "reviewed product adjudication differs")
    counts = gate.get("counts")
    expected_counts = {
        "native_run_count": 2,
        "candidate_count": 1,
        "binding_count": 3,
        "contextual_occurrence_count": 4,
        "internal_relation_count": 4,
        "mutation_case_count": 15,
    }
    if counts != expected_counts:
        _fail(SEMANTIC_INVALID, "schema Gate counts do not reconcile")
    if (
        contextual.get("profile_id") != PROFILE_ID
        or contextual.get("status") != "resolved_unique"
        or contextual.get("call_context", {}).get("claim_scope")
        != "static_contextual_occurrence_not_runtime_invocation"
    ):
        _fail(SCOPE_INVALID, "contextual result exceeds the frozen static profile")

    checks = {
        "schema_gate_commit_bound": True,
        "schema_gate_commit_tree_exact": True,
        "schema_gate_checks_pass": True,
        "source_and_audit_runs_byte_identical": True,
        "asset_and_raw_hashes_match": True,
        "auxiliary_schemas_match": True,
        "reviewed_ground_truth_matches": True,
        "reviewed_truth_semantics_revalidated": True,
        "contextual_slice_rebuild_exact": True,
        "all_15_mutations_rebuilt_and_rejected": True,
        "static_context_identity_distinct_from_runtime_invocation": True,
        "core_v1_opaque_boundary_preserved": True,
        "frozen_scope_is_bounded": True,
    }
    return {
        "format": "blueprint-lens-lc5-intra-bp-pure-readiness",
        "schema_version": "1.0.0",
        "status": "TRUTH_FROZEN",
        "scope": PROFILE_ID,
        "schema_gate_commit": schema_gate_commit,
        "core_v1_outcome": "opaque / function_body_not_expanded",
        "checks": checks,
        "counts": expected_counts,
        "hashes": {
            "asset_sha256": _sha256(asset),
            "raw_sha256": _sha256(raw),
            "call_resolution_schema_sha256": _sha256(source_schema_path),
            "contextual_slice_schema_sha256": _sha256(contextual_schema_path),
            "schema_gate_sha256": _sha256(gate_path),
            "contextual_slice_sha256": _sha256(contextual_path),
            "mutation_report_sha256": _sha256(mutation_path),
            "reviewed_ground_truth_sha256": _sha256(ground_truth_path),
            "source_sha256": hashes["source_sha256"],
            "audit_sha256": hashes["audit_sha256"],
        },
        "frozen_claims": [
            "One self-context pure non-latent CalculateRecovery call resolves uniquely to one same-Blueprint function graph.",
            "CurrentHealth and Bonus bind to typed formal inputs and NewHealth binds from the typed result.",
            "Four static contextual occurrences and four internal relations rebuild byte-identically from frozen products.",
            "Call-context identity is distinct from canonical source identity and is not a runtime invocation identity.",
        ],
        "boundaries": [
            limitation
            for limitation in deepcopy(review.get("limitations", []))
            if "not TRUTH_FROZEN" not in limitation
        ],
        "not_authorized": [
            "Runtime invocation, frequency, value-flow observation or temporal causality claims",
            "Macro, impure, latent, cross-Blueprint or dynamic-dispatch support",
            "Changing core-v1 from opaque / function_body_not_expanded",
            "Visual conditions, effect images, Slate portal or product default",
            "Human comprehension, preference or general scalability claims",
            "LC6 or LC7",
        ],
        "next_gate": "information-matched LC5 visual-condition design over the frozen contextual product",
    }


def freeze_intra_bp_pure_readiness(
    evidence_dir: str | Path,
    fixture_path: str | Path,
    asset_path: str | Path,
    raw_path: str | Path,
    source_schema_path: str | Path,
    contextual_schema_path: str | Path,
    schema_gate_commit: str,
) -> Path:
    """Publish readiness.json atomically only after the independent audit passes."""

    evidence = Path(evidence_dir).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    destination = evidence / _READINESS_NAME
    destination.unlink(missing_ok=True)
    decision = audit_intra_bp_pure_readiness(
        evidence,
        fixture_path,
        asset_path,
        raw_path,
        source_schema_path,
        contextual_schema_path,
        schema_gate_commit,
    )
    with tempfile.NamedTemporaryFile(
        prefix=".lc5-readiness-", suffix=".tmp", dir=evidence, delete=False
    ) as handle:
        temporary = Path(handle.name)
        handle.write(_canonical_bytes(decision))
    try:
        if _load_object(temporary, SEMANTIC_INVALID).get("status") != "TRUTH_FROZEN":
            _fail(SEMANTIC_INVALID, "temporary readiness decision is invalid")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination
