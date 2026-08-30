"""Independent commit-blob readiness audit for the bounded LC6-F1 truth."""

from __future__ import annotations

from hashlib import sha256
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
from tempfile import TemporaryDirectory
from typing import Any, Mapping, Sequence

from .lc6_artifacts import (
    BUDGET_NAME,
    BUDGET_SCHEMA_RELATIVE,
    CORE_NAME,
    CORE_SCHEMA_RELATIVE,
    EXPECTED_MUTATIONS,
    GATE_NAME,
    LC6EvidenceBundle,
    MUTATION_NAME,
    REVIEWED_NAME,
    build_lc6_schema_gate,
    build_verified_lc6_schema_gate,
    canonical_json_bytes,
    load_lc6_evidence,
    require_reviewed_truth,
    validate_lc6_schema_products,
)


CHECK_NAMES = (
    "schema_gate_commit_exists",
    "required_paths_are_git_blobs",
    "asset_hash_matches",
    "raw_runs_are_identical",
    "source_runs_are_identical",
    "audit_runs_are_identical",
    "source_audit_semantics_agree",
    "core_product_hash_matches",
    "budget_product_hash_matches",
    "schemas_hash_and_validate",
    "reviewed_ground_truth_matches",
    "mutation_matrix_rebuilds_25_of_25",
    "schema_gate_manifest_matches",
    "products_rebuild_byte_identically",
    "core_has_no_truncated_node",
    "scope_is_lc6_f1_only",
)

_EVIDENCE_RELATIVE = Path("artifacts/r1/lc6-boundary-truth")
_ASSET_RELATIVE = Path(
    "unreal/BlueprintLensProbe/Content/LensCorpus/BP_LC6_BoundaryMatrix.uasset"
)
_REQUIRED = {
    _ASSET_RELATIVE.as_posix(),
    CORE_SCHEMA_RELATIVE,
    BUDGET_SCHEMA_RELATIVE,
    *(
        (_EVIDENCE_RELATIVE / name).as_posix()
        for name in (CORE_NAME, BUDGET_NAME, REVIEWED_NAME, MUTATION_NAME, GATE_NAME)
    ),
    *(
        (_EVIDENCE_RELATIVE / run / name).as_posix()
        for run in ("run1", "run2")
        for name in (
            "BP_LC6_BoundaryMatrix.raw-0.2.json",
            "BP_LC6_BoundaryMatrix.boundary-source.json",
            "BP_LC6_BoundaryMatrix.boundary-audit.tsv",
            "ue-capture.log",
        )
    ),
}


class LC6ReadinessError(ValueError):
    """Stable fail-closed readiness diagnostic."""

    def __init__(self, message: str) -> None:
        self.code = "LC6_READINESS_INPUT_INVALID"
        super().__init__(f"{self.code}: {message}")


def _fail(message: str) -> None:
    raise LC6ReadinessError(message)


def _git(root: Path, arguments: Sequence[str], context: str) -> bytes:
    completed = subprocess.run(
        ["git", "-C", str(root), *arguments],
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).decode("utf-8", errors="replace").strip()
        _fail(f"{context}: {detail}")
    return completed.stdout


def _json(path: Path) -> Mapping[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read {path}: {error}")
    if not isinstance(value, Mapping):
        _fail(f"JSON root is not an object: {path}")
    return value


def _hash(path: Path) -> str:
    try:
        return sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(f"cannot hash {path}: {error}")


def _safe_paths(required_paths: Sequence[str]) -> tuple[str, ...]:
    if len(required_paths) != len(set(required_paths)):
        _fail("required path inventory contains duplicates")
    result: list[str] = []
    for value in required_paths:
        path = PurePosixPath(value)
        if path.is_absolute() or ".." in path.parts or not value:
            _fail(f"required path is unsafe: {value!r}")
        result.append(path.as_posix())
    if not _REQUIRED <= set(result):
        missing = sorted(_REQUIRED - set(result))
        _fail(f"required evidence inventory is incomplete: {missing}")
    return tuple(sorted(result))


def _extract_commit_tree(
    root: Path, schema_gate_commit: str, required_paths: Sequence[str], destination: Path
) -> None:
    commit_type = _git(
        root,
        ["cat-file", "-t", schema_gate_commit],
        "schema Gate commit does not exist",
    ).decode().strip()
    if commit_type != "commit":
        _fail("schema Gate identity is not a commit")
    for relative in _safe_paths(required_paths):
        object_type = _git(
            root,
            ["cat-file", "-t", f"{schema_gate_commit}:{relative}"],
            f"required path is not a Git blob: {relative}",
        ).decode().strip()
        if object_type != "blob":
            _fail(f"required path is not a blob: {relative}")
        payload = _git(
            root,
            ["show", f"{schema_gate_commit}:{relative}"],
            f"cannot reopen Git blob: {relative}",
        )
        path = destination / PurePosixPath(relative)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)


