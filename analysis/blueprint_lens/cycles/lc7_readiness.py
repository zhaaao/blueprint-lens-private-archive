"""Commit-blob readiness audit for the bounded LC7 static-SCC truth."""

from __future__ import annotations

from hashlib import sha256
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
from tempfile import TemporaryDirectory, mkstemp
from typing import Any, Mapping, Sequence

from .lc7_artifacts import (
    EXPECTED_MUTATIONS,
    LC7_EVIDENCE_RELATIVE,
    MUTATION_NAME,
    REVIEWED_NAME,
    SCHEMA_GATE_NAME,
    build_verified_lc7_schema_gate,
    canonical_json_bytes,
    frozen_product_bytes,
    lc7_required_paths,
    load_lc7_evidence,
    require_reviewed_truth,
)


CHECK_NAMES = (
    "schema_gate_commit_exists",
    "required_path_inventory_is_exact",
    "required_paths_are_git_blobs",
    "asset_hash_matches",
    "raw_runs_are_identical",
    "source_runs_are_identical",
    "audit_runs_are_identical",
    "capture_logs_are_bound",
    "source_audit_semantics_agree",
    "typed_ir_rebuilds_byte_identically",
    "slice_rebuilds_byte_identically",
    "profile_rebuilds_byte_identically",
    "explanation_rebuilds_byte_identically",
    "reviewed_ground_truth_matches",
    "schemas_hash_and_validate",
    "mutation_matrix_rebuilds_19_of_19",
    "python_verification_is_fully_green",
    "focused_ue_is_3_of_3",
    "full_ue_is_70_of_70",
    "schema_gate_manifest_rebuilds_byte_identically",
    "scope_is_static_source_visible_scc_only",
    "runtime_iterations_are_not_claimed",
)

_COMMIT = re.compile(r"[0-9a-fA-F]{40}(?:[0-9a-fA-F]{24})?")


class LC7ReadinessError(ValueError):
    """Stable fail-closed LC7 readiness diagnostic."""

    def __init__(self, message: str) -> None:
        self.code = "LC7_READINESS_INPUT_INVALID"
        super().__init__(f"{self.code}: {message}")


def _fail(message: str) -> None:
    raise LC7ReadinessError(message)


def _git(root: Path, *arguments: str, context: str) -> bytes:
    completed = subprocess.run(
        ["git", "-c", "core.longpaths=true", "-C", str(root), *arguments],
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout).decode(
            "utf-8", errors="replace"
        ).strip()
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


def _safe_exact_paths(required_paths: Sequence[str]) -> tuple[str, ...]:
    if len(required_paths) != len(set(required_paths)):
        _fail("required path inventory contains duplicates")
    normalized: list[str] = []
    for value in required_paths:
        path = PurePosixPath(value)
        if (
            not value
            or "\\" in value
            or path.is_absolute()
            or ".." in path.parts
            or path.as_posix() != value
        ):
            _fail(f"required path is unsafe: {value!r}")
        normalized.append(value)
    expected = lc7_required_paths()
    if tuple(sorted(normalized)) != expected:
        missing = sorted(set(expected) - set(normalized))
        extra = sorted(set(normalized) - set(expected))
        _fail(f"required path inventory differs: missing={missing} extra={extra}")
    return expected


def _extract_commit_tree(
    root: Path,
    schema_gate_commit: str,
    required_paths: Sequence[str],
    destination: Path,
) -> tuple[str, ...]:
    if _COMMIT.fullmatch(schema_gate_commit) is None:
        _fail("schema Gate identity must be a full hexadecimal commit ID")
    commit_type = _git(
        root,
        "cat-file",
        "-t",
        schema_gate_commit,
        context="schema Gate commit does not exist",
    ).decode("ascii", errors="replace").strip()
    if commit_type != "commit":
        _fail("schema Gate identity is not a commit")
    safe_paths = _safe_exact_paths(required_paths)
    for relative in safe_paths:
        revision_path = f"{schema_gate_commit}:{relative}"
        object_type = _git(
            root,
            "cat-file",
            "-t",
            revision_path,
            context=f"required path is not a Git blob: {relative}",
        ).decode("ascii", errors="replace").strip()
        if object_type != "blob":
            _fail(f"required path is not a blob: {relative}")
        payload = _git(
            root,
            "show",
            revision_path,
            context=f"cannot reopen Git blob: {relative}",
        )
        target = destination / PurePosixPath(relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)
    return safe_paths


