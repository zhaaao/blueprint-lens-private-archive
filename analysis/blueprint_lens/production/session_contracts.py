"""Canonical request and controlled-scenario contracts for M6."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
from typing import Any, Literal, Mapping, NoReturn

from ..m6_errors import M6Error
from ..schema_validation import validate_instance


_PROJECT_ROOT = Path(__file__).resolve().parents[3]
_REQUEST_SCHEMA_PATH = (
    _PROJECT_ROOT / "schemas" / "blueprint-lens-m6-request-v1.schema.json"
)
_REGISTRY_SCHEMA_PATH = (
    _PROJECT_ROOT
    / "schemas"
    / "blueprint-lens-m6-controlled-scenarios-v1.schema.json"
)
_SHA256 = re.compile(r"[0-9a-f]{64}")
_ASSET_PATH = re.compile(
    r"/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+\.[A-Za-z0-9_]+"
)
_GUID = re.compile(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}")
_RENDERER_ID = "R1_GENERIC_FRAME_FLOW_V1"
_EXPECTED_SCENARIOS = {
    "M6-E01": {
        "query_kind": "execution",
        "nodes": 9,
        "relations": 10,
        "truth_sha256": (
            "f8a45bba980aa6966f492bd3a155a62c5ff9067cfaa56bd45fe0d488622c696a"
        ),
    },
    "M6-D01": {
        "query_kind": "data",
        "nodes": 7,
        "relations": 6,
        "truth_sha256": (
            "ce1fa1f7a080216f472ca4563426e1101ab463da131c8e74589790e200504178"
        ),
    },
}


@dataclass(frozen=True, slots=True)
class ExecutionSessionCriterion:
    kind: Literal["execution"]
    graph_id: str
    criterion_node_id: str
    direction: Literal["backward"]


@dataclass(frozen=True, slots=True)
class DataSessionCriterion:
    kind: Literal["data"]
    graph_id: str
    member_guid: str
    expected_member_name: str
    direction: Literal["backward"]


@dataclass(frozen=True, slots=True)
class SemanticBudget:
    max_selected_nodes: int
    max_selected_relations: int


@dataclass(frozen=True, slots=True)
class PresentationBudget:
    max_visible_entities: int
    max_visible_relations: int


@dataclass(frozen=True, slots=True)
class SessionRequest:
    schema_version: Literal["1.0.0"]
    asset_path: str
    graph_id: str
    source_fingerprint: str
    query_kind: Literal["execution", "data"]
    criterion: ExecutionSessionCriterion | DataSessionCriterion
    semantic_budget: SemanticBudget
    presentation_budget: PresentationBudget
    raw_version: Literal["0.2"]
    typed_ir_version: Literal["1.0.0"]
    slice_rules_version: Literal["1.0.0"]
    renderer_id: Literal["R1_GENERIC_FRAME_FLOW_V1"]


@dataclass(frozen=True, slots=True)
class ControlledScenario:
    scenario_id: Literal["M6-E01", "M6-D01"]
    request: SessionRequest
    truth_path: str
    truth_sha256: str
    expected_node_count: int
    expected_relation_count: int


def canonical_json_bytes(value: object) -> bytes:
    """Return the project canonical compact JSON encoding."""

    return (
        json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        )
        + "\n"
    ).encode("utf-8")


def _fail(message: str, *, cause: Exception | None = None) -> NoReturn:
    raise M6Error(
        "M6_PRECONDITION_QUERY_INVALID",
        message,
        phase="preflight",
        retryable=False,
        cause=cause,
    )


def _object(value: Any, context: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        _fail(f"{context} must be an object")
    return value


def _string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(f"{context} must be a non-empty string")
    return value


def _integer(value: Any, context: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 1:
        _fail(f"{context} must be a positive integer")
    return value


def _load_object(path: Path, context: str) -> Mapping[str, Any]:
    try:
        raw = path.read_bytes()
        if raw.startswith(b"\xef\xbb\xbf"):
            _fail(f"{context} must not contain a UTF-8 BOM")
        return _object(json.loads(raw.decode("utf-8")), context)
    except M6Error:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(f"cannot read {context}: {error}", cause=error)


def _schema(path: Path, context: str) -> Mapping[str, Any]:
    return _load_object(path, context)


def _validate(value: Mapping[str, Any], schema_path: Path, context: str) -> None:
    try:
        validate_instance(value, _schema(schema_path, f"{context} schema"))
    except M6Error:
        raise
    except Exception as error:
        _fail(f"{context} violates its schema: {error}", cause=error)


def _portable_relative_path(value: Any, context: str) -> str:
    result = _string(value, context)
    posix = PurePosixPath(result)
    windows = PureWindowsPath(result)
    if (
        "\\" in result
        or posix.is_absolute()
        or windows.is_absolute()
        or bool(windows.drive)
        or any(part in {"", ".", ".."} for part in posix.parts)
    ):
        _fail(f"{context} must be a portable project-relative path")
    return result


def _request_from_value(value: Mapping[str, Any]) -> SessionRequest:
    _validate(value, _REQUEST_SCHEMA_PATH, "M6 request")
    asset_path = _string(value.get("asset_path"), "asset_path")
    if _ASSET_PATH.fullmatch(asset_path) is None or ".." in asset_path:
        _fail("asset_path must be a portable /Game object path")
    graph_id = _string(value.get("graph_id"), "graph_id")
    if not graph_id.startswith(f"{asset_path}:") or graph_id == f"{asset_path}:":
        _fail("graph_id must belong to asset_path")
    source_fingerprint = _string(
        value.get("source_fingerprint"), "source_fingerprint"
    )
    if _SHA256.fullmatch(source_fingerprint) is None:
        _fail("source_fingerprint must be a lowercase SHA-256")

    query_kind = _string(value.get("query_kind"), "query_kind")
    criterion_value = _object(value.get("criterion"), "criterion")
    criterion_kind = _string(criterion_value.get("kind"), "criterion.kind")
    criterion_graph = _string(
        criterion_value.get("graph_id"), "criterion.graph_id"
    )
    if criterion_kind != query_kind or criterion_graph != graph_id:
        _fail("criterion kind/graph must agree with the request")
    if query_kind == "execution":
        node_id = _string(
            criterion_value.get("criterion_node_id"),
            "criterion.criterion_node_id",
        )
        if not node_id.startswith(f"{graph_id}::node::"):
            _fail("execution criterion node must belong to graph_id")
        criterion: ExecutionSessionCriterion | DataSessionCriterion = (
            ExecutionSessionCriterion(
                kind="execution",
                graph_id=graph_id,
                criterion_node_id=node_id,
                direction="backward",
            )
        )
    elif query_kind == "data":
        member_guid = _string(
            criterion_value.get("member_guid"), "criterion.member_guid"
        )
        if _GUID.fullmatch(member_guid) is None:
            _fail("criterion.member_guid must be a lowercase GUID")
        criterion = DataSessionCriterion(
            kind="data",
            graph_id=graph_id,
            member_guid=member_guid,
            expected_member_name=_string(
                criterion_value.get("expected_member_name"),
                "criterion.expected_member_name",
            ),
            direction="backward",
        )
    else:
        _fail(f"unsupported query kind: {query_kind}")

    semantic = _object(value.get("semantic_budget"), "semantic_budget")
    presentation = _object(
        value.get("presentation_budget"), "presentation_budget"
    )
    renderer = _string(value.get("renderer_id"), "renderer_id")
    if renderer != _RENDERER_ID:
        _fail(f"unsupported renderer_id: {renderer}")
    return SessionRequest(
        schema_version="1.0.0",
        asset_path=asset_path,
        graph_id=graph_id,
        source_fingerprint=source_fingerprint,
        query_kind=query_kind,  # type: ignore[arg-type]
        criterion=criterion,
        semantic_budget=SemanticBudget(
            max_selected_nodes=_integer(
                semantic.get("max_selected_nodes"),
                "semantic_budget.max_selected_nodes",
            ),
            max_selected_relations=_integer(
                semantic.get("max_selected_relations"),
                "semantic_budget.max_selected_relations",
            ),
        ),
        presentation_budget=PresentationBudget(
            max_visible_entities=_integer(
                presentation.get("max_visible_entities"),
                "presentation_budget.max_visible_entities",
            ),
            max_visible_relations=_integer(
                presentation.get("max_visible_relations"),
                "presentation_budget.max_visible_relations",
            ),
        ),
        raw_version="0.2",
        typed_ir_version="1.0.0",
        slice_rules_version="1.0.0",
        renderer_id="R1_GENERIC_FRAME_FLOW_V1",
    )


def load_session_request(path: str | Path) -> SessionRequest:
    """Load one strict M6 request union from canonical JSON."""

    source = Path(path)
    value = _load_object(source, "M6 request")
    try:
        if source.read_bytes() != canonical_json_bytes(value):
            _fail("M6 request is not canonical JSON")
    except OSError as error:
        _fail(f"cannot reopen M6 request: {error}", cause=error)
    return _request_from_value(value)


def _sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(f"cannot hash controlled truth {path}", cause=error)


def load_controlled_scenarios(path: str | Path) -> tuple[ControlledScenario, ...]:
    """Load the exact two-scenario bounded G6 registry."""

    value = _load_object(Path(path), "M6 controlled scenario registry")
    _validate(value, _REGISTRY_SCHEMA_PATH, "M6 controlled scenario registry")
    rows = value.get("scenarios")
    if not isinstance(rows, list):
        _fail("scenarios must be an array")
    indexed: dict[str, ControlledScenario] = {}
    for position, raw_row in enumerate(rows):
        row = _object(raw_row, f"scenarios[{position}]")
        scenario_id = _string(row.get("scenario_id"), "scenario_id")
        expected_contract = _EXPECTED_SCENARIOS.get(scenario_id)
        if expected_contract is None or scenario_id in indexed:
            _fail(f"invalid or duplicate controlled scenario: {scenario_id}")
        request = _request_from_value(_object(row.get("request"), "request"))
        truth = _object(row.get("ground_truth"), "ground_truth")
        truth_path = _portable_relative_path(truth.get("path"), "ground_truth.path")
        truth_sha = _string(truth.get("sha256"), "ground_truth.sha256")
        truth_file = (_PROJECT_ROOT / Path(*PurePosixPath(truth_path).parts)).resolve()
        try:
            truth_file.relative_to(_PROJECT_ROOT.resolve())
        except ValueError as error:
            _fail("ground_truth.path escapes the project root", cause=error)
        if not truth_file.is_file() or _sha256(truth_file) != truth_sha:
            _fail(f"{scenario_id} ground-truth SHA-256 mismatch")
        expected = _object(row.get("expected"), "expected")
        node_count = _integer(expected.get("node_count"), "expected.node_count")
        relation_count = _integer(
            expected.get("relation_count"), "expected.relation_count"
        )
        if (
            request.query_kind != expected_contract["query_kind"]
            or node_count != expected_contract["nodes"]
            or relation_count != expected_contract["relations"]
            or truth_sha != expected_contract["truth_sha256"]
        ):
            _fail(f"{scenario_id} disagrees with the approved controlled contract")
        indexed[scenario_id] = ControlledScenario(
            scenario_id=scenario_id,  # type: ignore[arg-type]
            request=request,
            truth_path=truth_path,
            truth_sha256=truth_sha,
            expected_node_count=node_count,
            expected_relation_count=relation_count,
        )
    if set(indexed) != set(_EXPECTED_SCENARIOS):
        _fail("controlled scenarios must be exactly M6-E01 and M6-D01")
    return tuple(indexed[key] for key in sorted(indexed))
