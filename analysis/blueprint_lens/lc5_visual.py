"""Frozen-truth visual ledger for the bounded LC5 static pure-call profile."""

from __future__ import annotations

from copy import deepcopy
from fractions import Fraction
import hashlib
import html
import json
import math
from pathlib import Path
import re
import shutil
import tempfile
from types import MappingProxyType
from typing import Any, Mapping

from PIL import Image, ImageDraw, ImageFont


class LC5VisualError(ValueError):
    """Raised when an LC5 visual projection violates frozen static-call truth."""


_FRONTIER = (
    "Frontier · depth 1 · macro, impure, latent, cross-Blueprint and dynamic dispatch excluded"
)
_ACTIONS = (
    {"action_id": "open_source", "label": "Open source"},
    {"action_id": "select", "label": "Select"},
    {"action_id": "show_complete_text", "label": "Show complete text"},
    {"action_id": "show_evidence", "label": "Show evidence"},
)
_READINESS_BOUNDARIES = (
    "Occurrences are static contextual occurrences and do not prove runtime invocations.",
    "Macro, impure, latent, cross-Blueprint and dynamic-dispatch calls remain outside this profile.",
    "Core-v1 remains opaque/function_body_not_expanded.",
    "No visual condition, Slate portal, comprehension, scalability or product-default claim follows.",
)
_CORE_V1_OUTCOME = "opaque / function_body_not_expanded"
_BINDING_CONTRACT = (
    (0, "argument", "argument_bind", "CurrentHealth", "int32", "input"),
    (1, "argument", "argument_bind", "Bonus", "int32", "input"),
    (2, "result", "result_bind", "NewHealth", "int32", "return"),
)
_INTERNAL_EDGE_CONTRACT = (
    ("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::edge::/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::7f7af0cb-4b10-02dc-5658-2cbe99e8ffd7::pin::locator-output-Bonus-0->/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::b8a8574b-4461-61a3-ead7-dcbca3344400::pin::locator-input-B-0", "data"),
    ("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::edge::/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::7f7af0cb-4b10-02dc-5658-2cbe99e8ffd7::pin::locator-output-CurrentHealth-0->/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::b8a8574b-4461-61a3-ead7-dcbca3344400::pin::locator-input-A-0", "data"),
    ("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::edge::/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::7f7af0cb-4b10-02dc-5658-2cbe99e8ffd7::pin::locator-output-then-0->/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::eed3df57-4cad-64ee-819a-0687bebbc9a6::pin::locator-input-execute-0", "execution"),
    ("/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::edge::/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::b8a8574b-4461-61a3-ead7-dcbca3344400::pin::locator-output-ReturnValue-0->/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:CalculateRecovery::node::eed3df57-4cad-64ee-819a-0687bebbc9a6::pin::locator-input-NewHealth-0", "data"),
)
_INTERNAL_EDGE_IDS = tuple(item[0] for item in _INTERNAL_EDGE_CONTRACT)
_INTERNAL_KIND_BY_EDGE = dict(_INTERNAL_EDGE_CONTRACT)

CONDITIONS = MappingProxyType({
    "LC5_COMPLETE_TEXT": (
        "Complete Text",
        "permanent_control",
        "lc5-complete-text-effect-700",
    ),
    "LC5_TYPED_PORTAL_BRIDGE": (
        "Typed Portal Bridge",
        "atlas_adaptation:DPA-03",
        "lc5-typed-portal-bridge-effect-700",
    ),
    "LC5_ADJACENT_COMPOUND_EXPANSION": (
        "Adjacent Compound Expansion",
        "atlas_adaptation:DPA-12",
        "lc5-adjacent-compound-expansion-effect-700",
    ),
    "LC5_PERSISTENT_CONTEXT_REGIONS": (
        "Persistent Context Regions",
        "atlas_adaptation:DPA-11+DPA-09",
        "lc5-persistent-context-regions-effect-700",
    ),
    "LC5_CALL_RETURN_WEAVE": (
        "Call–Return Weave",
        "project_synthesis:Ishio_query_paths",
        "lc5-call-return-weave-effect-700",
    ),
})
CONDITION_IDS = tuple(CONDITIONS)
_ENGINEERING_RECOMMENDATION_ID = "LC5_TYPED_PORTAL_BRIDGE"
_AUTHORING_EVIDENCE_STATE = "authoring_design_target"

CANVAS = (700, 1000)
BANDS = {
    "header": (24, 24, 652, 124),
    "plot": (24, 164, 652, 610),
    "frontier": (24, 798, 652, 92),
    "actions": (24, 914, 652, 62),
}
TOKENS = {
    "background": "#0E1117", "surface": "#171C24", "caller_fill": "#172231",
    "callee_fill": "#211C2E", "text": "#F2F5F8", "muted": "#A9B3C1",
    "enter": "#67B7FF", "argument": "#F0B35A", "internal": "#A7D46F",
    "result": "#D997FF", "frontier": "#F07178", "radius": 10,
    "stroke": 2, "route": 3, "margin": 24, "gap": 16,
}
FONTS = MappingProxyType({
    "regular": "C:/Windows/Fonts/segoeui.ttf",
    "bold": "C:/Windows/Fonts/segoeuib.ttf",
})
LAYOUTS = MappingProxyType({
    "LC5_COMPLETE_TEXT": {"body": (36, 184, 628, 570), "rail_x": 52},
    "LC5_TYPED_PORTAL_BRIDGE": {
        "caller": (36, 184, 188, 570), "portal_x": 254, "callee": (292, 184, 372, 570),
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "caller": (36, 238, 180, 260), "bracket_x": 248, "callee": (284, 184, 380, 570),
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "caller": (36, 184, 246, 570), "callee": (314, 184, 350, 570),
    },
    "LC5_CALL_RETURN_WEAVE": {
        "caller_x": 54, "portal_x": 252, "callee_x": 350, "return_x": 628,
    },
})
REQUIRED_PRIMITIVES = MappingProxyType({
    "LC5_COMPLETE_TEXT": frozenset({"complete.reading.rail", "complete.relation.index"}),
    "LC5_TYPED_PORTAL_BRIDGE": frozenset({
        "portal.boundary", "portal.enter.arrow", "portal.input.current_health",
        "portal.input.bonus", "portal.output.new_health", "portal.return.arrow",
    }),
    "LC5_ADJACENT_COMPOUND_EXPANSION": frozenset({
        "adjacent.call.anchor", "adjacent.expansion.bracket", "adjacent.callee.compound",
        "adjacent.return.route",
    }),
    "LC5_PERSISTENT_CONTEXT_REGIONS": frozenset({
        "context.caller.region", "context.callee.region", "context.enter.route",
        "context.return.route", "context.source.anchors",
    }),
    "LC5_CALL_RETURN_WEAVE": frozenset({
        "weave.enter.route", "weave.argument.current_health", "weave.argument.bonus",
        "weave.result.new_health", "weave.return.route",
    }),
})
_REQUIRED_PRIMITIVE_KINDS = MappingProxyType({
    "complete.reading.rail": "polyline",
    "complete.relation.index": "text",
    "portal.boundary": "polyline",
    "portal.enter.arrow": "polyline",
    "portal.input.current_health": "polyline",
    "portal.input.bonus": "polyline",
    "portal.output.new_health": "polyline",
    "portal.return.arrow": "polyline",
    "adjacent.call.anchor": "circle",
    "adjacent.expansion.bracket": "polyline",
    "adjacent.callee.compound": "rect",
    "adjacent.return.route": "polyline",
    "context.caller.region": "rect",
    "context.callee.region": "rect",
    "context.enter.route": "polyline",
    "context.return.route": "polyline",
    "context.source.anchors": "polyline",
    "weave.enter.route": "polyline",
    "weave.argument.current_health": "polyline",
    "weave.argument.bonus": "polyline",
    "weave.result.new_health": "polyline",
    "weave.return.route": "polyline",
})
_REQUIRED_COMPOSITION_COMMANDS = MappingProxyType({
    "LC5_COMPLETE_TEXT": frozenset(),
    "LC5_TYPED_PORTAL_BRIDGE": frozenset({"portal.caller.region", "portal.callee.region"}),
    "LC5_ADJACENT_COMPOUND_EXPANSION": frozenset({"adjacent.caller.region"}),
    "LC5_PERSISTENT_CONTEXT_REGIONS": frozenset(),
    "LC5_CALL_RETURN_WEAVE": frozenset({
        "weave.caller.axis", "weave.portal.axis", "weave.callee.axis",
    }),
})
_QUESTION = (
    "How does CalculateRecovery use CurrentHealth and Bonus to produce NewHealth, "
    "and how do those values correspond across the call boundary?"
)
_READER_LABELS = frozenset({
    "CalculateRecovery",
    "Entry · CurrentHealth, Bonus",
    "CurrentHealth + Bonus",
    "Return · NewHealth",
})
_CALLEE_CONTEXT_ID = "lc5-context-422dac1c06037a2eee1916cb"
_CALLER_CONTEXT_ID = "lc5-context-e3b0c44298fc1c149afbf4c8"
_ENTRY_SOURCE_ID = _INTERNAL_EDGE_IDS[0].split("::edge::", 1)[1].split("::pin::", 1)[0]
_ADDITION_SOURCE_ID = _INTERNAL_EDGE_IDS[0].split("->", 1)[1].rsplit("::pin::", 1)[0]
_RESULT_SOURCE_ID = _INTERNAL_EDGE_IDS[2].split("->", 1)[1].rsplit("::pin::", 1)[0]
_CALL_SOURCE_ID = (
    "/Game/Probe/BP_SlicingProbe.BP_SlicingProbe:EventGraph::node::"
    "efbd1d7a-47d3-fd55-7775-26bd488ee92d"
)
_EXPECTED_SEMANTIC_UNITS = MappingProxyType({
    "CalculateRecovery": {
        "occurrence_id": f"{_CALL_SOURCE_ID}::occurrence::{_CALLER_CONTEXT_ID}",
        "source_node_id": _CALL_SOURCE_ID,
        "role": "call_site",
    },
    "Entry · CurrentHealth, Bonus": {
        "occurrence_id": f"{_ENTRY_SOURCE_ID}::occurrence::{_CALLEE_CONTEXT_ID}",
        "source_node_id": _ENTRY_SOURCE_ID,
        "role": "callee",
    },
    "CurrentHealth + Bonus": {
        "occurrence_id": f"{_ADDITION_SOURCE_ID}::occurrence::{_CALLEE_CONTEXT_ID}",
        "source_node_id": _ADDITION_SOURCE_ID,
        "role": "callee",
    },
    "Return · NewHealth": {
        "occurrence_id": f"{_RESULT_SOURCE_ID}::occurrence::{_CALLEE_CONTEXT_ID}",
        "source_node_id": _RESULT_SOURCE_ID,
        "role": "callee",
    },
})
_EXPECTED_BINDING_IDS = (
    "binding:argument:0:/Game/Probe/BP_SlicingProbe.BP_SlicingProbe_C:CalculateRecovery:CurrentHealth",
    "binding:argument:1:/Game/Probe/BP_SlicingProbe.BP_SlicingProbe_C:CalculateRecovery:Bonus",
    "binding:result:2:/Game/Probe/BP_SlicingProbe.BP_SlicingProbe_C:CalculateRecovery:NewHealth",
)
_EXPECTED_RELATION_FAMILIES = MappingProxyType({
    f"context:call_enter:{_EXPECTED_SEMANTIC_UNITS['CalculateRecovery']['occurrence_id']}"
    f"->{_EXPECTED_SEMANTIC_UNITS['Entry · CurrentHealth, Bonus']['occurrence_id']}": "enter",
    _EXPECTED_BINDING_IDS[0]: "argument",
    _EXPECTED_BINDING_IDS[1]: "argument",
    **{f"internal:{edge_id}": "internal" for edge_id in _INTERNAL_EDGE_IDS},
    _EXPECTED_BINDING_IDS[2]: "result",
    f"context:call_return:{_EXPECTED_SEMANTIC_UNITS['Return · NewHealth']['occurrence_id']}"
    f"->{_EXPECTED_SEMANTIC_UNITS['CalculateRecovery']['occurrence_id']}": "return",
})
_EXPECTED_INFORMATION_SET = {
    "profile_id": "LC5_INTRA_BP_PURE_CALL_V1",
    "occurrence_ids": sorted(item["occurrence_id"] for item in _EXPECTED_SEMANTIC_UNITS.values()),
    "binding_ids": sorted(_EXPECTED_BINDING_IDS),
    "relation_ids": sorted(_EXPECTED_RELATION_FAMILIES),
    "source_node_ids": sorted(item["source_node_id"] for item in _EXPECTED_SEMANTIC_UNITS.values()),
    "boundary_text": [*_READINESS_BOUNDARIES, _FRONTIER],
    "action_ids": sorted(item["action_id"] for item in _ACTIONS),
}
_TEXT_MEASURE_IMAGE = Image.new("L", (1, 1))
_TEXT_MEASURE_DRAW = ImageDraw.Draw(_TEXT_MEASURE_IMAGE)


def _binding_id(binding: Mapping[str, Any]) -> str:
    return f'binding:{binding["kind"]}:{binding["ordinal"]}:{binding["property"]["path"]}'


def _boundary_id(relation: Mapping[str, Any]) -> str:
    return f'context:{relation["kind"]}:{relation["source_occurrence_id"]}->{relation["target_occurrence_id"]}'


def _internal_id(relation: Mapping[str, Any]) -> str:
    return f'internal:{relation["source_edge_id"]}'


_EDGE_ID = re.compile(
    r"^(?P<graph>.+)::edge::(?P<source_node>.+)::pin::"
    r"(?P<source_pin>locator-(?:input|output)-[A-Za-z0-9_]+-[0-9]+)"
    r"->(?P<target_node>.+)::pin::"
    r"(?P<target_pin>locator-(?:input|output)-[A-Za-z0-9_]+-[0-9]+)$"
)


def _validate_internal_edge_identity(
    relation: Mapping[str, Any], occurrence_by_id: Mapping[str, Mapping[str, Any]]
) -> None:
    """Parse and reconstruct a contextual source edge without consulting fixture edges."""

    source_node_id = occurrence_by_id[relation["source_occurrence_id"]]["source_node_id"]
    target_node_id = occurrence_by_id[relation["target_occurrence_id"]]["source_node_id"]
    match = _EDGE_ID.fullmatch(relation.get("source_edge_id", ""))
    _require(match is not None, "internal source edge is not a canonical identity")
    fields = match.groupdict()
    source_graph_id = source_node_id.rsplit("::node::", 1)[0]
    _require(
        fields["graph"] == source_graph_id
        and fields["source_node"] == source_node_id
        and fields["target_node"] == target_node_id,
        "internal source edge endpoints differ from contextual occurrences",
    )
    canonical = (
        f'{fields["graph"]}::edge::{fields["source_node"]}::pin::{fields["source_pin"]}'
        f'->{fields["target_node"]}::pin::{fields["target_pin"]}'
    )
    _require(relation["source_edge_id"] == canonical, "internal source edge identity differs")


def _validate_internal_relations(
    relations: list[Mapping[str, Any]], occurrence_by_id: Mapping[str, Mapping[str, Any]], *, ordered: bool
) -> None:
    """Require the complete immutable LC5 source-edge inventory and identities."""

    edge_ids = [item.get("source_edge_id") for item in relations]
    _require(len(relations) == 4 and len(set(edge_ids)) == 4 and set(edge_ids) == set(_INTERNAL_EDGE_IDS),
             "internal source edge set differs")
    if ordered:
        _require(tuple(edge_ids) == _INTERNAL_EDGE_IDS, "internal source edge order differs")
    for relation in relations:
        _require(
            relation.get("source_occurrence_id") in occurrence_by_id
            and relation.get("target_occurrence_id") in occurrence_by_id,
            "internal source edge escapes contextual occurrences",
        )
        _require(relation.get("kind") == _INTERNAL_KIND_BY_EDGE[relation["source_edge_id"]],
                 "internal source edge kind differs")
        _validate_internal_edge_identity(relation, occurrence_by_id)


def _relation_projection(owner: Mapping[str, Any]) -> dict[str, Any]:
    """Return the immutable relation payload exposed by the flattened ledger."""

    return deepcopy(dict(owner))


def _load_json(path: str | Path) -> dict[str, Any]:
    try:
        return json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise LC5VisualError(f"cannot read frozen LC5 input: {path}") from error


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise LC5VisualError(message)


def _fixture_node_lookup(fixture: Mapping[str, Any]) -> tuple[dict[str, Mapping[str, Any]], set[str]]:
    nodes: dict[str, Mapping[str, Any]] = {}
    duplicate_nodes: set[str] = set()
    try:
        graphs = fixture["blueprint"]["graphs"]
    except (KeyError, TypeError) as error:
        raise LC5VisualError("display fixture has no Blueprint graphs") from error
    for graph in graphs:
        for node in graph.get("nodes", []):
            node_id = node.get("id")
            if node_id in nodes:
                duplicate_nodes.add(node_id)
            else:
                nodes[node_id] = node
    return nodes, duplicate_nodes


