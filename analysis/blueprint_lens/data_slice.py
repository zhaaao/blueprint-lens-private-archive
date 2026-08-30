"""Deterministic graph-local member-variable data slicing.

Indexing and traversal are ``O(V + P + E)``; deterministic normalization is
``O(V log V + E log E)``; working space is ``O(V + P + E)``. These are
algorithmic bounds for the bounded kernel, not measured performance evidence.
"""

from __future__ import annotations

from collections import defaultdict, deque
from dataclasses import dataclass, field
from types import MappingProxyType
from typing import Literal, Mapping, NoReturn

from .m5_errors import M5DataError
from .raw_probe import BlueprintDocument, Edge, Graph, Node, Pin


DATA_NODE_REASON_VOCABULARY = frozenset(
    {
        "member_get",
        "member_set",
        "required_data_producer",
        "opaque_input_producer",
        "direct_write_controller",
        "opaque_boundary",
        "uncertain_boundary",
        "unsupported_boundary",
    }
)

DATA_EDGE_REASON_VOCABULARY = frozenset(
    {
        "variable_set_value_dependency_relation",
        "branch_condition_dependency_relation",
        "required_data_dependency_relation",
        "opaque_input_dependency_relation",
        "direct_write_controller_relation",
        "induced_internal_execution_relation",
        "induced_internal_data_relation",
    }
)

_BOUNDARY_STATUSES = frozenset({"opaque", "uncertain", "unsupported"})


@dataclass(frozen=True, slots=True, order=True)
class DataSliceBoundary:
    node_id: str
    status: Literal["opaque", "uncertain", "unsupported"]
    reason: str


@dataclass(frozen=True, slots=True)
class MemberVariableDataSlice:
    graph_id: str
    member_guid: str
    member_name: str
    node_ids: tuple[str, ...]
    edge_ids: tuple[str, ...]
    inclusion_reasons: Mapping[str, tuple[str, ...]]
    edge_inclusion_reasons: Mapping[str, tuple[str, ...]] = field(
        default_factory=lambda: MappingProxyType({})
    )
    boundaries: tuple[DataSliceBoundary, ...] = ()


@dataclass(frozen=True, slots=True)
class _GraphIndex:
    nodes: Mapping[str, Node]
    pins: Mapping[str, Pin]
    edges: tuple[Edge, ...]
    incoming_execution: Mapping[str, tuple[Edge, ...]]
    incoming_data_by_node: Mapping[str, tuple[Edge, ...]]
    incoming_data_by_pin: Mapping[str, tuple[Edge, ...]]


def _criterion_fail(message: str) -> NoReturn:
    raise M5DataError("M5_CRITERION_INVALID", message)


def _invariant_fail(message: str, *, cause: Exception | None = None) -> NoReturn:
    raise M5DataError("M5_SLICE_INVARIANT_FAILED", message, cause=cause)