def _require_hash_manifest(
    extracted: Path,
    gate: Mapping[str, Any],
    required_paths: Sequence[str],
) -> None:
    hashes = gate.get("hashes")
    if not isinstance(hashes, Mapping):
        _fail("schema Gate hash manifest is missing")
    for relative in required_paths:
        if relative == (LC7_EVIDENCE_RELATIVE / SCHEMA_GATE_NAME).as_posix():
            continue
        expected = hashes.get(relative)
        if not isinstance(expected, str) or expected != _hash(extracted / relative):
            _fail(f"Git blob hash differs from schema Gate: {relative}")


def _require_verification(gate: Mapping[str, Any]) -> None:
    verification = gate.get("verification")
    if not isinstance(verification, Mapping):
        _fail("schema Gate verification is missing")
    python = verification.get("python")
    ue = verification.get("ue")
    if not isinstance(python, Mapping) or not isinstance(ue, Mapping):
        _fail("schema Gate verification sections are invalid")
    tests = python.get("tests")
    if (
        not isinstance(tests, int)
        or tests <= 0
        or python.get("passed") != tests
        or python.get("failures") != 0
        or python.get("errors") != 0
        or python.get("skipped") != 0
        or not isinstance(python.get("subtests_passed"), int)
    ):
        _fail("Python verification is not fully green")
    for name, expected in (("focused", 3), ("full", 70)):
        result = ue.get(name)
        if (
            not isinstance(result, Mapping)
            or result.get("discovered") != expected
            or result.get("completed") != expected
            or result.get("passed") != expected
            or result.get("failed") != 0
            or result.get("exit_code") != 0
        ):
            _fail(f"UE {name} verification is not {expected}/{expected}")


def audit_lc7_readiness(
    repository_root: Path,
    schema_gate_commit: str,
    required_paths: tuple[str, ...],
) -> dict[str, Any]:
    """Reopen the exact commit, rebuild all products, and return truth readiness."""

    root = Path(repository_root).resolve()
    try:
        with TemporaryDirectory(prefix="blueprint-lens-lc7-readiness-") as directory:
            extracted = Path(directory)
            safe_paths = _extract_commit_tree(
                root, schema_gate_commit, required_paths, extracted
            )
            evidence = extracted / LC7_EVIDENCE_RELATIVE
            gate = _json(evidence / SCHEMA_GATE_NAME)
            reviewed = _json(evidence / REVIEWED_NAME)
            mutations = _json(evidence / MUTATION_NAME)
            if gate.get("status") != "VERIFIED_PRE_COMMIT":
                _fail("schema Gate is not VERIFIED_PRE_COMMIT")
            if gate.get("required_paths") != list(safe_paths):
                _fail("schema Gate required path manifest differs")
            _require_hash_manifest(extracted, gate, safe_paths)
            _require_verification(gate)

            bundle = load_lc7_evidence(evidence, root=extracted)
            require_reviewed_truth(bundle, reviewed)
            products = frozen_product_bytes(bundle, output_dir=evidence)
            for name, payload in products.items():
                retained = evidence / name
                if retained.read_bytes() != payload:
                    _fail(f"product differs from independent rebuild: {name}")
                if gate["hashes"].get(name) != _hash(retained):
                    _fail(f"product hash differs from schema Gate: {name}")

            if (
                mutations.get("status") != "PASS"
                or mutations.get("case_count") != len(EXPECTED_MUTATIONS)
                or mutations.get("rejected_count") != len(EXPECTED_MUTATIONS)
                or {case.get("name") for case in mutations.get("cases", [])}
                != set(EXPECTED_MUTATIONS)
                or not all(case.get("rejected") is True for case in mutations["cases"])
            ):
                _fail("mutation report is not the complete rejected matrix")

            rebuilt = build_verified_lc7_schema_gate(
                bundle,
                reviewed=reviewed,
                root=extracted,
            )
            if rebuilt[MUTATION_NAME] != (evidence / MUTATION_NAME).read_bytes():
                _fail("mutation report does not rebuild byte-identically")
            if rebuilt[SCHEMA_GATE_NAME] != (evidence / SCHEMA_GATE_NAME).read_bytes():
                _fail("schema Gate does not rebuild byte-identically")
            if gate.get("profile_id") != "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1":
                _fail("readiness profile differs from LC7 static SCC")
            if gate.get("claim_scope") != "STATIC_SOURCE_VISIBLE_SCC":
                _fail("readiness scope differs from static source-visible SCC")
            if gate.get("runtime_iterations") != "NOT_CLAIMED":
                _fail("runtime iterations were claimed")

            checks = {name: True for name in CHECK_NAMES}
            return {
                "format": "blueprint-lens-lc7-readiness",
                "format_version": "1.0.0",
                "status": "TRUTH_FROZEN",
                "profile_id": "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1",
                "claim_scope": "STATIC_SOURCE_VISIBLE_SCC",
                "schema_gate_commit": schema_gate_commit.lower(),
                "required_file_count": len(safe_paths),
                "checks": checks,
                "checks_passed": len(checks),
                "checks_total": len(checks),
                "hashes": dict(sorted(gate["hashes"].items())),
                "not_authorized": [
                    "visual candidates or visual effects",
                    "responsive or native Slate implementation",
                    "runtime recurrence, iterations, order, timing, trace state, or playback",
                    "human preference or comprehension",
                    "superiority",
                    "general scalability",
                    "product default",
                ],
            }
    except LC7ReadinessError:
        raise
    except Exception as error:
        _fail(str(error))