def _source_title(node: Mapping[str, Any]) -> str:
    title = node.get("title")
    _require(isinstance(title, str) and title, "display fixture node lacks its source title")
    return title


def _find_one(items: list[Mapping[str, Any]], kind: str) -> Mapping[str, Any]:
    matching = [item for item in items if item.get("kind") == kind]
    _require(len(matching) == 1, f"expected exactly one {kind} relation")
    return matching[0]


def _validate_contextual_contract(
    bindings: list[dict[str, Any]], context_relations: list[dict[str, Any]], occurrence_ids: set[str]
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Bind all static context and property relations to the frozen LC5 contract."""

    _require(len(context_relations) == 5, "context relation cardinality differs")
    kinds = [item.get("kind") for item in context_relations]
    _require(kinds.count("call_enter") == 1 and kinds.count("call_return") == 1
             and kinds.count("argument_bind") == 2 and kinds.count("result_bind") == 1,
             "context relation kinds differ")
    call_enter = dict(_find_one(context_relations, "call_enter"))
    call_return = dict(_find_one(context_relations, "call_return"))
    _require(
        call_enter.get("claim_scope") == "static_context_boundary_not_runtime_event"
        and call_return.get("claim_scope") == "static_context_boundary_not_runtime_event",
        "context boundary scope differs",
    )
    call_site_id = call_enter.get("source_occurrence_id")
    entry_id = call_enter.get("target_occurrence_id")
    result_id = call_return.get("source_occurrence_id")
    _require(
        call_site_id in occurrence_ids and entry_id in occurrence_ids and result_id in occurrence_ids
        and call_return.get("target_occurrence_id") == call_site_id,
        "call context endpoints differ",
    )
    _require(len(bindings) == 3, "contextual bindings differ")
    by_ordinal = {item.get("ordinal"): item for item in bindings}
    _require(len(by_ordinal) == 3 and set(by_ordinal) == {0, 1, 2}, "LC5 binding ordinals differ")
    for ordinal, kind, relation_kind, name, cpp_type, direction in _BINDING_CONTRACT:
        binding = by_ordinal[ordinal]
        _require(
            binding.get("kind") == kind
            and binding.get("relation_kind") == relation_kind
            and binding.get("property", {}).get("name") == name
            and binding.get("property", {}).get("cpp_type") == cpp_type
            and binding.get("property", {}).get("direction") == direction,
            "LC5 binding contract differs",
        )
        source_id = binding.get("source_occurrence_id")
        target_id = binding.get("target_occurrence_id")
        _require(source_id in occurrence_ids and target_id in occurrence_ids, "binding endpoint escapes occurrences")
        if kind == "argument":
            _require(source_id == call_site_id and target_id == entry_id, "argument binding ownership differs")
        else:
            _require(source_id == result_id and target_id == call_site_id, "result binding ownership differs")
        matching_contexts = [
            item for item in context_relations
            if item.get("kind") == relation_kind
            and item.get("source_occurrence_id") == source_id
            and item.get("target_occurrence_id") == target_id
        ]
        required_count = 2 if relation_kind == "argument_bind" else 1
        _require(len(matching_contexts) == required_count, "binding lacks matching context relation")
        _require(
            all(item.get("claim_scope") == "static_parameter_binding" for item in matching_contexts),
            "parameter binding scope differs",
        )
    return call_enter, call_return


def _reader_labels(
    entry_id: str,
    addition_id: str,
    result_id: str,
    call_site_id: str,
    bindings: list[dict[str, Any]],
) -> dict[str, str]:
    arguments = sorted(
        (item for item in bindings if item["kind"] == "argument"), key=lambda item: item["ordinal"]
    )
    _require(len(arguments) == 2 and len([item for item in bindings if item["kind"] == "result"]) == 1,
             "LC5 binding roles differ")
    _require(call_site_id not in {entry_id, addition_id, result_id}, "call-site identity overlaps callee")
    _require(
        [item["property"]["name"] for item in arguments] == ["CurrentHealth", "Bonus"]
        and bindings[-1]["property"]["name"] == "NewHealth",
        "LC5 binding names differ",
    )
    return {
        call_site_id: "CalculateRecovery",
        entry_id: "Entry · CurrentHealth, Bonus",
        addition_id: "CurrentHealth + Bonus",
        result_id: "Return · NewHealth",
    }


def load_lc5_visual_ledger(
    contextual_path: str | Path, readiness_path: str | Path, fixture_path: str | Path
) -> dict[str, Any]:
    """Load the frozen static-call facts into a deterministic display ledger."""

    contextual_file = Path(contextual_path)
    readiness_file = Path(readiness_path)
    contextual = _load_json(contextual_file)
    readiness = _load_json(readiness_file)
    fixture = _load_json(fixture_path)
    contextual_hash = _sha256(contextual_file)

    _require(readiness.get("status") == "TRUTH_FROZEN", "readiness is not truth frozen")
    _require(readiness.get("scope") == "LC5_INTRA_BP_PURE_CALL_V1", "readiness scope differs")
    _require(all(readiness.get("checks", {}).values()) and readiness.get("checks"), "readiness checks differ")
    _require(
        readiness.get("counts", {}) == {
            "binding_count": 3,
            "candidate_count": 1,
            "contextual_occurrence_count": 4,
            "internal_relation_count": 4,
            "mutation_case_count": 15,
            "native_run_count": 2,
        },
        "readiness counts differ",
    )
    _require(readiness.get("core_v1_outcome") == _CORE_V1_OUTCOME, "readiness core-v1 outcome differs")
    _require(tuple(readiness.get("boundaries", [])) == _READINESS_BOUNDARIES, "readiness boundaries differ")
    _require(readiness.get("hashes", {}).get("contextual_slice_sha256") == contextual_hash,
             "readiness does not bind contextual bytes")

    _require(contextual.get("profile_id") == readiness["scope"], "contextual profile differs")
    _require(contextual.get("status") == "resolved_unique", "contextual status is not resolved_unique")
    _require(contextual.get("max_call_depth") == 1, "contextual call depth differs")
    _require(contextual.get("reason") == "", "contextual reason must be empty")
    occurrences = [deepcopy(item) for item in contextual.get("occurrences", [])]
    occurrence_ids = [item.get("occurrence_id") for item in occurrences]
    _require(len(occurrences) == 4 and len(set(occurrence_ids)) == 4 and all(occurrence_ids),
             "contextual occurrences are missing or duplicated")
    source_node_ids = [item.get("source_node_id") for item in occurrences]
    _require(len(set(source_node_ids)) == 4 and all(source_node_ids), "contextual source node identities differ")
    occurrence_by_id = {item["occurrence_id"]: item for item in occurrences}

    bindings = [deepcopy(item) for item in contextual.get("bindings", [])]
    for binding in bindings:
        binding["binding_id"] = _binding_id(binding)
    binding_ids = [item["binding_id"] for item in bindings]
    _require(len(bindings) == 3 and len(set(binding_ids)) == 3, "contextual bindings differ")

    internal_relations = [deepcopy(item) for item in contextual.get("internal_relations", [])]
    for relation in internal_relations:
        relation["relation_id"] = _internal_id(relation)
    _validate_internal_relations(internal_relations, occurrence_by_id, ordered=False)

    context_source = [deepcopy(item) for item in contextual.get("context_relations", [])]
    call_enter, call_return = _validate_contextual_contract(bindings, context_source, set(occurrence_ids))
    for relation in context_source:
        if relation["kind"] in {"call_enter", "call_return"}:
            relation["relation_id"] = _boundary_id(relation)
    call_enter = _find_one(context_source, "call_enter")
    call_return = _find_one(context_source, "call_return")

    nodes, duplicate_node_ids = _fixture_node_lookup(fixture)
    for occurrence in occurrences:
        source_node_id = occurrence.get("source_node_id")
        node = nodes.get(source_node_id)
        _require(node is not None and source_node_id not in duplicate_node_ids,
                 "contextual source node ID is absent or duplicated in display fixture")
        occurrence["source_title"] = _source_title(node)
    entry_id = call_enter["target_occurrence_id"]
    result_id = call_return["source_occurrence_id"]
    call_site_id = call_enter["source_occurrence_id"]
    _require(call_return["target_occurrence_id"] == call_site_id, "call return does not close at call site")
    callee_ids = set(occurrence_by_id) - {call_site_id, entry_id, result_id}
    _require(len(callee_ids) == 1, "cannot derive one addition occurrence")
    addition_id = next(iter(callee_ids))
    labels = _reader_labels(entry_id, addition_id, result_id, call_site_id, sorted(bindings, key=lambda item: item["ordinal"]))
    for occurrence in occurrences:
        occurrence["reader_label"] = labels[occurrence["occurrence_id"]]
        occurrence["provenance"] = {"occurrence_ids": [occurrence["occurrence_id"]], "binding_ids": []}
    for occurrence_id, binding_ids_for_label in {
        entry_id: [item["binding_id"] for item in bindings if item["kind"] == "argument"],
        addition_id: [item["binding_id"] for item in bindings if item["kind"] == "argument"],
        result_id: [item["binding_id"] for item in bindings if item["kind"] == "result"],
        call_site_id: [item["binding_id"] for item in bindings],
    }.items():
        occurrence_by_id[occurrence_id]["provenance"]["binding_ids"] = sorted(binding_ids_for_label)

    argument_bindings = sorted((item for item in bindings if item["kind"] == "argument"), key=lambda item: item["ordinal"])
    result_binding = next(item for item in bindings if item["kind"] == "result")
    for binding in bindings:
        binding["relation_id"] = binding["binding_id"]
    owners = [
        call_enter,
        *argument_bindings,
        *sorted(internal_relations, key=lambda item: item["source_edge_id"]),
        result_binding,
        call_return,
    ]
    relations = [_relation_projection(item) for item in owners]
    _require(len(relations) == 9 and len({item["relation_id"] for item in relations}) == 9,
             "LC5 relation ledger differs")

    boundaries = [*_READINESS_BOUNDARIES, _FRONTIER]
    ledger = {
        "format": "blueprint-lens-lc5-visual-ledger",
        "schema_version": "1.0.0",
        "profile_binding": {
            "profile_id": contextual["profile_id"],
            "contextual_sha256": contextual_hash,
            "readiness_sha256": _sha256(readiness_file),
            "core_v1_outcome": _CORE_V1_OUTCOME,
        },
        "occurrences": sorted(occurrences, key=lambda item: item["occurrence_id"]),
        "bindings": sorted(bindings, key=lambda item: item["binding_id"]),
        "internal_relations": sorted(internal_relations, key=lambda item: item["source_edge_id"]),
        "context_relations": sorted(context_source, key=lambda item: _boundary_id(item) if item["kind"] in {"call_enter", "call_return"} else f'{item["kind"]}:{item["source_occurrence_id"]}->{item["target_occurrence_id"]}'),
        "relations": relations,
        "boundaries": boundaries,
        "actions": [deepcopy(item) for item in _ACTIONS],
        "counts": {"occurrences": 4, "bindings": 3, "internal_relations": 4, "context_boundaries": 2, "relations": 9},
    }
    validate_lc5_visual_ledger(ledger)
    return ledger


def validate_lc5_visual_ledger(ledger: Mapping[str, Any]) -> None:
    """Recompute the frozen identity and relation contract of an LC5 ledger."""

    expected_counts = {"occurrences": 4, "bindings": 3, "internal_relations": 4, "context_boundaries": 2, "relations": 9}
    _require(ledger.get("counts") == expected_counts, "ledger counts differ")
    occurrences = ledger.get("occurrences", [])
    occurrence_ids = [item.get("occurrence_id") for item in occurrences]
    _require(len(occurrences) == 4 and len(set(occurrence_ids)) == 4, "ledger occurrences differ")
    source_node_ids = [item.get("source_node_id") for item in occurrences]
    _require(len(set(source_node_ids)) == 4 and all(source_node_ids), "ledger source node identities differ")
    bindings = ledger.get("bindings", [])
    _require(len(bindings) == 3 and {item.get("binding_id") for item in bindings} == {_binding_id(item) for item in bindings},
             "ledger binding identities differ")
    contexts = ledger.get("context_relations", [])
    call_enter, call_return = _validate_contextual_contract(
        bindings, [dict(item) for item in contexts], set(occurrence_ids)
    )
    internals = ledger.get("internal_relations", [])
    _require(len(internals) == 4 and {item.get("relation_id") for item in internals} == {_internal_id(item) for item in internals},
             "ledger internal relations differ")
    occurrence_by_id = {item["occurrence_id"]: item for item in occurrences}
    _validate_internal_relations(internals, occurrence_by_id, ordered=True)
    for relation in (call_enter, call_return):
        _require(relation.get("relation_id") == _boundary_id(relation), "ledger context ID differs")
    relations = ledger.get("relations", [])
    _require(len(relations) == 9 and len({item.get("relation_id") for item in relations}) == 9,
             "ledger relation identities differ")
    for relation in relations:
        _require(relation.get("source_occurrence_id") in occurrence_ids and relation.get("target_occurrence_id") in occurrence_ids,
                 "ledger relation escapes the four occurrences")
    _require(
        [item["relation_id"] for item in relations]
        == [
            call_enter["relation_id"],
            *[item["binding_id"] for item in sorted((item for item in bindings if item["kind"] == "argument"), key=lambda item: item["ordinal"])],
            *[item["relation_id"] for item in sorted(internals, key=lambda item: item["source_edge_id"])],
            next(item["binding_id"] for item in bindings if item["kind"] == "result"),
            call_return["relation_id"],
        ],
        "ledger relation family order differs",
    )
    expected_owners = [
        call_enter,
        *sorted((item for item in bindings if item["kind"] == "argument"), key=lambda item: item["ordinal"]),
        *sorted(internals, key=lambda item: item["source_edge_id"]),
        next(item for item in bindings if item["kind"] == "result"),
        call_return,
    ]
    _require(
        relations == [_relation_projection(item) for item in expected_owners],
        "ledger flattened relation payload differs from owner",
    )
    _require(tuple(ledger.get("actions", [])) == _ACTIONS, "ledger actions differ")
    _require(tuple(ledger.get("boundaries", [])) == (*_READINESS_BOUNDARIES, _FRONTIER),
             "ledger boundaries differ")
    _require(ledger.get("profile_binding", {}).get("core_v1_outcome") == _CORE_V1_OUTCOME,
             "ledger core-v1 outcome differs")


def information_set(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Return the canonical semantic content every LC5 visual condition must expose."""

    return {
        "profile_id": ledger["profile_binding"]["profile_id"],
        "occurrence_ids": sorted(item["occurrence_id"] for item in ledger["occurrences"]),
        "binding_ids": sorted(item["binding_id"] for item in ledger["bindings"]),
        "relation_ids": sorted(item["relation_id"] for item in ledger["relations"]),
        "source_node_ids": sorted(item["source_node_id"] for item in ledger["occurrences"]),
        "boundary_text": list(ledger["boundaries"]),
        "action_ids": sorted(item["action_id"] for item in ledger["actions"]),
    }


def _manifest_state(condition_id: str, matched: Mapping[str, Any]) -> dict[str, Any]:
    """Rebuild one immutable LC5 authoring-design state from its condition ID."""

    label, provenance_class, effect_stem = CONDITIONS[condition_id]
    return {
        "state_id": f"{condition_id}__700",
        "condition_id": condition_id,
        "condition_label": label,
        "width": 700,
        "information_set": deepcopy(dict(matched)),
        "effect_paths": {"svg": f"{effect_stem}.svg", "png": f"{effect_stem}.png"},
        "evidence_state": _AUTHORING_EVIDENCE_STATE,
        "responsive_states_deferred": [430, 480],
        "frontier_variants_deferred": True,
    }


def _manifest_conditions() -> list[dict[str, str]]:
    return [
        {
            "condition_id": condition_id,
            "label": label,
            "provenance_class": provenance_class,
            "role": (
                "engineering_recommendation"
                if condition_id == _ENGINEERING_RECOMMENDATION_ID
                else "information_matched_condition"
            ),
        }
        for condition_id, (label, provenance_class, _) in CONDITIONS.items()
    ]


def _expected_lc5_visual_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    validate_lc5_visual_ledger(ledger)
    matched = information_set(ledger)
    return {
        "format": "blueprint-lens-lc5-visual-manifest",
        "schema_version": "1.0.0",
        "status": "INFORMATION_MATCHED__FIVE_700PX_AUTHORING_TARGETS",
        "profile_binding": deepcopy(dict(ledger["profile_binding"])),
        "default_condition_id": None,
        "engineering_recommendation_id": _ENGINEERING_RECOMMENDATION_ID,
        "conditions": _manifest_conditions(),
        "target_widths_logical_px": [700],
        "responsive_states_deferred": [430, 480],
        "frontier_variants_deferred": True,
        "states": [_manifest_state(condition_id, matched) for condition_id in CONDITION_IDS],
        "non_claims": [
            "engineering recommendation is not a product default",
            "effects are authoring design targets, not Slate or UE-visible evidence",
            "no human comprehension, preference or scalability evidence is established",
        ],
    }


def build_lc5_visual_manifest(ledger: Mapping[str, Any]) -> dict[str, Any]:
    """Build the bounded five-condition, information-matched LC5 contract."""

    manifest = _expected_lc5_visual_manifest(ledger)
    validate_lc5_visual_manifest(manifest, ledger)
    return manifest


def validate_lc5_visual_manifest(
    manifest: Mapping[str, Any], ledger: Mapping[str, Any]
) -> None:
    """Reject every LC5 manifest deviation from frozen truth and target scope."""

    expected = _expected_lc5_visual_manifest(ledger)
    _require(
        manifest == expected,
        "LC5 visual manifest differs from the frozen five-condition contract",
    )


def _text_command(
    command_id: str,
    text: str,
    x: int,
    y: int,
    *,
    size: int = 11,
    weight: str = "regular",
    fill: str | None = None,
    **extra: Any,
) -> dict[str, Any]:
    """Create one renderer-ready text command with Pillow-measured Segoe bounds."""

    _require(weight in FONTS, "unknown LC5 font weight")
    font = ImageFont.truetype(FONTS[weight], size)
    bounds = list(_TEXT_MEASURE_DRAW.textbbox((x, y), text, font=font, anchor="lt"))
    return {
        "id": command_id,
        "kind": "text",
        "text": text,
        "x": x,
        "y": y,
        "font_path": FONTS[weight],
        "font_size": size,
        "weight": weight,
        "fill": fill or TOKENS["text"],
        "bounds": bounds,
        **extra,
    }


def _rect_command(
    command_id: str,
    bounds: tuple[int, int, int, int],
    *,
    fill: str,
    stroke: str | None = None,
    stroke_width: int = 0,
    radius: int = 0,
    **extra: Any,
) -> dict[str, Any]:
    x, y, width, height = bounds
    return {
        "id": command_id,
        "kind": "rect",
        "x": x,
        "y": y,
        "width": width,
        "height": height,
        "fill": fill,
        "stroke": stroke or fill,
        "stroke_width": stroke_width,
        "radius": radius,
        **extra,
    }


def _polyline_command(
    command_id: str,
    points: list[tuple[int, int]],
    *,
    stroke: str,
    stroke_width: int | None = None,
    line_style: str = "solid",
    marker: str = "none",
    **extra: Any,
) -> dict[str, Any]:
    return {
        "id": command_id,
        "kind": "polyline",
        "points": [list(point) for point in points],
        "stroke": stroke,
        "stroke_width": stroke_width or TOKENS["route"],
        "line_style": line_style,
        "marker": marker,
        **extra,
    }


def _circle_command(
    command_id: str,
    cx: int,
    cy: int,
    *,
    radius: int = 5,
    fill: str,
    stroke: str,
    stroke_width: int = 2,
    **extra: Any,
) -> dict[str, Any]:
    return {
        "id": command_id,
        "kind": "circle",
        "cx": cx,
        "cy": cy,
        "radius": radius,
        "fill": fill,
        "stroke": stroke,
        "stroke_width": stroke_width,
        **extra,
    }


def _shared_scene_commands() -> list[dict[str, Any]]:
    commands = [
        _rect_command("canvas.background", (0, 0, *CANVAS), fill=TOKENS["background"]),
        _rect_command(
            "shared.header", BANDS["header"], fill=TOKENS["surface"],
            stroke="#2A3340", stroke_width=TOKENS["stroke"], radius=TOKENS["radius"],
        ),
        _text_command("shared.question", _QUESTION, 36, 38, size=9, weight="bold"),
        _rect_command(
            "shared.criterion.chip", (36, 76, 170, 34), fill="#26364B",
            stroke=TOKENS["enter"], stroke_width=1, radius=8,
        ),
        _text_command("shared.criterion", "NewHealth · criterion", 48, 85, size=12, weight="bold"),
        _text_command(
            "shared.scope", "Static contextual slice · depth 1", 436, 88,
            size=10, fill=TOKENS["muted"],
        ),
        _rect_command(
            "shared.plot", BANDS["plot"], fill=TOKENS["surface"],
            stroke="#2A3340", stroke_width=1, radius=TOKENS["radius"],
        ),
        _rect_command(
            "shared.frontier", BANDS["frontier"], fill="#241A22",
            stroke=TOKENS["frontier"], stroke_width=TOKENS["stroke"], radius=TOKENS["radius"],
        ),
        _text_command("shared.frontier.title", _FRONTIER, 40, 818, size=10, weight="bold", fill=TOKENS["frontier"]),
        _text_command(
            "shared.frontier.boundary",
            "Static boundary · occurrences are not runtime invocations; no runtime order is claimed",
            40, 850, size=10, fill=TOKENS["muted"],
        ),
        _rect_command(
            "shared.actions", BANDS["actions"], fill=TOKENS["surface"],
            stroke="#2A3340", stroke_width=1, radius=TOKENS["radius"],
        ),
    ]
    action_bounds = ((36, 928, 140, 34), (188, 928, 140, 34), (340, 928, 160, 34), (512, 928, 152, 34))
    for action, bounds in zip(_ACTIONS, action_bounds, strict=True):
        action_id = action["action_id"]
        commands.append(
            _rect_command(
                f"action.{action_id}.button", bounds, fill="#202834", stroke="#465466",
                stroke_width=1, radius=7, action_id=action_id,
            )
        )
        commands.append(
            _text_command(
                f"action.{action_id}.label", action["label"], bounds[0] + 12, bounds[1] + 9,
                size=10, weight="bold", action_id=action_id,
            )
        )
    return commands


def _occurrence_roles(ledger: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    by_label = {item["reader_label"]: item for item in ledger["occurrences"]}
    expected = {
        "call": "CalculateRecovery",
        "entry": "Entry · CurrentHealth, Bonus",
        "addition": "CurrentHealth + Bonus",
        "result": "Return · NewHealth",
    }
    _require(set(expected.values()) <= set(by_label), "LC5 reader-label inventory differs")
    return {role: by_label[label] for role, label in expected.items()}


def _relation_roles(ledger: Mapping[str, Any]) -> list[tuple[Mapping[str, Any], str, str]]:
    roles: list[tuple[Mapping[str, Any], str, str]] = []
    for relation in ledger["relations"]:
        relation_id = relation["relation_id"]
        if relation.get("kind") == "call_enter":
            roles.append((relation, "enter", "static call_enter"))
        elif relation.get("kind") == "call_return":
            roles.append((relation, "return", "static call_return"))
        elif relation.get("kind") == "argument":
            name = relation["property"]["name"]
            roles.append((relation, "argument", f"argument · {name}: int32"))
        elif relation.get("kind") == "result":
            roles.append((relation, "result", "result · NewHealth: int32"))
        else:
            edge_id = relation.get("source_edge_id", "")
            detail = "execution" if relation.get("kind") == "execution" else next(
                name for name in ("Bonus", "CurrentHealth", "ReturnValue") if name in edge_id
            )
            roles.append((relation, "internal", f"internal {relation['kind']} · {detail}"))
        _require(roles[-1][0]["relation_id"] == relation_id, "relation role projection differs")
    _require(len(roles) == 9, "LC5 scene relation inventory differs")
    return roles


_RELATION_STYLE = {
    "enter": (TOKENS["enter"], "solid", "arrow"),
    "argument": (TOKENS["argument"], "dashed", "socket"),
    "internal": (TOKENS["internal"], "solid", "data_arrow"),
    "result": (TOKENS["result"], "double", "double_arrow"),
    "return": (TOKENS["enter"], "solid", "return_arrow"),
}


def _add_relation(
    commands: list[dict[str, Any]],
    coverage: dict[str, dict[str, Any]],
    relation: Mapping[str, Any],
    family: str,
    label: str,
    route_id: str,
    points: list[tuple[int, int]],
    label_position: tuple[int, int],
) -> None:
    colour, line_style, marker = _RELATION_STYLE[family]
    label_id = f"{route_id}.label"
    route = _polyline_command(
        route_id, points, stroke=colour, line_style=line_style, marker=marker,
        relation_id=relation["relation_id"], relation_family=family, label_command_id=label_id,
    )
    commands.extend((route, _text_command(label_id, label, *label_position, size=8, fill=colour)))
    if family == "argument":
        cx, cy = points[-1]
        commands.append(
            _circle_command(
                f"{route_id}.socket", cx, cy, fill=TOKENS["surface"], stroke=colour,
                relation_id=relation["relation_id"], relation_family=family,
            )
        )
    elif family == "result" and route_id == "portal.output.new_health":
        cx, cy = points[0]
        commands.append(
            _circle_command(
                f"{route_id}.socket", cx, cy, fill=TOKENS["surface"], stroke=colour,
                relation_id=relation["relation_id"], relation_family=family,
            )
        )
    coverage[relation["relation_id"]] = {
        "family": family,
        "route_command_ids": [route_id],
        "label_command_id": label_id,
    }


def _semantic_unit(
    commands: list[dict[str, Any]],
    occurrence: Mapping[str, Any],
    command_id: str,
    position: tuple[int, int],
    *,
    size: int = 11,
) -> dict[str, Any]:
    commands.append(
        _text_command(
            command_id, occurrence["reader_label"], *position, size=size, weight="bold",
            semantic_occurrence_id=occurrence["occurrence_id"],
        )
    )
    return {
        "occurrence_id": occurrence["occurrence_id"],
        "source_node_id": occurrence["source_node_id"],
        "role": occurrence["role"],
        "label": occurrence["reader_label"],
        "command_id": command_id,
    }


def _complete_text_plot(
    ledger: Mapping[str, Any], commands: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], list[dict[str, Any]]]:
    roles = _occurrence_roles(ledger)
    commands.extend((
        _polyline_command("complete.reading.rail", [(52, 210), (52, 730)], stroke="#465466", stroke_width=2),
        _text_command("complete.title", "Complete static contextual reading", 76, 198, size=13, weight="bold"),
        _text_command("complete.types", "CurrentHealth: int32 · Bonus: int32 · NewHealth: int32", 76, 230, size=10, fill=TOKENS["muted"]),
    ))
    semantic_units = [
        _semantic_unit(commands, roles["call"], "complete.unit.call", (76, 270)),
        _semantic_unit(commands, roles["entry"], "complete.unit.entry", (76, 306)),
        _semantic_unit(commands, roles["addition"], "complete.unit.addition", (76, 342)),
        _semantic_unit(commands, roles["result"], "complete.unit.result", (76, 378)),
    ]
    for role, cy in (("call", 270), ("entry", 306), ("addition", 342), ("result", 378)):
        commands.append(
            _circle_command(
                f"complete.station.{role}", 52, cy, radius=4,
                fill=TOKENS["surface"], stroke=TOKENS["muted"],
                semantic_anchor_for_occurrence_id=roles[role]["occurrence_id"],
            )
        )
    commands.append(_text_command("complete.relation.index", "Numbered relation index · static facts", 76, 416, size=10, weight="bold"))
    coverage: dict[str, dict[str, Any]] = {}
    station_routes = (
        [(52, 274), (68, 450), (52, 302)],
        [(56, 270), (68, 480), (48, 306)],
        [(48, 270), (68, 510), (56, 306)],
        [(52, 310), (68, 540), (52, 338)],
        [(56, 306), (68, 570), (48, 342)],
        [(48, 306), (68, 600), (48, 378)],
        [(52, 346), (68, 630), (52, 374)],
        [(56, 378), (68, 660), (56, 270)],
        [(52, 382), (68, 690), (52, 266)],
    )
    for index, ((relation, family, label), points) in enumerate(
        zip(_relation_roles(ledger), station_routes, strict=True), start=1
    ):
        y = 450 + (index - 1) * 30
        _add_relation(
            commands, coverage, relation, family, f"{index}. {label}",
            f"complete.relation.{index}", points, (80, y - 7),
        )
    regions = [{"id": "complete.body", "bounds": list(LAYOUTS["LC5_COMPLETE_TEXT"]["body"])}]
    return semantic_units, coverage, regions


def _portal_plot(
    ledger: Mapping[str, Any], commands: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], list[dict[str, Any]]]:
    roles = _occurrence_roles(ledger)
    layout = LAYOUTS["LC5_TYPED_PORTAL_BRIDGE"]
    commands.extend((
        _rect_command("portal.caller.region", layout["caller"], fill=TOKENS["caller_fill"], stroke="#31506F", stroke_width=2, radius=10),
        _rect_command("portal.callee.region", layout["callee"], fill=TOKENS["callee_fill"], stroke="#584170", stroke_width=2, radius=10),
        _text_command("portal.caller.title", "CALLER · EventGraph", 48, 200, size=10, weight="bold", fill=TOKENS["enter"]),
        _text_command("portal.callee.title", "CALLEE · CalculateRecovery", 306, 200, size=10, weight="bold", fill=TOKENS["result"]),
        _polyline_command("portal.boundary", [(254, 208), (254, 730)], stroke="#7F8B99", stroke_width=2, line_style="dashed"),
        _text_command("portal.boundary.label", "STATIC PORTAL", 262, 214, size=8, weight="bold", fill=TOKENS["muted"]),
        _rect_command("portal.call.node", (52, 278, 156, 124), fill="#1E2E42", stroke=TOKENS["enter"], stroke_width=2, radius=8),
        _text_command("portal.call.current", "CurrentHealth: int32", 64, 326, size=9, fill=TOKENS["argument"]),
        _text_command("portal.call.bonus", "Bonus: int32", 64, 350, size=9, fill=TOKENS["argument"]),
        _text_command("portal.call.result", "NewHealth: int32", 64, 374, size=9, fill=TOKENS["result"]),
        _rect_command("portal.entry.node", (316, 278, 320, 92), fill="#2A2339", stroke="#725795", stroke_width=2, radius=8),
        _text_command("portal.entry.current", "CurrentHealth: int32", 332, 326, size=9, fill=TOKENS["argument"]),
        _text_command("portal.entry.bonus", "Bonus: int32", 488, 326, size=9, fill=TOKENS["argument"]),
        _rect_command("portal.addition.node", (370, 444, 230, 70), fill="#263126", stroke=TOKENS["internal"], stroke_width=2, radius=8),
        _rect_command("portal.result.node", (370, 606, 230, 76), fill="#32233D", stroke=TOKENS["result"], stroke_width=2, radius=8),
        _text_command("portal.result.type", "NewHealth: int32", 390, 650, size=9, fill=TOKENS["result"]),
    ))
    semantic_units = [
        _semantic_unit(commands, roles["call"], "portal.unit.call", (64, 294)),
        _semantic_unit(commands, roles["entry"], "portal.unit.entry", (332, 294)),
        _semantic_unit(commands, roles["addition"], "portal.unit.addition", (390, 466)),
        _semantic_unit(commands, roles["result"], "portal.unit.result", (390, 622)),
    ]
    relation_specs = [
        ("portal.enter.arrow", [(208, 286), (254, 286), (316, 286)], (270, 236)),
        ("portal.input.current_health", [(208, 338), (254, 338), (316, 338)], (330, 312)),
        ("portal.input.bonus", [(208, 378), (254, 378), (316, 366)], (330, 354)),
        ("portal.internal.1", [(340, 370), (340, 430), (406, 444)], (430, 382)),
        ("portal.internal.2", [(382, 370), (382, 424), (450, 444)], (430, 400)),
        ("portal.internal.3", [(608, 350), (630, 350), (630, 630), (600, 630)], (514, 548)),
        ("portal.internal.4", [(500, 514), (500, 606)], (514, 558)),
        ("portal.output.new_health", [(370, 674), (300, 674), (300, 390), (208, 390)], (392, 690)),
        ("portal.return.arrow", [(370, 662), (254, 662), (254, 398), (208, 398)], (420, 730)),
    ]
    coverage: dict[str, dict[str, Any]] = {}
    for role, (route_id, points, label_position) in zip(_relation_roles(ledger), relation_specs, strict=True):
        _add_relation(commands, coverage, *role, route_id, points, label_position)
    regions = [
        {"id": "portal.caller", "bounds": list(layout["caller"])},
        {"id": "portal.callee", "bounds": list(layout["callee"])},
    ]
    return semantic_units, coverage, regions


