"""Independent readiness audit for the bounded LC4 asynchronous profile."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from typing import Any, Mapping

from .lc4_async import (
    PROFILE_ID,
    RULES_VERSION,
    LC4AsyncError,
    build_async_profile,
    load_async_products,
    validate_async_profile,
)
from .schema_validation import SchemaValidationError, validate_json_file


PRODUCT_HASH_INVALID = "LC4_ASYNC_READINESS_PRODUCT_HASH_INVALID"
SCHEMA_GATE_INVALID = "LC4_ASYNC_READINESS_SCHEMA_GATE_INVALID"
READINESS_SEMANTIC_INVALID = "LC4_ASYNC_READINESS_SEMANTIC_INVALID"
MUTATION_EVIDENCE_INVALID = "LC4_ASYNC_READINESS_MUTATION_EVIDENCE_INVALID"
SCOPE_INVALID = "LC4_ASYNC_READINESS_SCOPE_INVALID"

_PROFILE_NAME = "BP_LC4_AsyncBarrier.async-profile.v1.json"
_MUTATION_NAME = "mutation-report.json"
_GATE_NAME = "schema-gate.json"
_READINESS_NAME = "readiness.json"
_EXPECTED_MUTATIONS = {
    "wrong_latent_uuid",
    "swapped_participant_ids",
    "duplicate_event_occurrence",
    "cross_invocation_relation",
    "forged_completion_relation",
    "reversed_release_relation",
    "incomplete_relation_set",
    "scalar_only_incomparability",
    "missing_participant",
    "duplicate_arrival",
    "release_before_all",
    "duplicate_release",
    "reset_event_in_positive_trace",
    "cancelled_trace",
    "reentry_duplicate_invocation",
    "dropped_event",
    "truncated_trace",
    "wrong_schedule_header",
    "out_of_order_observation_index",
    "incomplete_invocation",
    "ordinary_merge_as_and_barrier",
    "undeclared_barrier_participant",
    "missing_compiler_linkage",
    "stale_compile_hash",
}
_SOURCE_SCOPED_RELATIONS = {"launch_order", "barrier_waits_for"}
_OBSERVED_RELATIONS = {
    "continuation_of",
    "local_resume_order",
    "participant_of",
    "barrier_release",
    "criterion_after_release",
}


class LC4AsyncReadinessError(ValueError):
    """A stable, fail-closed diagnostic from the independent readiness audit."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


def _fail(code: str, message: str) -> None:
    raise LC4AsyncReadinessError(code, message)


