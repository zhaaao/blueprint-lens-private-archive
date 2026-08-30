"""Deterministic graph-local execution-context slicing.

Indexing and traversal are ``O(V + P + E)``; deterministic normalization is
``O(V log V + E log E)``; working space is ``O(V + P + E)``. These are
algorithmic bounds for the bounded kernel, not measured performance evidence.
"""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass
from types import MappingProxyType
from typing import Literal, Mapping, NoReturn

from .m4_errors import M4ExecutionError
from .raw_probe import BlueprintDocument, Edge, Graph, Node, Pin


NODE_REASON_VOCABULARY = frozenset(
    {
        "criterion",
        "execution_predecessor",
        "required_data_producer",
        "opaque_input_producer",
        "opaque_boundary",
        "uncertain_boundary",
        "unsupported_boundary",
    }
)

EDGE_REASON_VOCABULARY = frozenset(
    {
        "execution_predecessor_relation",
        "required_data_dependency_relation",
        "opaque_input_dependency_relation",
        "induced_internal_execution_relation",
        "induced_internal_data_relation",
    }
)

_BOUNDARY_STATUSES = frozenset({"opaque", "uncertain", "unsupported"})


@dataclass(frozen=True, slots=True, order=True)
class SliceBoundary:
    node_id: str
    status: Literal["opaque", "uncertain", "unsupported"]
    reason: str


@dataclass(frozen=True, slots=True)
class ExecutionSlice:
    graph_id: str
    criterion_node_id: str
    node_ids: tuple[str, ...]
    edge_ids: tuple[str, ...]
    inclusion_reasons: Mapping[str, tuple[str, ...]]
    edge_inclusion_reasons: Mapping[str, tuple[str, ...]]
    boundaries: tuple[SliceBoundary, ...]


@dataclass(frozen=True, slots=True)
class _GraphIndex:
    nodes: Mapping[str, Node]
    pins: Mapping[str, Pin]
    edges: tuple[Edge, ...]
    incoming_execution: Mapping[str, tuple[Edge, ...]]
    incoming_data_by_node: Mapping[str, tuple[Edge, ...]]
    incoming_data_by_pin: Mapping[str, tuple[Edge, ...]]


def _criterion_fail(message: str) -> NoReturn:
    raise M4ExecutionError("M4_CRITERION_INVALID", message)


def _invariant_fail(message: str, *, cause: Exception | None = None) -> NoReturn:
    raise M4ExecutionError("M4_SLICE_INVARIANT_FAILED", message, cause=cause)


def _find_graph(document: BlueprintDocument, criterion_node_id: str) -> Graph:
    matches = [
        graph
        for graph in document.graphs
        if any(node.id == criterion_node_id for node in graph.nodes)
    ]
    if len(matches) != 1:
        _criterion_fail(
            "criterion node must resolve in exactly one graph: "
            f"{criterion_node_id} (matches={len(matches)})"
        )
    return matches[0]


def _add_unique(index: dict[str, object], entity_id: str, entity: object) -> None:
    if entity_id in index:
        _invariant_fail(f"duplicate graph-local identity: {entity_id}")
    index[entity_id] = entity


def _sorted_edge_groups(
    grouped: Mapping[str, list[Edge]],
) -> Mapping[str, tuple[Edge, ...]]:
    return MappingProxyType(
        {
            key: tuple(sorted(values, key=lambda edge: edge.id))
            for key, values in sorted(grouped.items())
        }
    )