def _adjacent_plot(
    ledger: Mapping[str, Any], commands: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], list[dict[str, Any]]]:
    roles = _occurrence_roles(ledger)
    layout = LAYOUTS["LC5_ADJACENT_COMPOUND_EXPANSION"]
    commands.extend((
        _rect_command("adjacent.caller.region", layout["caller"], fill=TOKENS["caller_fill"], stroke="#31506F", stroke_width=2, radius=10),
        _circle_command("adjacent.call.anchor", 200, 286, radius=7, fill=TOKENS["enter"], stroke=TOKENS["text"]),
        _text_command("adjacent.caller.title", "CALLER · source anchor", 52, 254, size=9, weight="bold", fill=TOKENS["enter"]),
        _text_command("adjacent.call.current", "CurrentHealth: int32", 52, 330, size=9, fill=TOKENS["argument"]),
        _text_command("adjacent.call.bonus", "Bonus: int32", 52, 366, size=9, fill=TOKENS["argument"]),
        _text_command("adjacent.call.result", "NewHealth: int32", 52, 446, size=9, fill=TOKENS["result"]),
        _polyline_command("adjacent.expansion.bracket", [(266, 206), (248, 206), (248, 732), (266, 732)], stroke="#8A96A5", stroke_width=2),
        _text_command("adjacent.bracket.label", "SOURCE-OWNED EXPANSION", 284, 204, size=8, weight="bold", fill=TOKENS["muted"]),
        _rect_command("adjacent.callee.compound", layout["callee"], fill=TOKENS["callee_fill"], stroke="#584170", stroke_width=2, radius=10),
        _text_command("adjacent.callee.title", "ONE CALLEE COMPOUND · static depth 1", 300, 230, size=9, weight="bold", fill=TOKENS["result"]),
        _rect_command("adjacent.entry.node", (316, 274, 314, 92), fill="#2A2339", stroke="#725795", stroke_width=2, radius=8),
        _text_command("adjacent.entry.current", "CurrentHealth: int32", 332, 322, size=9, fill=TOKENS["argument"]),
        _text_command("adjacent.entry.bonus", "Bonus: int32", 484, 322, size=9, fill=TOKENS["argument"]),
        _rect_command("adjacent.addition.node", (372, 438, 216, 70), fill="#263126", stroke=TOKENS["internal"], stroke_width=2, radius=8),
        _rect_command("adjacent.result.node", (372, 602, 216, 76), fill="#32233D", stroke=TOKENS["result"], stroke_width=2, radius=8),
        _text_command("adjacent.result.type", "NewHealth: int32", 390, 646, size=9, fill=TOKENS["result"]),
    ))
    semantic_units = [
        _semantic_unit(commands, roles["call"], "adjacent.unit.call", (52, 286)),
        _semantic_unit(commands, roles["entry"], "adjacent.unit.entry", (332, 290)),
        _semantic_unit(commands, roles["addition"], "adjacent.unit.addition", (390, 460)),
        _semantic_unit(commands, roles["result"], "adjacent.unit.result", (390, 618)),
    ]
    relation_specs = [
        ("adjacent.enter.route", [(200, 286), (248, 286), (316, 286)], (52, 222)),
        ("adjacent.argument.current_health", [(207, 286), (248, 342), (316, 342)], (52, 310)),
        ("adjacent.argument.bonus", [(200, 293), (248, 382), (316, 362)], (52, 350)),
        ("adjacent.internal.1", [(348, 366), (348, 422), (410, 438)], (438, 382)),
        ("adjacent.internal.2", [(390, 366), (390, 414), (456, 438)], (438, 400)),
        ("adjacent.internal.3", [(612, 346), (636, 346), (636, 626), (588, 626)], (510, 530)),
        ("adjacent.internal.4", [(482, 508), (482, 602)], (498, 570)),
        ("adjacent.result.route", [(372, 670), (264, 670), (264, 466), (207, 286)], (388, 686)),
        ("adjacent.return.route", [(372, 660), (228, 660), (228, 486), (200, 293)], (416, 722)),
    ]
    coverage: dict[str, dict[str, Any]] = {}
    for role, (route_id, points, label_position) in zip(_relation_roles(ledger), relation_specs, strict=True):
        _add_relation(commands, coverage, *role, route_id, points, label_position)
    return semantic_units, coverage, [
        {"id": "adjacent.caller", "bounds": list(layout["caller"])},
        {"id": "adjacent.callee.compound", "bounds": list(layout["callee"])},
    ]


