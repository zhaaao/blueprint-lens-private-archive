"""Build the split LC6 core-boundary and bounded-query truth products."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass
import json
from typing import Any, Mapping

from ..raw_probe import BlueprintDocument, Edge, Graph, Node
from .upstream_query import build_bounded_upstream_query


CORE_PROFILE_ID = "LC6_CORE_BOUNDARY_MATRIX_V1"
BUDGET_PROFILE_ID = "LC6_MAX_UPSTREAM_HOPS_V1"

FIXTURE_SHAPE_INVALID = "LC6_FIXTURE_SHAPE_INVALID"
COMPONENT_ISOLATION_INVALID = "LC6_COMPONENT_ISOLATION_INVALID"
CORE_CLASSIFICATION_MISMATCH = "LC6_CORE_CLASSIFICATION_MISMATCH"
QUERY_CONTRACT_INVALID = "LC6_QUERY_CONTRACT_INVALID"
BUDGET_TRUTH_MISMATCH = "LC6_BUDGET_TRUTH_MISMATCH"
SOURCE_AUDIT_MISMATCH = "LC6_SOURCE_AUDIT_MISMATCH"

_SCENARIO_IDS = (
    "LC6_OPAQUE",
    "LC6_TRUNCATED",
    "LC6_UNCERTAIN",
    "LC6_UNSUPPORTED",
)
_CORE_EXPECTATIONS = {
    "LC6_OPAQUE": ("opaque", "function_body_not_expanded", 2, 1),
    "LC6_UNCERTAIN": (
        "uncertain",
        "node_family_not_in_supported_matrix_v1",
        3,
        2,
    ),
    "LC6_UNSUPPORTED": ("unsupported", "latent_function", 2, 1),
}


class LC6ProfileError(ValueError):
    """Fail-closed LC6 profile diagnostic."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


@dataclass(frozen=True, slots=True)
class BoundaryAudit:
    blueprint_asset_path: str
    asset_sha256: str
    raw_sha256: str
    graph_id: str
    scenario_rows: tuple[Mapping[str, Any], ...]
    node_rows: tuple[Mapping[str, Any], ...]
    edge_rows: tuple[Mapping[str, Any], ...]


@dataclass(frozen=True, slots=True)
class LC6Profiles:
    core_boundary_matrix: Mapping[str, Any]
    upstream_budget: Mapping[str, Any]


def _error(code: str, message: str) -> LC6ProfileError:
    return LC6ProfileError(code, message)


def _require_columns(parts: list[str], count: int, context: str) -> None:
    if len(parts) != count:
        raise _error(SOURCE_AUDIT_MISMATCH, f"{context} has {len(parts)} columns")


