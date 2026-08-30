"""Deterministic bounded reverse traversal over execution relations."""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass

from ..raw_probe import BlueprintDocument, Edge, Graph


_ERROR = "LC6_QUERY_CONTRACT_INVALID"


@dataclass(frozen=True, slots=True)
class UpstreamFrontier:
    edge_id: str
    source_node_id: str
    target_node_id: str


@dataclass(frozen=True, slots=True)
class BoundedUpstreamResult:
    criterion_node_id: str
    max_upstream_hops: int
    status: str
    reason: str
    complete_node_ids: tuple[str, ...]
    complete_edge_ids: tuple[str, ...]
    selected_node_ids: tuple[str, ...]
    selected_edge_ids: tuple[str, ...]
    hop_distances: tuple[tuple[str, int], ...]
    frontiers: tuple[UpstreamFrontier, ...]
    omitted_node_count: int
    omitted_edge_count: int


def _reject(message: str) -> ValueError:
    return ValueError(f"{_ERROR}: {message}")


def _unique_graph(document: BlueprintDocument, graph_id: str) -> Graph:
    matches = [graph for graph in document.graphs if graph.id == graph_id]
    if len(matches) != 1:
        raise _reject(f"graph must resolve exactly once: {graph_id}")
    return matches[0]


def _execution_edges(graph: Graph, node_ids: set[str]) -> tuple[Edge, ...]:
    edges: list[Edge] = []
    seen: set[str] = set()
    for edge in graph.edges:
        if edge.kind != "execution":
            continue
        if edge.id in seen:
            raise _reject(f"duplicate execution edge identity: {edge.id}")
        if (
            edge.graph_id != graph.id
            or edge.source_node_id not in node_ids
            or edge.target_node_id not in node_ids
        ):
            raise _reject(f"execution edge endpoint is outside graph: {edge.id}")
        seen.add(edge.id)
        edges.append(edge)
    return tuple(sorted(edges, key=lambda edge: edge.id))


def build_bounded_upstream_query(
    document: BlueprintDocument,
    graph_id: str,
    criterion_node_id: str,
    max_upstream_hops: int,
) -> BoundedUpstreamResult:
    """Return an inclusive, execution-only, reverse-reachable bounded query."""

    if (
        isinstance(max_upstream_hops, bool)
        or not isinstance(max_upstream_hops, int)
        or max_upstream_hops < 0
    ):
        raise _reject("max_upstream_hops must be a non-negative integer")

    graph = _unique_graph(document, graph_id)
    node_ids = {node.id for node in graph.nodes}
    if len(node_ids) != len(graph.nodes):
        raise _reject("graph contains duplicate node identities")
    if criterion_node_id not in node_ids:
        raise _reject(f"criterion is not a node in graph: {criterion_node_id}")

    execution_edges = _execution_edges(graph, node_ids)
    incoming: dict[str, list[Edge]] = defaultdict(list)
    for edge in execution_edges:
        incoming[edge.target_node_id].append(edge)
    for edges in incoming.values():
        edges.sort(key=lambda edge: edge.id)

    distances: dict[str, int] = {criterion_node_id: 0}
    pending: deque[str] = deque([criterion_node_id])
    complete_nodes = {criterion_node_id}
    while pending:
        target = pending.popleft()
        next_distance = distances[target] + 1
        for edge in incoming.get(target, ()):
            source = edge.source_node_id
            complete_nodes.add(source)
            previous = distances.get(source)
            if previous is None or next_distance < previous:
                distances[source] = next_distance
                pending.append(source)

    complete_edges = tuple(
        edge
        for edge in execution_edges
        if edge.source_node_id in complete_nodes and edge.target_node_id in complete_nodes
    )
    selected_nodes = {
        node_id
        for node_id, distance in distances.items()
        if distance <= max_upstream_hops
    }
    selected_edges = tuple(
        edge
        for edge in complete_edges
        if edge.source_node_id in selected_nodes and edge.target_node_id in selected_nodes
    )
    frontiers = tuple(
        sorted(
            (
                UpstreamFrontier(
                    edge_id=edge.id,
                    source_node_id=edge.source_node_id,
                    target_node_id=edge.target_node_id,
                )
                for edge in complete_edges
                if edge.source_node_id not in selected_nodes
                and edge.target_node_id in selected_nodes
            ),
            key=lambda frontier: (
                frontier.edge_id,
                frontier.source_node_id,
                frontier.target_node_id,
            ),
        )
    )

    status = "complete" if selected_nodes == complete_nodes else "truncated"
    reason = "" if status == "complete" else "max_upstream_hops_exhausted"
    return BoundedUpstreamResult(
        criterion_node_id=criterion_node_id,
        max_upstream_hops=max_upstream_hops,
        status=status,
        reason=reason,
        complete_node_ids=tuple(sorted(complete_nodes)),
        complete_edge_ids=tuple(edge.id for edge in complete_edges),
        selected_node_ids=tuple(sorted(selected_nodes)),
        selected_edge_ids=tuple(edge.id for edge in selected_edges),
        hop_distances=tuple(sorted(distances.items())),
        frontiers=frontiers,
        omitted_node_count=len(complete_nodes - selected_nodes),
        omitted_edge_count=len(complete_edges) - len(selected_edges),
    )