def _context_plot(
    ledger: Mapping[str, Any], commands: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], list[dict[str, Any]]]:
    roles = _occurrence_roles(ledger)
    layout = LAYOUTS["LC5_PERSISTENT_CONTEXT_REGIONS"]
    commands.extend((
        _rect_command("context.caller.region", layout["caller"], fill=TOKENS["caller_fill"], stroke="#31506F", stroke_width=2, radius=10),
        _rect_command("context.callee.region", layout["callee"], fill=TOKENS["callee_fill"], stroke="#584170", stroke_width=2, radius=10),
        _text_command("context.caller.title", "CALLER CONTEXT · EventGraph", 52, 202, size=9, weight="bold", fill=TOKENS["enter"]),
        _text_command("context.callee.title", "CALLEE CONTEXT · CalculateRecovery", 330, 202, size=9, weight="bold", fill=TOKENS["result"]),
        _polyline_command("context.source.anchors", [(48, 260), (48, 468)], stroke="#8A96A5", stroke_width=2, line_style="dashed"),
        _rect_command("context.call.node", (68, 282, 190, 176), fill="#1E2E42", stroke=TOKENS["enter"], stroke_width=2, radius=8),
        _text_command("context.call.current", "CurrentHealth: int32", 84, 334, size=9, fill=TOKENS["argument"]),
        _text_command("context.call.bonus", "Bonus: int32", 84, 374, size=9, fill=TOKENS["argument"]),
        _text_command("context.call.result", "NewHealth: int32", 84, 424, size=9, fill=TOKENS["result"]),
        _rect_command("context.entry.node", (346, 282, 286, 92), fill="#2A2339", stroke="#725795", stroke_width=2, radius=8),
        _text_command("context.entry.current", "CurrentHealth: int32", 362, 334, size=9, fill=TOKENS["argument"]),
        _text_command("context.entry.bonus", "Bonus: int32", 510, 334, size=9, fill=TOKENS["argument"]),
        _rect_command("context.addition.node", (386, 448, 210, 70), fill="#263126", stroke=TOKENS["internal"], stroke_width=2, radius=8),
        _rect_command("context.result.node", (386, 612, 210, 76), fill="#32233D", stroke=TOKENS["result"], stroke_width=2, radius=8),
        _text_command("context.result.type", "NewHealth: int32", 404, 656, size=9, fill=TOKENS["result"]),
    ))
    semantic_units = [
        _semantic_unit(commands, roles["call"], "context.unit.call", (84, 300)),
        _semantic_unit(commands, roles["entry"], "context.unit.entry", (362, 300)),
        _semantic_unit(commands, roles["addition"], "context.unit.addition", (404, 470)),
        _semantic_unit(commands, roles["result"], "context.unit.result", (404, 628)),
    ]
    relation_specs = [
        ("context.enter.route", [(258, 294), (286, 294), (346, 294)], (214, 234)),
        ("context.argument.current_health", [(258, 346), (286, 346), (346, 346)], (220, 320)),
        ("context.argument.bonus", [(258, 388), (286, 388), (346, 370)], (220, 364)),
        ("context.internal.1", [(376, 374), (376, 432), (422, 448)], (440, 392)),
        ("context.internal.2", [(418, 374), (418, 426), (466, 448)], (440, 410)),
        ("context.internal.3", [(614, 354), (642, 354), (642, 636), (596, 636)], (518, 534)),
        ("context.internal.4", [(490, 518), (490, 612)], (506, 578)),
        ("context.result.route", [(386, 680), (300, 680), (300, 438), (258, 438)], (402, 698)),
        ("context.return.route", [(386, 668), (282, 668), (282, 458), (258, 458)], (428, 728)),
    ]
    coverage: dict[str, dict[str, Any]] = {}
    for role, (route_id, points, label_position) in zip(_relation_roles(ledger), relation_specs, strict=True):
        _add_relation(commands, coverage, *role, route_id, points, label_position)
    return semantic_units, coverage, [
        {"id": "context.caller", "bounds": list(layout["caller"])},
        {"id": "context.callee", "bounds": list(layout["callee"])},
    ]


def _weave_plot(
    ledger: Mapping[str, Any], commands: list[dict[str, Any]]
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], list[dict[str, Any]]]:
    roles = _occurrence_roles(ledger)
    layout = LAYOUTS["LC5_CALL_RETURN_WEAVE"]
    commands.extend((
        _text_command("weave.caller.title", "CALLER", 42, 194, size=9, weight="bold", fill=TOKENS["enter"]),
        _text_command("weave.portal.title", "STATIC PORTAL", 212, 194, size=8, weight="bold", fill=TOKENS["muted"]),
        _text_command("weave.callee.title", "CALLEE", 338, 194, size=9, weight="bold", fill=TOKENS["result"]),
        _polyline_command("weave.caller.axis", [(layout["caller_x"], 230), (layout["caller_x"], 730)], stroke="#31506F", stroke_width=2),
        _polyline_command("weave.portal.axis", [(layout["portal_x"], 220), (layout["portal_x"], 744)], stroke="#7F8B99", stroke_width=2, line_style="dashed"),
        _polyline_command("weave.callee.axis", [(layout["callee_x"], 230), (layout["callee_x"], 700)], stroke="#584170", stroke_width=2),
        _circle_command("weave.call.anchor", 104, 286, radius=8, fill=TOKENS["caller_fill"], stroke=TOKENS["enter"]),
        _circle_command("weave.entry.anchor", 382, 286, radius=8, fill=TOKENS["callee_fill"], stroke=TOKENS["result"]),
        _circle_command("weave.addition.anchor", 500, 480, radius=8, fill=TOKENS["callee_fill"], stroke=TOKENS["internal"]),
        _circle_command("weave.result.anchor", 500, 638, radius=8, fill=TOKENS["callee_fill"], stroke=TOKENS["result"]),
        _text_command("weave.call.current", "CurrentHealth: int32", 72, 326, size=9, fill=TOKENS["argument"]),
        _text_command("weave.call.bonus", "Bonus: int32", 72, 392, size=9, fill=TOKENS["argument"]),
        _text_command("weave.call.result", "NewHealth: int32", 72, 674, size=9, fill=TOKENS["result"]),
        _text_command("weave.entry.current", "CurrentHealth: int32", 368, 326, size=9, fill=TOKENS["argument"]),
        _text_command("weave.entry.bonus", "Bonus: int32", 368, 392, size=9, fill=TOKENS["argument"]),
        _text_command("weave.result.type", "NewHealth: int32", 514, 660, size=9, fill=TOKENS["result"]),
    ))
    semantic_units = [
        _semantic_unit(commands, roles["call"], "weave.unit.call", (72, 262)),
        _semantic_unit(commands, roles["entry"], "weave.unit.entry", (368, 262)),
        _semantic_unit(commands, roles["addition"], "weave.unit.addition", (514, 452)),
        _semantic_unit(commands, roles["result"], "weave.unit.result", (514, 612)),
    ]
    relation_specs = [
        ("weave.enter.route", [(96, 286), (60, 286), (60, 230), (650, 230), (650, 286), (390, 286)], (126, 214)),
        ("weave.argument.current_health", [(104, 294), (252, 294), (382, 294)], (126, 304)),
        ("weave.argument.bonus", [(112, 286), (252, 286), (374, 286)], (126, 390)),
        ("weave.internal.1", [(390, 286), (460, 286), (460, 480), (492, 480)], (530, 350)),
        ("weave.internal.2", [(382, 294), (470, 294), (470, 430), (500, 430), (500, 472)], (530, 382)),
        ("weave.internal.3", [(390, 286), (648, 286), (648, 638), (508, 638)], (510, 570)),
        ("weave.internal.4", [(500, 488), (500, 630)], (516, 594)),
        ("weave.result.new_health", [(500, 646), (660, 646), (660, 760), (40, 760), (40, 294), (104, 294)], (366, 662)),
        ("weave.return.route", [(508, 638), (652, 638), (652, 774), (32, 774), (32, 286), (112, 286)], (410, 710)),
    ]
    coverage: dict[str, dict[str, Any]] = {}
    for role, (route_id, points, label_position) in zip(_relation_roles(ledger), relation_specs, strict=True):
        _add_relation(commands, coverage, *role, route_id, points, label_position)
    return semantic_units, coverage, [
        {"id": "weave.open.plot", "bounds": [36, 184, 628, 570]},
    ]


_PLOT_BUILDERS = {
    "LC5_COMPLETE_TEXT": _complete_text_plot,
    "LC5_TYPED_PORTAL_BRIDGE": _portal_plot,
    "LC5_ADJACENT_COMPOUND_EXPANSION": _adjacent_plot,
    "LC5_PERSISTENT_CONTEXT_REGIONS": _context_plot,
    "LC5_CALL_RETURN_WEAVE": _weave_plot,
}

_EXPECTED_ROUTE_POINTS = MappingProxyType({
    "LC5_COMPLETE_TEXT": {
        "complete.relation.1": ((52, 274), (68, 450), (52, 302)),
        "complete.relation.2": ((56, 270), (68, 480), (48, 306)),
        "complete.relation.3": ((48, 270), (68, 510), (56, 306)),
        "complete.relation.4": ((52, 310), (68, 540), (52, 338)),
        "complete.relation.5": ((56, 306), (68, 570), (48, 342)),
        "complete.relation.6": ((48, 306), (68, 600), (48, 378)),
        "complete.relation.7": ((52, 346), (68, 630), (52, 374)),
        "complete.relation.8": ((56, 378), (68, 660), (56, 270)),
        "complete.relation.9": ((52, 382), (68, 690), (52, 266)),
    },
    "LC5_TYPED_PORTAL_BRIDGE": {
        "portal.enter.arrow": ((208, 286), (254, 286), (316, 286)),
        "portal.input.current_health": ((208, 338), (254, 338), (316, 338)),
        "portal.input.bonus": ((208, 378), (254, 378), (316, 366)),
        "portal.internal.1": ((340, 370), (340, 430), (406, 444)),
        "portal.internal.2": ((382, 370), (382, 424), (450, 444)),
        "portal.internal.3": ((608, 350), (630, 350), (630, 630), (600, 630)),
        "portal.internal.4": ((500, 514), (500, 606)),
        "portal.output.new_health": ((370, 674), (300, 674), (300, 390), (208, 390)),
        "portal.return.arrow": ((370, 662), (254, 662), (254, 398), (208, 398)),
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "adjacent.enter.route": ((200, 286), (248, 286), (316, 286)),
        "adjacent.argument.current_health": ((207, 286), (248, 342), (316, 342)),
        "adjacent.argument.bonus": ((200, 293), (248, 382), (316, 362)),
        "adjacent.internal.1": ((348, 366), (348, 422), (410, 438)),
        "adjacent.internal.2": ((390, 366), (390, 414), (456, 438)),
        "adjacent.internal.3": ((612, 346), (636, 346), (636, 626), (588, 626)),
        "adjacent.internal.4": ((482, 508), (482, 602)),
        "adjacent.result.route": ((372, 670), (264, 670), (264, 466), (207, 286)),
        "adjacent.return.route": ((372, 660), (228, 660), (228, 486), (200, 293)),
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "context.enter.route": ((258, 294), (286, 294), (346, 294)),
        "context.argument.current_health": ((258, 346), (286, 346), (346, 346)),
        "context.argument.bonus": ((258, 388), (286, 388), (346, 370)),
        "context.internal.1": ((376, 374), (376, 432), (422, 448)),
        "context.internal.2": ((418, 374), (418, 426), (466, 448)),
        "context.internal.3": ((614, 354), (642, 354), (642, 636), (596, 636)),
        "context.internal.4": ((490, 518), (490, 612)),
        "context.result.route": ((386, 680), (300, 680), (300, 438), (258, 438)),
        "context.return.route": ((386, 668), (282, 668), (282, 458), (258, 458)),
    },
    "LC5_CALL_RETURN_WEAVE": {
        "weave.enter.route": ((96, 286), (60, 286), (60, 230), (650, 230), (650, 286), (390, 286)),
        "weave.argument.current_health": ((104, 294), (252, 294), (382, 294)),
        "weave.argument.bonus": ((112, 286), (252, 286), (374, 286)),
        "weave.internal.1": ((390, 286), (460, 286), (460, 480), (492, 480)),
        "weave.internal.2": ((382, 294), (470, 294), (470, 430), (500, 430), (500, 472)),
        "weave.internal.3": ((390, 286), (648, 286), (648, 638), (508, 638)),
        "weave.internal.4": ((500, 488), (500, 630)),
        "weave.result.new_health": ((500, 646), (660, 646), (660, 760), (40, 760), (40, 294), (104, 294)),
        "weave.return.route": ((508, 638), (652, 638), (652, 774), (32, 774), (32, 286), (112, 286)),
    },
})

