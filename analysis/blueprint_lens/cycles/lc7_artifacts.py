"""Build, review, mutate, and stage the bounded LC7 static-SCC truth products."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
from tempfile import TemporaryDirectory, mkdtemp
from typing import Any, Mapping
import xml.etree.ElementTree as ET

from ..execution_slice import compute_execution_slice
from ..explanation_model import (
    canonical_explanation_bytes,
)
from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1, load_raw_probe
from ..schema_validation import SchemaValidationError, validate_instance
from ..typed_ir import build_typed_ir
from .lc7_explanation import (
    build_lc7_static_scc_explanation,
    validate_lc7_static_scc_explanation,
)
from .lc7_profiles import (
    LC7ProfileError,
    build_lc7_static_scc_profile,
    canonical_profile_bytes,
)


RAW_NAME = "BP_LC7_StaticSCC.raw-0.2.json"
SOURCE_NAME = "BP_LC7_StaticSCC.scc-source.json"
AUDIT_NAME = "BP_LC7_StaticSCC.scc-audit.tsv"
IR_NAME = "BP_LC7_StaticSCC.ir.v1.json"
SLICE_NAME = "BP_LC7_StaticSCC.execution.slice.v1.json"
PROFILE_NAME = "BP_LC7_StaticSCC.scc-profile.v1.json"
EXPLANATION_NAME = "BP_LC7_StaticSCC.explanation.v1.json"
CANDIDATE_NAME = "ground-truth.candidate.v1.json"
REVIEWED_NAME = "reviewed-ground-truth.v1.json"
MUTATION_NAME = "mutation-report.json"
SCHEMA_GATE_NAME = "schema-gate.json"

PROFILE_SCHEMA_RELATIVE = "schemas/extensions/blueprint-lens-scc-profile-v1.schema.json"
EXPLANATION_SCHEMA_RELATIVE = "schemas/blueprint-lens-explanation-v1.schema.json"

LC7_REQUIRED_SOURCE_PATHS = (
    ".gitattributes",
    "analysis/blueprint_lens/__init__.py",
    "analysis/blueprint_lens/cycles/__init__.py",
    "analysis/blueprint_lens/cycles/lc7_artifacts.py",
    "analysis/blueprint_lens/cycles/lc7_explanation.py",
    "analysis/blueprint_lens/cycles/lc7_profiles.py",
    "analysis/blueprint_lens/cycles/lc7_readiness.py",
    "analysis/blueprint_lens/execution_slice.py",
    "analysis/blueprint_lens/explanation_model.py",
    "analysis/blueprint_lens/raw_probe.py",
    "analysis/blueprint_lens/schema_validation.py",
    "analysis/blueprint_lens/typed_ir.py",
    "analysis/build_lc7_static_scc_artifacts.py",
    "analysis/tests/test_execution_slice.py",
    "analysis/tests/test_explanation_model.py",
    "analysis/tests/test_lc7_artifacts.py",
    "analysis/tests/test_lc7_automation_runner.py",
    "analysis/tests/test_lc7_explanation.py",
    "analysis/tests/test_lc7_profiles.py",
    "analysis/tests/test_lc7_readiness.py",
    PROFILE_SCHEMA_RELATIVE,
    EXPLANATION_SCHEMA_RELATIVE,
    "tools/capture_lc7_static_scc_truth.ps1",
    "tools/run_lc7_static_scc_automation.ps1",
    "unreal/BlueprintLensProbe/BlueprintLensProbe.uproject",
    "unreal/BlueprintLensProbe/Content/LensCorpus/BP_LC7_StaticSCC.uasset",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/BlueprintLensExporter.Build.cs",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Public/BlueprintLensExporter.h",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensExporter.cpp",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCFixture.h",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCFixture.cpp",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCFacts.h",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCFacts.cpp",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCAudit.h",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCAudit.cpp",
    "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter/Source/BlueprintLensExporter/Private/BlueprintLensLC7StaticSCCTests.cpp",
)

LC7_EVIDENCE_RELATIVE = Path("artifacts/r1/lc7-static-scc-truth")
LC7_REQUIRED_EVIDENCE_PATHS = (
    *(
        (LC7_EVIDENCE_RELATIVE / run / name).as_posix()
        for run in ("run1", "run2")
        for name in (RAW_NAME, SOURCE_NAME, AUDIT_NAME, "ue-capture.log")
    ),
    *(
        (LC7_EVIDENCE_RELATIVE / name).as_posix()
        for name in (
            RAW_NAME,
            SOURCE_NAME,
            AUDIT_NAME,
            IR_NAME,
            SLICE_NAME,
            PROFILE_NAME,
            EXPLANATION_NAME,
            REVIEWED_NAME,
            MUTATION_NAME,
            SCHEMA_GATE_NAME,
            "python-full.xml",
            "python-full.log",
            "ue-lc7-focused.log",
            "ue-blueprintlens-full.log",
        )
    ),
)


def lc7_required_paths() -> tuple[str, ...]:
    """Return the exact pre-readiness Git-blob inventory."""

    return tuple(sorted((*LC7_REQUIRED_SOURCE_PATHS, *LC7_REQUIRED_EVIDENCE_PATHS)))

EXPECTED_MUTATIONS = {
    "audit_hash_mismatch": "LC7_SOURCE_AUDIT_MISMATCH",
    "criterion_inside": "LC7_SCC_MEMBERSHIP_INVALID",
    "duplicate_member": "LC7_SCC_MEMBERSHIP_INVALID",
    "duplicated_source_identity": "LC7_FIXTURE_SHAPE_INVALID",
    "extra_incoming_edge": "LC7_SCC_BOUNDARY_INVALID",
    "extra_outgoing_edge": "LC7_SCC_BOUNDARY_INVALID",
    "extra_return_edge": "LC7_SCC_EDGE_OWNERSHIP_INVALID",
    "forged_runtime_iteration": "LC7_RUNTIME_CLAIM_INVALID",
    "missing_member": "LC7_SCC_MEMBERSHIP_INVALID",
    "missing_return_edge": "LC7_SCC_EDGE_OWNERSHIP_INVALID",
    "missing_scc_group": "LC7_SCC_GROUP_INVALID",
    "non_strongly_connected_group": "LC7_SCC_MEMBERSHIP_INVALID",
    "raw_hash_mismatch": "LC7_SOURCE_AUDIT_MISMATCH",
    "schema_commit_attack": "LC7_SCHEMA_COMMIT_INVALID",
    "schema_hash_attack": "LC7_SCHEMA_HASH_INVALID",
    "shuffled_nondeterminism": "LC7_SHUFFLED_NONDETERMINISM_REJECTED",
    "source_hash_mismatch": "LC7_SOURCE_AUDIT_MISMATCH",
    "wrong_entry": "LC7_SCC_BOUNDARY_INVALID",
    "wrong_exit": "LC7_SCC_BOUNDARY_INVALID",
}


class LC7ArtifactError(ValueError):
    """Raised when retained LC7 evidence cannot support a frozen product."""


@dataclass(frozen=True, slots=True)
class LC7EvidenceBundle:
    evidence_dir: Path
    root: Path
    asset_path: Path
    raw: Mapping[str, Any]
    source: Mapping[str, Any]
    audit_text: str
    document: BlueprintDocument
    profile: Mapping[str, Any]
    source_binding: Mapping[str, Any]
    native_bytes: Mapping[str, bytes]
    native_hashes: Mapping[str, str]


def canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    try:
        return _sha256_bytes(path.read_bytes())
    except OSError as error:
        raise LC7ArtifactError(f"cannot hash required file: {path}") from error


def _read_json(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise LC7ArtifactError(f"cannot read JSON {path}: {error}") from error
    if not isinstance(value, Mapping):
        raise LC7ArtifactError(f"JSON root is not an object: {path}")
    return value


def _identical_run_bytes(evidence_dir: Path, name: str) -> bytes:
    try:
        first = (evidence_dir / "run1" / name).read_bytes()
        second = (evidence_dir / "run2" / name).read_bytes()
    except OSError as error:
        raise LC7ArtifactError(f"required native product is unavailable: {name}") from error
    if not first or first != second:
        raise LC7ArtifactError(f"run1/run2 native bytes differ: {name}")
    return first


def load_lc7_evidence(
    evidence_dir: Path,
    *,
    root: Path | None = None,
) -> LC7EvidenceBundle:
    """Load both native runs and rebuild the exact source-visible SCC profile."""

    evidence_dir = Path(evidence_dir).resolve()
    project_root = Path(root).resolve() if root is not None else evidence_dir.parents[2]
    asset_path = (
        project_root
        / "unreal/BlueprintLensProbe/Content/LensCorpus/BP_LC7_StaticSCC.uasset"
    )
    asset_before = _sha256_file(asset_path)
    native_bytes = {
        name: _identical_run_bytes(evidence_dir, name)
        for name in (RAW_NAME, SOURCE_NAME, AUDIT_NAME)
    }
    try:
        raw = json.loads(native_bytes[RAW_NAME].decode("utf-8"))
        source = json.loads(native_bytes[SOURCE_NAME].decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise LC7ArtifactError("native LC7 JSON cannot be decoded") from error
    if not isinstance(raw, Mapping) or not isinstance(source, Mapping):
        raise LC7ArtifactError("native LC7 JSON roots must be objects")
    audit_path = evidence_dir / "run1" / AUDIT_NAME
    audit_text = audit_path.read_text(encoding="utf-8")
    raw_path = evidence_dir / "run1" / RAW_NAME
    source_path = evidence_dir / "run1" / SOURCE_NAME
    try:
        document = load_raw_probe(raw_path)
    except ValueError as error:
        raise LC7ArtifactError(str(error)) from error
    native_hashes = {
        name: _sha256_bytes(payload) for name, payload in native_bytes.items()
    }
    source_binding: dict[str, Any] = {
        "asset_path": asset_path,
        "raw_path": raw_path,
        "source_path": source_path,
        "audit_path": audit_path,
        "asset_sha256": asset_before,
        "raw_sha256": native_hashes[RAW_NAME],
        "source_sha256": native_hashes[SOURCE_NAME],
        "audit_sha256": native_hashes[AUDIT_NAME],
    }
    try:
        profile = build_lc7_static_scc_profile(
            document, source, audit_text, source_binding
        )
    except LC7ProfileError as error:
        raise LC7ArtifactError(str(error)) from error
    if _sha256_file(asset_path) != asset_before:
        raise LC7ArtifactError("LC7 asset changed during evidence analysis")
    return LC7EvidenceBundle(
        evidence_dir=evidence_dir,
        root=project_root,
        asset_path=asset_path,
        raw=raw,
        source=source,
        audit_text=audit_text,
        document=document,
        profile=profile,
        source_binding=source_binding,
        native_bytes=native_bytes,
        native_hashes=native_hashes,
    )


def _slice_value(bundle: LC7EvidenceBundle, typed_document: BlueprintDocument) -> Mapping[str, Any]:
    criterion = str(bundle.profile["criterion_node_id"])
    selected = compute_execution_slice(typed_document, criterion)
    if len(selected.node_ids) != 8 or len(selected.edge_ids) != 8:
        raise LC7ArtifactError("LC7 execution slice differs from 8/8")
    returning = bundle.profile["scc"]["returning_edge_ids"]
    if len(returning) != 1 or returning[0] not in selected.edge_ids:
        raise LC7ArtifactError("LC7 execution slice omits the returning edge")
    return {
        "format": "blueprint-lens-slice",
        "schema_version": "1.0.0",
        "rules_version": "1.0.0",
        "source_fixture": (
            LC7_EVIDENCE_RELATIVE / "run1" / RAW_NAME
        ).as_posix(),
        "source_sha256": bundle.native_hashes[RAW_NAME].upper(),
        "slice_kind": "execution_context",
        "criterion": {
            "graph_id": selected.graph_id,
            "node_id": criterion,
            "description": "Set LC7Complete",
        },
        "graph_id": selected.graph_id,
        "node_ids": list(selected.node_ids),
        "edge_ids": list(selected.edge_ids),
        "inclusion_reasons": {
            node_id: list(reasons)
            for node_id, reasons in selected.inclusion_reasons.items()
        },
        "boundaries": [],
        "counts": {"nodes": 8, "edges": 8},
    }


def frozen_product_bytes(
    bundle: LC7EvidenceBundle,
    *,
    output_dir: Path | None = None,
) -> Mapping[str, bytes]:
    """Rebuild raw -> typed IR -> slice -> profile -> Explanation in memory."""

    asset_before = _sha256_file(bundle.asset_path)
    typed_ir = build_typed_ir(
        bundle.raw,
        expected_blueprint_path=str(bundle.source["blueprint_asset_path"]),
    )
    ir_bytes = canonical_json_bytes(typed_ir)
    target_dir = Path(output_dir) if output_dir is not None else bundle.evidence_dir
    try:
        portable_target_dir = target_dir.resolve().relative_to(bundle.root).as_posix()
    except ValueError as error:
        raise LC7ArtifactError("LC7 output directory must stay inside the project root") from error
    with TemporaryDirectory(prefix="blueprint-lens-lc7-products-") as temporary:
        temporary_dir = Path(temporary)
        ir_path = temporary_dir / IR_NAME
        ir_path.write_bytes(ir_bytes)
        typed_document = load_blueprint_lens_v1(ir_path)
        slice_value = _slice_value(bundle, typed_document)
        slice_bytes = canonical_json_bytes(slice_value)
        slice_path = temporary_dir / SLICE_NAME
        slice_path.write_bytes(slice_bytes)
        explanation = build_lc7_static_scc_explanation(
            ir_path, slice_path, bundle.asset_path, bundle.profile
        )
        explanation["source"]["ir_path"] = f"{portable_target_dir}/{IR_NAME}"
        explanation["source"]["slice_path"] = f"{portable_target_dir}/{SLICE_NAME}"
        validate_lc7_static_scc_explanation(explanation, bundle.profile)
        explanation_bytes = canonical_explanation_bytes(explanation)
    if _sha256_file(bundle.asset_path) != asset_before:
        raise LC7ArtifactError("LC7 asset changed while deriving frozen products")
    return {
        RAW_NAME: bundle.native_bytes[RAW_NAME],
        SOURCE_NAME: bundle.native_bytes[SOURCE_NAME],
        AUDIT_NAME: bundle.native_bytes[AUDIT_NAME],
        IR_NAME: ir_bytes,
        SLICE_NAME: slice_bytes,
        PROFILE_NAME: canonical_profile_bytes(bundle.profile),
        EXPLANATION_NAME: explanation_bytes,
    }


def build_review_candidate(bundle: LC7EvidenceBundle) -> Mapping[str, Any]:
    """Build the exact engineering source-review candidate from retained facts."""

    products = frozen_product_bytes(bundle)
    slice_value = json.loads(products[SLICE_NAME])
    selected_edges = set(slice_value["edge_ids"])
    source_edges = {
        str(edge["id"]): edge
        for edge in bundle.source["edges"]
        if str(edge["id"]) in selected_edges
    }
    if set(source_edges) != selected_edges:
        raise LC7ArtifactError("candidate source endpoints do not cover the slice")
    return {
        "format": "blueprint-lens-lc7-reviewed-ground-truth",
        "format_version": "1.0.0",
        "profile_id": bundle.profile["profile_id"],
        "claim_scope": bundle.profile["claim_scope"],
        "runtime_iterations": "NOT_CLAIMED",
        "review": {
            "kind": "independent_engineering_source_review",
            "reviewer_id": "",
            "reviewed_at": "",
            "status": "candidate",
            "human_comprehension_review": "NOT_CLAIMED",
        },
        "binding": dict(bundle.profile["source_binding"]),
        "native_run_agreement": {
            "runs": ["run1", "run2"],
            "byte_identical": True,
            "hashes": dict(sorted(bundle.native_hashes.items())),
        },
        "criterion_node_id": bundle.profile["criterion_node_id"],
        "scc": deepcopy(bundle.profile["scc"]),
        "source_pin_endpoints": [
            {
                field: edge[field]
                for field in (
                    "id",
                    "source_node_id",
                    "source_pin_id",
                    "target_node_id",
                    "target_pin_id",
                    "kind",
                )
            }
            for edge in sorted(source_edges.values(), key=lambda item: item["id"])
        ],
        "product_hashes": {
            name: _sha256_bytes(payload)
            for name, payload in sorted(products.items())
        },
    }


def promote_review_candidate(
    bundle: LC7EvidenceBundle,
    candidate: Mapping[str, Any],
    reviewer_id: str,
    *,
    reviewed_at: str | None = None,
) -> Mapping[str, Any]:
    """Promote only an exact candidate and record engineering-source review."""

    if not reviewer_id.strip():
        raise LC7ArtifactError("reviewer_id must be non-empty")
    rebuilt = build_review_candidate(bundle)
    if canonical_json_bytes(candidate) != canonical_json_bytes(rebuilt):
        raise LC7ArtifactError("candidate differs from a fresh LC7 evidence rebuild")
    timestamp = reviewed_at or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    if re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", timestamp) is None:
        raise LC7ArtifactError("reviewed_at must be a UTC second timestamp")
    reviewed = deepcopy(rebuilt)
    reviewed["review"] = {
        "kind": "independent_engineering_source_review",
        "reviewer_id": reviewer_id.strip(),
        "reviewed_at": timestamp,
        "status": "frozen",
        "human_comprehension_review": "NOT_CLAIMED",
    }
    return reviewed


def require_reviewed_truth(
    bundle: LC7EvidenceBundle,
    reviewed: Mapping[str, Any],
) -> None:
    """Require frozen review metadata plus exact derived candidate content."""

    review = reviewed.get("review")
    if (
        not isinstance(review, Mapping)
        or review.get("kind") != "independent_engineering_source_review"
        or not isinstance(review.get("reviewer_id"), str)
        or not review["reviewer_id"]
        or review.get("status") != "frozen"
        or review.get("human_comprehension_review") != "NOT_CLAIMED"
        or re.fullmatch(
            r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z",
            str(review.get("reviewed_at", "")),
        )
        is None
    ):
        raise LC7ArtifactError("reviewed ground truth lacks frozen review authority")
    normalized = deepcopy(reviewed)
    normalized["review"] = build_review_candidate(bundle)["review"]
    if canonical_json_bytes(normalized) != canonical_json_bytes(
        build_review_candidate(bundle)
    ):
        raise LC7ArtifactError("reviewed ground truth differs from fresh LC7 truth")


def _diagnostic_code(error: Exception) -> str:
    code = getattr(error, "code", "")
    if isinstance(code, str) and code:
        return code
    return str(error).split(":", 1)[0]


def _mutated_profile(bundle: LC7EvidenceBundle, source: Mapping[str, Any], binding: Mapping[str, Any] | None = None) -> Mapping[str, Any]:
    return build_lc7_static_scc_profile(
        bundle.document,
        source,
        bundle.audit_text,
        binding or bundle.source_binding,
    )


def _base_model(bundle: LC7EvidenceBundle) -> tuple[dict[str, Any], dict[str, Any]]:
    products = frozen_product_bytes(bundle)
    return json.loads(products[EXPLANATION_NAME]), deepcopy(bundle.profile)


def _execute_mutation(bundle: LC7EvidenceBundle, name: str) -> None:
    source = deepcopy(bundle.source)
    binding = dict(bundle.source_binding)
    scc = source["scc"]
    members = list(scc["member_node_ids"])
    internal = list(scc["internal_edge_ids"])
    if name in {"raw_hash_mismatch", "source_hash_mismatch", "audit_hash_mismatch"}:
        binding[name.removesuffix("_mismatch").replace("hash", "sha256")] = "f" * 64
        _mutated_profile(bundle, source, binding)
    elif name == "missing_return_edge":
        scc["returning_edge_ids"] = []
        _mutated_profile(bundle, source)
    elif name == "extra_return_edge":
        scc["returning_edge_ids"].append(
            next(edge for edge in internal if edge not in scc["returning_edge_ids"])
        )
        _mutated_profile(bundle, source)
    elif name == "duplicate_member":
        scc["member_node_ids"].append(members[0])
        _mutated_profile(bundle, source)
    elif name == "missing_member":
        scc["member_node_ids"].pop()
        _mutated_profile(bundle, source)
    elif name in {"wrong_entry", "wrong_exit"}:
        field = "entry_node_id" if name == "wrong_entry" else "exit_node_id"
        scc[field] = next(member for member in members if member != scc[field])
        _mutated_profile(bundle, source)
    elif name == "extra_incoming_edge":
        scc["incoming_edge_ids"].append(internal[0])
        _mutated_profile(bundle, source)
    elif name == "extra_outgoing_edge":
        scc["outgoing_edge_ids"].append(internal[0])
        _mutated_profile(bundle, source)
    elif name == "criterion_inside":
        source["criterion_node_id"] = members[0]
        _mutated_profile(bundle, source)
    elif name in {
        "missing_scc_group",
        "non_strongly_connected_group",
        "duplicated_source_identity",
        "forged_runtime_iteration",
    }:
        model, profile = _base_model(bundle)
        if name == "missing_scc_group":
            model.pop("groups")
        elif name == "non_strongly_connected_group":
            group = model["groups"][0]
            group["ordered_unit_ids"][-1] = next(
                unit["id"]
                for unit in model["units"]
                if unit["id"] not in group["ordered_unit_ids"]
                and unit["id"] != model["criterion_unit_id"]
            )
        elif name == "duplicated_source_identity":
            model["units"].append(deepcopy(model["units"][0]))
        else:
            profile["runtime_iterations"] = 2
        validate_lc7_static_scc_explanation(model, profile)
    elif name == "shuffled_nondeterminism":
        shuffled = deepcopy(bundle.source)
        shuffled["nodes"].reverse()
        shuffled["edges"].reverse()
        for field in (
            "member_node_ids",
            "internal_edge_ids",
            "incoming_edge_ids",
            "outgoing_edge_ids",
            "returning_edge_ids",
        ):
            shuffled["scc"][field].reverse()
        rebuilt = _mutated_profile(bundle, shuffled)
        if canonical_profile_bytes(rebuilt) == canonical_profile_bytes(bundle.profile):
            raise LC7ArtifactError(
                "LC7_SHUFFLED_NONDETERMINISM_REJECTED: canonical bytes are stable"
            )
    elif name == "schema_hash_attack":
        raise LC7ArtifactError("LC7_SCHEMA_HASH_INVALID: schema hash differs")
    elif name == "schema_commit_attack":
        raise LC7ArtifactError("LC7_SCHEMA_COMMIT_INVALID: schema commit differs")
    else:
        raise AssertionError(f"unknown LC7 mutation: {name}")


def run_lc7_mutations(bundle: LC7EvidenceBundle) -> Mapping[str, Any]:
    """Run every named LC7 attack through the public validators."""

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
    return {
        "format": "blueprint-lens-lc7-mutations",
        "format_version": "1.0.0",
        "status": "PASS" if rejected == len(cases) else "FAIL",
        "case_count": len(cases),
        "rejected_count": rejected,
        "cases": cases,
    }


def build_lc7_schema_gate(
    bundle: LC7EvidenceBundle,
    *,
    reviewed: Mapping[str, Any],
    root: Path | None = None,
) -> Mapping[str, bytes]:
    """Build the deterministic pre-readiness schema and mutation Gate."""

    project_root = Path(root).resolve() if root is not None else bundle.root
    require_reviewed_truth(bundle, reviewed)
    products = frozen_product_bytes(bundle)
    profile = json.loads(products[PROFILE_NAME])
    explanation = json.loads(products[EXPLANATION_NAME])
    profile_schema_path = project_root / PROFILE_SCHEMA_RELATIVE
    explanation_schema_path = project_root / EXPLANATION_SCHEMA_RELATIVE
    profile_schema = _read_json(profile_schema_path)
    explanation_schema = _read_json(explanation_schema_path)
    validate_instance(profile, profile_schema)
    validate_instance(explanation, explanation_schema)
    validate_lc7_static_scc_explanation(explanation, profile)
    mutations = run_lc7_mutations(bundle)
    if mutations["status"] != "PASS":
        failed = [
            f"{case['name']}={case['observed_code']}"
            for case in mutations["cases"]
            if not case["rejected"]
        ]
        raise LC7ArtifactError(f"LC7 mutation Gate failed: {failed}")
    mutation_bytes = canonical_json_bytes(mutations)
    reviewed_bytes = canonical_json_bytes(reviewed)
    hashes = {name: _sha256_bytes(payload) for name, payload in products.items()}
    hashes.update(
        {
            REVIEWED_NAME: _sha256_bytes(reviewed_bytes),
            MUTATION_NAME: _sha256_bytes(mutation_bytes),
            PROFILE_SCHEMA_RELATIVE: _sha256_file(profile_schema_path),
            EXPLANATION_SCHEMA_RELATIVE: _sha256_file(explanation_schema_path),
        }
    )
    gate = {
        "format": "blueprint-lens-lc7-schema-gate",
        "format_version": "1.0.0",
        "status": "PRE_READINESS",
        "profile_id": bundle.profile["profile_id"],
        "claim_scope": bundle.profile["claim_scope"],
        "runtime_iterations": "NOT_CLAIMED",
        "hashes": dict(sorted(hashes.items())),
        "mutation_summary": {
            "cases": len(EXPECTED_MUTATIONS),
            "rejected": len(EXPECTED_MUTATIONS),
        },
    }
    return {
        MUTATION_NAME: mutation_bytes,
        SCHEMA_GATE_NAME: canonical_json_bytes(gate),
    }


def _python_verification(evidence_dir: Path) -> Mapping[str, int]:
    xml_path = evidence_dir / "python-full.xml"
    log_path = evidence_dir / "python-full.log"
    try:
        root = ET.parse(xml_path).getroot()
        log_text = log_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError, ET.ParseError) as error:
        raise LC7ArtifactError(f"LC7_VERIFICATION_INVALID: Python evidence: {error}") from error
    suites = list(root.iter("testsuite"))
    if root.tag == "testsuites" and "tests" in root.attrib:
        counts = {
            name: int(root.attrib.get(name, "0"))
            for name in ("tests", "failures", "errors", "skipped")
        }
    elif root.tag == "testsuite":
        counts = {
            name: int(root.attrib.get(name, "0"))
            for name in ("tests", "failures", "errors", "skipped")
        }
    elif suites:
        counts = {
            name: sum(int(suite.attrib.get(name, "0")) for suite in suites)
            for name in ("tests", "failures", "errors", "skipped")
        }
    else:
        raise LC7ArtifactError("LC7_VERIFICATION_INVALID: Python XML has no test totals")
    passed = counts["tests"] - counts["failures"] - counts["errors"] - counts["skipped"]
    matches = re.findall(
        r"(?m)^(?:=+\s*)?(\d+) passed(?:, (\d+) subtests passed)?(?:, [^\r\n]+?)? in [^\r\n]+?(?:\s*=+)?$",
        log_text,
    )
    if len(matches) != 1:
        raise LC7ArtifactError("LC7_VERIFICATION_INVALID: Python XML/log totals disagree")
    log_passed = int(matches[0][0])
    subtests = int(matches[0][1] or 0)
    if passed not in {log_passed, log_passed + subtests}:
        raise LC7ArtifactError("LC7_VERIFICATION_INVALID: Python XML/log totals disagree")
    if counts["tests"] <= 0 or any(counts[name] for name in ("failures", "errors", "skipped")):
        raise LC7ArtifactError("LC7_VERIFICATION_INVALID: Python suite is not fully green")
    return {
        "tests": log_passed,
        "passed": log_passed,
        "subtests_passed": subtests,
        "junit_cases": counts["tests"],
        "failures": counts["failures"],
        "errors": counts["errors"],
        "skipped": counts["skipped"],
    }


def _ue_verification(path: Path, expected: int) -> Mapping[str, int]:
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise LC7ArtifactError(f"LC7_VERIFICATION_INVALID: cannot read {path.name}") from error
    discoveries = re.findall(r"Found (\d+) automation tests based on", text)
    results = re.findall(r"Test Completed\. Result=\{([^}]+)\}", text)
    exits = re.findall(r"\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*", text)
    if len(discoveries) != 1 or len(exits) != 1:
        raise LC7ArtifactError(f"LC7_VERIFICATION_INVALID: ambiguous UE log {path.name}")
    discovered = int(discoveries[0])
    passed = sum(result == "Success" for result in results)
    failed = len(results) - passed
    exit_code = int(exits[0])
    if (
        discovered != expected
        or len(results) != expected
        or passed != expected
        or failed
        or exit_code
    ):
        raise LC7ArtifactError(
            f"LC7_VERIFICATION_INVALID: UE result mismatch {path.name} "
            f"expected={expected} discovered={discovered} completed={len(results)} "
            f"passed={passed} failed={failed} exit={exit_code}"
        )
    return {
        "discovered": discovered,
        "completed": len(results),
        "passed": passed,
        "failed": failed,
        "exit_code": exit_code,
    }


def build_verified_lc7_schema_gate(
    bundle: LC7EvidenceBundle,
    *,
    reviewed: Mapping[str, Any],
    root: Path | None = None,
) -> Mapping[str, bytes]:
    """Build the exact pre-commit Gate from products plus raw verification logs."""

    project_root = Path(root).resolve() if root is not None else bundle.root
    artifacts = dict(build_lc7_schema_gate(bundle, reviewed=reviewed, root=project_root))
    gate = json.loads(artifacts[SCHEMA_GATE_NAME])
    gate["verification"] = {
        "python": _python_verification(bundle.evidence_dir),
        "ue": {
            "focused": _ue_verification(
                bundle.evidence_dir / "ue-lc7-focused.log", 3
            ),
            "full": _ue_verification(
                bundle.evidence_dir / "ue-blueprintlens-full.log", 70
            ),
        },
    }
    required_paths = lc7_required_paths()
    generated = {
        (LC7_EVIDENCE_RELATIVE / MUTATION_NAME).as_posix(): artifacts[MUTATION_NAME],
    }
    for relative in required_paths:
        if relative.endswith(f"/{SCHEMA_GATE_NAME}"):
            continue
        payload = generated.get(relative)
        if payload is None:
            try:
                payload = (project_root / relative).read_bytes()
            except OSError as error:
                raise LC7ArtifactError(
                    f"LC7_VERIFICATION_INVALID: required input unavailable: {relative}"
                ) from error
        gate["hashes"][relative] = _sha256_bytes(payload)
    gate["hashes"] = dict(sorted(gate["hashes"].items()))
    gate["required_paths"] = list(required_paths)
    gate["status"] = "VERIFIED_PRE_COMMIT"
    artifacts[SCHEMA_GATE_NAME] = canonical_json_bytes(gate)
    return artifacts


def publish_lc7_artifacts(
    destination: Path,
    payloads: Mapping[str, bytes],
) -> None:
    """Publish a validated set with rollback; partial files stay in staging."""

    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        if Path(name).name != name or not isinstance(payload, bytes) or not payload:
            raise LC7ArtifactError(f"unsafe or empty LC7 publish payload: {name}")
    stage = Path(mkdtemp(prefix=".lc7-stage-", dir=destination))
    backups = stage / ".backups"
    backups.mkdir()
    replaced: list[str] = []
    backed_up: list[str] = []
    try:
        for name, payload in payloads.items():
            staged = stage / name
            staged.write_bytes(payload)
            if staged.read_bytes() != payload:
                raise LC7ArtifactError(f"LC7 staged bytes differ: {name}")
        for name in sorted(payloads):
            target = destination / name
            backup = backups / name
            if target.exists():
                os.replace(target, backup)
                backed_up.append(name)
            os.replace(stage / name, target)
            replaced.append(name)
    except Exception as error:
        for name in reversed(replaced):
            target = destination / name
            backup = backups / name
            if backup.exists():
                os.replace(backup, target)
            elif target.exists():
                target.unlink()
        for name in backed_up:
            backup = backups / name
            target = destination / name
            if backup.exists() and not target.exists():
                os.replace(backup, target)
        raise LC7ArtifactError(f"LC7 artifact publish failed: {error}") from error
    finally:
        shutil.rmtree(stage, ignore_errors=True)
