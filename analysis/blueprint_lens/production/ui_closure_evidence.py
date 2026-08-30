"""Independent verifier for the bounded M6 UI-closure visible-review packet.

The G6 review found that a packet can pin every denominator as a schema
constant and attest a row with a bare boolean. This verifier does neither: it
recomputes every denominator from the retained rows, requires an evidence
pointer on each row, and requires an explicit record for any design section 7.2
state the review could not reach.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, NoReturn, cast

from ..m6_errors import M6Error
from ..schema_validation import validate_instance
from .session_contracts import canonical_json_bytes


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas/blueprint-lens-m6-ui-closure-evidence-v1.schema.json"
_REPORT_NAME = "ui-closure-evidence.v1.json"
_WIDTHS = (430, 480, 700)
_STATES = (
    "tab_available_by_default",
    "python_auto_detected",
    "python_missing_or_invalid",
    "execution_target_lock_unlock",
    "data_member_list_with_unused_row",
    "session_lifecycle_states",
    "results_and_source_jump",
    "width_without_clipping_or_overlap",
)


def _fail(message: str, *, cause: Exception | None = None) -> NoReturn:
    raise M6Error(
        "M6_PACKET_REFERENCE_INVALID",
        message,
        phase="ui_closure_evidence",
        retryable=False,
        cause=cause,
    )


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(f"cannot hash retained evidence {path.name}: {error}", cause=error)


def _schema() -> Mapping[str, Any]:
    try:
        return json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read the UI-closure evidence schema: {error}", cause=error)


def _canonical_object(path: Path, context: str) -> dict[str, Any]:
    try:
        payload = path.read_bytes()
        value = json.loads(payload.decode("utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read {context}: {error}", cause=error)
    if not isinstance(value, dict):
        _fail(f"{context} must be an object")
    if payload != canonical_json_bytes(value):
        _fail(f"{context} is not canonical JSON")
    return cast(dict[str, Any], value)


def _resolve(root: Path, stored: str, context: str) -> Path:
    relative = PurePosixPath(stored)
    if relative.is_absolute() or any(part in {"", ".", ".."} for part in relative.parts):
        _fail(f"{context} path must be portable and relative")
    path = root.joinpath(*relative.parts).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError as error:
        _fail(f"{context} path escapes the evidence root", cause=error)
    if not path.is_file():
        _fail(f"{context} path does not resolve to a file: {stored}")
    return path


def _enumerate(root: Path) -> set[str]:
    try:
        return {
            item.relative_to(root).as_posix()
            for item in root.rglob("*")
            if item.is_file()
        }
    except OSError as error:
        _fail(f"cannot enumerate the UI-closure evidence: {error}", cause=error)


def derive_denominators(rows: list[Mapping[str, Any]]) -> dict[str, Any]:
    """Recompute every denominator from the rows themselves."""

    verdicts = [row["verdict"] for row in rows]
    return {
        "rows_total": len(rows),
        "rows_pass": sum(verdict == "PASS" for verdict in verdicts),
        "rows_accepted_deviation": sum(
            verdict == "ACCEPTED_DEVIATION" for verdict in verdicts
        ),
        "rows_open_defect": sum(verdict == "OPEN_DEFECT" for verdict in verdicts),
        "states_covered": len({row["state"] for row in rows}),
        "states_total": 8,
        "widths_covered": sorted({row["width"] for row in rows}),
    }


def verify_ui_closure_evidence(artifact_root: Path) -> Mapping[str, Any]:
    """Reopen a retained UI-closure packet and check it against its own rows."""

    root = Path(artifact_root).resolve()
    report_path = root / _REPORT_NAME
    if not report_path.is_file():
        _fail(f"the UI-closure report is missing: {_REPORT_NAME}")
    report = _canonical_object(report_path, "UI closure evidence report")
    try:
        validate_instance(report, _schema())
    except M6Error:
        raise
    except Exception as error:
        _fail(f"UI closure report violates its schema: {error}", cause=error)

    rows = cast(list[Mapping[str, Any]], report["review_rows"])
    seen: set[tuple[str, int]] = set()
    for row in rows:
        key = (row["state"], row["width"])
        if key in seen:
            _fail(f"duplicate review row: {key[0]} at {key[1]}")
        seen.add(key)
        if row["verdict"] == "ACCEPTED_DEVIATION" and not row.get("deviation_reason"):
            _fail(f"accepted deviation without a reason: {key[0]} at {key[1]}")
        if row["verdict"] != "ACCEPTED_DEVIATION" and row.get("deviation_reason"):
            _fail(f"deviation reason on a non-deviating row: {key[0]} at {key[1]}")
        evidence = _resolve(root, row["evidence_path"], "review evidence")
        pointer = row["evidence_pointer"]
        if evidence.suffix == ".json" or evidence.suffix == ".txt":
            try:
                text = evidence.read_text(encoding="utf-8", errors="replace")
            except OSError as error:
                _fail(f"cannot read review evidence {row['evidence_path']}", cause=error)
            if pointer not in text:
                _fail(
                    "evidence pointer does not appear in its artefact: "
                    f"{key[0]} at {key[1]}"
                )
        elif evidence.suffix == ".png":
            try:
                signature = evidence.read_bytes()[:8]
            except OSError as error:
                _fail(f"cannot read review screenshot {row['evidence_path']}", cause=error)
            if signature != b"\x89PNG\r\n\x1a\n":
                _fail(f"review screenshot is not a PNG: {row['evidence_path']}")
        else:
            _fail(f"unsupported evidence artefact type: {row['evidence_path']}")

    expected = derive_denominators(list(rows))
    if report["denominators"] != expected:
        _fail(
            "declared denominators disagree with the retained rows: "
            f"declared={report['denominators']} derived={expected}"
        )

    covered = {row["state"] for row in rows}
    unreachable = {item["state"] for item in report["unreachable_states"]}
    if covered & unreachable:
        _fail(f"a state is both covered and unreachable: {sorted(covered & unreachable)}")
    if covered | unreachable != set(_STATES):
        missing = sorted(set(_STATES) - covered - unreachable)
        _fail(f"design section 7.2 states are neither covered nor recorded: {missing}")

    entry = report["panel_entry"]
    if entry["review_seam_used"] and not entry["review_seam_scope"].strip():
        _fail("a review seam was used without recording what it replaced")

    records = cast(list[Mapping[str, str]], report["retained_files"])
    declared = [record["path"] for record in records]
    if declared != sorted(declared):
        _fail("retained_files must be sorted by path")
    for record in records:
        path = _resolve(root, record["path"], "retained file")
        if record["sha256"] != _sha256(path):
            _fail(f"retained file hash mismatch: {record['path']}")
    actual = _enumerate(root) - {_REPORT_NAME}
    if actual != set(declared):
        _fail(
            "retained set disagrees with the declaration: "
            f"missing={sorted(set(declared) - actual)} extra={sorted(actual - set(declared))}"
        )
    if report["counts"] != {
        "retained_files": len(records) + 1,
        "review_rows": len(rows),
    }:
        _fail("counts disagree with the retained content")
    return report


__all__ = ["derive_denominators", "verify_ui_closure_evidence"]
