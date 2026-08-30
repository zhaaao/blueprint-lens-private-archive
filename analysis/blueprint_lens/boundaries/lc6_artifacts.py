"""Load real LC6 evidence and build pre-schema review artifacts."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass, replace
from hashlib import sha256
import json
from pathlib import Path
import re
from tempfile import TemporaryDirectory
from typing import Any, Mapping

from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1
from ..schema_validation import SchemaValidationError, validate_instance
from ..typed_ir import build_typed_ir
from .lc6_profiles import (
    LC6Profiles,
    build_lc6_profiles,
    canonical_profile_bytes,
)


RAW_NAME = "BP_LC6_BoundaryMatrix.raw-0.2.json"
SOURCE_NAME = "BP_LC6_BoundaryMatrix.boundary-source.json"
AUDIT_NAME = "BP_LC6_BoundaryMatrix.boundary-audit.tsv"
CORE_NAME = "BP_LC6_BoundaryMatrix.core-boundary-matrix.v1.json"
BUDGET_NAME = "BP_LC6_BoundaryMatrix.upstream-budget.v1.json"
CANDIDATE_NAME = "ground-truth.candidate.v1.json"
REVIEWED_NAME = "reviewed-ground-truth.v1.json"
MUTATION_NAME = "mutation-report.json"
GATE_NAME = "schema-gate.json"
FOCUSED_LOG_NAME = "ue-lc6-focused.log"
FULL_LOG_NAME = "ue-blueprintlens-full.log"
CORE_SCHEMA_RELATIVE = "schemas/extensions/blueprint-lens-boundary-matrix-v1.schema.json"
BUDGET_SCHEMA_RELATIVE = "schemas/extensions/blueprint-lens-upstream-budget-v1.schema.json"

EXPECTED_MUTATIONS = {
    "opaque_status_swapped": "LC6_CORE_CLASSIFICATION_MISMATCH",
    "opaque_reason_forged": "LC6_CORE_CLASSIFICATION_MISMATCH",
    "uncertain_status_swapped": "LC6_CORE_CLASSIFICATION_MISMATCH",
    "uncertain_reason_forged": "LC6_CORE_CLASSIFICATION_MISMATCH",
    "unsupported_status_swapped": "LC6_CORE_CLASSIFICATION_MISMATCH",
    "unsupported_reason_forged": "LC6_CORE_CLASSIFICATION_MISMATCH",
    "cross_component_edge": "LC6_COMPONENT_ISOLATION_INVALID",
    "missing_root": "LC6_FIXTURE_SHAPE_INVALID",
    "duplicate_criterion": "LC6_FIXTURE_SHAPE_INVALID",
    "duplicate_source_identity": "LC6_FIXTURE_SHAPE_INVALID",
    "non_supported_truncation_member": "LC6_QUERY_CONTRACT_INVALID",
    "budget_two": "LC6_QUERY_CONTRACT_INVALID",
    "budget_four": "LC6_QUERY_CONTRACT_INVALID",
    "criterion_shifted": "LC6_QUERY_CONTRACT_INVALID",
    "frontier_endpoint_forged": "LC6_BUDGET_TRUTH_MISMATCH",
    "omitted_node_count_forged": "LC6_BUDGET_TRUTH_MISMATCH",
    "omitted_edge_count_forged": "LC6_BUDGET_TRUTH_MISMATCH",
    "node_level_truncated": "LC6_QUERY_CONTRACT_INVALID",
    "complete_baseline_missing": "LC6_QUERY_CONTRACT_INVALID",
    "schema_gate_commit_wrong": "LC6_READINESS_INPUT_INVALID",
    "asset_hash_changed": "LC6_READINESS_INPUT_INVALID",
    "raw_hash_changed": "LC6_READINESS_INPUT_INVALID",
    "audit_hash_changed": "LC6_READINESS_INPUT_INVALID",
    "schema_hash_changed": "LC6_READINESS_INPUT_INVALID",
    "required_evidence_missing": "LC6_READINESS_INPUT_INVALID",
}

REVIEW_BASIS = (
    "asset_shape_and_four_disconnected_scenarios",
    "four_unique_roots_and_criteria",
    "core_status_ownership_and_exact_reasons",
    "exact_core_slice_membership",
    "budget_three_hops_frontier_and_omissions",
    "run1_run2_raw_source_audit_byte_agreement",
)


class LC6ArtifactError(ValueError):
    """Raised when real LC6 evidence cannot support a review artifact."""


@dataclass(frozen=True, slots=True)
class LC6EvidenceBundle:
    evidence_dir: Path
    raw: Mapping[str, Any]
    source: Mapping[str, Any]
    audit_text: str
    document: BlueprintDocument
    profiles: LC6Profiles
    native_hashes: Mapping[str, str]


def canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def _sha256_bytes(value: bytes) -> str:
    return sha256(value).hexdigest()


def _read_json(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC6ArtifactError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, Mapping):
        raise LC6ArtifactError(f"JSON root is not an object: {path}")
    return value


def _identical_run_bytes(evidence_dir: Path, name: str) -> bytes:
    run1 = evidence_dir / "run1" / name
    run2 = evidence_dir / "run2" / name
    try:
        first = run1.read_bytes()
        second = run2.read_bytes()
    except OSError as error:
        raise LC6ArtifactError(f"required native product is unavailable: {error}") from error
    if not first or first != second:
        raise LC6ArtifactError(f"run1/run2 native bytes differ: {name}")
    return first


def _typed_document(raw: Mapping[str, Any]) -> BlueprintDocument:
    typed_ir = build_typed_ir(raw)
    with TemporaryDirectory(prefix="blueprint-lens-lc6-typed-") as directory:
        path = Path(directory) / "typed-ir.json"
        path.write_bytes(canonical_json_bytes(typed_ir))
        return load_blueprint_lens_v1(path)


def load_lc6_evidence(evidence_dir: Path) -> LC6EvidenceBundle:
    """Load byte-identical native runs and rebuild both profiles from raw facts."""

    evidence_dir = evidence_dir.resolve()
    raw_bytes = _identical_run_bytes(evidence_dir, RAW_NAME)
    source_bytes = _identical_run_bytes(evidence_dir, SOURCE_NAME)
    audit_bytes = _identical_run_bytes(evidence_dir, AUDIT_NAME)
    try:
        raw = json.loads(raw_bytes.decode("utf-8"))
        source = json.loads(source_bytes.decode("utf-8"))
        audit_text = audit_bytes.decode("utf-8")
    except (UnicodeError, json.JSONDecodeError) as error:
        raise LC6ArtifactError(f"native evidence cannot be decoded: {error}") from error
    if not isinstance(raw, Mapping) or not isinstance(source, Mapping):
        raise LC6ArtifactError("native JSON roots must be objects")
    if source.get("raw_sha256") != _sha256_bytes(raw_bytes):
        raise LC6ArtifactError("source raw hash does not bind retained raw bytes")
    document = _typed_document(raw)
    profiles = build_lc6_profiles(document, source, audit_text)
    return LC6EvidenceBundle(
        evidence_dir=evidence_dir,
        raw=raw,
        source=source,
        audit_text=audit_text,
        document=document,
        profiles=profiles,
        native_hashes={
            RAW_NAME: _sha256_bytes(raw_bytes),
            SOURCE_NAME: _sha256_bytes(source_bytes),
            AUDIT_NAME: _sha256_bytes(audit_bytes),
        },
    )


def build_review_candidate(bundle: LC6EvidenceBundle) -> Mapping[str, Any]:
    """Build a candidate from native evidence and freshly derived profiles."""

    core_bytes = canonical_profile_bytes(bundle.profiles.core_boundary_matrix)
    budget_bytes = canonical_profile_bytes(bundle.profiles.upstream_budget)
    source_scenarios = bundle.source["scenarios"]
    assert isinstance(source_scenarios, list)
    return {
        "format": "blueprint-lens-lc6-reviewed-ground-truth",
        "format_version": "1.0.0",
        "scope": "LC6-F1",
        "review_status": "candidate",
        "reviewer_id": "",
        "review_basis": [],
        "binding": {
            "blueprint_asset_path": bundle.source["blueprint_asset_path"],
            "graph_id": bundle.source["graph_id"],
            "asset_sha256": bundle.source["asset_sha256"],
            "raw_sha256": bundle.source["raw_sha256"],
        },
        "native_run_agreement": {
            "runs": ["run1", "run2"],
            "byte_identical": True,
            "hashes": dict(sorted(bundle.native_hashes.items())),
        },
        "scenario_anchors": [
            {
                "scenario_id": scenario["scenario_id"],
                "root_node_id": scenario["root_node_id"],
                "criterion_node_id": scenario["criterion_node_id"],
                "node_count": len(scenario["nodes"]),
                "edge_count": len(scenario["edges"]),
            }
            for scenario in sorted(source_scenarios, key=lambda item: item["scenario_id"])
        ],
        "core_expectations": [
            {
                "scenario_id": scenario["scenario_id"],
                "boundary_node_id": scenario["boundary_node_id"],
                "status": scenario["status"],
                "reason": scenario["reason"],
                "slice_node_ids": scenario["slice_node_ids"],
                "slice_edge_ids": scenario["slice_edge_ids"],
            }
            for scenario in bundle.profiles.core_boundary_matrix["scenarios"]
        ],
        "budget_expectation": {
            key: bundle.profiles.upstream_budget[key]
            for key in (
                "criterion_node_id",
                "max_upstream_hops",
                "status",
                "reason",
                "complete_node_ids",
                "complete_edge_ids",
                "selected_node_ids",
                "selected_edge_ids",
                "hop_distances",
                "frontiers",
                "counts",
            )
        },
        "product_hashes": {
            CORE_NAME: _sha256_bytes(core_bytes),
            BUDGET_NAME: _sha256_bytes(budget_bytes),
        },
    }


def promote_review_candidate(
    bundle: LC6EvidenceBundle,
    candidate: Mapping[str, Any],
    reviewer_id: str,
) -> Mapping[str, Any]:
    """Promote only a byte-current candidate and change review metadata alone."""

    if not reviewer_id.strip():
        raise LC6ArtifactError("reviewer_id must be non-empty")
    rebuilt = build_review_candidate(bundle)
    if canonical_json_bytes(candidate) != canonical_json_bytes(rebuilt):
        raise LC6ArtifactError("candidate differs from a fresh native-evidence rebuild")
    reviewed = dict(rebuilt)
    reviewed["review_status"] = "reviewed"
    reviewed["reviewer_id"] = reviewer_id.strip()
    reviewed["review_basis"] = list(REVIEW_BASIS)
    return reviewed


def require_reviewed_truth(
    bundle: LC6EvidenceBundle, reviewed: Mapping[str, Any]
) -> None:
    """Require review metadata plus exact agreement with a fresh candidate rebuild."""

    if (
        reviewed.get("review_status") != "reviewed"
        or not reviewed.get("reviewer_id")
        or reviewed.get("review_basis") != list(REVIEW_BASIS)
    ):
        raise LC6ArtifactError("reviewed ground truth has incomplete review authority")
    normalized = dict(reviewed)
    normalized["review_status"] = "candidate"
    normalized["reviewer_id"] = ""
    normalized["review_basis"] = []
    if canonical_json_bytes(normalized) != canonical_json_bytes(build_review_candidate(bundle)):
        raise LC6ArtifactError("reviewed ground truth differs from fresh derived truth")


def frozen_product_bytes(bundle: LC6EvidenceBundle) -> Mapping[str, bytes]:
    return {
        CORE_NAME: canonical_profile_bytes(bundle.profiles.core_boundary_matrix),
        BUDGET_NAME: canonical_profile_bytes(bundle.profiles.upstream_budget),
    }


def _semantic_profile_validation(profiles: LC6Profiles) -> None:
    from .lc6_profiles import (
        BUDGET_TRUTH_MISMATCH,
        QUERY_CONTRACT_INVALID,
        LC6ProfileError,
        validate_lc6_profiles,
    )

    budget = profiles.upstream_budget
    if (
        not isinstance(budget.get("complete_node_ids"), list)
        or not isinstance(budget.get("complete_edge_ids"), list)
        or len(budget["complete_node_ids"]) != 7
        or len(budget["complete_edge_ids"]) != 6
    ):
        raise LC6ProfileError(
            QUERY_CONTRACT_INVALID, "complete 7-node/6-edge baseline is missing"
        )
    validate_lc6_profiles(profiles)
    complete_nodes = set(budget["complete_node_ids"])
    selected_nodes = set(budget["selected_node_ids"])
    complete_edges = set(budget["complete_edge_ids"])
    selected_edges = set(budget["selected_edge_ids"])
    counts = budget["counts"]
    if not selected_nodes <= complete_nodes or not selected_edges <= complete_edges:
        raise LC6ProfileError(
            QUERY_CONTRACT_INVALID, "selected query is outside complete baseline"
        )
    if (
        counts["omitted_nodes"] != len(complete_nodes - selected_nodes)
        or counts["omitted_edges"] != len(complete_edges - selected_edges)
    ):
        raise LC6ProfileError(
            BUDGET_TRUTH_MISMATCH, "omitted counts differ from product sets"
        )
    frontiers = budget["frontiers"]
    if any(
        frontier["source_node_id"] in selected_nodes
        or frontier["source_node_id"] not in complete_nodes
        or frontier["target_node_id"] not in selected_nodes
        or frontier["edge_id"] not in complete_edges
        or frontier["edge_id"] in selected_edges
        for frontier in frontiers
    ):
        raise LC6ProfileError(
            BUDGET_TRUTH_MISMATCH, "Frontier does not cross omitted to selected"
        )
    distances = budget["hop_distances"]
    if (
        len(distances) != len(complete_nodes)
        or {item["node_id"] for item in distances} != complete_nodes
        or not any(
            item["node_id"] == budget["criterion_node_id"]
            and item["distance"] == 0
            for item in distances
        )
    ):
        raise LC6ProfileError(
            QUERY_CONTRACT_INVALID, "complete hop baseline is incomplete"
        )


def validate_lc6_schema_products(
    profiles: LC6Profiles,
    core_schema: Mapping[str, Any],
    budget_schema: Mapping[str, Any],
) -> None:
    """Require both strict schemas and the separate semantic contract."""

    validate_instance(profiles.core_boundary_matrix, core_schema)
    validate_instance(profiles.upstream_budget, budget_schema)
    _semantic_profile_validation(profiles)


def _mutated_document_status(
    bundle: LC6EvidenceBundle, suffix: str, status: str, reason: str
) -> BlueprintDocument:
    graph = bundle.document.graphs[0]
    changed = replace(
        graph,
        nodes=tuple(
            replace(node, semantic_status=status, semantic_reason=reason)
            if node.id.endswith(suffix)
            else node
            for node in graph.nodes
        ),
    )
    return replace(bundle.document, graphs=(changed,))


def _build_mutated_source(
    bundle: LC6EvidenceBundle,
    source: Mapping[str, Any],
    *,
    document: BlueprintDocument | None = None,
    budget: int = 3,
) -> None:
    build_lc6_profiles(
        document or bundle.document,
        source,
        bundle.audit_text,
        max_upstream_hops=budget,
    )


def _readiness_bundle_attack(bundle: LC6EvidenceBundle, name: str) -> None:
    from .lc6_readiness import exercise_named_readiness_attack

    exercise_named_readiness_attack(bundle, name)


def _execute_mutation(bundle: LC6EvidenceBundle, name: str) -> None:
    source = deepcopy(bundle.source)
    if name == "opaque_status_swapped":
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                "::node::a57de288-4b50-da44-886d-f18f1a5b7ca1",
                "uncertain",
                "node_family_not_in_supported_matrix_v1",
            ),
        )
    elif name == "opaque_reason_forged":
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                "::node::a57de288-4b50-da44-886d-f18f1a5b7ca1",
                "opaque",
                "forged_reason",
            ),
        )
    elif name == "uncertain_status_swapped":
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                "::node::c1a6b8ce-4841-5381-abdc-1d90061375cf",
                "opaque",
                "function_body_not_expanded",
            ),
        )
    elif name == "uncertain_reason_forged":
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                "::node::c1a6b8ce-4841-5381-abdc-1d90061375cf",
                "uncertain",
                "forged_reason",
            ),
        )
    elif name == "unsupported_status_swapped":
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                "::node::0cfac241-446f-516a-841c-e3b64f7e7346",
                "opaque",
                "function_body_not_expanded",
            ),
        )
    elif name == "unsupported_reason_forged":
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                "::node::0cfac241-446f-516a-841c-e3b64f7e7346",
                "unsupported",
                "forged_reason",
            ),
        )
    elif name == "cross_component_edge":
        source["scenarios"][1]["edges"].append(source["scenarios"][0]["edges"][0])
        _build_mutated_source(bundle, source)
    elif name == "missing_root":
        source["scenarios"][0]["root_node_id"] = "missing"
        _build_mutated_source(bundle, source)
    elif name == "duplicate_criterion":
        source["scenarios"][1]["criterion_node_id"] = source["scenarios"][0][
            "criterion_node_id"
        ]
        _build_mutated_source(bundle, source)
    elif name == "duplicate_source_identity":
        source["scenarios"][0]["nodes"].append(source["scenarios"][0]["nodes"][0])
        _build_mutated_source(bundle, source)
    elif name == "non_supported_truncation_member":
        truncated = source["scenarios"][1]
        node_id = truncated["nodes"][0]["id"]
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle, node_id.removeprefix(bundle.document.graphs[0].id), "uncertain", "forged"
            ),
        )
    elif name in {"budget_two", "budget_four"}:
        _build_mutated_source(bundle, source, budget=2 if name == "budget_two" else 4)
    elif name == "criterion_shifted":
        truncated = source["scenarios"][1]
        truncated["criterion_node_id"] = next(
            node["id"]
            for node in truncated["nodes"]
            if node.get("member", {}).get("name") == "LC6Truncated05"
        )
        _build_mutated_source(bundle, source)
    elif name in {
        "frontier_endpoint_forged",
        "omitted_node_count_forged",
        "omitted_edge_count_forged",
        "complete_baseline_missing",
    }:
        budget = deepcopy(bundle.profiles.upstream_budget)
        if name == "frontier_endpoint_forged":
            budget["frontiers"][0]["source_node_id"] = budget["selected_node_ids"][0]
        elif name == "omitted_node_count_forged":
            budget["counts"]["omitted_nodes"] += 1
        elif name == "omitted_edge_count_forged":
            budget["counts"]["omitted_edges"] += 1
        else:
            budget["complete_node_ids"].pop()
        _semantic_profile_validation(
            LC6Profiles(bundle.profiles.core_boundary_matrix, budget)
        )
    elif name == "node_level_truncated":
        truncated = source["scenarios"][1]
        node_id = truncated["nodes"][0]["id"]
        _build_mutated_source(
            bundle,
            source,
            document=_mutated_document_status(
                bundle,
                node_id.removeprefix(bundle.document.graphs[0].id),
                "truncated",
                "max_upstream_hops_exhausted",
            ),
        )
    else:
        _readiness_bundle_attack(bundle, name)


def _diagnostic_code(error: Exception) -> str:
    code = getattr(error, "code", "")
    if isinstance(code, str) and code:
        return code
    text = str(error)
    return text.split(":", 1)[0]


def _shuffled_determinism(bundle: LC6EvidenceBundle) -> bool:
    source = deepcopy(bundle.source)
    source["scenarios"].reverse()
    for scenario in source["scenarios"]:
        scenario["nodes"].reverse()
        scenario["edges"].reverse()
        for node in scenario["nodes"]:
            node["pins"].reverse()
    graph = bundle.document.graphs[0]
    shuffled_graph = replace(
        graph,
        nodes=tuple(reversed(graph.nodes)),
        edges=tuple(reversed(graph.edges)),
    )
    shuffled_document = replace(bundle.document, graphs=(shuffled_graph,))
    shuffled = build_lc6_profiles(
        shuffled_document, source, bundle.audit_text, max_upstream_hops=3
    )
    return (
        canonical_profile_bytes(shuffled.core_boundary_matrix)
        == canonical_profile_bytes(bundle.profiles.core_boundary_matrix)
        and canonical_profile_bytes(shuffled.upstream_budget)
        == canonical_profile_bytes(bundle.profiles.upstream_budget)
    )


def run_lc6_mutations(bundle: LC6EvidenceBundle) -> Mapping[str, Any]:
    """Run the exact 25 negative cases through public validators."""

    cases: list[dict[str, Any]] = []
    for name, expected in sorted(EXPECTED_MUTATIONS.items()):
        observed = "ACCEPTED"
        try:
            _execute_mutation(bundle, name)
        except (ValueError, SchemaValidationError) as error:
            observed = _diagnostic_code(error)
        cases.append(
            {
                "name": name,
                "expected_code": expected,
                "observed_code": observed,
                "rejected": observed == expected,
            }
        )
    rejected = sum(case["rejected"] is True for case in cases)
    deterministic = _shuffled_determinism(bundle)
    return {
        "format": "blueprint-lens-lc6-mutations",
        "format_version": "1.0.0",
        "status": "PASS" if rejected == 25 and deterministic else "FAIL",
        "case_count": len(cases),
        "rejected_count": rejected,
        "shuffled_determinism": "pass" if deterministic else "fail",
        "cases": cases,
    }


def _project_root(bundle: LC6EvidenceBundle, root: Path | None) -> Path:
    if root is not None:
        return root.resolve()
    for candidate in (bundle.evidence_dir, *bundle.evidence_dir.parents):
        if (candidate / "schemas").is_dir() and (candidate / "unreal").is_dir():
            return candidate
    raise LC6ArtifactError("cannot resolve project root for schema Gate")


def build_lc6_schema_gate(
    bundle: LC6EvidenceBundle, *, root: Path | None = None
) -> Mapping[str, bytes]:
    """Build pre-readiness mutation and schema-Gate artifacts."""

    project_root = _project_root(bundle, root)
    core_schema_path = project_root / CORE_SCHEMA_RELATIVE
    budget_schema_path = project_root / BUDGET_SCHEMA_RELATIVE
    core_schema = _read_json(core_schema_path)
    budget_schema = _read_json(budget_schema_path)
    validate_lc6_schema_products(bundle.profiles, core_schema, budget_schema)
    reviewed = _read_json(bundle.evidence_dir / REVIEWED_NAME)
    require_reviewed_truth(bundle, reviewed)
    mutation_report = run_lc6_mutations(bundle)
    if mutation_report["status"] != "PASS":
        failed = [
            f"{case['name']}={case['observed_code']}"
            for case in mutation_report["cases"]
            if not case["rejected"]
        ]
        raise LC6ArtifactError(f"LC6 mutation matrix failed: {failed}")
    core_path = bundle.evidence_dir / CORE_NAME
    budget_path = bundle.evidence_dir / BUDGET_NAME
    asset_path = (
        project_root
        / "unreal/BlueprintLensProbe/Content/LensCorpus/BP_LC6_BoundaryMatrix.uasset"
    )
    gate = {
        "format": "blueprint-lens-lc6-schema-gate",
        "format_version": "1.0.0",
        "status": "PRE_READINESS",
        "scope": "LC6-F1",
        "profile_ids": [
            "LC6_CORE_BOUNDARY_MATRIX_V1",
            "LC6_MAX_UPSTREAM_HOPS_V1",
        ],
        "hashes": {
            "asset_sha256": _sha256_bytes(asset_path.read_bytes()),
            CORE_NAME: _sha256_bytes(core_path.read_bytes()),
            BUDGET_NAME: _sha256_bytes(budget_path.read_bytes()),
            REVIEWED_NAME: _sha256_bytes(
                (bundle.evidence_dir / REVIEWED_NAME).read_bytes()
            ),
            MUTATION_NAME: _sha256_bytes(canonical_json_bytes(mutation_report)),
            CORE_SCHEMA_RELATIVE: _sha256_bytes(core_schema_path.read_bytes()),
            BUDGET_SCHEMA_RELATIVE: _sha256_bytes(budget_schema_path.read_bytes()),
            **dict(sorted(bundle.native_hashes.items())),
        },
        "mutation_summary": {"cases": 25, "rejected": 25},
        "shuffled_determinism": "pass",
    }
    return {
        MUTATION_NAME: canonical_json_bytes(mutation_report),
        GATE_NAME: canonical_json_bytes(gate),
    }


def _ue_automation_result(path: Path, expected: int) -> Mapping[str, int]:
    try:
        text = path.read_text(encoding="utf-8", errors="strict")
    except (OSError, UnicodeError) as error:
        raise LC6ArtifactError(f"cannot read UE automation log {path}: {error}") from error
    discoveries = re.findall(r"Found (\d+) automation tests based on", text)
    results = re.findall(r"Test Completed\. Result=\{([^}]+)\}", text)
    exits = re.findall(r"\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*", text)
    if len(discoveries) != 1 or len(exits) != 1:
        raise LC6ArtifactError(f"UE automation log has ambiguous summary: {path.name}")
    discovered = int(discoveries[0])
    exit_code = int(exits[0])
    passed = sum(result == "Success" for result in results)
    failed = len(results) - passed
    if (
        discovered != expected
        or len(results) != expected
        or passed != expected
        or failed != 0
        or exit_code != 0
    ):
        raise LC6ArtifactError(
            f"UE automation result mismatch for {path.name}: "
            f"discovered={discovered} completed={len(results)} passed={passed} "
            f"failed={failed} exit={exit_code} expected={expected}"
        )
    return {
        "discovered": discovered,
        "completed": len(results),
        "passed": passed,
        "failed": failed,
        "exit_code": exit_code,
    }


def build_verified_lc6_schema_gate(
    bundle: LC6EvidenceBundle,
    *,
    root: Path | None = None,
    python_tests: int,
    python_subtests: int,
) -> Mapping[str, bytes]:
    """Bind observed Python totals and retained UE raw logs to the pre-readiness Gate."""

    if python_tests <= 0 or python_subtests < 0:
        raise LC6ArtifactError("Python verification counts are invalid")
    artifacts = dict(build_lc6_schema_gate(bundle, root=root))
    gate = json.loads(artifacts[GATE_NAME])
    focused_path = bundle.evidence_dir / FOCUSED_LOG_NAME
    full_path = bundle.evidence_dir / FULL_LOG_NAME
    gate["verification"] = {
        "python": {
            "tests_passed": python_tests,
            "subtests_passed": python_subtests,
        },
        "ue": {
            "focused": _ue_automation_result(focused_path, 2),
            "full": _ue_automation_result(full_path, 62),
        },
    }
    gate["hashes"][FOCUSED_LOG_NAME] = _sha256_bytes(focused_path.read_bytes())
    gate["hashes"][FULL_LOG_NAME] = _sha256_bytes(full_path.read_bytes())
    artifacts[GATE_NAME] = canonical_json_bytes(gate)
    return artifacts