def _repository_root(evidence_dir: Path) -> Path:
    completed = subprocess.run(
        ["git", "-C", str(evidence_dir), "rev-parse", "--show-toplevel"],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0 or not completed.stdout.strip():
        _fail(f"cannot resolve Git root from evidence directory: {evidence_dir}")
    return Path(completed.stdout.strip()).resolve()


def _restore_readiness(path: Path, previous: bytes | None) -> None:
    if previous is None:
        path.unlink(missing_ok=True)
        return
    descriptor, temporary_name = mkstemp(prefix="readiness.json.restore-", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(previous)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def freeze_lc7_readiness(evidence_dir: Path, schema_gate_commit: str) -> Path:
    """Publish readiness atomically last after the exact commit passes audit."""

    evidence_dir = Path(evidence_dir).resolve()
    root = _repository_root(evidence_dir)
    gate_relative = (LC7_EVIDENCE_RELATIVE / SCHEMA_GATE_NAME).as_posix()
    gate_bytes = _git(
        root,
        "show",
        f"{schema_gate_commit}:{gate_relative}",
        context="cannot reopen committed schema Gate",
    )
    try:
        gate = json.loads(gate_bytes.decode("utf-8"))
        required = gate["required_paths"]
    except (UnicodeError, json.JSONDecodeError, KeyError, TypeError) as error:
        _fail(f"committed schema Gate inventory is invalid: {error}")
    if not isinstance(required, list) or not all(
        isinstance(path, str) for path in required
    ):
        _fail("committed schema Gate required_paths is invalid")

    payload = audit_lc7_readiness(root, schema_gate_commit, tuple(required))
    readiness_path = evidence_dir / "readiness.json"
    previous = readiness_path.read_bytes() if readiness_path.exists() else None
    readiness_bytes = canonical_json_bytes(payload)
    descriptor, temporary_name = mkstemp(
        prefix="readiness.json.tmp-", dir=evidence_dir
    )
    temporary = Path(temporary_name)
    replaced = False
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(readiness_bytes)
            stream.flush()
            os.fsync(stream.fileno())
        staged = _json(temporary)
        if canonical_json_bytes(staged) != readiness_bytes:
            _fail("staged readiness does not reopen byte-identically")
        os.replace(temporary, readiness_path)
        replaced = True
        reopened = _json(readiness_path)
        if canonical_json_bytes(reopened) != readiness_bytes:
            _fail("published readiness does not reopen byte-identically")
    except Exception:
        if replaced:
            _restore_readiness(readiness_path, previous)
        raise
    finally:
        temporary.unlink(missing_ok=True)
    return readiness_path