def parse_boundary_audit(text: str) -> BoundaryAudit:
    """Parse the independent native audit without interpreting core statuses."""

    blueprint_asset_path = ""
    graph_id = ""
    asset_sha256 = ""
    raw_sha256 = ""
    scenarios: list[dict[str, Any]] = []
    nodes: list[dict[str, Any]] = []
    edges: list[dict[str, Any]] = []
    counts: tuple[int, int, int] | None = None
    format_seen = False
    for line_number, line in enumerate(text.splitlines(), start=1):
        if not line:
            continue
        parts = line.split("\t")
        kind = parts[0]
        if kind == "FORMAT":
            _require_columns(parts, 3, f"FORMAT line {line_number}")
            format_seen = parts[1:] == [
                "blueprint-lens-lc6-boundary-audit",
                "1.0.0",
            ]
        elif kind == "BLUEPRINT":
            _require_columns(parts, 3, f"BLUEPRINT line {line_number}")
            blueprint_asset_path, graph_id = parts[1:]
        elif kind == "COMPILE":
            _require_columns(parts, 6, f"COMPILE line {line_number}")
            if parts[1] != "up_to_date" or not parts[2] or not parts[3]:
                raise _error(SOURCE_AUDIT_MISMATCH, "audit compile provenance is stale")
            asset_sha256, raw_sha256 = parts[4], parts[5]
        elif kind == "SCENARIO":
            _require_columns(parts, 6, f"SCENARIO line {line_number}")
            try:
                node_count, edge_count = int(parts[4]), int(parts[5])
            except ValueError as error:
                raise _error(
                    SOURCE_AUDIT_MISMATCH, "audit scenario counts are not integers"
                ) from error
            scenarios.append(
                {
                    "scenario_id": parts[1],
                    "root_node_id": parts[2],
                    "criterion_node_id": parts[3],
                    "node_count": node_count,
                    "edge_count": edge_count,
                }
            )
        elif kind == "NODE":
            _require_columns(parts, 5, f"NODE line {line_number}")
            nodes.append(
                {
                    "id": parts[1],
                    "native_guid": parts[2],
                    "class": parts[3],
                    "detail": parts[4],
                }
            )
        elif kind == "EDGE":
            _require_columns(parts, 5, f"EDGE line {line_number}")
            edges.append(
                {
                    "id": parts[1],
                    "source_node_id": parts[2],
                    "target_node_id": parts[3],
                    "kind": parts[4],
                }
            )
        elif kind == "COUNTS":
            _require_columns(parts, 4, f"COUNTS line {line_number}")
            try:
                counts = (int(parts[1]), int(parts[2]), int(parts[3]))
            except ValueError as error:
                raise _error(
                    SOURCE_AUDIT_MISMATCH, "audit totals are not integers"
                ) from error
        elif kind != "PIN":
            raise _error(SOURCE_AUDIT_MISMATCH, f"unknown audit row: {kind}")

    if not format_seen or not blueprint_asset_path or not graph_id:
        raise _error(SOURCE_AUDIT_MISMATCH, "audit header is incomplete")
    if counts != (len(scenarios), len(nodes), len(edges)):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit totals do not match rows")
    if counts != (4, 16, 12):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit is not the bounded 4/16/12 source")
    if len({row["scenario_id"] for row in scenarios}) != len(scenarios):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit scenario identity is duplicated")
    if len({row["id"] for row in nodes}) != len(nodes):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit node identity is duplicated")
    if len({row["id"] for row in edges}) != len(edges):
        raise _error(SOURCE_AUDIT_MISMATCH, "audit edge identity is duplicated")
    return BoundaryAudit(
        blueprint_asset_path=blueprint_asset_path,
        asset_sha256=asset_sha256,
        raw_sha256=raw_sha256,
        graph_id=graph_id,
        scenario_rows=tuple(sorted(scenarios, key=lambda row: row["scenario_id"])),
        node_rows=tuple(sorted(nodes, key=lambda row: row["id"])),
        edge_rows=tuple(sorted(edges, key=lambda row: row["id"])),
    )


def _graph(document: BlueprintDocument, graph_id: str) -> Graph:
    matches = [graph for graph in document.graphs if graph.id == graph_id]
    if len(matches) != 1:
        raise _error(FIXTURE_SHAPE_INVALID, "source graph must resolve exactly once")
    return matches[0]