def _find_graph(document: BlueprintDocument, graph_id: str) -> Graph:
    matches = [graph for graph in document.graphs if graph.id == graph_id]
    if len(matches) != 1:
        _criterion_fail(
            f"graph must resolve exactly once: {graph_id} (matches={len(matches)})"
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
        if node.semantic_status not in {
            "supported",
            "unclassified",
            *_BOUNDARY_STATUSES,
        }:
            _invariant_fail(
                f"node {node.id} has invalid semantic status {node.semantic_status!r}"
            )
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


def _is_member_access(node: Node, member_guid: str) -> bool:
    symbol = node.symbol
    return bool(
        isinstance(symbol, Mapping)
        and symbol.get("kind") == "variable"
        and symbol.get("guid") == member_guid
        and symbol.get("access") in {"get", "set"}
        and symbol.get("is_local_scope") is False
    )


def _freeze_reason_mapping(
    reasons: Mapping[str, set[str]],
) -> Mapping[str, tuple[str, ...]]:
    return MappingProxyType(
        {
            entity_id: tuple(sorted(entity_reasons))
            for entity_id, entity_reasons in sorted(reasons.items())
        }
    )


def _compute_member_variable_data_slice(
    document: BlueprintDocument,
    graph_id: str,
    member_guid: str,
) -> MemberVariableDataSlice:
    graph = _find_graph(document, graph_id)
    index = _build_graph_index(graph)
    accesses = tuple(
        node
        for node in sorted(index.nodes.values(), key=lambda item: item.id)
        if _is_member_access(node, member_guid)
    )
    if not accesses:
        _criterion_fail(
            f"member variable has no accesses in graph (after non-local filtering): "
            f"{member_guid} in {graph_id}"
        )
    resolved_names = tuple(
        node.symbol.get("name")
        for node in accesses
        if isinstance(node.symbol, Mapping)
    )
    if any(not isinstance(name, str) or not name.strip() for name in resolved_names):
        _criterion_fail("member GUID resolves to a non-string or empty name")
    member_names = set(resolved_names)
    if len(member_names) != 1:
        _criterion_fail(
            f"member GUID resolves to inconsistent names: {sorted(member_names)}"
        )
    member_name = next(iter(member_names))

    selected: set[str] = set()
    node_reasons: dict[str, set[str]] = defaultdict(set)
    edge_reasons: dict[str, set[str]] = defaultdict(set)
    writes: list[Node] = []
    for node in accesses:
        selected.add(node.id)
        access = node.symbol.get("access")
        node_reasons[node.id].add(f"member_{access}")
        if access == "set":
            writes.append(node)

    data_queue: deque[str] = deque()
    for write in sorted(writes, key=lambda item: item.id):
        value_pins = [
            pin
            for pin in write.pins
            if pin.pin_role == "variable_set_value"
            and pin.direction == "input"
            and pin.kind == "data"
        ]
        if len(value_pins) != 1:
            _invariant_fail(
                f"member Set requires exactly one variable_set_value input: {write.id}"
            )
        for edge in index.incoming_data_by_pin.get(value_pins[0].id, ()):
            selected.add(edge.source_node_id)
            node_reasons[edge.source_node_id].add("required_data_producer")
            edge_reasons[edge.id].add("variable_set_value_dependency_relation")
            data_queue.append(edge.source_node_id)

        for edge in index.incoming_execution.get(write.id, ()):
            controller = index.nodes[edge.source_node_id]
            selected.add(controller.id)
            node_reasons[controller.id].add("direct_write_controller")
            edge_reasons[edge.id].add("direct_write_controller_relation")
            if controller.class_path.endswith("K2Node_IfThenElse"):
                condition_pins = [
                    pin
                    for pin in controller.pins
                    if pin.pin_role == "branch_condition"
                    and pin.direction == "input"
                    and pin.kind == "data"
                ]
                if len(condition_pins) != 1:
                    _invariant_fail(
                        "Branch controller requires exactly one branch_condition "
                        f"input: {controller.id}"
                    )
                for condition_edge in index.incoming_data_by_pin.get(
                    condition_pins[0].id, ()
                ):
                    selected.add(condition_edge.source_node_id)
                    node_reasons[condition_edge.source_node_id].add(
                        "required_data_producer"
                    )
                    edge_reasons[condition_edge.id].add(
                        "branch_condition_dependency_relation"
                    )
                    data_queue.append(condition_edge.source_node_id)

    expanded: set[str] = set()
    while data_queue:
        producer_node_id = data_queue.popleft()
        if producer_node_id in expanded:
            continue
        expanded.add(producer_node_id)
        producer = index.nodes[producer_node_id]
        status = producer.semantic_status
        if status in {"uncertain", "unsupported"}:
            node_reasons[producer_node_id].add(f"{status}_boundary")
            continue
        if status == "opaque":
            node_reasons[producer_node_id].add("opaque_boundary")
            if not producer.class_path.endswith("K2Node_CallFunction"):
                continue
            next_node_reason = "opaque_input_producer"
            next_edge_reason = "opaque_input_dependency_relation"
        else:
            next_node_reason = "required_data_producer"
            next_edge_reason = "required_data_dependency_relation"

        for edge in index.incoming_data_by_node.get(producer_node_id, ()):
            selected.add(edge.source_node_id)
            node_reasons[edge.source_node_id].add(next_node_reason)
            edge_reasons[edge.id].add(next_edge_reason)
            data_queue.append(edge.source_node_id)

    for node_id in sorted(selected):
        status = index.nodes[node_id].semantic_status
        if status in _BOUNDARY_STATUSES:
            node_reasons[node_id].add(f"{status}_boundary")

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
        DataSliceBoundary(
            node_id=node_id,
            status=index.nodes[node_id].semantic_status,
            reason=index.nodes[node_id].semantic_reason,
        )
        for node_id in sorted(selected)
        if index.nodes[node_id].semantic_status in _BOUNDARY_STATUSES
    )
    selected_edge_ids = {edge.id for edge in selected_edges}
    if set(node_reasons) != selected or any(
        not reasons or not reasons <= DATA_NODE_REASON_VOCABULARY
        for reasons in node_reasons.values()
    ):
        _invariant_fail("selected nodes do not have complete approved reasons")
    if set(edge_reasons) != selected_edge_ids or any(
        not reasons or not reasons <= DATA_EDGE_REASON_VOCABULARY
        for reasons in edge_reasons.values()
    ):
        _invariant_fail("selected edges do not have complete approved reasons")
    expected_boundary_ids = {
        node_id
        for node_id in selected
        if index.nodes[node_id].semantic_status in _BOUNDARY_STATUSES
    }
    if {boundary.node_id for boundary in boundaries} != expected_boundary_ids:
        _invariant_fail("boundary accounting disagrees with selected nodes")

    return MemberVariableDataSlice(
        graph_id=graph.id,
        member_guid=member_guid,
        member_name=member_name,
        node_ids=tuple(sorted(selected)),
        edge_ids=tuple(edge.id for edge in selected_edges),
        inclusion_reasons=_freeze_reason_mapping(node_reasons),
        edge_inclusion_reasons=_freeze_reason_mapping(edge_reasons),
        boundaries=boundaries,
    )


def compute_member_variable_data_slice(
    document: BlueprintDocument,
    graph_id: str,
    member_guid: str,
) -> MemberVariableDataSlice:
    """Compute one deterministic frozen-v1 member slice in one source graph."""

    try:
        return _compute_member_variable_data_slice(document, graph_id, member_guid)
    except M5DataError:
        raise
    except (KeyError, TypeError, ValueError, AttributeError) as error:
        _invariant_fail(f"unexpected kernel failure: {error}", cause=error)