def audit_lc6_readiness(
    root: Path,
    schema_gate_commit: str,
    required_paths: Sequence[str],
) -> Mapping[str, Any]:
    """Reopen exact Git blobs, rebuild both LC6 products and return 16 checks."""

    root = root.resolve()
    try:
        with TemporaryDirectory(prefix="blueprint-lens-lc6-readiness-") as directory:
            extracted = Path(directory)
            _extract_commit_tree(root, schema_gate_commit, required_paths, extracted)
            evidence = extracted / _EVIDENCE_RELATIVE
            bundle = load_lc6_evidence(evidence)
            gate = _json(evidence / GATE_NAME)
            mutations = _json(evidence / MUTATION_NAME)
            reviewed = _json(evidence / REVIEWED_NAME)
            core_schema = _json(extracted / CORE_SCHEMA_RELATIVE)
            budget_schema = _json(extracted / BUDGET_SCHEMA_RELATIVE)
            validate_lc6_schema_products(bundle.profiles, core_schema, budget_schema)
            require_reviewed_truth(bundle, reviewed)

            asset_hash = _hash(extracted / _ASSET_RELATIVE)
            if asset_hash != bundle.source.get("asset_sha256") or asset_hash != gate.get(
                "hashes", {}
            ).get("asset_sha256"):
                _fail("asset hash differs from source or schema Gate")
            for name, expected in (
                (CORE_NAME, bundle.profiles.core_boundary_matrix),
                (BUDGET_NAME, bundle.profiles.upstream_budget),
            ):
                path = evidence / name
                if path.read_bytes() != canonical_json_bytes(expected):
                    _fail(f"product differs from semantic rebuild: {name}")
                if gate["hashes"].get(name) != _hash(path):
                    _fail(f"product hash differs from schema Gate: {name}")
            for schema_relative in (CORE_SCHEMA_RELATIVE, BUDGET_SCHEMA_RELATIVE):
                if gate["hashes"].get(schema_relative) != _hash(
                    extracted / schema_relative
                ):
                    _fail(f"schema hash differs from schema Gate: {schema_relative}")
            if gate["hashes"].get(REVIEWED_NAME) != _hash(evidence / REVIEWED_NAME):
                _fail("reviewed truth hash differs from schema Gate")
            if gate["hashes"].get(MUTATION_NAME) != _hash(evidence / MUTATION_NAME):
                _fail("mutation hash differs from schema Gate")
            if (
                mutations.get("case_count") != 25
                or mutations.get("rejected_count") != 25
                or mutations.get("status") != "PASS"
                or {case.get("name") for case in mutations.get("cases", [])}
                != set(EXPECTED_MUTATIONS)
                or not all(case.get("rejected") is True for case in mutations["cases"])
            ):
                _fail("mutation matrix is not 25/25")
            verification = gate.get("verification")
            if isinstance(verification, Mapping):
                python = verification.get("python")
                if not isinstance(python, Mapping):
                    _fail("schema Gate Python verification is missing")
                tests = python.get("tests_passed")
                subtests = python.get("subtests_passed")
                if not isinstance(tests, int) or not isinstance(subtests, int):
                    _fail("schema Gate Python verification counts are invalid")
                rebuilt_gate = build_verified_lc6_schema_gate(
                    bundle,
                    root=extracted,
                    python_tests=tests,
                    python_subtests=subtests,
                )
            else:
                rebuilt_gate = build_lc6_schema_gate(bundle, root=extracted)
            if rebuilt_gate[MUTATION_NAME] != (evidence / MUTATION_NAME).read_bytes():
                _fail("mutation report does not rebuild byte-identically")
            if rebuilt_gate[GATE_NAME] != (evidence / GATE_NAME).read_bytes():
                _fail("schema Gate manifest does not rebuild byte-identically")
            if any(node.semantic_status == "truncated" for node in bundle.document.nodes):
                _fail("core typed IR contains node-level truncated")
            if gate.get("scope") != "LC6-F1" or reviewed.get("scope") != "LC6-F1":
                _fail("readiness scope is not LC6-F1 only")

            checks = {name: True for name in CHECK_NAMES}
            return {
                "format": "blueprint-lens-lc6-readiness",
                "format_version": "1.0.0",
                "status": "TRUTH_FROZEN",
                "scope": "LC6-F1",
                "profile_ids": [
                    "LC6_CORE_BOUNDARY_MATRIX_V1",
                    "LC6_MAX_UPSTREAM_HOPS_V1",
                ],
                "schema_gate_commit": schema_gate_commit,
                "checks": checks,
                "checks_passed": len(checks),
                "checks_total": len(checks),
                "hashes": dict(sorted(gate["hashes"].items())),
                "not_authorized": [
                    "visual condition or effect image",
                    "Slate surface or UE-visible fidelity",
                    "human comprehension or general scalability",
                    "product-default decision",
                ],
            }
    except LC6ReadinessError:
        raise
    except Exception as error:
        _fail(str(error))