def _source_scenarios(source: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    values = source.get("scenarios")
    if not isinstance(values, list) or len(values) != 4:
        raise _error(FIXTURE_SHAPE_INVALID, "source must contain four scenarios")
    result: dict[str, Mapping[str, Any]] = {}
    roots: set[str] = set()
    criteria: set[str] = set()
    for value in values:
        if not isinstance(value, Mapping):
            raise _error(FIXTURE_SHAPE_INVALID, "source scenario must be an object")
        scenario_id = value.get("scenario_id")
        root = value.get("root_node_id")
        criterion = value.get("criterion_node_id")
        if (
            not isinstance(scenario_id, str)
            or scenario_id in result
            or not isinstance(root, str)
            or not isinstance(criterion, str)
        ):
            raise _error(FIXTURE_SHAPE_INVALID, "source scenario anchors are invalid")
        result[scenario_id] = value
        roots.add(root)
        criteria.add(criterion)
    if tuple(sorted(result)) != _SCENARIO_IDS or len(roots) != 4 or len(criteria) != 4:
        raise _error(FIXTURE_SHAPE_INVALID, "source scenario identities are invalid")
    return result


def _mapping_list(value: Any, field: str) -> list[Mapping[str, Any]]:
    if not isinstance(value, list) or any(not isinstance(item, Mapping) for item in value):
        raise _error(FIXTURE_SHAPE_INVALID, f"{field} must be an object array")
    return value


def _validate_source_shape(
    document: BlueprintDocument,
    graph: Graph,
    source: Mapping[str, Any],
    scenarios: Mapping[str, Mapping[str, Any]],
    max_upstream_hops: int,
) -> None:
    if source.get("format") != "blueprint-lens-lc6-boundary-source" or source.get(
        "format_version"
    ) != "1.0.0":
        raise _error(FIXTURE_SHAPE_INVALID, "source format is invalid")
    if (
        source.get("blueprint_asset_path") != document.blueprint_path
        or source.get("graph_id") != graph.id
        or source.get("compile_provenance", {}).get("status") != "up_to_date"
    ):
        raise _error(FIXTURE_SHAPE_INVALID, "source binding or compile state is invalid")
    if max_upstream_hops != 3:
        raise _error(QUERY_CONTRACT_INVALID, "bounded profile requires hop budget three")
    counts = source.get("counts")
    if counts != {"scenarios": 4, "nodes": 16, "edges": 12}:
        raise _error(FIXTURE_SHAPE_INVALID, "source totals differ from 4/16/12")

    graph_nodes = {node.id: node for node in graph.nodes}
    graph_edges = {edge.id: edge for edge in graph.edges}
    if len(graph_nodes) != 16 or len(graph_edges) != 12:
        raise _error(FIXTURE_SHAPE_INVALID, "typed source totals differ from 16/12")
    owned_nodes: set[str] = set()
    owned_edges: set[str] = set()
    expected_counts = {
        "LC6_OPAQUE": (3, 2),
        "LC6_UNCERTAIN": (3, 2),
        "LC6_UNSUPPORTED": (3, 2),
        "LC6_TRUNCATED": (7, 6),
    }
    for scenario_id, scenario in scenarios.items():
        nodes = _mapping_list(scenario.get("nodes"), f"{scenario_id}.nodes")
        edges = _mapping_list(scenario.get("edges"), f"{scenario_id}.edges")
        node_ids = {str(node.get("id")) for node in nodes}
        edge_ids = {str(edge.get("id")) for edge in edges}
        if len(node_ids) != len(nodes) or len(edge_ids) != len(edges):
            raise _error(FIXTURE_SHAPE_INVALID, f"{scenario_id} identity is duplicated")
        if (len(nodes), len(edges)) != expected_counts[scenario_id]:
            raise _error(COMPONENT_ISOLATION_INVALID, f"{scenario_id} shape is invalid")
        if owned_nodes & node_ids or owned_edges & edge_ids:
            raise _error(COMPONENT_ISOLATION_INVALID, "scenario ownership overlaps")
        owned_nodes |= node_ids
        owned_edges |= edge_ids
        if scenario["root_node_id"] not in node_ids or scenario["criterion_node_id"] not in node_ids:
            raise _error(FIXTURE_SHAPE_INVALID, f"{scenario_id} anchors are outside scenario")
        for source_node in nodes:
            node = graph_nodes.get(str(source_node.get("id")))
            if (
                node is None
                or source_node.get("class") != node.class_path
                or source_node.get("native_guid") != node.native_guid
            ):
                raise _error(SOURCE_AUDIT_MISMATCH, "source node differs from typed source")
            source_pins = _mapping_list(source_node.get("pins"), "source node pins")
            if {pin.get("id") for pin in source_pins} != {pin.id for pin in node.pins}:
                raise _error(SOURCE_AUDIT_MISMATCH, "source pins differ from typed source")
        for source_edge in edges:
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
                raise _error(SOURCE_AUDIT_MISMATCH, "source edge differs from typed source")
            if edge.source_node_id not in node_ids or edge.target_node_id not in node_ids:
                raise _error(COMPONENT_ISOLATION_INVALID, "source edge crosses scenario")
    if owned_nodes != set(graph_nodes) or owned_edges != set(graph_edges):
        raise _error(COMPONENT_ISOLATION_INVALID, "source scenarios do not cover graph")

    truncated = scenarios["LC6_TRUNCATED"]
    truncated_ids = {str(node["id"]) for node in truncated["nodes"]}
    if any(graph_nodes[node_id].semantic_status != "supported" for node_id in truncated_ids):
        raise _error(QUERY_CONTRACT_INVALID, "truncation component is not core-supported")
    criterion = str(truncated["criterion_node_id"])
    outgoing_execution = [
        edge
        for edge in graph.edges
        if edge.kind == "execution" and edge.source_node_id == criterion
    ]
    if outgoing_execution:
        raise _error(QUERY_CONTRACT_INVALID, "truncation criterion is not the terminal Set")


def _reconcile_audit(
    source: Mapping[str, Any],
    scenarios: Mapping[str, Mapping[str, Any]],
    audit: BoundaryAudit,
) -> None:
    if (
        audit.blueprint_asset_path != source["blueprint_asset_path"]
        or audit.graph_id != source["graph_id"]
        or audit.asset_sha256 != source.get("asset_sha256")
        or audit.raw_sha256 != source.get("raw_sha256")
    ):
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit binding or hash differs")
    source_scenario_rows = tuple(
        sorted(
            (
                {
                    "scenario_id": scenario_id,
                    "root_node_id": scenario["root_node_id"],
                    "criterion_node_id": scenario["criterion_node_id"],
                    "node_count": len(scenario["nodes"]),
                    "edge_count": len(scenario["edges"]),
                }
                for scenario_id, scenario in scenarios.items()
            ),
            key=lambda row: row["scenario_id"],
        )
    )
    if source_scenario_rows != audit.scenario_rows:
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit scenario anchors differ")
    source_nodes = tuple(
        sorted(
            (
                {
                    "id": node["id"],
                    "native_guid": node["native_guid"],
                    "class": node["class"],
                }
                for scenario in scenarios.values()
                for node in scenario["nodes"]
            ),
            key=lambda row: row["id"],
        )
    )
    audit_nodes = tuple(
        {key: row[key] for key in ("id", "native_guid", "class")}
        for row in audit.node_rows
    )
    if source_nodes != audit_nodes:
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit node inventory differs")
    source_edges = tuple(
        sorted(
            (
                {
                    "id": edge["id"],
                    "source_node_id": edge["source_node_id"],
                    "target_node_id": edge["target_node_id"],
                    "kind": edge["kind"],
                }
                for scenario in scenarios.values()
                for edge in scenario["edges"]
            ),
            key=lambda row: row["id"],
        )
    )
    if source_edges != audit.edge_rows:
        raise _error(SOURCE_AUDIT_MISMATCH, "source/audit edge inventory differs")


def _core_slice(graph: Graph, criterion_id: str) -> tuple[tuple[str, ...], tuple[str, ...]]:
    nodes = {node.id: node for node in graph.nodes}
    incoming: dict[str, list[Edge]] = defaultdict(list)
    for edge in graph.edges:
        incoming[edge.target_node_id].append(edge)
    selected_nodes = {criterion_id}
    selected_edges: set[str] = set()
    pending: deque[str] = deque([criterion_id])
    while pending:
        target = pending.popleft()
        for edge in sorted(incoming.get(target, ()), key=lambda value: value.id):
            predecessor = nodes[edge.source_node_id]
            selected_edges.add(edge.id)
            if predecessor.id in selected_nodes:
                continue
            selected_nodes.add(predecessor.id)
            if predecessor.semantic_status == "supported":
                pending.append(predecessor.id)
    return tuple(sorted(selected_nodes)), tuple(sorted(selected_edges))


def _boundary_node(
    graph: Graph,
    scenario: Mapping[str, Any],
    expected_status: str,
    expected_reason: str,
) -> Node:
    nodes = {node.id: node for node in graph.nodes}
    scenario_ids = {str(node["id"]) for node in scenario["nodes"]}
    candidates = [
        nodes[node_id]
        for node_id in scenario_ids
        if nodes[node_id].semantic_status != "supported"
    ]
    if len(candidates) != 1:
        raise _error(CORE_CLASSIFICATION_MISMATCH, "core scenario has no unique boundary")
    boundary = candidates[0]
    if (boundary.semantic_status, boundary.semantic_reason) != (
        expected_status,
        expected_reason,
    ):
        raise _error(CORE_CLASSIFICATION_MISMATCH, "core status/reason differs from contract")
    return boundary


def _binding(source: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "blueprint_asset_path": source["blueprint_asset_path"],
        "graph_id": source["graph_id"],
        "asset_sha256": source["asset_sha256"],
        "raw_sha256": source["raw_sha256"],
    }


def build_lc6_profiles(
    document: BlueprintDocument,
    source: Mapping[str, Any],
    audit_text: str,
    max_upstream_hops: int = 3,
) -> LC6Profiles:
    """Reconcile native products and derive the two separately owned profiles."""

    scenarios = _source_scenarios(source)
    graph_id = source.get("graph_id")
    if not isinstance(graph_id, str):
        raise _error(FIXTURE_SHAPE_INVALID, "source graph identity is missing")
    graph = _graph(document, graph_id)
    _validate_source_shape(document, graph, source, scenarios, max_upstream_hops)
    audit = parse_boundary_audit(audit_text)
    _reconcile_audit(source, scenarios, audit)

    core_scenarios: list[dict[str, Any]] = []
    graph_edges = {edge.id: edge for edge in graph.edges}
    for scenario_id in ("LC6_OPAQUE", "LC6_UNCERTAIN", "LC6_UNSUPPORTED"):
        scenario = scenarios[scenario_id]
        status, reason, node_count, edge_count = _CORE_EXPECTATIONS[scenario_id]
        boundary = _boundary_node(graph, scenario, status, reason)
        slice_nodes, slice_edges = _core_slice(graph, str(scenario["criterion_node_id"]))
        if (len(slice_nodes), len(slice_edges)) != (node_count, edge_count):
            raise _error(CORE_CLASSIFICATION_MISMATCH, "core slice differs from contract")
        boundary_pins = tuple(sorted(pin.id for pin in boundary.pins))
        incident_edges = tuple(
            sorted(
                edge.id
                for edge in graph_edges.values()
                if boundary.id in (edge.source_node_id, edge.target_node_id)
            )
        )
        if not boundary_pins or not incident_edges:
            raise _error(CORE_CLASSIFICATION_MISMATCH, "boundary source evidence is empty")
        core_scenarios.append(
            {
                "scenario_id": scenario_id,
                "root_node_id": scenario["root_node_id"],
                "criterion_node_id": scenario["criterion_node_id"],
                "boundary_node_id": boundary.id,
                "status": status,
                "reason": reason,
                "source_pin_ids": list(boundary_pins),
                "incident_edge_ids": list(incident_edges),
                "slice_node_ids": list(slice_nodes),
                "slice_edge_ids": list(slice_edges),
                "stop_location": {
                    "kind": "semantic_boundary",
                    "node_id": boundary.id,
                },
            }
        )

    truncated = scenarios["LC6_TRUNCATED"]
    query = build_bounded_upstream_query(
        document,
        graph.id,
        str(truncated["criterion_node_id"]),
        max_upstream_hops,
    )
    truncated_ids = {str(node["id"]) for node in truncated["nodes"]}
    truncated_edges = {str(edge["id"]) for edge in truncated["edges"]}
    if set(query.complete_node_ids) != truncated_ids or set(query.complete_edge_ids) != truncated_edges:
        raise _error(QUERY_CONTRACT_INVALID, "query universe differs from truncation component")
    if (
        query.status != "truncated"
        or query.reason != "max_upstream_hops_exhausted"
        or (len(query.complete_node_ids), len(query.complete_edge_ids)) != (7, 6)
        or (len(query.selected_node_ids), len(query.selected_edge_ids)) != (4, 3)
        or (query.omitted_node_count, query.omitted_edge_count) != (3, 3)
        or len(query.frontiers) != 1
    ):
        raise _error(BUDGET_TRUTH_MISMATCH, "budget-three truth differs from 7/6 -> 4/3")

    binding = _binding(source)
    core = {
        "format": "blueprint-lens-lc6-boundary-matrix",
        "format_version": "1.0.0",
        "profile_id": CORE_PROFILE_ID,
        "truth_owner": "core_node_classification",
        **binding,
        "scenarios": core_scenarios,
        "counts": {"scenarios": 3},
    }
    budget = {
        "format": "blueprint-lens-lc6-upstream-budget",
        "format_version": "1.0.0",
        "profile_id": BUDGET_PROFILE_ID,
        "truth_owner": "query_profile",
        **binding,
        "scenario_id": "LC6_TRUNCATED",
        "root_node_id": truncated["root_node_id"],
        "criterion_node_id": truncated["criterion_node_id"],
        "max_upstream_hops": max_upstream_hops,
        "status": query.status,
        "reason": query.reason,
        "complete_node_ids": list(query.complete_node_ids),
        "complete_edge_ids": list(query.complete_edge_ids),
        "selected_node_ids": list(query.selected_node_ids),
        "selected_edge_ids": list(query.selected_edge_ids),
        "hop_distances": [
            {"node_id": node_id, "distance": distance}
            for node_id, distance in query.hop_distances
        ],
        "frontiers": [
            {
                "edge_id": frontier.edge_id,
                "source_node_id": frontier.source_node_id,
                "target_node_id": frontier.target_node_id,
            }
            for frontier in query.frontiers
        ],
        "counts": {
            "complete_nodes": len(query.complete_node_ids),
            "complete_edges": len(query.complete_edge_ids),
            "selected_nodes": len(query.selected_node_ids),
            "selected_edges": len(query.selected_edge_ids),
            "omitted_nodes": query.omitted_node_count,
            "omitted_edges": query.omitted_edge_count,
            "frontiers": len(query.frontiers),
        },
    }
    profiles = LC6Profiles(core_boundary_matrix=core, upstream_budget=budget)
    validate_lc6_profiles(profiles)
    return profiles


def _unique_strings(values: Any, field: str) -> list[str]:
    if (
        not isinstance(values, list)
        or any(not isinstance(value, str) or not value for value in values)
        or len(values) != len(set(values))
    ):
        raise _error(FIXTURE_SHAPE_INVALID, f"{field} must contain unique identities")
    return values


def validate_lc6_profiles(profiles: LC6Profiles) -> None:
    """Apply the pre-schema semantic contract to both products."""

    core = profiles.core_boundary_matrix
    budget = profiles.upstream_budget
    if core.get("profile_id") != CORE_PROFILE_ID or core.get("truth_owner") != "core_node_classification":
        raise _error(CORE_CLASSIFICATION_MISMATCH, "core profile identity/owner is invalid")
    scenarios = core.get("scenarios")
    if not isinstance(scenarios, list) or len(scenarios) != 3:
        raise _error(CORE_CLASSIFICATION_MISMATCH, "core scenarios are incomplete")
    for scenario, scenario_id in zip(
        scenarios, ("LC6_OPAQUE", "LC6_UNCERTAIN", "LC6_UNSUPPORTED"), strict=True
    ):
        expected = _CORE_EXPECTATIONS[scenario_id]
        if (
            not isinstance(scenario, Mapping)
            or scenario.get("scenario_id") != scenario_id
            or (scenario.get("status"), scenario.get("reason")) != expected[:2]
            or scenario.get("status") == "truncated"
        ):
            raise _error(CORE_CLASSIFICATION_MISMATCH, "core scenario semantics are invalid")
        _unique_strings(scenario.get("source_pin_ids"), "source_pin_ids")
        _unique_strings(scenario.get("incident_edge_ids"), "incident_edge_ids")
        if (
            len(_unique_strings(scenario.get("slice_node_ids"), "slice_node_ids"))
            != expected[2]
            or len(_unique_strings(scenario.get("slice_edge_ids"), "slice_edge_ids"))
            != expected[3]
        ):
            raise _error(CORE_CLASSIFICATION_MISMATCH, "core slice counts are invalid")
    if budget.get("profile_id") != BUDGET_PROFILE_ID or budget.get("truth_owner") != "query_profile":
        raise _error(QUERY_CONTRACT_INVALID, "budget profile identity/owner is invalid")
    if (
        budget.get("scenario_id") != "LC6_TRUNCATED"
        or budget.get("max_upstream_hops") != 3
        or budget.get("status") != "truncated"
        or budget.get("reason") != "max_upstream_hops_exhausted"
    ):
        raise _error(QUERY_CONTRACT_INVALID, "budget profile contract is invalid")
    expected_counts = {
        "complete_nodes": 7,
        "complete_edges": 6,
        "selected_nodes": 4,
        "selected_edges": 3,
        "omitted_nodes": 3,
        "omitted_edges": 3,
        "frontiers": 1,
    }
    if budget.get("counts") != expected_counts:
        raise _error(BUDGET_TRUTH_MISMATCH, "budget counts are invalid")
    for field, count_field in (
        ("complete_node_ids", "complete_nodes"),
        ("complete_edge_ids", "complete_edges"),
        ("selected_node_ids", "selected_nodes"),
        ("selected_edge_ids", "selected_edges"),
    ):
        if len(_unique_strings(budget.get(field), field)) != expected_counts[count_field]:
            raise _error(BUDGET_TRUTH_MISMATCH, f"{field} count is invalid")
    frontiers = budget.get("frontiers")
    if not isinstance(frontiers, list) or len(frontiers) != 1:
        raise _error(BUDGET_TRUTH_MISMATCH, "Frontier inventory is invalid")


def canonical_profile_bytes(value: Mapping[str, Any]) -> bytes:
    """Serialize a product with stable mapping order and UTF-8 bytes."""

    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")
