"""Build deterministic Blueprint Lens v1 typed IR from raw exporter JSON."""

from __future__ import annotations

from collections import Counter
from copy import deepcopy
from typing import Any, Mapping


_SUPPORTED_CLASSES = {
    "K2Node_CustomEvent",
    "K2Node_Event",
    "K2Node_ExecutionSequence",
    "K2Node_FunctionEntry",
    "K2Node_FunctionResult",
    "K2Node_IfThenElse",
    "K2Node_Knot",
    "K2Node_VariableGet",
    "K2Node_VariableSet",
}
_SUPPORTED_OPERATORS = {"Add_IntInt", "Subtract_IntInt", "Greater_IntInt"}


class TypedIRBuildError(ValueError):
    """Raised when a raw export cannot be promoted to typed IR v1."""


def classify_raw_node(node: Mapping[str, Any]) -> tuple[str, str]:
    """Classify one raw node using the frozen supported-node matrix v1."""

    short_class = str(node["class"]).rsplit(".", 1)[-1]
    symbol = node.get("symbol") or {}
    if symbol.get("kind") == "function" and symbol.get("is_latent") is True:
        return "unsupported", "latent_function"
    if short_class == "K2Node_CallFunction":
        return "opaque", "function_body_not_expanded"
    if short_class == "K2Node_MacroInstance":
        return "opaque", "macro_body_not_expanded"
    if short_class == "K2Node_PromotableOperator":
        if symbol.get("name") in _SUPPORTED_OPERATORS:
            return "supported", ""
        return "uncertain", "operator_not_in_supported_matrix_v1"
    if short_class in {"K2Node_VariableGet", "K2Node_VariableSet"}:
        if symbol.get("guid") and not symbol.get("is_local_scope", False):
            return "supported", ""
        return "uncertain", "non_member_variable_access"
    if short_class in _SUPPORTED_CLASSES:
        return "supported", ""
    return "uncertain", "node_family_not_in_supported_matrix_v1"


def _canonicalize_entity_order(blueprint: dict[str, Any]) -> None:
    graphs = blueprint.get("graphs")
    if not isinstance(graphs, list):
        raise TypedIRBuildError("raw blueprint.graphs must be an array")
    for graph in graphs:
        if not isinstance(graph, dict):
            raise TypedIRBuildError("raw graph must be an object")
        nodes = graph.get("nodes")
        edges = graph.get("edges")
        if not isinstance(nodes, list) or not isinstance(edges, list):
            raise TypedIRBuildError("raw graph nodes/edges must be arrays")
        for node in nodes:
            if not isinstance(node, dict) or not isinstance(node.get("pins"), list):
                raise TypedIRBuildError("raw node and pins must be objects/arrays")
            for pin in node["pins"]:
                if not isinstance(pin, dict):
                    raise TypedIRBuildError("raw pin must be an object")
                links = pin.get("links")
                if isinstance(links, list):
                    links.sort()
            node["pins"].sort(key=lambda pin: str(pin.get("id", "")))
        nodes.sort(key=lambda node: str(node.get("id", "")))
        edges.sort(key=lambda edge: str(edge.get("id", "")))
    graphs.sort(key=lambda graph: str(graph.get("id", "")))


def build_typed_ir(
    raw: Mapping[str, Any],
    *,
    expected_blueprint_path: str | None = None,
    canonicalize_entity_order: bool = True,
) -> dict[str, Any]:
    """Promote raw format 0.2 to deterministic typed IR schema v1."""

    if raw.get("format") != "blueprint-lens-raw-probe":
        raise TypedIRBuildError("source must be a raw Blueprint Lens export")
    if raw.get("format_version") != "0.2":
        raise TypedIRBuildError("source must use raw format version 0.2")
    blueprint_value = raw.get("blueprint")
    if not isinstance(blueprint_value, Mapping):
        raise TypedIRBuildError("raw blueprint must be an object")
    if (
        expected_blueprint_path is not None
        and blueprint_value.get("path") != expected_blueprint_path
    ):
        raise TypedIRBuildError("source Blueprint path does not match contract")
    counts = raw.get("counts")
    if not isinstance(counts, Mapping):
        raise TypedIRBuildError("raw counts must be an object")

    blueprint = deepcopy(dict(blueprint_value))
    if canonicalize_entity_order:
        _canonicalize_entity_order(blueprint)
    statuses: Counter[str] = Counter()
    for graph in blueprint["graphs"]:
        for node in graph["nodes"]:
            status, reason = classify_raw_node(node)
            node["semantic_status"] = status
            node["semantic_reason"] = reason
            statuses[status] += 1

    return {
        "format": "blueprint-lens",
        "schema_version": "1.0.0",
        "engine_version": raw["engine_version"],
        "blueprint": blueprint,
        "counts": {
            "graphs": counts["graphs"],
            "nodes": counts["nodes"],
            "pins": counts["pins"],
            "edges": counts["edges"],
            "supported_nodes": statuses["supported"],
            "opaque_nodes": statuses["opaque"],
            "uncertain_nodes": statuses["uncertain"],
            "unsupported_nodes": statuses["unsupported"],
        },
    }