def freeze_lc6_readiness(evidence_dir: Path, schema_gate_commit: str) -> Path:
    """Atomically publish readiness only after the exact commit passes 16 checks."""

    evidence_dir = evidence_dir.resolve()
    root = evidence_dir
    while root.parent != root and not (root / ".git").exists():
        root = root.parent
    if not (root / ".git").exists():
        _fail(f"cannot resolve Git root from evidence directory: {evidence_dir}")
    inventory = _git(
        root,
        ["ls-tree", "-r", "--name-only", schema_gate_commit],
        "cannot list schema Gate commit blobs",
    ).decode("utf-8", errors="strict").splitlines()
    required_paths = tuple(path for path in inventory if path)
    payload = audit_lc6_readiness(root, schema_gate_commit, required_paths)
    readiness_path = evidence_dir / "readiness.json"
    temporary = readiness_path.with_name("readiness.json.tmp")
    readiness_bytes = canonical_json_bytes(payload)
    try:
        temporary.write_bytes(readiness_bytes)
        temporary.replace(readiness_path)
    except OSError as error:
        temporary.unlink(missing_ok=True)
        _fail(f"cannot publish readiness atomically: {error}")
    return readiness_path


def _bundle_root(bundle: LC6EvidenceBundle) -> Path:
    for candidate in (bundle.evidence_dir, *bundle.evidence_dir.parents):
        if (candidate / "schemas").is_dir() and (candidate / "unreal").is_dir():
            return candidate
    _fail("cannot resolve project root for readiness attack")


def _copy_blob(source_root: Path, repository: Path, relative: str) -> None:
    source = source_root / PurePosixPath(relative)
    destination = repository / PurePosixPath(relative)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def exercise_named_readiness_attack(bundle: LC6EvidenceBundle, name: str) -> None:
    """Run one named Git-input attack through the public readiness auditor."""

    attacks = {
        "schema_gate_commit_wrong",
        "asset_hash_changed",
        "raw_hash_changed",
        "audit_hash_changed",
        "schema_hash_changed",
        "required_evidence_missing",
    }
    if name not in attacks:
        _fail(f"unknown readiness attack: {name}")
    source_root = _bundle_root(bundle)
    with TemporaryDirectory(prefix="blueprint-lens-lc6-readiness-attack-") as directory:
        repository = Path(directory) / "repo"
        repository.mkdir()
        _copy_blob(source_root, repository, ".gitattributes")
        for relative in sorted(_REQUIRED - {
            (_EVIDENCE_RELATIVE / MUTATION_NAME).as_posix(),
            (_EVIDENCE_RELATIVE / GATE_NAME).as_posix(),
        }):
            _copy_blob(source_root, repository, relative)
        asset_hash = sha256((source_root / _ASSET_RELATIVE).read_bytes()).hexdigest()
        evidence = repository / _EVIDENCE_RELATIVE
        (evidence / MUTATION_NAME).write_bytes(canonical_json_bytes({}))
        (evidence / GATE_NAME).write_bytes(
            canonical_json_bytes({"hashes": {"asset_sha256": asset_hash}})
        )
        _git(repository, ["init", "-q"], "cannot initialize readiness attack repo")
        _git(
            repository,
            ["config", "user.email", "lc6@example.invalid"],
            "cannot configure readiness attack repo",
        )
        _git(
            repository,
            ["config", "user.name", "LC6 Readiness Attack"],
            "cannot configure readiness attack repo",
        )
        _git(repository, ["add", "."], "cannot stage readiness attack repo")
        _git(
            repository,
            ["commit", "-qm", "readiness attack base"],
            "cannot commit readiness attack repo",
        )
        commit = _git(repository, ["rev-parse", "HEAD"], "cannot resolve attack commit").decode().strip()
        required = tuple(
            line
            for line in _git(
                repository,
                ["ls-tree", "-r", "--name-only", commit],
                "cannot list readiness attack blobs",
            ).decode().splitlines()
            if line
        )

        attacked_commit = commit
        attacked_required = list(required)
        if name == "schema_gate_commit_wrong":
            attacked_commit = "0" * 40
        elif name == "required_evidence_missing":
            attacked_required.remove(
                (_EVIDENCE_RELATIVE / "run1" / "BP_LC6_BoundaryMatrix.boundary-audit.tsv").as_posix()
            )
        else:
            relative = {
                "asset_hash_changed": _ASSET_RELATIVE.as_posix(),
                "raw_hash_changed": (
                    _EVIDENCE_RELATIVE / "run1" / "BP_LC6_BoundaryMatrix.raw-0.2.json"
                ).as_posix(),
                "audit_hash_changed": (
                    _EVIDENCE_RELATIVE / "run1" / "BP_LC6_BoundaryMatrix.boundary-audit.tsv"
                ).as_posix(),
                "schema_hash_changed": CORE_SCHEMA_RELATIVE,
            }[name]
            target = repository / PurePosixPath(relative)
            target.write_bytes(target.read_bytes() + b"attack")
            _git(repository, ["add", relative], "cannot stage readiness attack")
            _git(
                repository,
                ["commit", "-qm", name],
                "cannot commit readiness attack",
            )
            attacked_commit = _git(
                repository, ["rev-parse", "HEAD"], "cannot resolve attacked commit"
            ).decode().strip()

        audit_lc6_readiness(repository, attacked_commit, tuple(attacked_required))