_EXPECTED_REGIONS = MappingProxyType({
    "LC5_COMPLETE_TEXT": (
        {"id": "complete.body", "bounds": [36, 184, 628, 570]},
    ),
    "LC5_TYPED_PORTAL_BRIDGE": (
        {"id": "portal.caller", "bounds": [36, 184, 188, 570]},
        {"id": "portal.callee", "bounds": [292, 184, 372, 570]},
    ),
    "LC5_ADJACENT_COMPOUND_EXPANSION": (
        {"id": "adjacent.caller", "bounds": [36, 238, 180, 260]},
        {"id": "adjacent.callee.compound", "bounds": [284, 184, 380, 570]},
    ),
    "LC5_PERSISTENT_CONTEXT_REGIONS": (
        {"id": "context.caller", "bounds": [36, 184, 246, 570]},
        {"id": "context.callee", "bounds": [314, 184, 350, 570]},
    ),
    "LC5_CALL_RETURN_WEAVE": (
        {"id": "weave.open.plot", "bounds": [36, 184, 628, 570]},
    ),
})

_EXPECTED_REGION_DRAW_COMMANDS = MappingProxyType({
    "LC5_COMPLETE_TEXT": {},
    "LC5_TYPED_PORTAL_BRIDGE": {
        "portal.caller.region": {
            "kind": "rect", "x": 36, "y": 184, "width": 188,
            "height": 570, "radius": 10,
        },
        "portal.callee.region": {
            "kind": "rect", "x": 292, "y": 184, "width": 372,
            "height": 570, "radius": 10,
        },
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "adjacent.caller.region": {
            "kind": "rect", "x": 36, "y": 238, "width": 180,
            "height": 260, "radius": 10,
        },
        "adjacent.callee.compound": {
            "kind": "rect", "x": 284, "y": 184, "width": 380,
            "height": 570, "radius": 10,
        },
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "context.caller.region": {
            "kind": "rect", "x": 36, "y": 184, "width": 246,
            "height": 570, "radius": 10,
        },
        "context.callee.region": {
            "kind": "rect", "x": 314, "y": 184, "width": 350,
            "height": 570, "radius": 10,
        },
    },
    "LC5_CALL_RETURN_WEAVE": {},
})

_EXPECTED_SEMANTIC_ANCHOR_GEOMETRY = MappingProxyType({
    "LC5_COMPLETE_TEXT": {
        "complete.station.call": {
            "kind": "circle", "cx": 52, "cy": 270, "radius": 4,
            "semantic_anchor_for_occurrence_id": _EXPECTED_SEMANTIC_UNITS[
                "CalculateRecovery"
            ]["occurrence_id"],
        },
        "complete.station.entry": {
            "kind": "circle", "cx": 52, "cy": 306, "radius": 4,
            "semantic_anchor_for_occurrence_id": _EXPECTED_SEMANTIC_UNITS[
                "Entry · CurrentHealth, Bonus"
            ]["occurrence_id"],
        },
        "complete.station.addition": {
            "kind": "circle", "cx": 52, "cy": 342, "radius": 4,
            "semantic_anchor_for_occurrence_id": _EXPECTED_SEMANTIC_UNITS[
                "CurrentHealth + Bonus"
            ]["occurrence_id"],
        },
        "complete.station.result": {
            "kind": "circle", "cx": 52, "cy": 378, "radius": 4,
            "semantic_anchor_for_occurrence_id": _EXPECTED_SEMANTIC_UNITS[
                "Return · NewHealth"
            ]["occurrence_id"],
        },
    },
    "LC5_TYPED_PORTAL_BRIDGE": {
        "portal.call.node": {
            "kind": "rect", "x": 52, "y": 278, "width": 156,
            "height": 124, "radius": 8,
        },
        "portal.entry.node": {
            "kind": "rect", "x": 316, "y": 278, "width": 320,
            "height": 92, "radius": 8,
        },
        "portal.addition.node": {
            "kind": "rect", "x": 370, "y": 444, "width": 230,
            "height": 70, "radius": 8,
        },
        "portal.result.node": {
            "kind": "rect", "x": 370, "y": 606, "width": 230,
            "height": 76, "radius": 8,
        },
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "adjacent.call.anchor": {
            "kind": "circle", "cx": 200, "cy": 286, "radius": 7,
        },
        "adjacent.entry.node": {
            "kind": "rect", "x": 316, "y": 274, "width": 314,
            "height": 92, "radius": 8,
        },
        "adjacent.addition.node": {
            "kind": "rect", "x": 372, "y": 438, "width": 216,
            "height": 70, "radius": 8,
        },
        "adjacent.result.node": {
            "kind": "rect", "x": 372, "y": 602, "width": 216,
            "height": 76, "radius": 8,
        },
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "context.call.node": {
            "kind": "rect", "x": 68, "y": 282, "width": 190,
            "height": 176, "radius": 8,
        },
        "context.entry.node": {
            "kind": "rect", "x": 346, "y": 282, "width": 286,
            "height": 92, "radius": 8,
        },
        "context.addition.node": {
            "kind": "rect", "x": 386, "y": 448, "width": 210,
            "height": 70, "radius": 8,
        },
        "context.result.node": {
            "kind": "rect", "x": 386, "y": 612, "width": 210,
            "height": 76, "radius": 8,
        },
    },
    "LC5_CALL_RETURN_WEAVE": {
        "weave.call.anchor": {
            "kind": "circle", "cx": 104, "cy": 286, "radius": 8,
        },
        "weave.entry.anchor": {
            "kind": "circle", "cx": 382, "cy": 286, "radius": 8,
        },
        "weave.addition.anchor": {
            "kind": "circle", "cx": 500, "cy": 480, "radius": 8,
        },
        "weave.result.anchor": {
            "kind": "circle", "cx": 500, "cy": 638, "radius": 8,
        },
    },
})

_SEMANTIC_ANCHOR_COMMANDS_BY_ROLE = MappingProxyType({
    "LC5_COMPLETE_TEXT": {
        "call": "complete.station.call", "entry": "complete.station.entry",
        "addition": "complete.station.addition", "result": "complete.station.result",
    },
    "LC5_TYPED_PORTAL_BRIDGE": {
        "call": "portal.call.node", "entry": "portal.entry.node",
        "addition": "portal.addition.node", "result": "portal.result.node",
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "call": "adjacent.call.anchor", "entry": "adjacent.entry.node",
        "addition": "adjacent.addition.node", "result": "adjacent.result.node",
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "call": "context.call.node", "entry": "context.entry.node",
        "addition": "context.addition.node", "result": "context.result.node",
    },
    "LC5_CALL_RETURN_WEAVE": {
        "call": "weave.call.anchor", "entry": "weave.entry.anchor",
        "addition": "weave.addition.anchor", "result": "weave.result.anchor",
    },
})

_RELATION_ENDPOINT_ROLES = MappingProxyType({
    tuple(_EXPECTED_RELATION_FAMILIES)[0]: {"start": "call", "end": "entry"},
    _EXPECTED_BINDING_IDS[0]: {"start": "call", "end": "entry"},
    _EXPECTED_BINDING_IDS[1]: {"start": "call", "end": "entry"},
    f"internal:{_INTERNAL_EDGE_IDS[0]}": {"start": "entry", "end": "addition"},
    f"internal:{_INTERNAL_EDGE_IDS[1]}": {"start": "entry", "end": "addition"},
    f"internal:{_INTERNAL_EDGE_IDS[2]}": {"start": "entry", "end": "result"},
    f"internal:{_INTERNAL_EDGE_IDS[3]}": {"start": "addition", "end": "result"},
    _EXPECTED_BINDING_IDS[2]: {"start": "result", "end": "call"},
    tuple(_EXPECTED_RELATION_FAMILIES)[-1]: {"start": "result", "end": "call"},
})

_EXPECTED_STRUCTURAL_PRIMITIVES = MappingProxyType({
    "LC5_COMPLETE_TEXT": {
        "complete.reading.rail": {
            "kind": "polyline", "points": [[52, 210], [52, 730]],
        },
        "complete.relation.index": {"kind": "text", "x": 76, "y": 416},
    },
    "LC5_TYPED_PORTAL_BRIDGE": {
        "portal.boundary": {
            "kind": "polyline", "points": [[254, 208], [254, 730]],
        },
        **{
            command_id: {"kind": "polyline", "points": [list(point) for point in points]}
            for command_id, points in _EXPECTED_ROUTE_POINTS["LC5_TYPED_PORTAL_BRIDGE"].items()
            if command_id in REQUIRED_PRIMITIVES["LC5_TYPED_PORTAL_BRIDGE"]
        },
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "adjacent.call.anchor": {"kind": "circle", "cx": 200, "cy": 286, "radius": 7},
        "adjacent.expansion.bracket": {
            "kind": "polyline",
            "points": [[266, 206], [248, 206], [248, 732], [266, 732]],
        },
        "adjacent.callee.compound": {
            "kind": "rect", "x": 284, "y": 184, "width": 380,
            "height": 570, "radius": 10,
        },
        "adjacent.return.route": {
            "kind": "polyline",
            "points": [
                list(point)
                for point in _EXPECTED_ROUTE_POINTS["LC5_ADJACENT_COMPOUND_EXPANSION"][
                    "adjacent.return.route"
                ]
            ],
        },
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "context.caller.region": {
            "kind": "rect", "x": 36, "y": 184, "width": 246,
            "height": 570, "radius": 10,
        },
        "context.callee.region": {
            "kind": "rect", "x": 314, "y": 184, "width": 350,
            "height": 570, "radius": 10,
        },
        "context.enter.route": {
            "kind": "polyline",
            "points": [
                list(point)
                for point in _EXPECTED_ROUTE_POINTS["LC5_PERSISTENT_CONTEXT_REGIONS"][
                    "context.enter.route"
                ]
            ],
        },
        "context.return.route": {
            "kind": "polyline",
            "points": [
                list(point)
                for point in _EXPECTED_ROUTE_POINTS["LC5_PERSISTENT_CONTEXT_REGIONS"][
                    "context.return.route"
                ]
            ],
        },
        "context.source.anchors": {
            "kind": "polyline", "points": [[48, 260], [48, 468]],
        },
    },
    "LC5_CALL_RETURN_WEAVE": {
        command_id: {"kind": "polyline", "points": [list(point) for point in points]}
        for command_id, points in _EXPECTED_ROUTE_POINTS["LC5_CALL_RETURN_WEAVE"].items()
        if command_id in REQUIRED_PRIMITIVES["LC5_CALL_RETURN_WEAVE"]
    },
})

_ROUTE_IDS_BY_CONDITION = MappingProxyType({
    "LC5_COMPLETE_TEXT": tuple(f"complete.relation.{index}" for index in range(1, 10)),
    "LC5_TYPED_PORTAL_BRIDGE": (
        "portal.enter.arrow", "portal.input.current_health", "portal.input.bonus",
        "portal.internal.1", "portal.internal.2", "portal.internal.3",
        "portal.internal.4", "portal.output.new_health", "portal.return.arrow",
    ),
    "LC5_ADJACENT_COMPOUND_EXPANSION": (
        "adjacent.enter.route", "adjacent.argument.current_health",
        "adjacent.argument.bonus", "adjacent.internal.1", "adjacent.internal.2",
        "adjacent.internal.3", "adjacent.internal.4", "adjacent.result.route",
        "adjacent.return.route",
    ),
    "LC5_PERSISTENT_CONTEXT_REGIONS": (
        "context.enter.route", "context.argument.current_health", "context.argument.bonus",
        "context.internal.1", "context.internal.2", "context.internal.3",
        "context.internal.4", "context.result.route", "context.return.route",
    ),
    "LC5_CALL_RETURN_WEAVE": (
        "weave.enter.route", "weave.argument.current_health", "weave.argument.bonus",
        "weave.internal.1", "weave.internal.2", "weave.internal.3",
        "weave.internal.4", "weave.result.new_health", "weave.return.route",
    ),
})

_SOCKET_RELATION_INDEXES = MappingProxyType({
    "LC5_COMPLETE_TEXT": frozenset({1, 2}),
    "LC5_TYPED_PORTAL_BRIDGE": frozenset({1, 2, 7}),
    "LC5_ADJACENT_COMPOUND_EXPANSION": frozenset({1, 2}),
    "LC5_PERSISTENT_CONTEXT_REGIONS": frozenset({1, 2}),
    "LC5_CALL_RETURN_WEAVE": frozenset({1, 2}),
})


def _build_expected_relation_channels() -> dict[str, dict[str, dict[str, Any]]]:
    relation_ids = tuple(_EXPECTED_RELATION_FAMILIES)
    channels: dict[str, dict[str, dict[str, Any]]] = {}
    for condition_id, route_ids in _ROUTE_IDS_BY_CONDITION.items():
        condition_channels: dict[str, dict[str, Any]] = {}
        for index, (relation_id, route_id) in enumerate(zip(relation_ids, route_ids, strict=True)):
            family = _EXPECTED_RELATION_FAMILIES[relation_id]
            points = _EXPECTED_ROUTE_POINTS[condition_id][route_id]
            label = _expected_relation_label(relation_id)
            if condition_id == "LC5_COMPLETE_TEXT":
                label = f"{index + 1}. {label}"
            socket_id = f"{route_id}.socket" if index in _SOCKET_RELATION_INDEXES[condition_id] else None
            socket_point = points[0] if family == "result" else points[-1]
            condition_channels[relation_id] = {
                "family": family,
                "route_id": route_id,
                "label_id": f"{route_id}.label",
                "label": label,
                "marker": _RELATION_STYLE[family][2],
                "points": [list(point) for point in points],
                "socket_id": socket_id,
                "socket": (
                    {"cx": socket_point[0], "cy": socket_point[1], "radius": 5}
                    if socket_id is not None else None
                ),
            }
        channels[condition_id] = condition_channels
    return channels


def build_lc5_scene(
    ledger: Mapping[str, Any], condition_id: str, width: int = 700
) -> dict[str, Any]:
    """Build one deterministic measured-command scene for a frozen LC5 condition."""

    validate_lc5_visual_ledger(ledger)
    _require(width == CANVAS[0], "first LC5 packet supports only 700 logical pixels")
    _require(condition_id in CONDITION_IDS, "unknown LC5 visual condition")
    commands = _shared_scene_commands()
    semantic_units, relation_coverage, regions = _PLOT_BUILDERS[condition_id](ledger, commands)
    scene = {
        "format": "blueprint-lens-lc5-scene",
        "schema_version": "1.0.0",
        "condition_id": condition_id,
        "condition_label": CONDITIONS[condition_id][0],
        "canvas": {"width": width, "height": CANVAS[1]},
        "bands": deepcopy(BANDS),
        "tokens": deepcopy(TOKENS),
        "semantic_units": semantic_units,
        "relation_coverage": relation_coverage,
        "action_coverage": [deepcopy(item) for item in ledger["actions"]],
        "commands": commands,
        "regions": regions,
        "information_set": information_set(ledger),
        "copy_claims": {
            "question": _QUESTION,
            "criterion": "NewHealth · criterion",
            "scope": "Static contextual slice · depth 1",
            "frontier": _FRONTIER,
            "static_only": True,
            "runtime_order_claimed": False,
        },
    }
    checks = validate_lc5_scene(scene)
    _require(checks["pass"], f"LC5 scene geometry failed: {checks}")
    return scene


def _rectangles_overlap(first: list[int], second: list[int]) -> bool:
    return (
        max(first[0], second[0]) < min(first[2], second[2])
        and max(first[1], second[1]) < min(first[3], second[3])
    )


def _point_in_rect(point: tuple[float, float], bounds: list[int]) -> bool:
    return bounds[0] <= point[0] <= bounds[2] and bounds[1] <= point[1] <= bounds[3]


def _orientation(
    first: tuple[float, float], second: tuple[float, float], third: tuple[float, float]
) -> float:
    return (second[0] - first[0]) * (third[1] - first[1]) - (
        second[1] - first[1]
    ) * (third[0] - first[0])


def _on_segment(
    first: tuple[float, float], second: tuple[float, float], point: tuple[float, float]
) -> bool:
    epsilon = 1e-9
    return (
        min(first[0], second[0]) - epsilon <= point[0] <= max(first[0], second[0]) + epsilon
        and min(first[1], second[1]) - epsilon <= point[1] <= max(first[1], second[1]) + epsilon
        and abs(_orientation(first, second, point)) <= epsilon
    )


def _segments_intersect(
    first_start: tuple[float, float],
    first_end: tuple[float, float],
    second_start: tuple[float, float],
    second_end: tuple[float, float],
) -> bool:
    first_a = _orientation(first_start, first_end, second_start)
    first_b = _orientation(first_start, first_end, second_end)
    second_a = _orientation(second_start, second_end, first_start)
    second_b = _orientation(second_start, second_end, first_end)
    epsilon = 1e-9
    if ((first_a > epsilon and first_b < -epsilon) or (first_a < -epsilon and first_b > epsilon)) and (
        (second_a > epsilon and second_b < -epsilon)
        or (second_a < -epsilon and second_b > epsilon)
    ):
        return True
    return (
        (abs(first_a) <= epsilon and _on_segment(first_start, first_end, second_start))
        or (abs(first_b) <= epsilon and _on_segment(first_start, first_end, second_end))
        or (abs(second_a) <= epsilon and _on_segment(second_start, second_end, first_start))
        or (abs(second_b) <= epsilon and _on_segment(second_start, second_end, first_end))
    )


