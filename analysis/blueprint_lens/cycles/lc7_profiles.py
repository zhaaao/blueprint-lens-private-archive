"""Validate and canonicalize the bounded LC7 static source-visible SCC."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from typing import Any, Mapping

from ..raw_probe import BlueprintDocument, Graph, load_raw_probe


PROFILE_ID = "LC7_STATIC_SINGLE_ENTRY_EXIT_SCC_V1"
CLAIM_SCOPE = "STATIC_SOURCE_VISIBLE_SCC"

FIXTURE_SHAPE_INVALID = "LC7_FIXTURE_SHAPE_INVALID"
SOURCE_AUDIT_MISMATCH = "LC7_SOURCE_AUDIT_MISMATCH"
SCC_MEMBERSHIP_INVALID = "LC7_SCC_MEMBERSHIP_INVALID"
SCC_EDGE_OWNERSHIP_INVALID = "LC7_SCC_EDGE_OWNERSHIP_INVALID"
SCC_BOUNDARY_INVALID = "LC7_SCC_BOUNDARY_INVALID"
RUNTIME_CLAIM_INVALID = "LC7_RUNTIME_CLAIM_INVALID"

_HASH = re.compile(r"[0-9a-f]{64}")
_BINDING_HASH_FIELDS = (
    "asset_sha256",
    "raw_sha256",
    "source_sha256",
    "audit_sha256",
)
_BINDING_PATH_FIELDS = (
    "asset_path",
    "raw_path",
    "source_path",
    "audit_path",
)


class LC7ProfileError(ValueError):
    """Fail-closed LC7 profile diagnostic with a stable machine code."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


@dataclass(frozen=True, slots=True)
class SourceAudit:
    asset_path: str
    graph_id: str
    member_node_ids: tuple[str, ...]
    internal_edge_ids: tuple[str, ...]
    incoming_edge_ids: tuple[str, ...]
    outgoing_edge_ids: tuple[str, ...]
    entry_node_id: str
    exit_node_id: str
    criterion_node_id: str


@dataclass(frozen=True, slots=True)
class _AuditDetails:
    public: SourceAudit
    asset_sha256: str
    raw_sha256: str
    returning_edge_ids: tuple[str, ...]
    node_rows: tuple[Mapping[str, str], ...]
    pin_rows: tuple[Mapping[str, str], ...]
    edge_rows: tuple[Mapping[str, str], ...]


def _error(code: str, message: str) -> LC7ProfileError:
    return LC7ProfileError(code, message)


def _require_columns(parts: list[str], count: int, context: str) -> None:
    if len(parts) != count:
        raise _error(
            SOURCE_AUDIT_MISMATCH,
            f"{context} has {len(parts)} columns instead of {count}",
        )


def _single_row(
    rows: dict[str, list[list[str]]], kind: str, columns: int
) -> list[str]:
    matches = rows.get(kind, [])
    if len(matches) != 1:
        raise _error(SOURCE_AUDIT_MISMATCH, f"audit requires one {kind} row")
    _require_columns(matches[0], columns, kind)
    return matches[0]


def _unique_row_values(
    rows: dict[str, list[list[str]]], kind: str
) -> tuple[str, ...]:
    values: list[str] = []
    for index, parts in enumerate(rows.get(kind, []), start=1):
        _require_columns(parts, 2, f"{kind} row {index}")
        values.append(parts[1])
    if len(values) != len(set(values)):
        raise _error(SOURCE_AUDIT_MISMATCH, f"audit {kind} identity is duplicated")
    return tuple(sorted(values))