def _build_graph_index(graph: Graph) -> _GraphIndex:
    node_index: dict[str, Node] = {}
    pin_index: dict[str, Pin] = {}
    for node in sorted(graph.nodes, key=lambda item: item.id):
        _add_unique(node_index, node.id, node)
        if node.graph_id != graph.id:
            _invariant_fail(f"node {node.id} disagrees with containing graph {graph.id}")
        for pin in sorted(node.pins, key=lambda item: item.id):
            _add_unique(pin_index, pin.id, pin)
            if pin.node_id != node.id:
                _invariant_fail(f"pin {pin.id} disagrees with containing node {node.id}")
            if pin.direction not in {"input", "output"}:
                _invariant_fail(f"pin {pin.id} has invalid direction {pin.direction!r}")
            if pin.kind not in {"execution", "data"}:
                _invariant_fail(f"pin {pin.id} has invalid kind {pin.kind!r}")

    edge_index: dict[str, Edge] = {}
    incoming_execution_mutable: dict[str, list[Edge]] = defaultdict(list)
    incoming_data_node_mutable: dict[str, list[Edge]] = defaultdict(list)
    incoming_data_pin_mutable: dict[str, list[Edge]] = defaultdict(list)
    for edge in sorted(graph.edges, key=lambda item: item.id):
        _add_unique(edge_index, edge.id, edge)
        if edge.graph_id != graph.id:
            _invariant_fail(f"edge {edge.id} disagrees with containing graph {graph.id}")
        if edge.kind not in {"execution", "data"}:
            _invariant_fail(f"edge {edge.id} has invalid kind {edge.kind!r}")
        source_node = node_index.get(edge.source_node_id)
        target_node = node_index.get(edge.target_node_id)
        source_pin = pin_index.get(edge.source_pin_id)
        target_pin = pin_index.get(edge.target_pin_id)
        if source_node is None or target_node is None:
            _invariant_fail(f"edge {edge.id} has a dangling node endpoint")
        if source_pin is None or target_pin is None:
            _invariant_fail(f"edge {edge.id} has a dangling pin endpoint")
        if source_pin.node_id != source_node.id or target_pin.node_id != target_node.id:
            _invariant_fail(f"edge {edge.id} pin/node endpoints disagree")
        if source_pin.direction != "output" or target_pin.direction != "input":
            _invariant_fail(f"edge {edge.id} endpoint directions are invalid")
        if source_pin.kind != edge.kind or target_pin.kind != edge.kind:
            _invariant_fail(f"edge {edge.id} endpoint kinds disagree")
        if edge.kind == "execution":
            incoming_execution_mutable[edge.target_node_id].append(edge)
        else:
            incoming_data_node_mutable[edge.target_node_id].append(edge)
            incoming_data_pin_mutable[edge.target_pin_id].append(edge)

    return _GraphIndex(
        nodes=MappingProxyType(node_index),
        pins=MappingProxyType(pin_index),
        edges=tuple(value for _, value in sorted(edge_index.items())),
        incoming_execution=_sorted_edge_groups(incoming_execution_mutable),
        incoming_data_by_node=_sorted_edge_groups(incoming_data_node_mutable),
        incoming_data_by_pin=_sorted_edge_groups(incoming_data_pin_mutable),
    )


def _is_pure_data_producer(node: Node) -> bool:
    if not any(pin.kind == "execution" for pin in node.pins):
        return True
    return isinstance(node.symbol, Mapping) and node.symbol.get("is_pure") is True


def _freeze_reason_mapping(
    reasons: Mapping[str, set[str]],
) -> Mapping[str, tuple[str, ...]]:
    return MappingProxyType(
        {
            entity_id: tuple(sorted(entity_reasons))
            for entity_id, entity_reasons in sorted(reasons.items())
        }
    )