def _segment_intersects_rect(
    start: tuple[float, float], end: tuple[float, float], bounds: list[int]
) -> bool:
    """Detect touch, containment, entry or crossing for a closed rectangle."""

    if _point_in_rect(start, bounds) or _point_in_rect(end, bounds):
        return True
    left, top, right, bottom = bounds
    edges = (
        ((left, top), (right, top)),
        ((right, top), (right, bottom)),
        ((right, bottom), (left, bottom)),
        ((left, bottom), (left, top)),
    )
    return any(_segments_intersect(start, end, edge_start, edge_end) for edge_start, edge_end in edges)


def _command_bounds(command: Mapping[str, Any]) -> list[int] | None:
    kind = command.get("kind")
    if kind == "text":
        bounds = command.get("bounds")
        return list(bounds) if isinstance(bounds, list) and len(bounds) == 4 else None
    if kind == "rect":
        x, y = command.get("x"), command.get("y")
        width, height = command.get("width"), command.get("height")
        if all(isinstance(value, (int, float)) for value in (x, y, width, height)):
            return [x, y, x + width, y + height]
    if kind == "circle":
        cx, cy, radius = command.get("cx"), command.get("cy"), command.get("radius")
        if all(isinstance(value, (int, float)) for value in (cx, cy, radius)):
            return [cx - radius, cy - radius, cx + radius, cy + radius]
    if kind == "polyline":
        points = command.get("points", [])
        if points and all(isinstance(point, list) and len(point) == 2 for point in points):
            return [
                min(point[0] for point in points), min(point[1] for point in points),
                max(point[0] for point in points), max(point[1] for point in points),
            ]
    return None


def _paint_bounds(command: Mapping[str, Any]) -> list[float] | None:
    bounds = _command_bounds(command)
    if bounds is None:
        return None
    if command.get("kind") in {"rect", "circle", "polyline"}:
        try:
            half_stroke = max(0.0, float(command.get("stroke_width", 0))) / 2.0
        except (TypeError, ValueError):
            return None
        return [
            bounds[0] - half_stroke,
            bounds[1] - half_stroke,
            bounds[2] + half_stroke,
            bounds[3] + half_stroke,
        ]
    return [float(value) for value in bounds]


def _measured_text_bounds(command: Mapping[str, Any]) -> list[int] | None:
    try:
        font = ImageFont.truetype(str(command["font_path"]), int(command["font_size"]))
        return list(
            _TEXT_MEASURE_DRAW.textbbox(
                (int(command["x"]), int(command["y"])), str(command["text"]),
                font=font, anchor="lt",
            )
        )
    except (KeyError, OSError, TypeError, ValueError):
        return None


def _resolve_physical_anchor(
    command: Mapping[str, Any], anchor: Mapping[str, Any]
) -> list[Fraction] | None:
    try:
        if anchor["kind"] == "box" and command.get("kind") == "rect":
            x_ratio, y_ratio = anchor["x_ratio"], anchor["y_ratio"]
            if not (0 <= x_ratio <= 1 and 0 <= y_ratio <= 1):
                return None
            return [
                Fraction(command["x"]) + x_ratio * command["width"],
                Fraction(command["y"]) + y_ratio * command["height"],
            ]
        if anchor["kind"] == "box_port" and command.get("kind") == "rect":
            offset = anchor["offset"]
            if not 0 <= offset <= 1:
                return None
            x, y = Fraction(command["x"]), Fraction(command["y"])
            width, height = Fraction(command["width"]), Fraction(command["height"])
            return {
                "left": [x, y + offset * height],
                "right": [x + width, y + offset * height],
                "top": [x + offset * width, y],
                "bottom": [x + offset * width, y + height],
            }.get(anchor["side"])
        if anchor["kind"] == "circle" and command.get("kind") == "circle":
            x_ratio = anchor["x_radius_ratio"]
            y_ratio = anchor["y_radius_ratio"]
            if x_ratio ** 2 + y_ratio ** 2 > 1:
                return None
            return [
                Fraction(command["cx"]) + x_ratio * command["radius"],
                Fraction(command["cy"]) + y_ratio * command["radius"],
            ]
        if anchor["kind"] == "text_box" and command.get("kind") == "text":
            x_ratio, y_ratio = anchor["x_ratio"], anchor["y_ratio"]
            bounds = command.get("bounds")
            if (
                not (0 <= x_ratio <= 1 and 0 <= y_ratio <= 1)
                or not isinstance(bounds, list) or len(bounds) != 4
            ):
                return None
            return [
                Fraction(bounds[0]) + x_ratio * (bounds[2] - bounds[0]),
                Fraction(bounds[1]) + y_ratio * (bounds[3] - bounds[1]),
            ]
    except (KeyError, TypeError, ValueError, ZeroDivisionError):
        return None
    return None


def _point_on_shape_boundary(command: Mapping[str, Any], point: list[Any]) -> bool:
    if command.get("kind") == "rect":
        x, y = command.get("x"), command.get("y")
        width, height = command.get("width"), command.get("height")
        if not all(isinstance(value, (int, float)) for value in (x, y, width, height)):
            return False
        return (
            x <= point[0] <= x + width
            and y <= point[1] <= y + height
            and (
                point[0] in {x, x + width}
                or point[1] in {y, y + height}
            )
        )
    if command.get("kind") == "circle":
        cx, cy, radius = command.get("cx"), command.get("cy"), command.get("radius")
        if not all(isinstance(value, (int, float)) for value in (cx, cy, radius)):
            return False
        return (point[0] - cx) ** 2 + (point[1] - cy) ** 2 == radius ** 2
    if command.get("kind") == "text":
        bounds = command.get("bounds")
        if not isinstance(bounds, list) or len(bounds) != 4:
            return False
        return (
            bounds[0] <= point[0] <= bounds[2]
            and bounds[1] <= point[1] <= bounds[3]
        )
    return False


def _expected_relation_label(relation_id: str) -> str | None:
    if relation_id.startswith("context:call_enter:"):
        return "static call_enter"
    if relation_id.startswith("context:call_return:"):
        return "static call_return"
    if relation_id.startswith("binding:argument:0:"):
        return "argument · CurrentHealth: int32"
    if relation_id.startswith("binding:argument:1:"):
        return "argument · Bonus: int32"
    if relation_id.startswith("binding:result:2:"):
        return "result · NewHealth: int32"
    if relation_id.startswith("internal:"):
        edge_label = next(
            (name for name in ("Bonus", "CurrentHealth", "then", "ReturnValue") if f"locator-output-{name}-" in relation_id),
            None,
        )
        if edge_label == "then":
            return "internal execution · execution"
        if edge_label is not None:
            return f"internal data · {edge_label}"
    return None


_EXPECTED_RELATION_CHANNELS = MappingProxyType(_build_expected_relation_channels())


def _physical_anchor_descriptor(
    geometry: Mapping[str, Any], point: list[int]
) -> dict[str, Any]:
    kind = geometry["kind"]
    if kind == "rect":
        x_ratio = Fraction(point[0] - geometry["x"], geometry["width"])
        y_ratio = Fraction(point[1] - geometry["y"], geometry["height"])
        if not (0 <= x_ratio <= 1 and 0 <= y_ratio <= 1):
            raise LC5VisualError("LC5 rect anchor extrapolates beyond semantic geometry")
        if x_ratio == 0:
            return {"kind": "box_port", "side": "left", "offset": y_ratio}
        if x_ratio == 1:
            return {"kind": "box_port", "side": "right", "offset": y_ratio}
        if y_ratio == 0:
            return {"kind": "box_port", "side": "top", "offset": x_ratio}
        if y_ratio == 1:
            return {"kind": "box_port", "side": "bottom", "offset": x_ratio}
        return {
            "kind": "box", "x_ratio": x_ratio, "y_ratio": y_ratio,
        }
    if kind == "circle":
        descriptor = {
            "kind": "circle",
            "x_radius_ratio": Fraction(point[0] - geometry["cx"], geometry["radius"]),
            "y_radius_ratio": Fraction(point[1] - geometry["cy"], geometry["radius"]),
        }
        if (
            descriptor["x_radius_ratio"] ** 2
            + descriptor["y_radius_ratio"] ** 2
            > 1
        ):
            raise LC5VisualError("LC5 circle anchor lies outside semantic geometry")
        return descriptor
    if kind == "text":
        bounds = geometry.get("bounds")
        if not isinstance(bounds, list) or len(bounds) != 4:
            raise LC5VisualError("LC5 text anchor requires exact measured bounds")
        x_ratio = Fraction(point[0] - bounds[0], bounds[2] - bounds[0])
        y_ratio = Fraction(point[1] - bounds[1], bounds[3] - bounds[1])
        if not (0 <= x_ratio <= 1 and 0 <= y_ratio <= 1):
            raise LC5VisualError("LC5 text anchor extrapolates beyond measured bounds")
        return {
            "kind": "text_box", "x_ratio": x_ratio, "y_ratio": y_ratio,
        }
    raise LC5VisualError(f"unsupported LC5 semantic anchor kind: {kind}")


def _build_expected_relation_attachments() -> dict[str, dict[str, dict[str, Any]]]:
    attachments: dict[str, dict[str, dict[str, Any]]] = {}
    for condition_id in CONDITION_IDS:
        condition_attachments: dict[str, dict[str, Any]] = {}
        role_commands = _SEMANTIC_ANCHOR_COMMANDS_BY_ROLE[condition_id]
        channels = _EXPECTED_RELATION_CHANNELS[condition_id]
        for relation_id, channel in channels.items():
            endpoint_roles = _RELATION_ENDPOINT_ROLES[relation_id]
            endpoint_specs = {}
            for endpoint, role, point in (
                ("start", endpoint_roles["start"], channel["points"][0]),
                ("end", endpoint_roles["end"], channel["points"][-1]),
            ):
                command_id = role_commands[role]
                endpoint_specs[endpoint] = {
                    "semantic_command_id": command_id,
                    "anchor_point": list(point),
                    "anchor": _physical_anchor_descriptor(
                        _EXPECTED_SEMANTIC_ANCHOR_GEOMETRY[condition_id][command_id],
                        point,
                    ),
                }
            if channel["socket_id"] is not None:
                socket_endpoint = "start" if channel["family"] == "result" else "end"
                endpoint_specs[socket_endpoint]["socket_command_id"] = channel["socket_id"]
            condition_attachments[relation_id] = {
                "route_id": channel["route_id"],
                **endpoint_specs,
            }
        attachments[condition_id] = condition_attachments
    return attachments


_EXPECTED_RELATION_ATTACHMENTS = MappingProxyType(
    _build_expected_relation_attachments()
)


def _expected_text_contract(
    text: str,
    font_size: int,
    weight: str = "regular",
    *,
    action_id: str | None = None,
    semantic_occurrence_id: str | None = None,
) -> dict[str, Any]:
    return {
        "text": text,
        "font_size": font_size,
        "weight": weight,
        "action_id": action_id,
        "semantic_occurrence_id": semantic_occurrence_id,
    }


_EXPECTED_COMMON_TEXT_COMMANDS = MappingProxyType({
    "shared.question": _expected_text_contract(_QUESTION, 9, "bold"),
    "shared.criterion": _expected_text_contract("NewHealth · criterion", 12, "bold"),
    "shared.scope": _expected_text_contract("Static contextual slice · depth 1", 10),
    "shared.frontier.title": _expected_text_contract(_FRONTIER, 10, "bold"),
    "shared.frontier.boundary": _expected_text_contract(
        "Static boundary · occurrences are not runtime invocations; no runtime order is claimed",
        10,
    ),
    "action.open_source.label": _expected_text_contract(
        "Open source", 10, "bold", action_id="open_source",
    ),
    "action.select.label": _expected_text_contract(
        "Select", 10, "bold", action_id="select",
    ),
    "action.show_complete_text.label": _expected_text_contract(
        "Show complete text", 10, "bold", action_id="show_complete_text",
    ),
    "action.show_evidence.label": _expected_text_contract(
        "Show evidence", 10, "bold", action_id="show_evidence",
    ),
})

_CONDITION_TEXT_COMMANDS = MappingProxyType({
    "LC5_COMPLETE_TEXT": {
        "complete.title": ("Complete static contextual reading", 13, "bold"),
        "complete.types": (
            "CurrentHealth: int32 · Bonus: int32 · NewHealth: int32", 10, "regular",
        ),
        "complete.relation.index": ("Numbered relation index · static facts", 10, "bold"),
    },
    "LC5_TYPED_PORTAL_BRIDGE": {
        "portal.caller.title": ("CALLER · EventGraph", 10, "bold"),
        "portal.callee.title": ("CALLEE · CalculateRecovery", 10, "bold"),
        "portal.boundary.label": ("STATIC PORTAL", 8, "bold"),
        "portal.call.current": ("CurrentHealth: int32", 9, "regular"),
        "portal.call.bonus": ("Bonus: int32", 9, "regular"),
        "portal.call.result": ("NewHealth: int32", 9, "regular"),
        "portal.entry.current": ("CurrentHealth: int32", 9, "regular"),
        "portal.entry.bonus": ("Bonus: int32", 9, "regular"),
        "portal.result.type": ("NewHealth: int32", 9, "regular"),
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "adjacent.caller.title": ("CALLER · source anchor", 9, "bold"),
        "adjacent.call.current": ("CurrentHealth: int32", 9, "regular"),
        "adjacent.call.bonus": ("Bonus: int32", 9, "regular"),
        "adjacent.call.result": ("NewHealth: int32", 9, "regular"),
        "adjacent.bracket.label": ("SOURCE-OWNED EXPANSION", 8, "bold"),
        "adjacent.callee.title": ("ONE CALLEE COMPOUND · static depth 1", 9, "bold"),
        "adjacent.entry.current": ("CurrentHealth: int32", 9, "regular"),
        "adjacent.entry.bonus": ("Bonus: int32", 9, "regular"),
        "adjacent.result.type": ("NewHealth: int32", 9, "regular"),
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "context.caller.title": ("CALLER CONTEXT · EventGraph", 9, "bold"),
        "context.callee.title": ("CALLEE CONTEXT · CalculateRecovery", 9, "bold"),
        "context.call.current": ("CurrentHealth: int32", 9, "regular"),
        "context.call.bonus": ("Bonus: int32", 9, "regular"),
        "context.call.result": ("NewHealth: int32", 9, "regular"),
        "context.entry.current": ("CurrentHealth: int32", 9, "regular"),
        "context.entry.bonus": ("Bonus: int32", 9, "regular"),
        "context.result.type": ("NewHealth: int32", 9, "regular"),
    },
    "LC5_CALL_RETURN_WEAVE": {
        "weave.caller.title": ("CALLER", 9, "bold"),
        "weave.portal.title": ("STATIC PORTAL", 8, "bold"),
        "weave.callee.title": ("CALLEE", 9, "bold"),
        "weave.call.current": ("CurrentHealth: int32", 9, "regular"),
        "weave.call.bonus": ("Bonus: int32", 9, "regular"),
        "weave.call.result": ("NewHealth: int32", 9, "regular"),
        "weave.entry.current": ("CurrentHealth: int32", 9, "regular"),
        "weave.entry.bonus": ("Bonus: int32", 9, "regular"),
        "weave.result.type": ("NewHealth: int32", 9, "regular"),
    },
})

_SEMANTIC_TEXT_COMMANDS = MappingProxyType({
    "LC5_COMPLETE_TEXT": {
        "complete.unit.call": "CalculateRecovery",
        "complete.unit.entry": "Entry · CurrentHealth, Bonus",
        "complete.unit.addition": "CurrentHealth + Bonus",
        "complete.unit.result": "Return · NewHealth",
    },
    "LC5_TYPED_PORTAL_BRIDGE": {
        "portal.unit.call": "CalculateRecovery",
        "portal.unit.entry": "Entry · CurrentHealth, Bonus",
        "portal.unit.addition": "CurrentHealth + Bonus",
        "portal.unit.result": "Return · NewHealth",
    },
    "LC5_ADJACENT_COMPOUND_EXPANSION": {
        "adjacent.unit.call": "CalculateRecovery",
        "adjacent.unit.entry": "Entry · CurrentHealth, Bonus",
        "adjacent.unit.addition": "CurrentHealth + Bonus",
        "adjacent.unit.result": "Return · NewHealth",
    },
    "LC5_PERSISTENT_CONTEXT_REGIONS": {
        "context.unit.call": "CalculateRecovery",
        "context.unit.entry": "Entry · CurrentHealth, Bonus",
        "context.unit.addition": "CurrentHealth + Bonus",
        "context.unit.result": "Return · NewHealth",
    },
    "LC5_CALL_RETURN_WEAVE": {
        "weave.unit.call": "CalculateRecovery",
        "weave.unit.entry": "Entry · CurrentHealth, Bonus",
        "weave.unit.addition": "CurrentHealth + Bonus",
        "weave.unit.result": "Return · NewHealth",
    },
})