def _load_object(path: Path, code: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(code, f"cannot load {path.name}: {error}")
    if not isinstance(value, dict):
        _fail(code, f"{path.name} must contain one JSON object")
    return value


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(PRODUCT_HASH_INVALID, f"cannot hash {path}: {error}")


def _canonical_bytes(value: Mapping[str, Any]) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode(
        "utf-8"
    )


def _safe_product_path(evidence: Path, relative: Any, context: str) -> Path:
    if not isinstance(relative, str) or not relative:
        _fail(PRODUCT_HASH_INVALID, f"{context} path is absent")
    candidate = (evidence / relative).resolve()
    try:
        candidate.relative_to(evidence)
    except ValueError:
        _fail(PRODUCT_HASH_INVALID, f"{context} escapes the evidence directory")
    if not candidate.is_file():
        _fail(PRODUCT_HASH_INVALID, f"{context} is missing: {relative}")
    return candidate


def _audit_product_bindings(
    evidence: Path,
    profile: Mapping[str, Any],
) -> dict[str, str]:
    bindings = profile.get("product_bindings")
    if not isinstance(bindings, list) or len(bindings) != 4:
        _fail(PRODUCT_HASH_INVALID, "profile must bind exactly four product sets")
    result: dict[str, str] = {}
    expected_products = {"run1/A_FIRST", "run1/B_FIRST", "run2/A_FIRST", "run2/B_FIRST"}
    seen: set[str] = set()
    for binding in bindings:
        if not isinstance(binding, dict):
            _fail(PRODUCT_HASH_INVALID, "product binding must be an object")
        product_id = binding.get("product_id")
        if product_id not in expected_products or product_id in seen:
            _fail(PRODUCT_HASH_INVALID, f"unexpected or duplicate product id: {product_id!r}")
        seen.add(str(product_id))
        for prefix in ("source", "compiler_audit", "trace"):
            path = _safe_product_path(
                evidence,
                binding.get(f"{prefix}_file"),
                f"{product_id} {prefix}",
            )
            actual = _sha256(path)
            if actual != binding.get(f"{prefix}_sha256"):
                _fail(PRODUCT_HASH_INVALID, f"{product_id} {prefix} hash differs")
            result[f"{product_id}:{prefix}"] = actual
    if seen != expected_products:
        _fail(PRODUCT_HASH_INVALID, "product binding coverage is incomplete")
    return dict(sorted(result.items()))


def _git(
    root: Path,
    arguments: list[str],
    code: str,
    context: str,
) -> str:
    completed = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        _fail(code, f"{context}: {completed.stderr.strip() or completed.stdout.strip()}")
    return completed.stdout.strip()


def _audit_commit_tree(
    evidence: Path,
    asset: Path,
    schema: Path,
    profile: Mapping[str, Any],
    schema_gate_commit: str,
) -> None:
    root = Path(
        _git(
            asset.parent,
            ["rev-parse", "--show-toplevel"],
            PRODUCT_HASH_INVALID,
            "cannot resolve Git root",
        )
    ).resolve()
    _git(
        root,
        ["cat-file", "-e", f"{schema_gate_commit}^{{commit}}"],
        PRODUCT_HASH_INVALID,
        "schema Gate object is not a commit",
    )
    canonical_evidence = Path("artifacts/r1/lc4-async-truth")
    targets: list[tuple[Path, Path]] = [
        (schema, schema.relative_to(root)),
        (asset, asset.relative_to(root)),
        (evidence / _PROFILE_NAME, canonical_evidence / _PROFILE_NAME),
        (evidence / _MUTATION_NAME, canonical_evidence / _MUTATION_NAME),
        (evidence / _GATE_NAME, canonical_evidence / _GATE_NAME),
        (
            evidence / "reviewed-ground-truth.v1.json",
            canonical_evidence / "reviewed-ground-truth.v1.json",
        ),
    ]
    bindings = profile.get("product_bindings")
    if not isinstance(bindings, list):
        _fail(PRODUCT_HASH_INVALID, "commit audit cannot read product bindings")
    for binding in bindings:
        if not isinstance(binding, dict):
            _fail(PRODUCT_HASH_INVALID, "commit audit found malformed binding")
        for prefix in ("source", "compiler_audit", "trace"):
            relative = binding.get(f"{prefix}_file")
            local = _safe_product_path(evidence, relative, f"commit {prefix}")
            targets.append((local, canonical_evidence / str(relative)))
    seen: set[str] = set()
    for local, target in targets:
        target_text = target.as_posix()
        if target_text in seen:
            continue
        seen.add(target_text)
        committed_oid = _git(
            root,
            ["rev-parse", f"{schema_gate_commit}:{target_text}"],
            PRODUCT_HASH_INVALID,
            f"schema Gate commit lacks {target_text}",
        )
        working_oid = _git(
            root,
            ["hash-object", f"--path={target_text}", str(local)],
            PRODUCT_HASH_INVALID,
            f"cannot hash {target_text} with Git attributes",
        )
        if committed_oid != working_oid:
            _fail(
                PRODUCT_HASH_INVALID,
                f"schema Gate commit bytes differ for {target_text}",
            )


def _audit_schema_gate(
    evidence: Path,
    gate: Mapping[str, Any],
    profile_path: Path,
    mutation_path: Path,
) -> None:
    if (
        gate.get("format") != "blueprint-lens-lc4-async-schema-gate"
        or gate.get("schema_version") != "1.0.0"
        or gate.get("status")
        != "SCHEMA_VALIDATOR_MUTATIONS_VERIFIED__READINESS_DECISION_NEXT"
        or gate.get("profile_id") != PROFILE_ID
        or gate.get("rules_version") != RULES_VERSION
        or gate.get("next_gate") != "readiness/TRUTH_FROZEN decision"
    ):
        _fail(SCHEMA_GATE_INVALID, "schema Gate identity or status differs")
    checks = gate.get("checks")
    if not isinstance(checks, dict) or not checks or not all(value is True for value in checks.values()):
        _fail(SCHEMA_GATE_INVALID, "schema Gate contains a failed or malformed check")
    hashes = gate.get("hashes")
    if not isinstance(hashes, dict):
        _fail(SCHEMA_GATE_INVALID, "schema Gate hashes are absent")
    if hashes.get("async_profile_sha256") != _sha256(profile_path):
        _fail(PRODUCT_HASH_INVALID, "schema Gate profile hash differs")
    if hashes.get("mutation_report_sha256") != _sha256(mutation_path):
        _fail(PRODUCT_HASH_INVALID, "schema Gate mutation hash differs")
    reviewed = evidence / "reviewed-ground-truth.v1.json"
    if hashes.get("reviewed_ground_truth_sha256") != _sha256(reviewed):
        _fail(PRODUCT_HASH_INVALID, "reviewed ground-truth hash differs")


def _audit_mutations(mutations: Mapping[str, Any]) -> None:
    cases = mutations.get("cases")
    if (
        mutations.get("format") != "blueprint-lens-lc4-async-mutations"
        or mutations.get("schema_version") != "1.0.0"
        or mutations.get("status") != "PASS"
        or mutations.get("case_count") != 24
        or not isinstance(cases, list)
        or len(cases) != 24
    ):
        _fail(MUTATION_EVIDENCE_INVALID, "mutation report header or count differs")
    names = [case.get("name") for case in cases if isinstance(case, dict)]
    if len(names) != 24 or set(names) != _EXPECTED_MUTATIONS or len(names) != len(set(names)):
        _fail(MUTATION_EVIDENCE_INVALID, "mutation case coverage differs")
    for case in cases:
        if (
            case.get("passed") is not True
            or not isinstance(case.get("expected_diagnostic"), str)
            or not case["expected_diagnostic"].startswith("LC4_ASYNC_")
        ):
            _fail(MUTATION_EVIDENCE_INVALID, f"mutation did not fail closed: {case!r}")


def _audit_relation_claims(profile: Mapping[str, Any]) -> None:
    invocations = profile.get("invocations")
    if not isinstance(invocations, list) or len(invocations) != 4:
        _fail(READINESS_SEMANTIC_INVALID, "exactly four invocations are required")
    for invocation in invocations:
        relations = invocation.get("relations") if isinstance(invocation, dict) else None
        checks = invocation.get("incomparability_checks") if isinstance(invocation, dict) else None
        if not isinstance(relations, list) or len(relations) != 11:
            _fail(READINESS_SEMANTIC_INVALID, "invocation relation coverage differs")
        completion_ids = set(invocation.get("completion_event_ids", []))
        for relation in relations:
            relation_type = relation.get("relation_type")
            scope = relation.get("claim_scope")
            if relation_type in _SOURCE_SCOPED_RELATIONS and scope != "source_guaranteed":
                _fail(READINESS_SEMANTIC_INVALID, f"source relation scope differs: {relation_type}")
            if relation_type in _OBSERVED_RELATIONS and scope != "observed_invocation":
                _fail(READINESS_SEMANTIC_INVALID, f"observed relation was promoted: {relation_type}")
            if relation.get("from_id") in completion_ids and relation.get("to_id") in completion_ids:
                _fail(READINESS_SEMANTIC_INVALID, "completion order was promoted to causality")
            evidence_refs = relation.get("evidence_refs")
            if not isinstance(evidence_refs, list) or not evidence_refs:
                _fail(READINESS_SEMANTIC_INVALID, "relation evidence is empty")
        if not isinstance(checks, list) or len(checks) != 1:
            _fail(READINESS_SEMANTIC_INVALID, "one incomparability check is required")
        check = checks[0]
        if (
            check.get("proof_basis") != "pairwise_reachability_plus_completeness"
            or check.get("left_reaches_right") is not False
            or check.get("right_reaches_left") is not False
            or check.get("relation_set_complete") is not True
            or check.get("result") != "incomparable"
            or check.get("evidence_relation_ids")
            != [relation.get("relation_id") for relation in relations]
        ):
            _fail(READINESS_SEMANTIC_INVALID, "incomparability is not fully proven")


def audit_async_readiness(
    evidence_dir: str | Path,
    asset_path: str | Path,
    schema_path: str | Path,
    schema_gate_commit: str,
) -> dict[str, Any]:
    """Re-open every frozen input and independently decide bounded readiness."""

    evidence = Path(evidence_dir).resolve()
    asset = Path(asset_path).resolve()
    schema = Path(schema_path).resolve()
    if not isinstance(schema_gate_commit, str) or len(schema_gate_commit) != 40:
        _fail(SCOPE_INVALID, "schema Gate commit must be a full Git object id")
    profile_path = evidence / _PROFILE_NAME
    mutation_path = evidence / _MUTATION_NAME
    gate_path = evidence / _GATE_NAME
    profile = _load_object(profile_path, PRODUCT_HASH_INVALID)
    mutations = _load_object(mutation_path, MUTATION_EVIDENCE_INVALID)
    gate = _load_object(gate_path, SCHEMA_GATE_INVALID)

    _audit_schema_gate(evidence, gate, profile_path, mutation_path)
    product_hashes = _audit_product_bindings(evidence, profile)
    if gate.get("hashes", {}).get("asset_sha256_before") != _sha256(asset):
        _fail(PRODUCT_HASH_INVALID, "asset hash differs from schema Gate")
    if gate.get("hashes", {}).get("asset_sha256_after") != _sha256(asset):
        _fail(PRODUCT_HASH_INVALID, "asset changed across schema Gate")
    try:
        validate_json_file(profile_path, schema)
    except SchemaValidationError as error:
        _fail(READINESS_SEMANTIC_INVALID, f"profile schema validation failed: {error}")
    _audit_relation_claims(profile)
    _audit_mutations(mutations)

    try:
        products = load_async_products(evidence, asset)
        rebuilt = build_async_profile(products)
        validate_async_profile(profile, products)
    except (LC4AsyncError, ValueError) as error:
        _fail(READINESS_SEMANTIC_INVALID, f"independent semantic rebuild failed: {error}")
    if _canonical_bytes(rebuilt) != profile_path.read_bytes():
        _fail(READINESS_SEMANTIC_INVALID, "profile differs from a fresh semantic rebuild")
    _audit_commit_tree(evidence, asset, schema, profile, schema_gate_commit)

    counts = profile.get("counts", {})
    if (
        counts.get("invocation_count") != 4
        or counts.get("relation_count") != 44
        or counts.get("incomparability_check_count") != 4
        or mutations.get("case_count") != 24
    ):
        _fail(READINESS_SEMANTIC_INVALID, "readiness counts do not reconcile")
    checks = {
        "asset_hash_stable": True,
        "schema_gate_commit_bound": True,
        "schema_gate_commit_tree_exact": True,
        "schema_gate_checks_pass": True,
        "profile_hash_matches_gate": True,
        "mutation_hash_matches_gate": True,
        "reviewed_ground_truth_hash_matches_gate": True,
        "all_product_bindings_byte_exact": True,
        "profile_schema_valid": True,
        "profile_semantic_rebuild_exact": True,
        "relation_claim_scopes_valid": True,
        "no_completion_order_promoted_to_causality": True,
        "incomparability_requires_pairwise_reachability_and_completeness": True,
        "all_24_mutations_rejected": True,
        "frozen_scope_is_bounded": True,
        "core_v1_frontier_preserved": True,
    }
    return {
        "format": "blueprint-lens-lc4-async-readiness",
        "schema_version": "1.0.0",
        "status": "TRUTH_FROZEN",
        "scope": PROFILE_ID,
        "rules_version": RULES_VERSION,
        "schema_gate_commit": schema_gate_commit,
        "core_v1_outcome": "DEFERRED__CORE_V1_FRONTIER_ONLY",
        "checks": checks,
        "counts": {
            "source_product_count": counts.get("source_product_count"),
            "compiler_audit_count": counts.get("compiler_audit_count"),
            "invocation_count": counts.get("invocation_count"),
            "relation_count": counts.get("relation_count"),
            "incomparability_check_count": counts.get("incomparability_check_count"),
            "mutation_case_count": mutations.get("case_count"),
        },
        "hashes": {
            "asset_sha256": _sha256(asset),
            "schema_sha256": _sha256(schema),
            "schema_gate_sha256": _sha256(gate_path),
            "async_profile_sha256": _sha256(profile_path),
            "mutation_report_sha256": _sha256(mutation_path),
            "reviewed_ground_truth_sha256": _sha256(
                evidence / "reviewed-ground-truth.v1.json"
            ),
            "product_sha256": product_hashes,
        },
        "frozen_claims": [
            "Two source-ordered Delay launch sites A then B are bound to two typed continuations.",
            "Each retained complete invocation has two completions, two participant arrivals, one barrier release and one criterion occurrence.",
            "A_FIRST and B_FIRST each have two identity-distinct retained invocations.",
            "Completion A and completion B are incomparable only within each complete retained invocation relation set.",
            "The project-owned barrier requires declared participants A and B, releases once after both arrivals and has explicit reset/cancel policy.",
        ],
        "boundaries": deepcopy(profile.get("boundaries", [])),
        "not_authorized": [
            "Generalization beyond the bounded two-Delay BP_LC4_AsyncBarrier profile",
            "Changing frozen core-v1 or removing LC4_ASYNC_FRONTIER",
            "Treating observation index, world tick, wall-clock time or spatial position as causal proof",
            "UE-visible surface implementation before the separate information-matched ledger/effect-image Gate",
            "Human comprehension, preference, general scalability or product-default claims",
            "LC5",
        ],
        "next_gate": "information-matched complete-text/register/Barrier-Lanes ledger and effect images",
    }


def freeze_async_readiness(
    evidence_dir: str | Path,
    asset_path: str | Path,
    schema_path: str | Path,
    schema_gate_commit: str,
) -> Path:
    """Publish readiness.json atomically after the independent audit succeeds."""

    evidence = Path(evidence_dir).resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    destination = evidence / _READINESS_NAME
    destination.unlink(missing_ok=True)
    decision = audit_async_readiness(evidence, asset_path, schema_path, schema_gate_commit)
    with tempfile.NamedTemporaryFile(
        prefix=".lc4-async-readiness-",
        suffix=".tmp",
        dir=evidence,
        delete=False,
    ) as handle:
        temporary = Path(handle.name)
        handle.write(_canonical_bytes(decision))
    try:
        if _load_object(temporary, READINESS_SEMANTIC_INVALID).get("status") != "TRUTH_FROZEN":
            _fail(READINESS_SEMANTIC_INVALID, "temporary readiness decision is invalid")
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)
    return destination