def _compute_execution_slice(
    document: BlueprintDocument, criterion_node_id: str
) -> ExecutionSlice:
    graph = _find_graph(document, criterion_node_id)
    index = _build_graph_index(graph)
    criterion = index.nodes.get(criterion_node_id)
    if criterion is None:
        _criterion_fail(f"criterion disappeared from graph index: {criterion_node_id}")
    if not any(pin.kind == "execution" for pin in criterion.pins):
        _criterion_fail(f"execution criterion has no execution pin: {criterion_node_id}")

    node_reasons: dict[str, set[str]] = defaultdict(set)
    edge_reasons: dict[str, set[str]] = defaultdict(set)
    selected: set[str] = {criterion_node_id}
    node_reasons[criterion_node_id].add("criterion")

    execution_queue: deque[str] = deque([criterion_node_id])
    execution_visited: set[str] = set()
    while execution_queue:
        target_node_id = execution_queue.popleft()
        if target_node_id in execution_visited:
            continue
        execution_visited.add(target_node_id)
        for edge in index.incoming_execution.get(target_node_id, ()):
            source_node_id = edge.source_node_id
            source_node = index.nodes[source_node_id]
            selected.add(source_node_id)
            node_reasons[source_node_id].add("execution_predecessor")
            edge_reasons[edge.id].add("execution_predecessor_relation")
            if source_node.semantic_status in _BOUNDARY_STATUSES:
                node_reasons[source_node_id].add(
                    f"{source_node.semantic_status}_boundary"
                )
                continue
            if source_node_id not in execution_visited:
                execution_queue.append(source_node_id)

    data_queue: deque[tuple[str, str]] = deque()
    root_pin_ids = sorted(
        pin.id
        for node_id in selected
        for pin in index.nodes[node_id].pins
        if pin.pin_role == "branch_condition"
        or (node_id == criterion_node_id and pin.pin_role == "variable_set_value")
    )
    for pin_id in root_pin_ids:
        for edge in index.incoming_data_by_pin.get(pin_id, ()):
            source_node_id = edge.source_node_id
            selected.add(source_node_id)
            node_reasons[source_node_id].add("required_data_producer")
            edge_reasons[edge.id].add("required_data_dependency_relation")
            data_queue.append((source_node_id, "required_data_producer"))

    data_visited: set[tuple[str, str]] = set()
    while data_queue:
        producer_node_id, inherited_reason = data_queue.popleft()
        visit = (producer_node_id, inherited_reason)
        if visit in data_visited:
            continue
        data_visited.add(visit)
        producer = index.nodes[producer_node_id]
        status = producer.semantic_status
        if status in _BOUNDARY_STATUSES:
            node_reasons[producer_node_id].add(f"{status}_boundary")
            is_opaque_call = status == "opaque" and producer.class_path.endswith(
                "K2Node_CallFunction"
            )
            if not is_opaque_call:
                continue
            next_node_reason = "opaque_input_producer"
            next_edge_reason = "opaque_input_dependency_relation"
        elif _is_pure_data_producer(producer):
            next_node_reason = inherited_reason
            next_edge_reason = "required_data_dependency_relation"
        else:
            continue

        for edge in index.incoming_data_by_node.get(producer_node_id, ()):
            source_node_id = edge.source_node_id
            selected.add(source_node_id)
            node_reasons[source_node_id].add(next_node_reason)
            edge_reasons[edge.id].add(next_edge_reason)
            data_queue.append((source_node_id, next_node_reason))

    selected_edges: list[Edge] = []
    for edge in index.edges:
        if edge.source_node_id not in selected or edge.target_node_id not in selected:
            continue
        selected_edges.append(edge)
        if not edge_reasons[edge.id]:
            edge_reasons[edge.id].add(
                "induced_internal_execution_relation"
                if edge.kind == "execution"
                else "induced_internal_data_relation"
            )

    boundaries = tuple(
        SliceBoundary(
            node_id=node_id,
            status=index.nodes[node_id].semantic_status,
            reason=index.nodes[node_id].semantic_reason,
        )
        for node_id in sorted(selected)
        if index.nodes[node_id].semantic_status in _BOUNDARY_STATUSES
    )

    selected_edge_ids = {edge.id for edge in selected_edges}
    if set(node_reasons) != selected or any(
        not reasons or not reasons <= NODE_REASON_VOCABULARY
        for reasons in node_reasons.values()
    ):
        _invariant_fail("selected nodes do not have complete approved reasons")
    if set(edge_reasons) != selected_edge_ids or any(
        not reasons or not reasons <= EDGE_REASON_VOCABULARY
        for reasons in edge_reasons.values()
    ):
        _invariant_fail("selected edges do not have complete approved reasons")
    if any(
        edge.source_node_id not in selected or edge.target_node_id not in selected
        for edge in selected_edges
    ):
        _invariant_fail("a selected edge crosses the selected-node set")
    expected_boundary_ids = {
        node_id
        for node_id in selected
        if index.nodes[node_id].semantic_status in _BOUNDARY_STATUSES
    }
    if {boundary.node_id for boundary in boundaries} != expected_boundary_ids:
        _invariant_fail("boundary accounting disagrees with selected nodes")

    return ExecutionSlice(
        graph_id=graph.id,
        criterion_node_id=criterion_node_id,
        node_ids=tuple(sorted(selected)),
        edge_ids=tuple(edge.id for edge in selected_edges),
        inclusion_reasons=_freeze_reason_mapping(node_reasons),
        edge_inclusion_reasons=_freeze_reason_mapping(edge_reasons),
        boundaries=boundaries,
    )


def compute_execution_slice(
    document: BlueprintDocument, criterion_node_id: str
) -> ExecutionSlice:
    """Compute one deterministic backward execution context in one source graph."""

    try:
        return _compute_execution_slice(document, criterion_node_id)
    except M4ExecutionError:
        raise
    except Exception as error:
        _invariant_fail(f"unexpected kernel failure: {error}", cause=error)