def _build_expected_text_commands() -> dict[str, Mapping[str, Mapping[str, Any]]]:
    expected_by_condition: dict[str, Mapping[str, Mapping[str, Any]]] = {}
    for condition_id in CONDITION_IDS:
        commands = {
            command_id: dict(contract)
            for command_id, contract in _EXPECTED_COMMON_TEXT_COMMANDS.items()
        }
        for command_id, (text, size, weight) in _CONDITION_TEXT_COMMANDS[condition_id].items():
            commands[command_id] = _expected_text_contract(text, size, weight)
        for command_id, reader_label in _SEMANTIC_TEXT_COMMANDS[condition_id].items():
            commands[command_id] = _expected_text_contract(
                reader_label,
                11,
                "bold",
                semantic_occurrence_id=_EXPECTED_SEMANTIC_UNITS[reader_label]["occurrence_id"],
            )
        for channel in _EXPECTED_RELATION_CHANNELS[condition_id].values():
            commands[channel["label_id"]] = _expected_text_contract(channel["label"], 8)
        expected_by_condition[condition_id] = MappingProxyType(commands)
    return expected_by_condition


_EXPECTED_TEXT_COMMANDS = MappingProxyType(_build_expected_text_commands())


def validate_lc5_scene(scene: Mapping[str, Any]) -> dict[str, Any]:
    """Independently validate LC5 semantic coverage, cues and measured geometry."""

    canvas = scene.get("canvas", {})
    width, height = canvas.get("width"), canvas.get("height")
    condition_id = scene.get("condition_id")
    commands = scene.get("commands", [])
    command_ids = [item.get("id") for item in commands if isinstance(item, Mapping)]
    duplicate_command_ids = sorted({item for item in command_ids if command_ids.count(item) > 1}, key=str)
    commands_by_id = {
        item.get("id"): item for item in commands
        if isinstance(item, Mapping) and isinstance(item.get("id"), str)
    }
    malformed_command_ids: list[str] = []
    text_measurement_mismatch_ids: list[str] = []
    out_of_bounds_ids: list[str] = []
    for index, command in enumerate(commands):
        command_id = str(command.get("id", f"command[{index}]")) if isinstance(command, Mapping) else f"command[{index}]"
        if not isinstance(command, Mapping):
            malformed_command_ids.append(command_id)
            continue
        bounds = _command_bounds(command)
        paint_bounds = _paint_bounds(command)
        if bounds is None or paint_bounds is None:
            malformed_command_ids.append(command_id)
            continue
        if command.get("kind") == "text" and _measured_text_bounds(command) != bounds:
            text_measurement_mismatch_ids.append(command_id)
        if (
            not isinstance(width, int) or not isinstance(height, int)
            or paint_bounds[0] < 0 or paint_bounds[1] < 0
            or paint_bounds[2] > width or paint_bounds[3] > height
            or paint_bounds[0] > paint_bounds[2] or paint_bounds[1] > paint_bounds[3]
        ):
            out_of_bounds_ids.append(command_id)

    text_commands = [item for item in commands if isinstance(item, Mapping) and item.get("kind") == "text"]
    expected_text_commands = _EXPECTED_TEXT_COMMANDS.get(condition_id, {})
    actual_text_ids = {
        item.get("id") for item in text_commands if isinstance(item.get("id"), str)
    }
    text_contract_errors = sorted(
        set(expected_text_commands).symmetric_difference(actual_text_ids), key=str,
    )
    font_contract_errors: list[str] = []
    for command_id in set(expected_text_commands) & actual_text_ids:
        command = commands_by_id[command_id]
        expected = expected_text_commands[command_id]
        if any(
            command.get(field) != expected[field]
            for field in (
                "text", "font_size", "weight", "action_id", "semantic_occurrence_id",
            )
        ):
            text_contract_errors.append(command_id)
        weight = command.get("weight")
        if (
            not isinstance(weight, str)
            or weight not in FONTS
            or command.get("font_path") != FONTS[weight]
        ):
            font_contract_errors.append(command_id)
    text_overlap_pairs = []
    for index, first in enumerate(text_commands):
        first_bounds = _command_bounds(first)
        if first_bounds is None:
            continue
        for second in text_commands[index + 1:]:
            second_bounds = _command_bounds(second)
            if second_bounds is not None and _rectangles_overlap(first_bounds, second_bounds):
                text_overlap_pairs.append([first["id"], second["id"]])

    route_commands = [item for item in commands if isinstance(item, Mapping) and item.get("kind") == "polyline"]
    expected_route_points = _EXPECTED_ROUTE_POINTS.get(condition_id, {})
    route_geometry_errors = sorted(
        route_id for route_id, expected_points in expected_route_points.items()
        if tuple(tuple(point) for point in commands_by_id.get(route_id, {}).get("points", []))
        != expected_points
    )
    route_text_collision_pairs = []
    for route in route_commands:
        points = route.get("points", [])
        if len(points) < 2:
            continue
        for text_command in text_commands:
            text_bounds = _command_bounds(text_command)
            if text_bounds is None:
                continue
            expanded = [
                text_bounds[0] - 3, text_bounds[1] - 3,
                text_bounds[2] + 3, text_bounds[3] + 3,
            ]
            if any(
                _segment_intersects_rect(tuple(start), tuple(end), expanded)
                for start, end in zip(points, points[1:])
            ):
                route_text_collision_pairs.append([route["id"], text_command["id"]])

    information = scene.get("information_set", {})
    information_set_errors = [] if information == _EXPECTED_INFORMATION_SET else ["information_set"]
    semantic_units = scene.get("semantic_units", [])
    semantic_ids = [item.get("occurrence_id") for item in semantic_units if isinstance(item, Mapping)]
    expected_semantic_ids = _EXPECTED_INFORMATION_SET["occurrence_ids"]
    semantic_command_ids = [
        item.get("semantic_occurrence_id") for item in commands
        if isinstance(item, Mapping) and item.get("semantic_occurrence_id") is not None
    ]
    duplicate_semantic_unit_ids = sorted(
        {item for item in semantic_ids if semantic_ids.count(item) > 1}, key=str
    )
    missing_semantic_unit_ids = sorted(set(expected_semantic_ids) - set(semantic_ids), key=str)
    extra_semantic_unit_ids = sorted(set(semantic_ids) - set(expected_semantic_ids), key=str)
    occurrence_ownership_errors = sorted(
        set(expected_semantic_ids).symmetric_difference(semantic_command_ids)
        | {item for item in semantic_command_ids if semantic_command_ids.count(item) != 1},
        key=str,
    )
    semantic_label_errors: list[str] = []
    if {item.get("label") for item in semantic_units if isinstance(item, Mapping)} != _READER_LABELS:
        semantic_label_errors.append("reader_labels")
    for item in semantic_units:
        if not isinstance(item, Mapping):
            continue
        command = commands_by_id.get(item.get("command_id"))
        if (
            command is None
            or command.get("kind") != "text"
            or command.get("text") != item.get("label")
            or command.get("semantic_occurrence_id") != item.get("occurrence_id")
        ):
            semantic_label_errors.append(str(item.get("occurrence_id")))
        expected_unit = _EXPECTED_SEMANTIC_UNITS.get(str(item.get("label")))
        if expected_unit is None or any(
            item.get(field) != expected_unit[field]
            for field in ("occurrence_id", "source_node_id", "role")
        ):
            semantic_label_errors.append(str(item.get("occurrence_id")))

    relation_coverage = scene.get("relation_coverage", {})
    expected_relation_ids = set(_EXPECTED_RELATION_FAMILIES)
    covered_relation_ids = set(relation_coverage) if isinstance(relation_coverage, Mapping) else set()
    missing_relation_ids = sorted(expected_relation_ids - covered_relation_ids)
    extra_relation_ids = sorted(covered_relation_ids - expected_relation_ids)
    relation_route_errors: list[str] = []
    relation_channel_errors: list[str] = []
    redundant_cue_errors: list[str] = []
    expected_styles = {
        "enter": ("solid", "arrow"), "argument": ("dashed", "socket"),
        "internal": ("solid", "data_arrow"), "result": ("double", "double_arrow"),
        "return": ("solid", "return_arrow"),
    }
    if isinstance(relation_coverage, Mapping):
        for relation_id, coverage in relation_coverage.items():
            family = coverage.get("family") if isinstance(coverage, Mapping) else None
            expected_family = _EXPECTED_RELATION_FAMILIES.get(str(relation_id))
            expected_channel = _EXPECTED_RELATION_CHANNELS.get(condition_id, {}).get(
                str(relation_id)
            )
            if expected_channel is None or coverage != {
                "family": expected_channel["family"],
                "route_command_ids": [expected_channel["route_id"]],
                "label_command_id": expected_channel["label_id"],
            }:
                relation_channel_errors.append(str(relation_id))
            if family != expected_family:
                relation_route_errors.append(str(relation_id))
            route_ids = coverage.get("route_command_ids", []) if isinstance(coverage, Mapping) else []
            if not route_ids:
                relation_route_errors.append(str(relation_id))
                continue
            for route_id in route_ids:
                route = commands_by_id.get(route_id)
                if route is None or route.get("kind") != "polyline" or route.get("relation_id") != relation_id:
                    relation_route_errors.append(str(relation_id))
                    continue
                if len(route.get("points", [])) < 2 or route.get("relation_family") != expected_family:
                    relation_route_errors.append(str(relation_id))
                expected_style = expected_styles.get(family)
                label_id = route.get("label_command_id")
                label = commands_by_id.get(label_id)
                if (
                    expected_style is None
                    or route.get("line_style") != expected_style[0]
                    or route.get("marker") != expected_style[1]
                    or route.get("stroke") != _RELATION_STYLE.get(family, (None,))[0]
                    or label is None or label.get("kind") != "text"
                ):
                    redundant_cue_errors.append(str(relation_id))
                expected_label = _expected_relation_label(str(relation_id))
                actual_label = label.get("text") if isinstance(label, Mapping) else None
                normalized_label = re.sub(r"^[0-9]+\.\s+", "", actual_label or "")
                if expected_label is None or normalized_label != expected_label:
                    relation_route_errors.append(str(relation_id))
                socket_id = expected_channel.get("socket_id") if expected_channel else None
                socket = commands_by_id.get(socket_id) if socket_id else None
                if (
                    expected_channel is None
                    or route_id != expected_channel["route_id"]
                    or route.get("points") != expected_channel["points"]
                    or route.get("relation_id") != relation_id
                    or route.get("relation_family") != expected_channel["family"]
                    or route.get("label_command_id") != expected_channel["label_id"]
                    or route.get("marker") != expected_channel["marker"]
                    or label_id != expected_channel["label_id"]
                    or actual_label != expected_channel["label"]
                    or (
                        socket_id is not None
                        and (
                            socket is None
                            or socket.get("kind") != "circle"
                            or socket.get("relation_id") != relation_id
                            or socket.get("relation_family") != expected_channel["family"]
                            or any(
                                socket.get(field) != expected_channel["socket"][field]
                                for field in ("cx", "cy", "radius")
                            )
                        )
                    )
                ):
                    relation_channel_errors.append(str(relation_id))
                if family == "argument" and not any(
                    item.get("kind") == "circle" and item.get("relation_id") == relation_id
                    for item in commands if isinstance(item, Mapping)
                ):
                    redundant_cue_errors.append(str(relation_id))

    relation_owner_command_errors: list[str] = []
    commands_by_relation: dict[str, set[str]] = {}
    for command in commands:
        if not isinstance(command, Mapping) or command.get("relation_id") is None:
            continue
        relation_id = str(command["relation_id"])
        command_id = command.get("id")
        if not isinstance(command_id, str):
            relation_owner_command_errors.append(relation_id)
            continue
        commands_by_relation.setdefault(relation_id, set()).add(command_id)
    for relation_id, channel in _EXPECTED_RELATION_CHANNELS.get(condition_id, {}).items():
        expected_owner_ids = {channel["route_id"]}
        if channel["socket_id"] is not None:
            expected_owner_ids.add(channel["socket_id"])
        if commands_by_relation.get(relation_id, set()) != expected_owner_ids:
            relation_owner_command_errors.append(relation_id)
    relation_owner_command_errors.extend(
        sorted(set(commands_by_relation) - expected_relation_ids)
    )

    actions = scene.get("action_coverage", [])
    action_ids = [item.get("action_id") for item in actions if isinstance(item, Mapping)]
    expected_action_ids = set(_EXPECTED_INFORMATION_SET["action_ids"])
    missing_action_ids = sorted(expected_action_ids - set(action_ids))
    extra_action_ids = sorted(set(action_ids) - expected_action_ids)
    action_command_errors = sorted(
        action_id for action_id in expected_action_ids
        if not any(
            item.get("action_id") == action_id and item.get("kind") == "text"
            for item in commands if isinstance(item, Mapping)
        )
    )
    action_contract_error = tuple(actions) != _ACTIONS
    for action in _ACTIONS:
        matching_labels = [
            item for item in commands
            if isinstance(item, Mapping)
            and item.get("kind") == "text"
            and item.get("action_id") == action["action_id"]
        ]
        if len(matching_labels) != 1 or matching_labels[0].get("text") != action["label"]:
            action_command_errors.append(action["action_id"])
        matching_buttons = [
            item for item in commands
            if isinstance(item, Mapping)
            and item.get("kind") == "rect"
            and item.get("action_id") == action["action_id"]
        ]
        if len(matching_buttons) != 1:
            action_command_errors.append(action["action_id"])

    required_primitives = REQUIRED_PRIMITIVES.get(condition_id, frozenset())
    missing_primitive_ids = sorted(required_primitives - set(command_ids))
    primitive_kind_errors = sorted(
        primitive_id for primitive_id in required_primitives
        if commands_by_id.get(primitive_id, {}).get("kind") != _REQUIRED_PRIMITIVE_KINDS[primitive_id]
    )
    structural_geometry_errors: list[str] = []
    for primitive_id, expected_geometry in _EXPECTED_STRUCTURAL_PRIMITIVES.get(
        condition_id, {}
    ).items():
        command = commands_by_id.get(primitive_id)
        if command is None or any(
            command.get(field) != expected_value
            for field, expected_value in expected_geometry.items()
        ):
            structural_geometry_errors.append(primitive_id)
    region_contract_errors = (
        []
        if scene.get("regions") == list(_EXPECTED_REGIONS.get(condition_id, ()))
        else [str(condition_id)]
    )
    region_draw_geometry_errors: list[str] = []
    for command_id, expected_geometry in _EXPECTED_REGION_DRAW_COMMANDS.get(
        condition_id, {}
    ).items():
        command = commands_by_id.get(command_id)
        if command is None or any(
            command.get(field) != expected_value
            for field, expected_value in expected_geometry.items()
        ):
            region_draw_geometry_errors.append(command_id)
    semantic_geometry_errors: list[str] = []
    for command_id, expected_geometry in _EXPECTED_SEMANTIC_ANCHOR_GEOMETRY.get(
        condition_id, {}
    ).items():
        command = commands_by_id.get(command_id)
        if command is None or any(
            command.get(field) != expected_value
            for field, expected_value in expected_geometry.items()
        ):
            semantic_geometry_errors.append(command_id)
    expected_anchor_owners = {
        command_id: geometry["semantic_anchor_for_occurrence_id"]
        for command_id, geometry in _EXPECTED_SEMANTIC_ANCHOR_GEOMETRY.get(
            condition_id, {}
        ).items()
        if "semantic_anchor_for_occurrence_id" in geometry
    }
    actual_anchor_owners = {
        command.get("id"): command.get("semantic_anchor_for_occurrence_id")
        for command in commands
        if isinstance(command, Mapping)
        and command.get("semantic_anchor_for_occurrence_id") is not None
    }
    semantic_anchor_ownership_errors = sorted(
        {
            command_id for command_id in set(expected_anchor_owners) | set(actual_anchor_owners)
            if expected_anchor_owners.get(command_id) != actual_anchor_owners.get(command_id)
        },
        key=str,
    )
    relation_attachment_errors: list[str] = []
    outside_attachment_errors: list[str] = []
    socket_attachment_errors: list[str] = []
    physical_attachment_endpoint_count = 0
    for relation_id, attachment in _EXPECTED_RELATION_ATTACHMENTS.get(
        condition_id, {}
    ).items():
        route = commands_by_id.get(attachment["route_id"])
        route_points = route.get("points", []) if isinstance(route, Mapping) else []
        for endpoint, point_index in (("start", 0), ("end", -1)):
            endpoint_contract = attachment[endpoint]
            semantic_command = commands_by_id.get(
                endpoint_contract["semantic_command_id"]
            )
            expected_point = (
                _resolve_physical_anchor(semantic_command, endpoint_contract["anchor"])
                if isinstance(semantic_command, Mapping)
                else None
            )
            physical_attachment_endpoint_count += 1
            actual_point = (
                route_points[point_index]
                if isinstance(route_points, list) and route_points
                else None
            )
            socket_id = endpoint_contract.get("socket_command_id")
            socket = commands_by_id.get(socket_id) if socket_id else None
            socket_point = (
                [socket.get("cx"), socket.get("cy")]
                if isinstance(socket, Mapping) else None
            )
            if (
                actual_point != endpoint_contract["anchor_point"]
                or endpoint_contract["anchor_point"] != expected_point
                or (socket_id is not None and socket_point != expected_point)
            ):
                relation_attachment_errors.append(f"{relation_id}:{endpoint}")
            if expected_point is None:
                outside_attachment_errors.append(f"{relation_id}:{endpoint}")
            if socket_id is not None and (
                socket_point != actual_point
                or not isinstance(semantic_command, Mapping)
                or not _point_on_shape_boundary(semantic_command, socket_point or [])
            ):
                socket_attachment_errors.append(f"{relation_id}:{endpoint}")
    composition_command_errors = sorted(
        command_id for command_id in _REQUIRED_COMPOSITION_COMMANDS.get(condition_id, frozenset())
        if command_id not in commands_by_id
    )
    condition_errors = [] if condition_id in CONDITION_IDS else [str(condition_id)]
    copy_claims = scene.get("copy_claims", {})
    copy_errors = []
    expected_copy_claims = {
        "question": _QUESTION,
        "criterion": "NewHealth · criterion",
        "scope": "Static contextual slice · depth 1",
        "frontier": _FRONTIER,
        "static_only": True,
        "runtime_order_claimed": False,
    }
    if copy_claims != expected_copy_claims:
        copy_errors.append("copy_claims")
    if scene.get("bands") != BANDS:
        copy_errors.append("bands")
    if scene.get("tokens") != TOKENS:
        copy_errors.append("tokens")
    if copy_claims.get("question") != _QUESTION:
        copy_errors.append("question")
    if copy_claims.get("frontier") != _FRONTIER:
        copy_errors.append("frontier")
    if copy_claims.get("static_only") is not True or copy_claims.get("runtime_order_claimed") is not False:
        copy_errors.append("static_boundary")
    command_texts = {item.get("text") for item in text_commands}
    for required_text in (
        _QUESTION,
        _FRONTIER,
        "NewHealth · criterion",
        "Static contextual slice · depth 1",
        "Static boundary · occurrences are not runtime invocations; no runtime order is claimed",
    ):
        if required_text not in command_texts:
            copy_errors.append(required_text)
    prohibited_claim_fragments = (
        "runtime order", "runtime invocation", "observed invocation", "executed", "live value",
    )
    approved_static_boundary = (
        "Static boundary · occurrences are not runtime invocations; no runtime order is claimed"
    )
    if any(
        fragment in str(text).casefold()
        for text in command_texts
        if text is not None and text != approved_static_boundary
        for fragment in prohibited_claim_fragments
    ):
        copy_errors.append("runtime_claim")
    background = commands_by_id.get("canvas.background", {})
    if (
        background.get("kind") != "rect"
        or _command_bounds(background) != [0, 0, *CANVAS]
        or background.get("fill") != TOKENS["background"]
    ):
        copy_errors.append("canvas.background")

    checks: dict[str, Any] = {
        "canvas": [width, height],
        "semantic_unit_count": len(semantic_units),
        "relation_coverage_count": len(covered_relation_ids),
        "action_coverage_count": len(action_ids),
        "text_command_count": len(text_commands),
        "route_command_count": len(route_commands),
        "duplicate_command_ids": duplicate_command_ids,
        "malformed_command_ids": malformed_command_ids,
        "text_measurement_mismatch_ids": sorted(text_measurement_mismatch_ids),
        "text_contract_errors": sorted(set(text_contract_errors), key=str),
        "font_contract_errors": sorted(set(font_contract_errors), key=str),
        "out_of_bounds_ids": sorted(out_of_bounds_ids),
        "text_overlap_pairs": text_overlap_pairs,
        "route_text_collision_pairs": route_text_collision_pairs,
        "route_geometry_errors": route_geometry_errors,
        "duplicate_semantic_unit_ids": duplicate_semantic_unit_ids,
        "missing_semantic_unit_ids": missing_semantic_unit_ids,
        "extra_semantic_unit_ids": extra_semantic_unit_ids,
        "occurrence_ownership_errors": occurrence_ownership_errors,
        "semantic_label_errors": sorted(set(semantic_label_errors)),
        "information_set_errors": information_set_errors,
        "missing_relation_ids": missing_relation_ids,
        "extra_relation_ids": extra_relation_ids,
        "relation_route_errors": sorted(set(relation_route_errors)),
        "relation_channel_errors": sorted(set(relation_channel_errors)),
        "relation_owner_command_errors": sorted(set(relation_owner_command_errors)),
        "redundant_cue_errors": sorted(set(redundant_cue_errors)),
        "missing_action_ids": missing_action_ids,
        "extra_action_ids": extra_action_ids,
        "action_command_errors": sorted(set(action_command_errors)),
        "action_contract_error": action_contract_error,
        "missing_primitive_ids": missing_primitive_ids,
        "primitive_kind_errors": primitive_kind_errors,
        "structural_geometry_errors": structural_geometry_errors,
        "region_contract_errors": region_contract_errors,
        "region_draw_geometry_errors": region_draw_geometry_errors,
        "semantic_geometry_errors": semantic_geometry_errors,
        "semantic_anchor_ownership_errors": semantic_anchor_ownership_errors,
        "relation_attachment_errors": sorted(set(relation_attachment_errors)),
        "outside_attachment_errors": sorted(set(outside_attachment_errors)),
        "socket_attachment_errors": sorted(set(socket_attachment_errors)),
        "physical_attachment_endpoint_count": physical_attachment_endpoint_count,
        "composition_command_errors": composition_command_errors,
        "condition_errors": condition_errors,
        "copy_errors": sorted(set(copy_errors)),
    }
    checks["pass"] = (
        checks["canvas"] == list(CANVAS)
        and checks["semantic_unit_count"] == 4
        and checks["relation_coverage_count"] == 9
        and checks["action_coverage_count"] == 4
        and not any(
            value for key, value in checks.items()
            if key not in {
                "canvas", "semantic_unit_count", "relation_coverage_count",
                "action_coverage_count", "text_command_count", "route_command_count", "pass",
                "physical_attachment_endpoint_count",
            }
        )
    )
    return checks


