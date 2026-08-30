"""Append-only, privacy-filtered and replayable M6 engineering telemetry."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import hashlib
import json
import math
import os
from pathlib import Path
import re
from typing import Any, Literal, Mapping, NoReturn, cast

from ..m6_errors import M6_ERROR_CODES, M6Error
from ..schema_validation import validate_instance
from .session_contracts import canonical_json_bytes


_ROOT = Path(__file__).resolve().parents[3]
_SCHEMA_PATH = _ROOT / "schemas/blueprint-lens-m6-telemetry-event-v1.schema.json"
REQUIRED_STAGES = (
    "request",
    "preflight",
    "export",
    "typed_document",
    "slice",
    "explanation",
    "baseline_projection",
    "layout",
    "render",
    "packet",
    "reset",
)
_COUNT_KEYS = frozenset(
    {
        "nodes",
        "relations",
        "edges",
        "sccs",
        "collapses",
        "fallbacks",
        "opaque",
        "uncertain",
        "unsupported",
        "truncated",
        "error_reasons",
    }
)
_PRIVATE_KEYS = frozenset(
    {
        "absolute_path",
        "cwd",
        "home",
        "hostname",
        "machine",
        "machine_name",
        "path",
        "user",
        "user_name",
        "username",
        "workspace",
    }
)
_WINDOWS_ABSOLUTE = re.compile(r"^[A-Za-z]:[\\/]")


@dataclass(frozen=True, slots=True)
class ReplayedSessionState:
    baseline_id: Literal["A", "B", "C"]
    selected_entity_id: str | None
    expanded_entity_ids: tuple[str, ...]
    reset: bool


def _fail(
    code: str,
    message: str,
    *,
    cause: Exception | None = None,
) -> NoReturn:
    raise M6Error(
        code,
        message,
        phase="telemetry",
        retryable=False,
        cause=cause,
    )


def _load_schema() -> Mapping[str, Any]:
    try:
        value = json.loads(_SCHEMA_PATH.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"cannot read telemetry schema: {error}",
            cause=error,
        )
    if not isinstance(value, dict):
        _fail("M6_TELEMETRY_SCHEMA_INVALID", "telemetry schema must be an object")
    return value


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        _fail("M6_TELEMETRY_SCHEMA_INVALID", f"{context} must be an object")
    return cast(dict[str, Any], value)


def _private_value(value: Any) -> bool:
    if isinstance(value, Mapping):
        for key, child in value.items():
            if str(key).casefold() in _PRIVATE_KEYS or _private_value(child):
                return True
        return False
    if isinstance(value, list):
        return any(_private_value(item) for item in value)
    if not isinstance(value, str):
        return False
    return bool(
        _WINDOWS_ABSOLUTE.match(value)
        or value.startswith("\\\\")
        or value.startswith("file://")
        or (value.startswith("/") and not value.startswith("/Game/"))
    )


def _exact_keys(value: Mapping[str, Any], expected: set[str], context: str) -> None:
    if set(value) != expected:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"{context} fields must be exactly {sorted(expected)}",
        )


def _validate_counts(value: Any, *, require_complete: bool) -> None:
    counts = _object(value, "stage counts")
    keys = set(counts)
    if not keys <= _COUNT_KEYS or (require_complete and keys != _COUNT_KEYS):
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            "stage count vocabulary is incomplete or contains unknown fields",
        )
    if any(
        not isinstance(item, int) or isinstance(item, bool) or item < 0
        for item in counts.values()
    ):
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            "stage counts must be non-negative integers",
        )


def _validate_result(value: Mapping[str, Any], allowed: set[str]) -> str:
    status = value.get("status")
    error_code = value.get("error_code")
    if status not in allowed or not isinstance(error_code, str):
        _fail("M6_TELEMETRY_SCHEMA_INVALID", "event result is invalid")
    if status == "error":
        if error_code not in M6_ERROR_CODES:
            _fail(
                "M6_TELEMETRY_SCHEMA_INVALID",
                "error result requires a registered M6 error code",
            )
    elif error_code:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            "non-error result must have an empty error code",
        )
    return cast(str, status)


def _validate_event_semantics(event: Mapping[str, Any], *, internal: bool = False) -> None:
    event_type = event["event_type"]
    payload = _object(event["payload"], "event payload")
    result = _object(event["result"], "event result")
    if _private_value(event):
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            "telemetry contains a private key or absolute filesystem path",
        )
    if event_type == "stage_started":
        _exact_keys(payload, {"stage"}, "stage_started payload")
        if payload["stage"] not in REQUIRED_STAGES:
            _fail("M6_TELEMETRY_SCHEMA_INVALID", "unknown telemetry stage")
        _validate_result(result, {"ok"})
    elif event_type == "stage_result":
        _exact_keys(
            payload,
            {"stage", "applicability", "counts"},
            "stage_result payload",
        )
        stage = payload["stage"]
        applicability = payload["applicability"]
        if stage not in REQUIRED_STAGES or applicability not in {
            "applicable",
            "inapplicable",
        }:
            _fail("M6_TELEMETRY_SCHEMA_INVALID", "invalid stage result binding")
        _validate_counts(
            payload["counts"],
            require_complete=stage == "baseline_projection" and applicability == "applicable",
        )
        status = _validate_result(result, {"ok", "error", "inapplicable"})
        if (applicability == "inapplicable") != (status == "inapplicable"):
            _fail(
                "M6_TELEMETRY_SCHEMA_INVALID",
                "stage applicability and result status disagree",
            )
    elif event_type == "baseline_changed":
        _exact_keys(payload, {"baseline_id"}, "baseline_changed payload")
        if payload["baseline_id"] not in {"A", "B", "C"}:
            _fail("M6_TELEMETRY_SCHEMA_INVALID", "invalid baseline ID")
        _validate_result(result, {"ok"})
    elif event_type in {
        "entity_selected",
        "entity_expanded",
        "entity_collapsed",
        "source_jump",
    }:
        _exact_keys(payload, {"entity_id"}, f"{event_type} payload")
        if not isinstance(payload["entity_id"], str) or not payload["entity_id"]:
            _fail("M6_TELEMETRY_SCHEMA_INVALID", "entity ID must be non-empty")
        _validate_result(result, {"ok", "error"} if event_type == "source_jump" else {"ok"})
    elif event_type in {"reset", "replay"}:
        _exact_keys(payload, set(), f"{event_type} payload")
        _validate_result(result, {"ok"})
    elif event_type == "record_sealed":
        if not internal:
            _fail(
                "M6_TELEMETRY_SCHEMA_INVALID",
                "record_sealed may only be produced by seal_telemetry_record",
            )
        _exact_keys(payload, {"event_count", "prior_sha256"}, "seal payload")
        if (
            not isinstance(payload["event_count"], int)
            or payload["event_count"] < 1
            or not isinstance(payload["prior_sha256"], str)
            or re.fullmatch(r"[0-9a-f]{64}", payload["prior_sha256"]) is None
        ):
            _fail("M6_TELEMETRY_SCHEMA_INVALID", "seal payload is invalid")
        if set(result) != {"status", "error_code", "final_state"}:
            _fail("M6_TELEMETRY_SCHEMA_INVALID", "seal result fields are invalid")
        _validate_result(result, {"sealed"})
        _state_from_value(result["final_state"])


def _validate_event(event: Mapping[str, Any], *, internal: bool = False) -> None:
    try:
        validate_instance(event, _load_schema())
    except M6Error:
        raise
    except Exception as error:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"telemetry event violates its schema: {error}",
            cause=error,
        )
    _validate_event_semantics(event, internal=internal)


def _read_rows(path: Path) -> tuple[list[dict[str, Any]], list[bytes]]:
    try:
        payload = path.read_bytes()
    except OSError as error:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"cannot read telemetry record: {error}",
            cause=error,
        )
    if not payload or payload.startswith(b"\xef\xbb\xbf"):
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            "telemetry record must be non-empty UTF-8 without BOM",
        )
    lines = payload.splitlines(keepends=True)
    if not lines or any(not line.endswith(b"\n") for line in lines):
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            "each telemetry event must occupy one complete LF-terminated line",
        )
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(lines, start=1):
        try:
            row = _object(json.loads(line.decode("utf-8")), f"event line {index}")
        except M6Error:
            raise
        except (UnicodeError, json.JSONDecodeError) as error:
            _fail(
                "M6_TELEMETRY_SCHEMA_INVALID",
                f"invalid telemetry JSON at line {index}: {error}",
                cause=error,
            )
        if line != canonical_json_bytes(row):
            _fail(
                "M6_TELEMETRY_SCHEMA_INVALID",
                f"telemetry line {index} is not canonical JSON",
            )
        _validate_event(row, internal=row.get("event_type") == "record_sealed")
        rows.append(row)
    _validate_sequence(rows)
    return rows, lines


def _validate_sequence(rows: list[Mapping[str, Any]]) -> None:
    if [row["sequence"] for row in rows] != list(range(1, len(rows) + 1)):
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "telemetry sequences must be gap-free and start at one",
        )
    run_ids = {row["run_id"] for row in rows}
    semantic_hashes = {row["semantic_sha256"] for row in rows}
    if len(run_ids) != 1 or len(semantic_hashes) != 1:
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "telemetry run ID and semantic hash must remain stable",
        )
    sealed_positions = [
        index for index, row in enumerate(rows) if row["event_type"] == "record_sealed"
    ]
    if sealed_positions and sealed_positions != [len(rows) - 1]:
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "record_sealed must occur exactly once and last",
        )


def _validate_required_stages(rows: list[Mapping[str, Any]]) -> None:
    results = [row for row in rows if row["event_type"] == "stage_result"]
    if [row["payload"]["stage"] for row in results] != list(REQUIRED_STAGES):
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "required stage results must appear exactly once in declared order",
        )
    starts = [row for row in rows if row["event_type"] == "stage_started"]
    started_stages = [row["payload"]["stage"] for row in starts]
    if len(started_stages) != len(set(started_stages)):
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "stage_started events must be unique",
        )
    for result in results:
        stage = result["payload"]["stage"]
        applicable = result["payload"]["applicability"] == "applicable"
        if applicable != (stage in started_stages):
            _fail(
                "M6_TELEMETRY_SEQUENCE_INVALID",
                "applicable stages require one start; inapplicable stages require none",
            )
        if applicable:
            start_sequence = next(
                row["sequence"]
                for row in starts
                if row["payload"]["stage"] == stage
            )
            if start_sequence >= result["sequence"]:
                _fail(
                    "M6_TELEMETRY_SEQUENCE_INVALID",
                    "stage result must follow its start",
                )


def append_telemetry_event(path: Path, event: Mapping[str, object]) -> None:
    """Validate and durably append one complete canonical telemetry event."""

    path = Path(path)
    row = _object(dict(event), "telemetry event")
    sequence = row.get("sequence")
    if not isinstance(sequence, int) or isinstance(sequence, bool) or sequence < 1:
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "telemetry sequence must be a positive integer",
        )
    _validate_event(row)
    if path.exists():
        rows, _ = _read_rows(path)
        if rows[-1]["event_type"] == "record_sealed":
            _fail(
                "M6_TELEMETRY_SEQUENCE_INVALID",
                "cannot append after record_sealed",
            )
        if row["sequence"] != len(rows) + 1:
            _fail(
                "M6_TELEMETRY_SEQUENCE_INVALID",
                "next telemetry sequence is not contiguous",
            )
        if (
            row["run_id"] != rows[0]["run_id"]
            or row["semantic_sha256"] != rows[0]["semantic_sha256"]
        ):
            _fail(
                "M6_TELEMETRY_SEQUENCE_INVALID",
                "appended event changed run ID or semantic hash",
            )
    elif row["sequence"] != 1:
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "first telemetry sequence must be one",
        )
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("ab") as handle:
            handle.write(canonical_json_bytes(row))
            handle.flush()
            os.fsync(handle.fileno())
    except OSError as error:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"cannot append telemetry event: {error}",
            cause=error,
        )


def _replay_rows(rows: list[Mapping[str, Any]]) -> ReplayedSessionState:
    baseline = "A"
    selected: str | None = None
    expanded: set[str] = set()
    reset = False
    for row in rows:
        event_type = row["event_type"]
        payload = row["payload"]
        if event_type == "baseline_changed":
            baseline = payload["baseline_id"]
            reset = False
        elif event_type == "entity_selected":
            selected = payload["entity_id"]
            reset = False
        elif event_type == "entity_expanded":
            expanded.add(payload["entity_id"])
            reset = False
        elif event_type == "entity_collapsed":
            expanded.discard(payload["entity_id"])
            reset = False
        elif event_type == "reset":
            baseline = "A"
            selected = None
            expanded.clear()
            reset = True
    return ReplayedSessionState(
        baseline_id=cast(Literal["A", "B", "C"], baseline),
        selected_entity_id=selected,
        expanded_entity_ids=tuple(sorted(expanded)),
        reset=reset,
    )


def _state_from_value(value: Any) -> ReplayedSessionState:
    state = _object(value, "replayed final state")
    _exact_keys(
        state,
        {"baseline_id", "selected_entity_id", "expanded_entity_ids", "reset"},
        "replayed final state",
    )
    baseline = state["baseline_id"]
    selected = state["selected_entity_id"]
    expanded = state["expanded_entity_ids"]
    reset = state["reset"]
    if (
        baseline not in {"A", "B", "C"}
        or (selected is not None and (not isinstance(selected, str) or not selected))
        or not isinstance(expanded, list)
        or any(not isinstance(item, str) or not item for item in expanded)
        or expanded != sorted(set(expanded))
        or not isinstance(reset, bool)
    ):
        _fail("M6_TELEMETRY_SCHEMA_INVALID", "replayed final state is invalid")
    return ReplayedSessionState(
        baseline_id=cast(Literal["A", "B", "C"], baseline),
        selected_entity_id=cast(str | None, selected),
        expanded_entity_ids=tuple(cast(list[str], expanded)),
        reset=reset,
    )


def seal_telemetry_record(path: Path) -> str:
    """Validate required stages and append one hash-binding seal event."""

    path = Path(path)
    rows, lines = _read_rows(path)
    if rows[-1]["event_type"] == "record_sealed":
        _fail("M6_TELEMETRY_SEQUENCE_INVALID", "telemetry record is already sealed")
    _validate_required_stages(rows)
    digest = hashlib.sha256(b"".join(lines)).hexdigest()
    final_state = _replay_rows(rows)
    final_state_value = asdict(final_state)
    final_state_value["expanded_entity_ids"] = list(final_state.expanded_entity_ids)
    seal = {
        "schema_version": "1.0.0",
        "run_id": rows[0]["run_id"],
        "semantic_sha256": rows[0]["semantic_sha256"],
        "sequence": len(rows) + 1,
        "phase": "seal",
        "event_type": "record_sealed",
        "payload": {"event_count": len(rows), "prior_sha256": digest},
        "result": {
            "status": "sealed",
            "error_code": "",
            "final_state": final_state_value,
        },
    }
    _validate_event(seal, internal=True)
    try:
        with path.open("ab") as handle:
            handle.write(canonical_json_bytes(seal))
            handle.flush()
            os.fsync(handle.fileno())
    except OSError as error:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"cannot seal telemetry record: {error}",
            cause=error,
        )
    return digest


def _validated_rows(
    path: Path,
    *,
    semantic_sha256: str | None,
    require_sealed: bool,
) -> list[dict[str, Any]]:
    rows, lines = _read_rows(Path(path))
    if semantic_sha256 is not None and rows[0]["semantic_sha256"] != semantic_sha256:
        _fail(
            "M6_TELEMETRY_SEQUENCE_INVALID",
            "telemetry semantic hash does not match the Session",
        )
    sealed = rows[-1]["event_type"] == "record_sealed"
    if require_sealed and not sealed:
        _fail("M6_TELEMETRY_SEQUENCE_INVALID", "telemetry record is not sealed")
    if sealed:
        seal = rows[-1]
        prior_rows = rows[:-1]
        prior_lines = lines[:-1]
        expected = hashlib.sha256(b"".join(prior_lines)).hexdigest()
        if (
            seal["payload"]["prior_sha256"] != expected
            or seal["payload"]["event_count"] != len(prior_rows)
        ):
            _fail(
                "M6_TELEMETRY_REPLAY_MISMATCH",
                "telemetry seal does not bind the preceding canonical events",
            )
        _validate_required_stages(prior_rows)
        actual_state = _replay_rows(prior_rows)
        declared_state = _state_from_value(seal["result"]["final_state"])
        if actual_state != declared_state:
            _fail(
                "M6_TELEMETRY_REPLAY_MISMATCH",
                "sealed final state disagrees with pure replay",
            )
    return rows


def validate_telemetry_record(
    path: Path,
    semantic_sha256: str,
    *,
    profile: Literal["retained", "closure"] = "retained",
) -> None:
    """Validate one sealed record, including prior-line hash and final replay."""

    rows = _validated_rows(
        Path(path),
        semantic_sha256=semantic_sha256,
        require_sealed=True,
    )
    if profile not in {"retained", "closure"}:
        _fail(
            "M6_TELEMETRY_SCHEMA_INVALID",
            f"unknown telemetry validation profile: {profile}",
        )
    if profile == "closure":
        for row in rows:
            if row["event_type"] != "stage_result":
                continue
            duration = row.get("duration_ms")
            if (
                not isinstance(duration, (int, float))
                or isinstance(duration, bool)
                or not math.isfinite(float(duration))
                or duration < 0
            ):
                _fail(
                    "M6_TELEMETRY_SCHEMA_INVALID",
                    "closure telemetry stage results require non-negative duration_ms",
                )


def replay_telemetry_record(
    path: Path,
    *,
    require_sealed: bool = True,
) -> ReplayedSessionState:
    """Purely replay semantic UI state from ordered telemetry payloads."""

    rows = _validated_rows(
        Path(path),
        semantic_sha256=None,
        require_sealed=require_sealed,
    )
    if rows[-1]["event_type"] == "record_sealed":
        rows = rows[:-1]
    return _replay_rows(rows)