def _parse_audit(text: str) -> _AuditDetails:
    allowed = {
        "FORMAT",
        "BLUEPRINT",
        "COMPILE",
        "CRITERION",
        "NODE",
        "PIN",
        "EDGE",
        "SCC_MEMBER",
        "SCC_INTERNAL",
        "SCC_INCOMING",
        "SCC_OUTGOING",
        "SCC_RETURN",
        "SCC_ENTRY",
        "SCC_EXIT",
        "COUNTS",
    }
    rows: dict[str, list[list[str]]] = defaultdict(list)
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line:
            continue
        parts = line.split("\t")
        if parts[0] not in allowed:
            raise _error(
                SOURCE_AUDIT_MISMATCH,
                f"unknown audit row {parts[0]} at line {line_number}",
            )
        rows[parts[0]].append(parts)

    if _single_row(rows, "FORMAT", 3)[1:] != [
        "blueprint-lens-lc7-static-scc-audit",
        "1.0.0",
    ]:
        raise _error(SOURCE_AUDIT_MISMATCH, "audit format tuple is invalid")
    blueprint = _single_row(rows, "BLUEPRINT", 3)
    compile_row = _single_row(rows, "COMPILE", 6)
    criterion = _single_row(rows, "CRITERION", 2)[1]
    entry = _single_row(rows, "SCC_ENTRY", 2)[1]
    exit_node = _single_row(rows, "SCC_EXIT", 2)[1]
    counts_row = _single_row(rows, "COUNTS", 7)
    if (
        compile_row[1] != "up_to_date"
        or not compile_row[2]
        or not compile_row[3]
        or _HASH.fullmatch(compile_row[4]) is None
        or _HASH.fullmatch(compile_row[5]) is None
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit compile provenance is invalid")
    try:
        counts = tuple(int(value) for value in counts_row[1:])
    except ValueError as error:
        raise _error(SOURCE_AUDIT_MISMATCH, "audit counts are not integers") from error

    node_rows: list[dict[str, str]] = []
    pin_rows: list[dict[str, str]] = []
    edge_rows: list[dict[str, str]] = []
    for index, parts in enumerate(rows.get("NODE", []), start=1):
        _require_columns(parts, 5, f"NODE row {index}")
        node_rows.append(
            {"id": parts[1], "native_guid": parts[2], "class": parts[3]}
        )
    for index, parts in enumerate(rows.get("PIN", []), start=1):
        _require_columns(parts, 6, f"PIN row {index}")
        pin_rows.append(
            {
                "id": parts[1],
                "node_id": parts[2],
                "name": parts[3],
                "direction": parts[4],
                "type_category": parts[5],
            }
        )
    for index, parts in enumerate(rows.get("EDGE", []), start=1):
        _require_columns(parts, 5, f"EDGE row {index}")
        edge_rows.append(
            {
                "id": parts[1],
                "source_node_id": parts[2],
                "target_node_id": parts[3],
                "kind": parts[4],
            }
        )
    for name, values in (
        ("node", [row["id"] for row in node_rows]),
        ("pin", [row["id"] for row in pin_rows]),
        ("edge", [row["id"] for row in edge_rows]),
    ):
        if len(values) != len(set(values)):
            raise _error(SOURCE_AUDIT_MISMATCH, f"audit {name} identity is duplicated")

    members = _unique_row_values(rows, "SCC_MEMBER")
    internal = _unique_row_values(rows, "SCC_INTERNAL")
    incoming = _unique_row_values(rows, "SCC_INCOMING")
    outgoing = _unique_row_values(rows, "SCC_OUTGOING")
    returning = _unique_row_values(rows, "SCC_RETURN")
    observed_counts = (
        len(node_rows),
        len(edge_rows),
        len(members),
        len(internal),
        len(incoming),
        len(outgoing),
    )
    if counts != observed_counts or counts != (10, 10, 3, 3, 1, 1):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit counts do not match 10/10/3/3/1/1")
    if len(returning) != 1:
        raise _error(SOURCE_AUDIT_MISMATCH, "audit requires one returning edge")

    node_ids = {row["id"] for row in node_rows}
    edge_ids = {row["id"] for row in edge_rows}
    if (
        not blueprint[1]
        or not blueprint[2]
        or criterion not in node_ids
        or entry not in node_ids
        or exit_node not in node_ids
        or set(members) - node_ids
        or (set(internal) | set(incoming) | set(outgoing) | set(returning)) - edge_ids
        or any(row["node_id"] not in node_ids for row in pin_rows)
        or any(
            row["source_node_id"] not in node_ids
            or row["target_node_id"] not in node_ids
            for row in edge_rows
        )
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit contains unresolved identity")

    public = SourceAudit(
        asset_path=blueprint[1],
        graph_id=blueprint[2],
        member_node_ids=members,
        internal_edge_ids=internal,
        incoming_edge_ids=incoming,
        outgoing_edge_ids=outgoing,
        entry_node_id=entry,
        exit_node_id=exit_node,
        criterion_node_id=criterion,
    )
    return _AuditDetails(
        public=public,
        asset_sha256=compile_row[4],
        raw_sha256=compile_row[5],
        returning_edge_ids=returning,
        node_rows=tuple(sorted(node_rows, key=lambda row: row["id"])),
        pin_rows=tuple(sorted(pin_rows, key=lambda row: row["id"])),
        edge_rows=tuple(sorted(edge_rows, key=lambda row: row["id"])),
    )


def parse_scc_audit(text: str) -> SourceAudit:
    """Parse the independent native audit under its strict row contract."""

    return _parse_audit(text).public


def _file_sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        raise _error(SOURCE_AUDIT_MISMATCH, f"cannot read binding path {path}") from error


def _binding(
    source_binding: Mapping[str, Any],
    source: Mapping[str, Any],
    audit_text: str,
) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for path_field, hash_field in zip(
        _BINDING_PATH_FIELDS, _BINDING_HASH_FIELDS, strict=True
    ):
        value = source_binding.get(hash_field)
        if not isinstance(value, str) or _HASH.fullmatch(value) is None:
            raise _error(SOURCE_AUDIT_MISMATCH, f"{hash_field} is not SHA-256")
        path_value = source_binding.get(path_field)
        if not isinstance(path_value, (str, Path)):
            raise _error(SOURCE_AUDIT_MISMATCH, f"{path_field} is required")
        path = Path(path_value)
        if _file_sha256(path) != value:
            raise _error(SOURCE_AUDIT_MISMATCH, f"{hash_field} differs from bound bytes")
        result[hash_field] = value

    try:
        bound_source = json.loads(Path(source_binding["source_path"]).read_text(encoding="utf-8"))
        bound_audit = Path(source_binding["audit_path"]).read_text(encoding="utf-8")
        bound_document = load_raw_probe(source_binding["raw_path"])
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as error:
        raise _error(SOURCE_AUDIT_MISMATCH, "bound source products are unreadable") from error
    if bound_audit != audit_text:
        raise _error(SOURCE_AUDIT_MISMATCH, "audit text differs from bound audit bytes")
    if not isinstance(bound_source, Mapping):
        raise _error(SOURCE_AUDIT_MISMATCH, "bound source is not an object")
    result["_bound_source"] = bound_source
    result["_bound_document"] = bound_document
    return result


def _graph(document: BlueprintDocument, graph_id: str) -> Graph:
    matches = [graph for graph in document.graphs if graph.id == graph_id]
    if len(matches) != 1:
        raise _error(FIXTURE_SHAPE_INVALID, "source graph must resolve exactly once")
    return matches[0]


def _object_array(value: Any, field: str) -> list[Mapping[str, Any]]:
    if not isinstance(value, list) or any(not isinstance(item, Mapping) for item in value):
        raise _error(FIXTURE_SHAPE_INVALID, f"{field} must be an object array")
    return value


def _unique_strings(value: Any, field: str, code: str) -> tuple[str, ...]:
    if (
        not isinstance(value, list)
        or any(not isinstance(item, str) or not item for item in value)
        or len(value) != len(set(value))
    ):
        raise _error(code, f"{field} must contain unique nonempty strings")
    return tuple(sorted(value))


def _reachable(start: str, adjacency: Mapping[str, set[str]]) -> set[str]:
    seen = {start}
    pending: deque[str] = deque([start])
    while pending:
        current = pending.popleft()
        for target in sorted(adjacency.get(current, set())):
            if target not in seen:
                seen.add(target)
                pending.append(target)
    return seen


def _semantic_source(
    document: BlueprintDocument,
    source: Mapping[str, Any],
) -> tuple[Graph, dict[str, Any]]:
    if (
        source.get("format") != "blueprint-lens-lc7-static-scc-source"
        or source.get("format_version") != "1.0.0"
        or source.get("compile_provenance", {}).get("status") != "up_to_date"
    ):
        raise _error(FIXTURE_SHAPE_INVALID, "source format or compile state is invalid")
    graph_id = source.get("graph_id")
    if not isinstance(graph_id, str):
        raise _error(FIXTURE_SHAPE_INVALID, "source graph identity is missing")
    graph = _graph(document, graph_id)
    if source.get("blueprint_asset_path") != document.blueprint_path:
        raise _error(FIXTURE_SHAPE_INVALID, "source asset differs from typed raw source")
    if source.get("counts") != {
        "nodes": 10,
        "edges": 10,
        "scc_members": 3,
        "internal_edges": 3,
        "incoming_edges": 1,
        "outgoing_edges": 1,
    }:
        raise _error(FIXTURE_SHAPE_INVALID, "source counts differ from 10/10/3/3/1/1")

    source_nodes = _object_array(source.get("nodes"), "nodes")
    source_edges = _object_array(source.get("edges"), "edges")
    graph_nodes = {node.id: node for node in graph.nodes}
    graph_edges = {edge.id: edge for edge in graph.edges}
    if len(graph_nodes) != 10 or len(graph_edges) != 10:
        raise _error(FIXTURE_SHAPE_INVALID, "typed raw graph differs from 10/10")
    if (
        len(source_nodes) != 10
        or len({str(node.get("id")) for node in source_nodes}) != 10
        or len(source_edges) != 10
        or len({str(edge.get("id")) for edge in source_edges}) != 10
    ):
        raise _error(FIXTURE_SHAPE_INVALID, "source node or edge identity is invalid")
    for source_node in source_nodes:
        node = graph_nodes.get(str(source_node.get("id")))
        if (
            node is None
            or source_node.get("native_guid") != node.native_guid
            or source_node.get("class") != node.class_path
        ):
            raise _error(SOURCE_AUDIT_MISMATCH, "source node differs from typed raw")
        source_pins = _object_array(source_node.get("pins"), "source node pins")
        source_pin_rows = {
            (
                pin.get("id"),
                pin.get("name"),
                pin.get("direction"),
                pin.get("kind"),
            )
            for pin in source_pins
        }
        typed_pin_rows = {
            (pin.id, pin.name, pin.direction, pin.kind) for pin in node.pins
        }
        if source_pin_rows != typed_pin_rows or len(source_pin_rows) != len(source_pins):
            raise _error(SOURCE_AUDIT_MISMATCH, "source pins differ from typed raw")
    for source_edge in source_edges:
        edge = graph_edges.get(str(source_edge.get("id")))
        if edge is None or any(
            source_edge.get(field) != getattr(edge, field)
            for field in (
                "source_node_id",
                "source_pin_id",
                "target_node_id",
                "target_pin_id",
                "kind",
            )
        ):
            raise _error(SOURCE_AUDIT_MISMATCH, "source edge differs from typed raw")

    scc = source.get("scc")
    if not isinstance(scc, Mapping):
        raise _error(SCC_MEMBERSHIP_INVALID, "source SCC object is missing")
    members = _unique_strings(
        scc.get("member_node_ids"), "member_node_ids", SCC_MEMBERSHIP_INVALID
    )
    if len(members) != 3 or set(members) - set(graph_nodes):
        raise _error(SCC_MEMBERSHIP_INVALID, "SCC membership differs from three nodes")
    criterion = source.get("criterion_node_id")
    if not isinstance(criterion, str) or criterion not in graph_nodes:
        raise _error(FIXTURE_SHAPE_INVALID, "criterion identity is invalid")
    if criterion in members:
        raise _error(SCC_MEMBERSHIP_INVALID, "criterion is inside the SCC")
    if any(not any(pin.kind == "execution" for pin in graph_nodes[node_id].pins) for node_id in members):
        raise _error(SCC_MEMBERSHIP_INVALID, "pure-data node appears in SCC membership")

    execution_edges = tuple(edge for edge in graph.edges if edge.kind == "execution")
    forward: dict[str, set[str]] = defaultdict(set)
    reverse: dict[str, set[str]] = defaultdict(set)
    for edge in execution_edges:
        forward[edge.source_node_id].add(edge.target_node_id)
        reverse[edge.target_node_id].add(edge.source_node_id)
    anchor = members[0]
    recomputed_members = _reachable(anchor, forward) & _reachable(anchor, reverse)
    if recomputed_members != set(members):
        raise _error(SCC_MEMBERSHIP_INVALID, "declared members are not the complete SCC")

    member_set = set(members)
    actual_internal = tuple(
        sorted(
            edge.id
            for edge in execution_edges
            if edge.source_node_id in member_set and edge.target_node_id in member_set
        )
    )
    actual_incoming_edges = tuple(
        sorted(
            (
                edge
                for edge in execution_edges
                if edge.source_node_id not in member_set
                and edge.target_node_id in member_set
            ),
            key=lambda edge: edge.id,
        )
    )
    actual_outgoing_edges = tuple(
        sorted(
            (
                edge
                for edge in execution_edges
                if edge.source_node_id in member_set
                and edge.target_node_id not in member_set
            ),
            key=lambda edge: edge.id,
        )
    )
    declared_internal = _unique_strings(
        scc.get("internal_edge_ids"), "internal_edge_ids", SCC_EDGE_OWNERSHIP_INVALID
    )
    declared_returning = _unique_strings(
        scc.get("returning_edge_ids"), "returning_edge_ids", SCC_EDGE_OWNERSHIP_INVALID
    )
    if declared_internal != actual_internal or len(actual_internal) != 3:
        raise _error(SCC_EDGE_OWNERSHIP_INVALID, "internal edge ownership is incomplete")

    declared_incoming = _unique_strings(
        scc.get("incoming_edge_ids"), "incoming_edge_ids", SCC_BOUNDARY_INVALID
    )
    declared_outgoing = _unique_strings(
        scc.get("outgoing_edge_ids"), "outgoing_edge_ids", SCC_BOUNDARY_INVALID
    )
    actual_incoming = tuple(edge.id for edge in actual_incoming_edges)
    actual_outgoing = tuple(edge.id for edge in actual_outgoing_edges)
    if (
        declared_incoming != actual_incoming
        or declared_outgoing != actual_outgoing
        or len(actual_incoming) != 1
        or len(actual_outgoing) != 1
    ):
        raise _error(SCC_BOUNDARY_INVALID, "SCC boundary edge ownership is invalid")
    actual_entry = actual_incoming_edges[0].target_node_id
    actual_exit = actual_outgoing_edges[0].source_node_id
    if (
        scc.get("entry_node_id") != actual_entry
        or scc.get("exit_node_id") != actual_exit
        or actual_entry != actual_exit
        or actual_outgoing_edges[0].target_node_id != criterion
    ):
        raise _error(SCC_BOUNDARY_INVALID, "SCC entry, exit, or criterion boundary is invalid")
    actual_returning = tuple(
        edge_id
        for edge_id in actual_internal
        if graph_edges[edge_id].target_node_id == actual_entry
    )
    if declared_returning != actual_returning or len(actual_returning) != 1:
        raise _error(SCC_EDGE_OWNERSHIP_INVALID, "returning edge ownership is invalid")

    return graph, {
        "member_node_ids": members,
        "internal_edge_ids": actual_internal,
        "incoming_edge_ids": actual_incoming,
        "outgoing_edge_ids": actual_outgoing,
        "returning_edge_ids": actual_returning,
        "entry_node_id": actual_entry,
        "exit_node_id": actual_exit,
        "criterion_node_id": criterion,
    }


def _normalize_unordered(value: Any) -> Any:
    if isinstance(value, Mapping):
        return {
            key: _normalize_unordered(item)
            for key, item in sorted(value.items())
        }
    if isinstance(value, list):
        normalized = [_normalize_unordered(item) for item in value]
        return sorted(
            normalized,
            key=lambda item: json.dumps(item, ensure_ascii=False, sort_keys=True),
        )
    return value


def _reconcile_audit(
    source: Mapping[str, Any],
    scc: Mapping[str, Any],
    details: _AuditDetails,
) -> None:
    audit = details.public
    if (
        audit.asset_path != source.get("blueprint_asset_path")
        or audit.graph_id != source.get("graph_id")
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit identity differs")
    if (
        audit.member_node_ids != scc["member_node_ids"]
        or audit.internal_edge_ids != scc["internal_edge_ids"]
        or audit.incoming_edge_ids != scc["incoming_edge_ids"]
        or audit.outgoing_edge_ids != scc["outgoing_edge_ids"]
        or details.returning_edge_ids != scc["returning_edge_ids"]
        or audit.entry_node_id != scc["entry_node_id"]
        or audit.exit_node_id != scc["exit_node_id"]
        or audit.criterion_node_id != scc["criterion_node_id"]
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit SCC facts differ")
    source_nodes = tuple(
        sorted(
            (
                {
                    "id": str(node["id"]),
                    "native_guid": str(node["native_guid"]),
                    "class": str(node["class"]),
                }
                for node in source["nodes"]
            ),
            key=lambda row: row["id"],
        )
    )
    source_pins = tuple(
        sorted(
            (
                {
                    "id": str(pin["id"]),
                    "node_id": str(node["id"]),
                    "name": str(pin["name"]),
                    "direction": str(pin["direction"]),
                    "type_category": str(pin["type_category"]),
                }
                for node in source["nodes"]
                for pin in node["pins"]
            ),
            key=lambda row: row["id"],
        )
    )
    source_edges = tuple(
        sorted(
            (
                {
                    "id": str(edge["id"]),
                    "source_node_id": str(edge["source_node_id"]),
                    "target_node_id": str(edge["target_node_id"]),
                    "kind": str(edge["kind"]),
                }
                for edge in source["edges"]
            ),
            key=lambda row: row["id"],
        )
    )
    if (
        source_nodes != details.node_rows
        or source_pins != details.pin_rows
        or source_edges != details.edge_rows
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit inventory differs")


def build_lc7_static_scc_profile(
    document: BlueprintDocument,
    source: Mapping[str, Any],
    audit_text: str,
    source_binding: Mapping[str, Any],
) -> dict[str, Any]:
    """Build the source-bound, structural-only LC7 SCC profile."""

    details = _parse_audit(audit_text)
    binding = _binding(source_binding, source, audit_text)
    if (
        source.get("asset_sha256") != binding["asset_sha256"]
        or source.get("raw_sha256") != binding["raw_sha256"]
        or details.asset_sha256 != binding["asset_sha256"]
        or details.raw_sha256 != binding["raw_sha256"]
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit hash binding differs")
    bound_document = binding.pop("_bound_document")
    bound_source = binding.pop("_bound_source")
    if bound_document != document:
        raise _error(SOURCE_AUDIT_MISMATCH, "typed document differs from bound raw bytes")

    _, scc = _semantic_source(document, source)
    _reconcile_audit(source, scc, details)
    if _normalize_unordered(bound_source) != _normalize_unordered(source):
        raise _error(SOURCE_AUDIT_MISMATCH, "source mapping differs from bound source bytes")

    return {
        "format": "blueprint-lens-lc7-static-scc-profile",
        "format_version": "1.0.0",
        "profile_id": PROFILE_ID,
        "claim_scope": CLAIM_SCOPE,
        "runtime_iterations": "NOT_CLAIMED",
        "source_binding": {
            "blueprint_asset_path": source["blueprint_asset_path"],
            "graph_id": source["graph_id"],
            **binding,
        },
        "criterion_node_id": scc["criterion_node_id"],
        "scc": {
            field: list(scc[field])
            for field in (
                "member_node_ids",
                "internal_edge_ids",
                "incoming_edge_ids",
                "outgoing_edge_ids",
                "returning_edge_ids",
            )
        }
        | {
            "entry_node_id": scc["entry_node_id"],
            "exit_node_id": scc["exit_node_id"],
        },
        "counts": {
            "nodes": 10,
            "edges": 10,
            "scc_members": 3,
            "internal_edges": 3,
            "incoming_edges": 1,
            "outgoing_edges": 1,
        },
    }


def validate_lc7_static_scc_profile(
    profile: Mapping[str, Any],
    document: BlueprintDocument,
    source: Mapping[str, Any],
    audit_text: str,
    source_binding: Mapping[str, Any],
) -> None:
    """Rebuild the profile and reject any claim or byte-relevant divergence."""

    if profile.get("runtime_iterations") != "NOT_CLAIMED":
        raise _error(RUNTIME_CLAIM_INVALID, "runtime iteration evidence is not owned")
    expected = build_lc7_static_scc_profile(
        document, source, audit_text, source_binding
    )
    if canonical_profile_bytes(profile) != canonical_profile_bytes(expected):
        raise _error(FIXTURE_SHAPE_INVALID, "profile differs from the independent rebuild")


def canonical_profile_bytes(profile: Mapping[str, Any]) -> bytes:
    """Serialize stable UTF-8 JSON with two-space indentation and one newline."""

    return (
        json.dumps(profile, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