def _json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    path.write_bytes(_json_bytes(value))


def _svg_dash(command: Mapping[str, Any]) -> str:
    return ' stroke-dasharray="7 6"' if command.get("line_style") == "dashed" else ""


def _svg_for_scene(scene: Mapping[str, Any]) -> str:
    width = scene["canvas"]["width"]
    height = scene["canvas"]["height"]
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="LC5 {html.escape(scene["condition_label"])}">',
        '<style>text{font-family:"Segoe UI",Arial,sans-serif}</style>',
    ]
    for command in scene["commands"]:
        kind = command["kind"]
        command_id = html.escape(command["id"], quote=True)
        if kind == "rect":
            out.append(
                f'<rect id="{command_id}" x="{command["x"]}" y="{command["y"]}" width="{command["width"]}" height="{command["height"]}" rx="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>'
            )
        elif kind == "circle":
            out.append(
                f'<circle id="{command_id}" cx="{command["cx"]}" cy="{command["cy"]}" r="{command["radius"]}" fill="{command["fill"]}" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}"/>'
            )
        elif kind == "polyline":
            points = " ".join(f"{x},{y}" for x, y in command["points"])
            out.append(
                f'<polyline id="{command_id}" points="{points}" fill="none" stroke="{command["stroke"]}" stroke-width="{command["stroke_width"]}" stroke-linecap="round" stroke-linejoin="round"{_svg_dash(command)}/>'
            )
            if command.get("marker") == "arrow" and len(command["points"]) >= 2:
                end = command["points"][-1]
                previous = command["points"][-2]
                angle = math.atan2(end[1] - previous[1], end[0] - previous[0])
                size = 8
                left = (
                    end[0] - size * math.cos(angle - math.pi / 6),
                    end[1] - size * math.sin(angle - math.pi / 6),
                )
                right = (
                    end[0] - size * math.cos(angle + math.pi / 6),
                    end[1] - size * math.sin(angle + math.pi / 6),
                )
                arrow_points = " ".join(
                    f"{x:.3f},{y:.3f}" for x, y in (tuple(end), left, right)
                )
                out.append(
                    f'<polygon id="{command_id}.arrow" points="{arrow_points}" fill="{command["stroke"]}"/>'
                )
        elif kind == "text":
            weight = "700" if command["weight"] == "bold" else "400"
            out.append(
                f'<text id="{command_id}" x="{command["x"]}" y="{command["y"]}" font-size="{command["font_size"]}" font-weight="{weight}" fill="{command["fill"]}" dominant-baseline="text-before-edge">{html.escape(command["text"])}</text>'
            )
        else:
            raise LC5VisualError(f"unsupported LC5 scene command kind: {kind}")
    out.append("</svg>")
    return "\n".join(out) + "\n"


def _dash_segment(
    draw: ImageDraw.ImageDraw,
    start: tuple[int, int],
    end: tuple[int, int],
    *,
    fill: str,
    width: int,
) -> None:
    dx, dy = end[0] - start[0], end[1] - start[1]
    length = math.hypot(dx, dy)
    if length == 0:
        return
    distance = 0.0
    while distance < length:
        segment_end = min(distance + 7.0, length)
        x1 = start[0] + dx * distance / length
        y1 = start[1] + dy * distance / length
        x2 = start[0] + dx * segment_end / length
        y2 = start[1] + dy * segment_end / length
        draw.line((x1, y1, x2, y2), fill=fill, width=width)
        distance += 13.0


def _draw_arrow(
    draw: ImageDraw.ImageDraw, points: list[list[int]], *, fill: str
) -> None:
    if len(points) < 2:
        return
    end = points[-1]
    previous = points[-2]
    angle = math.atan2(end[1] - previous[1], end[0] - previous[0])
    size = 8
    left = (
        end[0] - size * math.cos(angle - math.pi / 6),
        end[1] - size * math.sin(angle - math.pi / 6),
    )
    right = (
        end[0] - size * math.cos(angle + math.pi / 6),
        end[1] - size * math.sin(angle + math.pi / 6),
    )
    draw.polygon((tuple(end), left, right), fill=fill)


def _png_for_scene(scene: Mapping[str, Any], path: Path) -> None:
    size = (scene["canvas"]["width"], scene["canvas"]["height"])
    image = Image.new("RGB", size, scene["tokens"]["background"])
    draw = ImageDraw.Draw(image)
    for command in scene["commands"]:
        kind = command["kind"]
        if kind == "rect":
            box = (
                command["x"], command["y"],
                command["x"] + command["width"],
                command["y"] + command["height"],
            )
            draw.rounded_rectangle(
                box,
                radius=command["radius"],
                fill=command["fill"],
                outline=command["stroke"] if command["stroke_width"] else None,
                width=command["stroke_width"],
            )
        elif kind == "circle":
            box = (
                command["cx"] - command["radius"],
                command["cy"] - command["radius"],
                command["cx"] + command["radius"],
                command["cy"] + command["radius"],
            )
            draw.ellipse(
                box, fill=command["fill"], outline=command["stroke"],
                width=command["stroke_width"],
            )
        elif kind == "polyline":
            points = [tuple(point) for point in command["points"]]
            if command.get("line_style") == "dashed":
                for start, end in zip(points, points[1:]):
                    _dash_segment(
                        draw, start, end, fill=command["stroke"],
                        width=command["stroke_width"],
                    )
            else:
                draw.line(
                    points, fill=command["stroke"], width=command["stroke_width"],
                    joint="curve",
                )
            if command.get("marker") == "arrow":
                _draw_arrow(draw, command["points"], fill=command["stroke"])
        elif kind == "text":
            font = ImageFont.truetype(command["font_path"], command["font_size"])
            draw.text(
                (command["x"], command["y"]), command["text"],
                font=font, fill=command["fill"], anchor="lt",
            )
        else:
            raise LC5VisualError(f"unsupported LC5 scene command kind: {kind}")
    image.save(path, format="PNG", optimize=False, compress_level=9)


def _oracle_state(scene: Mapping[str, Any]) -> dict[str, Any]:
    checks = validate_lc5_scene(scene)
    return {
        "condition_id": scene["condition_id"],
        "width": scene["canvas"]["width"],
        "canvas_height": scene["canvas"]["height"],
        "semantic_unit_count": len(scene["semantic_units"]),
        "relation_coverage_count": len(scene["relation_coverage"]),
        "checks": checks,
    }


def _comparison_board(effect_paths: list[Path], path: Path) -> None:
    board = Image.new("RGB", (1460, 3104), TOKENS["background"])
    positions = ((20, 20), (740, 20), (20, 1052), (740, 1052), (20, 2084))
    for effect_path, position in zip(effect_paths, positions):
        with Image.open(effect_path) as effect:
            board.paste(effect.convert("RGB"), position)
    draw = ImageDraw.Draw(board)
    draw.rounded_rectangle(
        (740, 2084, 1440, 3084), radius=10, fill=TOKENS["surface"],
        outline="#2A3340", width=2,
    )
    draw.text(
        (770, 2140), _QUESTION, font=ImageFont.truetype(FONTS["bold"], 9),
        fill=TOKENS["text"], anchor="lt",
    )
    draw.text(
        (770, 2200), "No default selected",
        font=ImageFont.truetype(FONTS["regular"], 14),
        fill=TOKENS["muted"], anchor="lt",
    )
    board.save(path, format="PNG", optimize=False, compress_level=9)


def _replace_directory(staging: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if not destination.is_dir():
            raise LC5VisualError(f"LC5 packet destination is not a directory: {destination}")
        staged_files = sorted(path.name for path in staging.iterdir() if path.is_file())
        destination_files = sorted(
            path.name for path in destination.iterdir() if path.is_file()
        )
        identical = staged_files == destination_files and all(
            (staging / name).read_bytes() == (destination / name).read_bytes()
            for name in staged_files
        )
        if identical:
            shutil.rmtree(staging)
            return
        raise LC5VisualError(
            f"refusing to overwrite different LC5 packet evidence: {destination}"
        )
    staging.rename(destination)


def build_lc5_visual_artifacts(
    contextual_path: str | Path,
    readiness_path: str | Path,
    fixture_path: str | Path,
    output_dir: str | Path,
) -> dict[str, Path]:
    """Build the complete deterministic LC5 authoring packet atomically."""

    ledger = load_lc5_visual_ledger(contextual_path, readiness_path, fixture_path)
    manifest = build_lc5_visual_manifest(ledger)
    validate_lc5_visual_manifest(manifest, ledger)
    destination = Path(output_dir)
    destination_parent = destination.parent.resolve()
    destination_parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".lc5-packet-", dir=destination_parent))
    try:
        manifest_path = staging / "lc5-visual-manifest.v1.json"
        _write_json(manifest_path, manifest)
        oracle_states: list[dict[str, Any]] = []
        png_paths: list[Path] = []
        for state in manifest["states"]:
            scene = build_lc5_scene(ledger, state["condition_id"], state["width"])
            oracle_state = _oracle_state(scene)
            oracle_states.append(oracle_state)
            if not oracle_state["checks"]["pass"]:
                raise LC5VisualError(
                    f'LC5 geometry oracle failed for {state["condition_id"]}'
                )
            svg_path = staging / state["effect_paths"]["svg"]
            png_path = staging / state["effect_paths"]["png"]
            svg_path.write_text(_svg_for_scene(scene), encoding="utf-8", newline="\n")
            _png_for_scene(scene, png_path)
            png_paths.append(png_path)
        oracle = {
            "format": "blueprint-lens-lc5-geometry-oracle",
            "schema_version": "1.0.0",
            "profile_id": ledger["profile_binding"]["profile_id"],
            "states": oracle_states,
        }
        oracle_path = staging / "lc5-geometry-oracle.v1.json"
        _write_json(oracle_path, oracle)
        board_path = staging / "lc5-comparison-board-700.png"
        _comparison_board(png_paths, board_path)
        hashed_paths = sorted(
            [path for path in staging.iterdir() if path.is_file()],
            key=lambda path: path.name,
        )
        _require(len(hashed_paths) == 13, "LC5 packet pre-hash inventory must contain 13 files")
        hashes = {
            "format": "blueprint-lens-lc5-visual-hashes",
            "schema_version": "1.0.0",
            "file_count": 13,
            "files": {
                path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                for path in hashed_paths
            },
        }
        hashes_path = staging / "lc5-visual-hashes.v1.json"
        _write_json(hashes_path, hashes)
        _replace_directory(staging, destination)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise
    return {
        "manifest": destination / "lc5-visual-manifest.v1.json",
        "oracle": destination / "lc5-geometry-oracle.v1.json",
        "hashes": destination / "lc5-visual-hashes.v1.json",
        "board": destination / "lc5-comparison-board-700.png",
    }
