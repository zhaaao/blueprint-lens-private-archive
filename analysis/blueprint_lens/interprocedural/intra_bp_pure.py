"""Bounded LC5 intra-Blueprint pure-call source-truth contract."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence

from ..raw_probe import BlueprintDocument, load_blueprint_lens_v1


BINDING_MISMATCH = "LC5_BINDING_MISMATCH"
DUPLICATE_OCCURRENCE = "LC5_DUPLICATE_OCCURRENCE"
FUNCTION_REFERENCE_MISMATCH = "LC5_FUNCTION_REFERENCE_MISMATCH"
SOURCE_AUDIT_MISMATCH = "LC5_SOURCE_AUDIT_MISMATCH"

PROFILE_ID = "LC5_INTRA_BP_PURE_CALL_V1"
PRODUCT_FORMAT = "blueprint-lens-intra-bp-pure-call-resolution"
PRODUCT_FORMAT_VERSION = "1.0.0"


class LC5IntraBpPureError(ValueError):
    """A fail-closed LC5 diagnostic with a stable code."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


@dataclass(frozen=True, slots=True)
class FrozenProjectDocumentProvider:
    """Frozen-fixture implementation of the project-document provider seam."""

    document: BlueprintDocument
    asset_sha256: str
    raw_sha256: str
    compile_provenance: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class AuditProduct:
    blueprint_asset_path: str
    compile_provenance: Mapping[str, str]
    asset_sha256: str
    raw_sha256: str
    call_graph_id: str
    call_node_id: str
    function_reference: Mapping[str, Any]
    target: Mapping[str, Any] | None
    candidate_count: int
    bindings: tuple[Mapping[str, Any], ...]


def _fail(code: str, message: str) -> None:
    raise LC5IntraBpPureError(code, message)


def _object(value: Any, code: str, context: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        _fail(code, f"{context} must be an object")
    return value


def _array(value: Any, code: str, context: str) -> list[Any]:
    if not isinstance(value, list):
        _fail(code, f"{context} must be an array")
    return value


def _string(value: Any, code: str, context: str, *, empty: bool = False) -> str:
    if not isinstance(value, str) or (not empty and not value):
        qualifier = "a string" if empty else "a non-empty string"
        _fail(code, f"{context} must be {qualifier}")
    return value


def _boolean(value: Any, code: str, context: str) -> bool:
    if not isinstance(value, bool):
        _fail(code, f"{context} must be a boolean")
    return value


def _integer(value: Any, code: str, context: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        _fail(code, f"{context} must be an integer")
    return value


def _parse_bool(value: str, line_number: int) -> bool:
    if value not in {"0", "1"}:
        _fail(
            SOURCE_AUDIT_MISMATCH,
            f"audit boolean at row {line_number} must be 0 or 1",
        )
    return value == "1"


def _single(values: Sequence[Any], context: str) -> Any:
    if len(values) != 1:
        _fail(
            SOURCE_AUDIT_MISMATCH,
            f"audit must contain one {context} row; found {len(values)}",
        )
    return values[0]


def parse_resolution_audit(text: str) -> AuditProduct:
    """Parse the independently produced native-resolution TSV."""

    formats: list[tuple[str, str]] = []
    blueprints: list[str] = []
    compiles: list[tuple[dict[str, str], str, str]] = []
    calls: list[tuple[str, str]] = []
    references: list[dict[str, Any]] = []
    targets: list[dict[str, Any]] = []
    graphs: list[dict[str, str]] = []
    candidate_counts: list[int] = []
    bindings: list[dict[str, Any]] = []
    binding_counts: list[int] = []

    for line_number, line in enumerate(text.splitlines(), start=1):
        fields = line.split("\t")
        record = fields[0] if fields else ""
        try:
            if record == "FORMAT" and len(fields) == 3:
                formats.append((fields[1], fields[2]))
            elif record == "BLUEPRINT" and len(fields) == 2:
                blueprints.append(fields[1])
            elif record == "COMPILE" and len(fields) == 6:
                compiles.append(
                    (
                        {
                            "status": fields[1],
                            "package_guid": fields[2],
                            "generated_class_path": fields[3],
                        },
                        fields[4],
                        fields[5],
                    )
                )
            elif record == "CALL" and len(fields) == 3:
                calls.append((fields[1], fields[2]))
            elif record == "REFERENCE" and len(fields) == 5:
                references.append(
                    {
                        "name": fields[1],
                        "guid": fields[2],
                        "parent_class": fields[3],
                        "is_self_context": _parse_bool(fields[4], line_number),
                    }
                )
            elif record == "TARGET" and len(fields) == 7:
                targets.append(
                    {
                        "function_path": fields[1],
                        "owner_class_path": fields[2],
                        "name": fields[3],
                        "guid": fields[4],
                        "is_pure": _parse_bool(fields[5], line_number),
                        "is_latent": _parse_bool(fields[6], line_number),
                    }
                )
            elif record == "GRAPH" and len(fields) == 6:
                graphs.append(
                    {
                        "graph_id": fields[1],
                        "graph_guid": fields[2],
                        "owner_blueprint_path": fields[3],
                        "entry_node_id": fields[4],
                        "result_node_id": fields[5],
                    }
                )
            elif record == "CANDIDATES" and len(fields) == 2:
                candidate_counts.append(int(fields[1]))
            elif record == "BINDING" and len(fields) == 15:
                bindings.append(
                    {
                        "ordinal": int(fields[1]),
                        "kind": fields[2],
                        "property": {
                            "path": fields[3],
                            "name": fields[4],
                            "direction": fields[5],
                            "cpp_type": fields[6],
                            "pin_type": {
                                "category": fields[7],
                                "subcategory": fields[8],
                                "object_path": fields[9],
                                "container": fields[10],
                                "is_reference": _parse_bool(fields[11], line_number),
                                "is_const": _parse_bool(fields[12], line_number),
                            },
                        },
                        "call_pin_id": fields[13],
                        "formal_pin_id": fields[14],
                    }
                )
            elif record == "COUNTS" and len(fields) == 2:
                binding_counts.append(int(fields[1]))
            else:
                raise ValueError(f"unexpected {record!r} record shape")
        except ValueError as error:
            _fail(
                SOURCE_AUDIT_MISMATCH,
                f"malformed audit row {line_number}: {error}",
            )

    if formats != [("blueprint-lens-intra-bp-pure-call-audit", "1.0.0")]:
        _fail(SOURCE_AUDIT_MISMATCH, "audit format is not the accepted v1 contract")
    compile_value, asset_sha256, raw_sha256 = _single(compiles, "COMPILE")
    call_graph_id, call_node_id = _single(calls, "CALL")
    if binding_counts != [len(bindings)]:
        _fail(SOURCE_AUDIT_MISMATCH, "audit binding count does not match rows")
    candidate_count = _single(candidate_counts, "CANDIDATES")
    if candidate_count < 0:
        _fail(SOURCE_AUDIT_MISMATCH, "candidate count cannot be negative")
    target: dict[str, Any] | None = None
    if targets or graphs:
        target = dict(_single(targets, "TARGET"))
        target.update(_single(graphs, "GRAPH"))
    return AuditProduct(
        blueprint_asset_path=_single(blueprints, "BLUEPRINT"),
        compile_provenance=compile_value,
        asset_sha256=asset_sha256,
        raw_sha256=raw_sha256,
        call_graph_id=call_graph_id,
        call_node_id=call_node_id,
        function_reference=_single(references, "REFERENCE"),
        target=target,
        candidate_count=candidate_count,
        bindings=tuple(sorted(bindings, key=lambda value: value["ordinal"])),
    )


def _pin_type(value: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "category": value.get("category"),
        "subcategory": value.get("subcategory"),
        "object_path": value.get("object_path"),
        "container": value.get("container"),
        "is_reference": value.get("is_reference"),
        "is_const": value.get("is_const"),
    }


def _normalize_binding(value: Any) -> dict[str, Any]:
    binding = _object(value, SOURCE_AUDIT_MISMATCH, "source binding")
    prop = _object(
        binding.get("property"), SOURCE_AUDIT_MISMATCH, "source binding property"
    )
    pin_type = _object(
        prop.get("pin_type"), SOURCE_AUDIT_MISMATCH, "source property pin type"
    )
    return {
        "ordinal": _integer(
            binding.get("ordinal"), SOURCE_AUDIT_MISMATCH, "binding ordinal"
        ),
        "kind": _string(binding.get("kind"), SOURCE_AUDIT_MISMATCH, "binding kind"),
        "property": {
            "path": _string(
                prop.get("path"), SOURCE_AUDIT_MISMATCH, "property path"
            ),
            "name": _string(
                prop.get("name"), SOURCE_AUDIT_MISMATCH, "property name"
            ),
            "direction": _string(
                prop.get("direction"), SOURCE_AUDIT_MISMATCH, "property direction"
            ),
            "cpp_type": _string(
                prop.get("cpp_type"), SOURCE_AUDIT_MISMATCH, "property cpp type"
            ),
            "pin_type": _pin_type(pin_type),
        },
        "call_pin_id": _string(
            binding.get("call_pin_id"), SOURCE_AUDIT_MISMATCH, "call pin id"
        ),
        "formal_pin_id": _string(
            binding.get("formal_pin_id"), SOURCE_AUDIT_MISMATCH, "formal pin id"
        ),
    }


def _validate_source_and_audit(
    provider: FrozenProjectDocumentProvider,
    source: Mapping[str, Any],
    audit: AuditProduct,
    call_site_node_id: str,
) -> tuple[
    Mapping[str, Any],
    Mapping[str, Any],
    list[Mapping[str, Any]],
    list[dict[str, Any]],
]:
    if source.get("format") != "blueprint-lens-intra-bp-pure-call-source" or source.get(
        "format_version"
    ) != "1.0.0":
        _fail(SOURCE_AUDIT_MISMATCH, "source facts are not the accepted v1 contract")
    asset_path = _string(
        source.get("blueprint_asset_path"),
        SOURCE_AUDIT_MISMATCH,
        "source blueprint asset path",
    )
    compile_value = _object(
        source.get("compile_provenance"),
        SOURCE_AUDIT_MISMATCH,
        "source compile provenance",
    )
    compile_normalized = {
        "status": compile_value.get("status"),
        "package_guid": compile_value.get("package_guid"),
        "generated_class_path": compile_value.get("generated_class_path"),
    }
    call_site = _object(
        source.get("call_site"), SOURCE_AUDIT_MISMATCH, "source call site"
    )
    reference = _object(
        call_site.get("function_reference"),
        SOURCE_AUDIT_MISMATCH,
        "source function reference",
    )
    reference_normalized = {
        "name": reference.get("name"),
        "guid": reference.get("guid"),
        "parent_class": reference.get("parent_class"),
        "is_self_context": reference.get("is_self_context"),
    }
    targets = [
        _object(value, SOURCE_AUDIT_MISMATCH, "source target")
        for value in _array(source.get("targets"), SOURCE_AUDIT_MISMATCH, "source targets")
    ]
    bindings = sorted(
        [
            _normalize_binding(value)
            for value in _array(
                source.get("bindings"), SOURCE_AUDIT_MISMATCH, "source bindings"
            )
        ],
        key=lambda value: value["ordinal"],
    )
    source_binding = (
        asset_path,
        source.get("asset_sha256"),
        source.get("raw_sha256"),
        compile_normalized,
        call_site.get("graph_id"),
        call_site.get("node_id"),
        reference_normalized,
        len(targets),
    )
    audit_binding = (
        audit.blueprint_asset_path,
        audit.asset_sha256,
        audit.raw_sha256,
        dict(audit.compile_provenance),
        audit.call_graph_id,
        audit.call_node_id,
        dict(audit.function_reference),
        audit.candidate_count,
    )
    if source_binding != audit_binding:
        _fail(SOURCE_AUDIT_MISMATCH, "native source and independent audit differ")
    if call_site.get("node_id") != call_site_node_id:
        _fail(SOURCE_AUDIT_MISMATCH, "requested call site differs from source binding")
    if asset_path != provider.document.blueprint_path:
        _fail(SOURCE_AUDIT_MISMATCH, "provider and source asset paths differ")
    if source.get("asset_sha256") != provider.asset_sha256:
        _fail(SOURCE_AUDIT_MISMATCH, "provider and source asset hashes differ")
    if source.get("raw_sha256") != provider.raw_sha256:
        _fail(SOURCE_AUDIT_MISMATCH, "provider and source raw hashes differ")

    if len(targets) == 1:
        target = targets[0]
        target_normalized = {
            "function_path": target.get("function_path"),
            "owner_class_path": target.get("owner_class_path"),
            "name": target.get("name"),
            "guid": target.get("guid"),
            "is_pure": target.get("is_pure"),
            "is_latent": target.get("is_latent"),
            "graph_id": target.get("graph_id"),
            "graph_guid": target.get("graph_guid"),
            "owner_blueprint_path": target.get("owner_blueprint_path"),
            "entry_node_id": target.get("entry_node_id"),
            "result_node_id": target.get("result_node_id"),
        }
        if audit.target is None or target_normalized != dict(audit.target):
            _fail(SOURCE_AUDIT_MISMATCH, "source and audit target identities differ")
        if bindings != list(audit.bindings):
            _fail(SOURCE_AUDIT_MISMATCH, "source and audit binding ledgers differ")
    return call_site, reference_normalized, targets, bindings


def _context_id(call_site_stack: Sequence[str]) -> str:
    digest = hashlib.sha256("\n".join(call_site_stack).encode("utf-8")).hexdigest()
    return f"lc5-context-{digest[:24]}"


def _base_product(
    call_site: Mapping[str, Any],
    call_context: Sequence[str],
    status: str,
    reason: str,
) -> dict[str, Any]:
    stack = [*call_context, str(call_site["node_id"])]
    return {
        "format": PRODUCT_FORMAT,
        "format_version": PRODUCT_FORMAT_VERSION,
        "profile_id": PROFILE_ID,
        "max_call_depth": 1,
        "status": status,
        "reason": reason,
        "source_identity": {
            "call_graph_id": call_site["graph_id"],
            "call_site_node_id": call_site["node_id"],
        },
        "call_context": {
            "id": _context_id(stack),
            "parent_id": _context_id(call_context),
            "call_site_stack": stack,
            "claim_scope": "static_contextual_occurrence_not_runtime_invocation",
        },
        "occurrences": [],
        "bindings": [],
        "context_relations": [],
        "internal_relations": [],
    }


def _find_call(provider: FrozenProjectDocumentProvider, node_id: str):
    matches = [node for node in provider.document.nodes if node.id == node_id]
    if len(matches) != 1:
        _fail(
            FUNCTION_REFERENCE_MISMATCH,
            f"call site must resolve once in frozen document; found {len(matches)}",
        )
    call = matches[0]
    if not call.class_path.endswith("K2Node_CallFunction") or call.symbol is None:
        _fail(FUNCTION_REFERENCE_MISMATCH, "call site is not a function-call node")
    return call


def _pins_by_id(nodes: Sequence[Any]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for node in nodes:
        for pin in node.pins:
            if pin.id in result:
                _fail(BINDING_MISMATCH, f"duplicate source pin identity: {pin.id}")
            result[pin.id] = pin
    return result


def _expected_cpp_type(category: str) -> str | None:
    return {
        "int": "int32",
        "int64": "int64",
        "bool": "bool",
        "real": "double",
        "float": "float",
        "string": "FString",
        "name": "FName",
    }.get(category)


def _validate_bindings(
    call: Any,
    graph: Any,
    target: Mapping[str, Any],
    bindings: Sequence[Mapping[str, Any]],
) -> list[dict[str, Any]]:
    entry_matches = [node for node in graph.nodes if node.id == target.get("entry_node_id")]
    result_matches = [node for node in graph.nodes if node.id == target.get("result_node_id")]
    if len(entry_matches) != 1 or len(result_matches) != 1:
        _fail(BINDING_MISMATCH, "target entry and result must each resolve once")
    entry, result = entry_matches[0], result_matches[0]
    if not entry.class_path.endswith("K2Node_FunctionEntry"):
        _fail(BINDING_MISMATCH, "target entry identity is not FunctionEntry")
    if not result.class_path.endswith("K2Node_FunctionResult"):
        _fail(BINDING_MISMATCH, "target result identity is not FunctionResult")

    call_pins = _pins_by_id([call])
    formal_pins = _pins_by_id([entry, result])
    normalized: list[dict[str, Any]] = []
    expected_call_pin_ids = {
        pin.id
        for pin in call.pins
        if pin.kind == "data" and pin.name != "self"
    }
    if [value["ordinal"] for value in bindings] != list(range(len(bindings))):
        _fail(BINDING_MISMATCH, "binding ordinals must be unique and contiguous")
    if {value["call_pin_id"] for value in bindings} != expected_call_pin_ids:
        _fail(BINDING_MISMATCH, "binding coverage differs from non-self call pins")

    for binding in bindings:
        kind = binding["kind"]
        if kind not in {"argument", "result"}:
            _fail(BINDING_MISMATCH, f"unknown binding kind: {kind!r}")
        call_pin = call_pins.get(binding["call_pin_id"])
        formal_pin = formal_pins.get(binding["formal_pin_id"])
        if call_pin is None or formal_pin is None:
            _fail(BINDING_MISMATCH, "binding pin identity is absent from source graph")
        prop = _object(binding["property"], BINDING_MISMATCH, "binding property")
        name = prop.get("name")
        expected_call_direction = "input" if kind == "argument" else "output"
        expected_formal_direction = "output" if kind == "argument" else "input"
        expected_property_direction = "input" if kind == "argument" else "return"
        expected_owner = entry if kind == "argument" else result
        if formal_pin.node_id != expected_owner.id:
            _fail(BINDING_MISMATCH, "formal pin belongs to the wrong endpoint node")
        if (
            call_pin.name != name
            or formal_pin.name != name
            or call_pin.direction != expected_call_direction
            or formal_pin.direction != expected_formal_direction
            or call_pin.kind != "data"
            or formal_pin.kind != "data"
            or prop.get("direction") != expected_property_direction
        ):
            _fail(BINDING_MISMATCH, "property and pin identity/direction differ")
        declared_type = _object(
            prop.get("pin_type"), BINDING_MISMATCH, "binding property pin type"
        )
        type_value = _pin_type(declared_type)
        if type_value != _pin_type(call_pin.type) or type_value != _pin_type(formal_pin.type):
            _fail(BINDING_MISMATCH, "property and pin type/container shape differ")
        if prop.get("cpp_type") != _expected_cpp_type(str(type_value["category"])):
            _fail(BINDING_MISMATCH, "property C++ type differs from pin category")
        expected_path = f"{target['function_path']}:{name}"
        if prop.get("path") != expected_path:
            _fail(BINDING_MISMATCH, "property path differs from target function")
        normalized.append(dict(binding))
    return normalized


def _occurrence(source_node_id: str, context_id: str, role: str) -> dict[str, str]:
    return {
        "source_node_id": source_node_id,
        "call_context_id": context_id,
        "occurrence_id": f"{source_node_id}::occurrence::{context_id}",
        "role": role,
    }


def resolve_intra_bp_pure_call(
    provider: FrozenProjectDocumentProvider,
    source: Mapping[str, Any],
    audit_text: str,
    *,
    call_site_node_id: str,
    max_call_depth: int = 1,
    call_context: tuple[str, ...] = (),
) -> dict[str, Any]:
    """Resolve one accepted LC5 profile without changing graph-local core-v1."""

    if max_call_depth < 0:
        _fail(BINDING_MISMATCH, "max_call_depth cannot be negative")
    audit = parse_resolution_audit(audit_text)
    call_site, reference, targets, bindings = _validate_source_and_audit(
        provider, source, audit, call_site_node_id
    )
    if call_site_node_id in call_context:
        return _base_product(
            call_site, call_context, "truncated", "recursive_call_context"
        )
    if len(call_context) >= max_call_depth:
        return _base_product(call_site, call_context, "truncated", "depth_budget_exhausted")
    if source["compile_provenance"].get("status") != "up_to_date":
        return _base_product(call_site, call_context, "unresolved", "stale_compile_state")
    if len(targets) == 0:
        return _base_product(call_site, call_context, "unresolved", "missing_target")
    if len(targets) != 1:
        return _base_product(call_site, call_context, "unresolved", "ambiguous_target")
    target = targets[0]
    if reference.get("is_self_context") is not True:
        return _base_product(call_site, call_context, "ineligible", "non_self_context")
    if target.get("is_pure") is not True:
        return _base_product(call_site, call_context, "ineligible", "impure_call")
    if target.get("is_latent") is not False:
        return _base_product(call_site, call_context, "ineligible", "latent_call")
    if target.get("owner_blueprint_path") != provider.document.blueprint_path:
        return _base_product(
            call_site, call_context, "ineligible", "cross_blueprint_target"
        )
    if dict(source["compile_provenance"]) != dict(provider.compile_provenance):
        _fail(SOURCE_AUDIT_MISMATCH, "provider compile provenance differs from source")

    call = _find_call(provider, call_site_node_id)
    symbol = call.symbol or {}
    if (
        reference.get("name") != symbol.get("name")
        or reference.get("guid") != symbol.get("guid")
        or reference.get("parent_class") != symbol.get("parent_class")
        or reference.get("is_self_context") != symbol.get("is_self_context")
    ):
        _fail(
            FUNCTION_REFERENCE_MISMATCH,
            "native function reference differs from frozen call symbol",
        )
    if (
        target.get("name") != reference.get("name")
        or target.get("guid") != reference.get("guid")
        or target.get("owner_class_path")
        != source["compile_provenance"].get("generated_class_path")
    ):
        _fail(FUNCTION_REFERENCE_MISMATCH, "target UFunction identity differs from reference")

    graph_matches = [
        graph for graph in provider.document.graphs if graph.id == target.get("graph_id")
    ]
    if len(graph_matches) != 1:
        _fail(FUNCTION_REFERENCE_MISMATCH, "target function graph must resolve once")
    graph = graph_matches[0]
    if graph.name != target.get("name") or graph.kind != "function":
        _fail(FUNCTION_REFERENCE_MISMATCH, "target graph name/kind differs from UFunction")
    normalized_bindings = _validate_bindings(call, graph, target, bindings)

    product = _base_product(call_site, call_context, "resolved_unique", "")
    context_id = product["call_context"]["id"]
    parent_context_id = product["call_context"]["parent_id"]
    call_occurrence = _occurrence(call.id, parent_context_id, "call_site")
    callee_occurrences = [
        _occurrence(node.id, context_id, "callee")
        for node in sorted(graph.nodes, key=lambda value: value.id)
    ]
    product["occurrences"] = [call_occurrence, *callee_occurrences]
    occurrence_by_source = {
        value["source_node_id"]: value for value in callee_occurrences
    }
    occurrence_by_source[call.id] = call_occurrence

    product_bindings: list[dict[str, Any]] = []
    context_relations: list[dict[str, str]] = []
    entry_occurrence = occurrence_by_source[str(target["entry_node_id"])]
    result_occurrence = occurrence_by_source[str(target["result_node_id"])]
    context_relations.append(
        {
            "kind": "call_enter",
            "source_occurrence_id": call_occurrence["occurrence_id"],
            "target_occurrence_id": entry_occurrence["occurrence_id"],
            "claim_scope": "static_context_boundary_not_runtime_event",
        }
    )
    for binding in normalized_bindings:
        relation_kind = (
            "argument_bind" if binding["kind"] == "argument" else "result_bind"
        )
        formal_node_id = str(binding["formal_pin_id"]).split("::pin::", 1)[0]
        formal_occurrence = occurrence_by_source[formal_node_id]
        relation = {
            "kind": relation_kind,
            "source_occurrence_id": (
                call_occurrence["occurrence_id"]
                if binding["kind"] == "argument"
                else formal_occurrence["occurrence_id"]
            ),
            "target_occurrence_id": (
                formal_occurrence["occurrence_id"]
                if binding["kind"] == "argument"
                else call_occurrence["occurrence_id"]
            ),
            "claim_scope": "static_parameter_binding",
        }
        context_relations.append(relation)
        product_binding = dict(binding)
        product_binding["relation_kind"] = relation_kind
        product_binding["source_occurrence_id"] = relation["source_occurrence_id"]
        product_binding["target_occurrence_id"] = relation["target_occurrence_id"]
        product_bindings.append(product_binding)
    context_relations.append(
        {
            "kind": "call_return",
            "source_occurrence_id": result_occurrence["occurrence_id"],
            "target_occurrence_id": call_occurrence["occurrence_id"],
            "claim_scope": "static_context_boundary_not_runtime_event",
        }
    )
    product["bindings"] = product_bindings
    product["context_relations"] = context_relations
    product["internal_relations"] = [
        {
            "source_edge_id": edge.id,
            "kind": edge.kind,
            "source_occurrence_id": occurrence_by_source[edge.source_node_id][
                "occurrence_id"
            ],
            "target_occurrence_id": occurrence_by_source[edge.target_node_id][
                "occurrence_id"
            ],
        }
        for edge in sorted(graph.edges, key=lambda value: value.id)
    ]
    validate_resolution_product(product)
    return product


def validate_resolution_product(product: Mapping[str, Any]) -> None:
    """Validate the unpublished LC5 resolution product shape and semantics."""

    if product.get("format") != PRODUCT_FORMAT or product.get(
        "format_version"
    ) != PRODUCT_FORMAT_VERSION:
        _fail(BINDING_MISMATCH, "resolution product format is not v1")
    if product.get("profile_id") != PROFILE_ID:
        _fail(BINDING_MISMATCH, "resolution product profile differs")
    if product.get("status") not in {
        "resolved_unique",
        "unresolved",
        "ineligible",
        "truncated",
    }:
        _fail(BINDING_MISMATCH, "resolution status is invalid")
    occurrences = [
        _object(value, BINDING_MISMATCH, "contextual occurrence")
        for value in _array(
            product.get("occurrences"), BINDING_MISMATCH, "contextual occurrences"
        )
    ]
    occurrence_ids = [value.get("occurrence_id") for value in occurrences]
    occurrence_keys = [
        (value.get("source_node_id"), value.get("call_context_id"))
        for value in occurrences
    ]
    if len(occurrence_ids) != len(set(occurrence_ids)) or len(occurrence_keys) != len(
        set(occurrence_keys)
    ):
        _fail(DUPLICATE_OCCURRENCE, "contextual occurrence identity is duplicated")
    if product.get("status") != "resolved_unique":
        if occurrences or product.get("bindings") or product.get("internal_relations"):
            _fail(BINDING_MISMATCH, "non-resolved product must not carry expanded facts")
        return

    by_id = {value["occurrence_id"]: value for value in occurrences}
    context = _object(product.get("call_context"), BINDING_MISMATCH, "call context")
    callee_context_id = context.get("id")
    call_occurrences = [value for value in occurrences if value.get("role") == "call_site"]
    callee_occurrences = [value for value in occurrences if value.get("role") == "callee"]
    if len(call_occurrences) != 1 or not callee_occurrences:
        _fail(BINDING_MISMATCH, "resolved product needs one call and callee occurrences")
    if any(value.get("call_context_id") != callee_context_id for value in callee_occurrences):
        _fail(BINDING_MISMATCH, "callee occurrence escaped its call context")

    internal_relations = _array(
        product.get("internal_relations"), BINDING_MISMATCH, "internal relations"
    )
    edge_ids: list[Any] = []
    for value in internal_relations:
        relation = _object(value, BINDING_MISMATCH, "internal relation")
        source_occurrence = by_id.get(relation.get("source_occurrence_id"))
        target_occurrence = by_id.get(relation.get("target_occurrence_id"))
        if source_occurrence is None or target_occurrence is None:
            _fail(BINDING_MISMATCH, "internal relation endpoint is missing")
        if (
            source_occurrence.get("role") != "callee"
            or target_occurrence.get("role") != "callee"
            or source_occurrence.get("call_context_id") != callee_context_id
            or target_occurrence.get("call_context_id") != callee_context_id
        ):
            _fail(BINDING_MISMATCH, "internal relation crosses a context boundary")
        edge_ids.append(relation.get("source_edge_id"))
    if len(edge_ids) != len(set(edge_ids)):
        _fail(BINDING_MISMATCH, "internal source edge is duplicated")

    for value in _array(
        product.get("context_relations"), BINDING_MISMATCH, "context relations"
    ):
        relation = _object(value, BINDING_MISMATCH, "context relation")
        if relation.get("source_occurrence_id") not in by_id or relation.get(
            "target_occurrence_id"
        ) not in by_id:
            _fail(BINDING_MISMATCH, "context relation endpoint is missing")


def canonical_resolution_bytes(product: Mapping[str, Any]) -> bytes:
    """Return deterministic bytes for repeated-product comparison."""

    return (json.dumps(product, ensure_ascii=False, sort_keys=True) + "\n").encode(
        "utf-8"
    )


def run_intra_bp_pure_mutations(
    provider: FrozenProjectDocumentProvider,
    source: Mapping[str, Any],
    audit_text: str,
    *,
    call_site_node_id: str,
) -> dict[str, Any]:
    """Run the named LC5 adversarial matrix against one reviewed native product."""

    def changed_audit(
        text: str,
        record: str,
        field_index: int,
        replacement: str,
    ) -> str:
        lines = text.splitlines()
        matches = [index for index, line in enumerate(lines) if line.startswith(f"{record}\t")]
        if len(matches) != 1:
            raise AssertionError(f"mutation requires one {record} row")
        fields = lines[matches[0]].split("\t")
        fields[field_index] = replacement
        lines[matches[0]] = "\t".join(fields)
        return "\n".join(lines) + "\n"

    def changed_binding_audit(
        text: str,
        ordinal: int,
        field_index: int,
        replacement: str,
    ) -> str:
        lines = text.splitlines()
        matches = [
            index
            for index, line in enumerate(lines)
            if line.startswith(f"BINDING\t{ordinal}\t")
        ]
        if len(matches) != 1:
            raise AssertionError(f"mutation requires binding ordinal {ordinal}")
        fields = lines[matches[0]].split("\t")
        fields[field_index] = replacement
        lines[matches[0]] = "\t".join(fields)
        return "\n".join(lines) + "\n"

    cases: list[dict[str, Any]] = []

    def expect(name: str, expected: str, operation: Callable[[], Any]) -> None:
        actual = "NO_REJECTION"
        try:
            result = operation()
            if isinstance(result, Mapping):
                actual = f"{result.get('status')}:{result.get('reason')}"
        except LC5IntraBpPureError as error:
            actual = error.code
        cases.append(
            {
                "name": name,
                "expected_outcome": expected,
                "actual_outcome": actual,
                "passed": actual == expected,
            }
        )

    def resolve(changed_source: Mapping[str, Any], changed_audit_text: str, **kwargs: Any):
        return resolve_intra_bp_pure_call(
            provider,
            changed_source,
            changed_audit_text,
            call_site_node_id=call_site_node_id,
            **kwargs,
        )

    changed = deepcopy(dict(source))
    changed["compile_provenance"]["status"] = "dirty"
    expect(
        "stale_compile_state",
        "unresolved:stale_compile_state",
        lambda: resolve(changed, changed_audit(audit_text, "COMPILE", 1, "dirty")),
    )

    changed = deepcopy(dict(source))
    changed["targets"] = []
    missing_audit_lines = [
        line
        for line in audit_text.splitlines()
        if not line.startswith("TARGET\t") and not line.startswith("GRAPH\t")
    ]
    missing_audit = "\n".join(missing_audit_lines) + "\n"
    missing_audit = changed_audit(missing_audit, "CANDIDATES", 1, "0")
    expect(
        "missing_target",
        "unresolved:missing_target",
        lambda: resolve(changed, missing_audit),
    )

    changed = deepcopy(dict(source))
    changed["targets"].append(deepcopy(changed["targets"][0]))
    expect(
        "ambiguous_target",
        "unresolved:ambiguous_target",
        lambda: resolve(changed, changed_audit(audit_text, "CANDIDATES", 1, "2")),
    )

    changed = deepcopy(dict(source))
    changed["call_site"]["function_reference"]["is_self_context"] = False
    expect(
        "non_self_context",
        "ineligible:non_self_context",
        lambda: resolve(changed, changed_audit(audit_text, "REFERENCE", 4, "0")),
    )

    changed = deepcopy(dict(source))
    changed["targets"][0]["is_pure"] = False
    expect(
        "impure_call",
        "ineligible:impure_call",
        lambda: resolve(changed, changed_audit(audit_text, "TARGET", 5, "0")),
    )

    changed = deepcopy(dict(source))
    changed["targets"][0]["is_latent"] = True
    expect(
        "latent_call",
        "ineligible:latent_call",
        lambda: resolve(changed, changed_audit(audit_text, "TARGET", 6, "1")),
    )

    changed = deepcopy(dict(source))
    changed["targets"][0]["owner_blueprint_path"] = "/Game/Other.BP_Other"
    expect(
        "cross_blueprint_target",
        "ineligible:cross_blueprint_target",
        lambda: resolve(
            changed,
            changed_audit(audit_text, "GRAPH", 3, "/Game/Other.BP_Other"),
        ),
    )

    expect(
        "recursive_call_context",
        "truncated:recursive_call_context",
        lambda: resolve(source, audit_text, call_context=(call_site_node_id,)),
    )
    expect(
        "depth_budget_exhausted",
        "truncated:depth_budget_exhausted",
        lambda: resolve(source, audit_text, max_call_depth=0),
    )

    forged_guid = "00000000-0000-0000-0000-000000000000"
    changed = deepcopy(dict(source))
    changed["call_site"]["function_reference"]["guid"] = forged_guid
    expect(
        "function_reference_guid_mismatch",
        FUNCTION_REFERENCE_MISMATCH,
        lambda: resolve(changed, changed_audit(audit_text, "REFERENCE", 2, forged_guid)),
    )

    changed = deepcopy(dict(source))
    changed["targets"][0]["guid"] = forged_guid
    expect(
        "source_audit_target_mismatch",
        SOURCE_AUDIT_MISMATCH,
        lambda: resolve(changed, audit_text),
    )

    changed = deepcopy(dict(source))
    changed["bindings"][0]["formal_pin_id"] = changed["bindings"][1]["formal_pin_id"]
    expect(
        "formal_pin_identity_mismatch",
        BINDING_MISMATCH,
        lambda: resolve(
            changed,
            changed_binding_audit(
                audit_text, 0, 14, changed["bindings"][0]["formal_pin_id"]
            ),
        ),
    )

    changed = deepcopy(dict(source))
    changed["bindings"][0]["property"]["pin_type"]["container"] = "array"
    expect(
        "pin_container_mismatch",
        BINDING_MISMATCH,
        lambda: resolve(changed, changed_binding_audit(audit_text, 0, 10, "array")),
    )

    baseline = resolve(source, audit_text)
    duplicate = deepcopy(baseline)
    duplicate["occurrences"].append(deepcopy(duplicate["occurrences"][0]))
    expect(
        "duplicate_occurrence",
        DUPLICATE_OCCURRENCE,
        lambda: validate_resolution_product(duplicate),
    )

    cross_context = deepcopy(baseline)
    cross_context["internal_relations"][0]["target_occurrence_id"] = cross_context[
        "occurrences"
    ][0]["occurrence_id"]
    expect(
        "cross_context_internal_relation",
        BINDING_MISMATCH,
        lambda: validate_resolution_product(cross_context),
    )

    return {
        "format": "blueprint-lens-lc5-intra-bp-pure-mutations",
        "schema_version": "1.0.0",
        "status": "PASS" if all(case["passed"] for case in cases) else "FAIL",
        "case_count": len(cases),
        "cases": cases,
    }


def _file_sha256(path: Path) -> str:
    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        _fail(SOURCE_AUDIT_MISMATCH, f"cannot hash {path}: {error}")


def _load_json_object(path: Path, context: str) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        _fail(SOURCE_AUDIT_MISMATCH, f"cannot load {context}: {error}")
    if not isinstance(value, dict):
        _fail(SOURCE_AUDIT_MISMATCH, f"{context} must contain one object")
    return value


def build_intra_bp_pure_truth_gate(
    evidence_dir: str | Path,
    fixture_path: str | Path,
    asset_path: str | Path,
    raw_path: str | Path,
) -> dict[str, Any]:
    """Re-open native products and decide entry to the unpublished schema Gate."""

    evidence = Path(evidence_dir).resolve()
    fixture = Path(fixture_path).resolve()
    asset = Path(asset_path).resolve()
    raw = Path(raw_path).resolve()
    source_name = "BP_SlicingProbe.intra-bp-pure-source.json"
    audit_name = "BP_SlicingProbe.intra-bp-pure-audit.tsv"
    source_paths = [evidence / run / source_name for run in ("run1", "run2")]
    audit_paths = [evidence / run / audit_name for run in ("run1", "run2")]
    ground_truth_path = evidence / "reviewed-ground-truth.v1.json"
    for path in [*source_paths, *audit_paths, fixture, asset, raw, ground_truth_path]:
        if not path.is_file():
            _fail(SOURCE_AUDIT_MISMATCH, f"truth Gate input is missing: {path}")

    source_bytes = [path.read_bytes() for path in source_paths]
    audit_bytes = [path.read_bytes() for path in audit_paths]
    source_values = [
        _load_json_object(path, f"{path.parent.name} native source")
        for path in source_paths
    ]
    audit_values = [path.read_text(encoding="utf-8") for path in audit_paths]
    ground_truth = _load_json_object(ground_truth_path, "reviewed ground truth")
    if (
        ground_truth.get("format")
        != "blueprint-lens-lc5-intra-bp-pure-reviewed-ground-truth"
        or ground_truth.get("format_version") != "1.0.0"
        or ground_truth.get("review", {}).get("status") != "reviewed_for_schema_gate"
    ):
        _fail(SOURCE_AUDIT_MISMATCH, "reviewed ground-truth identity/status differs")

    asset_sha256 = _file_sha256(asset)
    raw_sha256 = _file_sha256(raw)
    truth = _object(
        ground_truth.get("source_truth"),
        SOURCE_AUDIT_MISMATCH,
        "reviewed source truth",
    )
    adjudication = _object(
        ground_truth.get("product_adjudication"),
        SOURCE_AUDIT_MISMATCH,
        "reviewed product adjudication",
    )
    if str(truth.get("asset_sha256", "")).lower() != asset_sha256:
        _fail(SOURCE_AUDIT_MISMATCH, "reviewed asset hash differs from current asset")
    if str(truth.get("raw_sha256", "")).lower() != raw_sha256:
        _fail(SOURCE_AUDIT_MISMATCH, "reviewed raw hash differs from frozen raw export")
    if source_bytes[0] != source_bytes[1] or audit_bytes[0] != audit_bytes[1]:
        _fail(SOURCE_AUDIT_MISMATCH, "run1/run2 native products are not byte-identical")
    source_sha256 = _file_sha256(source_paths[0])
    audit_sha256 = _file_sha256(audit_paths[0])
    if str(adjudication.get("source_sha256", "")).lower() != source_sha256:
        _fail(SOURCE_AUDIT_MISMATCH, "reviewed source product hash differs")
    if str(adjudication.get("audit_sha256", "")).lower() != audit_sha256:
        _fail(SOURCE_AUDIT_MISMATCH, "reviewed audit product hash differs")

    document = load_blueprint_lens_v1(fixture)
    core_call_matches = [
        node
        for node in document.nodes
        if node.id == source_values[0].get("call_site", {}).get("node_id")
    ]
    if (
        len(core_call_matches) != 1
        or core_call_matches[0].semantic_status != "opaque"
        or core_call_matches[0].semantic_reason != "function_body_not_expanded"
    ):
        _fail(
            SOURCE_AUDIT_MISMATCH,
            "frozen core-v1 no longer preserves the opaque function-call boundary",
        )
    rebuilt_products: list[dict[str, Any]] = []
    for source, audit in zip(source_values, audit_values, strict=True):
        if str(source.get("asset_sha256", "")).lower() != asset_sha256:
            _fail(SOURCE_AUDIT_MISMATCH, "native source asset hash differs")
        if str(source.get("raw_sha256", "")).lower() != raw_sha256:
            _fail(SOURCE_AUDIT_MISMATCH, "native source raw hash differs")
        provider = FrozenProjectDocumentProvider(
            document=document,
            asset_sha256=str(source["asset_sha256"]),
            raw_sha256=str(source["raw_sha256"]),
            compile_provenance=_object(
                source.get("compile_provenance"),
                SOURCE_AUDIT_MISMATCH,
                "native compile provenance",
            ),
        )
        call_site = _object(
            source.get("call_site"), SOURCE_AUDIT_MISMATCH, "native call site"
        )
        rebuilt = resolve_intra_bp_pure_call(
            provider,
            source,
            audit,
            call_site_node_id=str(call_site.get("node_id", "")),
        )
        validate_resolution_product(rebuilt)
        rebuilt_products.append(rebuilt)
    if canonical_resolution_bytes(rebuilt_products[0]) != canonical_resolution_bytes(
        rebuilt_products[1]
    ):
        _fail(SOURCE_AUDIT_MISMATCH, "run1/run2 semantic rebuild differs")

    source = source_values[0]
    provider = FrozenProjectDocumentProvider(
        document=document,
        asset_sha256=str(source["asset_sha256"]),
        raw_sha256=str(source["raw_sha256"]),
        compile_provenance=_object(
            source.get("compile_provenance"),
            SOURCE_AUDIT_MISMATCH,
            "native compile provenance",
        ),
    )
    mutation_report = run_intra_bp_pure_mutations(
        provider,
        source,
        audit_values[0],
        call_site_node_id=str(source["call_site"]["node_id"]),
    )
    if mutation_report.get("status") != "PASS" or mutation_report.get(
        "case_count"
    ) != 15:
        _fail(SOURCE_AUDIT_MISMATCH, "LC5 mutation matrix did not pass 15 cases")

    product = rebuilt_products[0]
    source_target = source["targets"][0]
    function_graph = next(
        graph for graph in document.graphs if graph.id == source_target["graph_id"]
    )
    actual_bindings = [
        {
            "ordinal": value["ordinal"],
            "kind": value["kind"],
            "name": value["property"]["name"],
            "cpp_type": value["property"]["cpp_type"],
            "pin_category": value["property"]["pin_type"]["category"],
            "container": value["property"]["pin_type"]["container"],
        }
        for value in source["bindings"]
    ]
    truth_checks = {
        "profile_id": truth.get("profile_id") == PROFILE_ID,
        "asset_path": truth.get("asset_path") == document.blueprint_path,
        "package_guid": truth.get("package_guid")
        == source["compile_provenance"]["package_guid"],
        "generated_class_path": truth.get("generated_class_path")
        == source["compile_provenance"]["generated_class_path"],
        "call_graph_id": truth.get("call_graph_id")
        == source["call_site"]["graph_id"],
        "call_site_node_id": truth.get("call_site_node_id")
        == source["call_site"]["node_id"],
        "function_guid": truth.get("function_guid") == source_target["guid"],
        "function_path": truth.get("function_path")
        == source_target["function_path"],
        "function_graph_id": truth.get("function_graph_id")
        == source_target["graph_id"],
        "function_graph_guid": truth.get("function_graph_guid")
        == source_target["graph_guid"],
        "entry_node_id": truth.get("entry_node_id")
        == source_target["entry_node_id"],
        "result_node_id": truth.get("result_node_id")
        == source_target["result_node_id"],
        "body_node_count": truth.get("body_node_count")
        == len(function_graph.nodes),
        "body_edge_count": truth.get("body_edge_count")
        == len(function_graph.edges),
        "bindings": truth.get("bindings") == actual_bindings,
    }
    if not all(truth_checks.values()):
        failed = sorted(name for name, passed in truth_checks.items() if not passed)
        _fail(
            SOURCE_AUDIT_MISMATCH,
            f"reviewed source truth differs from rebuilt products: {failed}",
        )
    if (
        adjudication.get("candidate_count") != len(source["targets"])
        or adjudication.get("binding_count") != len(source["bindings"])
        or adjudication.get("contextual_occurrence_count")
        != len(product["occurrences"])
        or adjudication.get("internal_relation_count")
        != len(product["internal_relations"])
        or adjudication.get("max_call_depth") != product["max_call_depth"]
    ):
        _fail(SOURCE_AUDIT_MISMATCH, "reviewed product counts differ")

    return {
        "format": "blueprint-lens-lc5-intra-bp-pure-truth-gate",
        "schema_version": "1.0.0",
        "status": "NATIVE_PRODUCTS_GROUND_TRUTH_MUTATIONS_VERIFIED__SCHEMA_GATE_NEXT",
        "profile_id": PROFILE_ID,
        "checks": {
            "asset_hash_matches_native_and_review": True,
            "raw_hash_matches_native_and_review": True,
            "source_runs_byte_identical": True,
            "audit_runs_byte_identical": True,
            "source_and_independent_audit_agree": True,
            "frozen_fixture_adapter_resolves_unique": True,
            "semantic_rebuild_runs_byte_identical": True,
            "reviewed_ground_truth_exact": True,
            "all_15_mutations_rejected": True,
            "core_v1_opaque_boundary_preserved": True,
        },
        "counts": {
            "native_run_count": 2,
            "candidate_count": len(source["targets"]),
            "binding_count": len(source["bindings"]),
            "contextual_occurrence_count": len(product["occurrences"]),
            "internal_relation_count": len(product["internal_relations"]),
            "mutation_case_count": mutation_report["case_count"],
        },
        "hashes": {
            "asset_sha256": asset_sha256,
            "raw_sha256": raw_sha256,
            "source_sha256": source_sha256,
            "audit_sha256": audit_sha256,
            "reviewed_ground_truth_sha256": _file_sha256(ground_truth_path),
            "contextual_resolution_sha256": hashlib.sha256(
                canonical_resolution_bytes(product)
            ).hexdigest(),
        },
        "mutation_report": mutation_report,
        "next_gate": "publish auxiliary call-resolution/contextual-slice schema only after schema validation passes",
        "not_authorized": [
            "TRUTH_FROZEN or readiness publication",
            "visual candidates, effect images or Slate portal",
            "generalization beyond LC5_INTRA_BP_PURE_CALL_V1",
        ],
    }


def build_intra_bp_pure_schema_artifacts(
    evidence_dir: str | Path,
    fixture_path: str | Path,
    asset_path: str | Path,
    raw_path: str | Path,
    source_schema_path: str | Path,
    contextual_schema_path: str | Path,
) -> dict[str, bytes]:
    """Build deterministic LC5 schema-Gate artifacts without publishing readiness."""

    from ..schema_validation import validate_instance

    evidence = Path(evidence_dir).resolve()
    fixture = Path(fixture_path).resolve()
    asset = Path(asset_path).resolve()
    raw = Path(raw_path).resolve()
    source_schema_path = Path(source_schema_path).resolve()
    contextual_schema_path = Path(contextual_schema_path).resolve()
    source_path = evidence / "run1/BP_SlicingProbe.intra-bp-pure-source.json"
    audit_path = evidence / "run1/BP_SlicingProbe.intra-bp-pure-audit.tsv"
    ground_truth_path = evidence / "reviewed-ground-truth.v1.json"
    for path in (
        source_schema_path,
        contextual_schema_path,
        source_path,
        audit_path,
        ground_truth_path,
    ):
        if not path.is_file():
            _fail(SOURCE_AUDIT_MISMATCH, f"schema Gate input is missing: {path}")

    truth_gate = build_intra_bp_pure_truth_gate(evidence, fixture, asset, raw)
    if (
        truth_gate.get("status")
        != "NATIVE_PRODUCTS_GROUND_TRUTH_MUTATIONS_VERIFIED__SCHEMA_GATE_NEXT"
        or not all(truth_gate.get("checks", {}).values())
    ):
        _fail(SOURCE_AUDIT_MISMATCH, "truth Gate does not authorize schema publication")
    source = _load_json_object(source_path, "run1 native source")
    audit = audit_path.read_text(encoding="utf-8")
    source_schema = _load_json_object(source_schema_path, "call-resolution schema")
    contextual_schema = _load_json_object(
        contextual_schema_path, "contextual-slice schema"
    )
    provider = FrozenProjectDocumentProvider(
        document=load_blueprint_lens_v1(fixture),
        asset_sha256=str(source["asset_sha256"]),
        raw_sha256=str(source["raw_sha256"]),
        compile_provenance=_object(
            source.get("compile_provenance"),
            SOURCE_AUDIT_MISMATCH,
            "native compile provenance",
        ),
    )
    contextual = resolve_intra_bp_pure_call(
        provider,
        source,
        audit,
        call_site_node_id=str(source["call_site"]["node_id"]),
    )
    validate_resolution_product(contextual)
    try:
        validate_instance(source, source_schema)
        validate_instance(contextual, contextual_schema)
        validate_instance(
            resolve_intra_bp_pure_call(
                provider,
                source,
                audit,
                call_site_node_id=str(source["call_site"]["node_id"]),
                max_call_depth=0,
            ),
            contextual_schema,
        )
    except ValueError as error:
        _fail(SOURCE_AUDIT_MISMATCH, f"auxiliary schema validation failed: {error}")

    mutation_report = truth_gate["mutation_report"]

    def pretty_bytes(value: Mapping[str, Any]) -> bytes:
        return (
            json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        ).encode("utf-8")

    contextual_bytes = pretty_bytes(contextual)
    mutation_bytes = pretty_bytes(mutation_report)
    gate = {
        "format": "blueprint-lens-lc5-intra-bp-pure-schema-gate",
        "schema_version": "1.0.0",
        "status": "SCHEMA_VALIDATOR_MUTATIONS_VERIFIED__COMMIT_BOUND_READINESS_NEXT",
        "profile_id": PROFILE_ID,
        "checks": {
            "truth_gate_passed": True,
            "native_source_schema_valid": True,
            "resolved_contextual_schema_valid": True,
            "truncated_contextual_schema_valid": True,
            "contextual_semantic_validator_passed": True,
            "two_native_runs_byte_identical": True,
            "two_semantic_rebuilds_byte_identical": True,
            "reviewed_ground_truth_exact": True,
            "all_15_mutations_rejected": True,
            "core_v1_opaque_boundary_preserved": True,
        },
        "counts": dict(truth_gate["counts"]),
        "hashes": {
            **dict(truth_gate["hashes"]),
            "call_resolution_schema_sha256": _file_sha256(source_schema_path),
            "contextual_slice_schema_sha256": _file_sha256(
                contextual_schema_path
            ),
            "contextual_slice_sha256": hashlib.sha256(
                contextual_bytes
            ).hexdigest(),
            "mutation_report_sha256": hashlib.sha256(mutation_bytes).hexdigest(),
        },
        "artifacts": {
            "native_source": "run1/BP_SlicingProbe.intra-bp-pure-source.json",
            "independent_audit": "run1/BP_SlicingProbe.intra-bp-pure-audit.tsv",
            "reviewed_ground_truth": "reviewed-ground-truth.v1.json",
            "contextual_slice": "BP_SlicingProbe.contextual-slice.v1.json",
            "mutation_report": "mutation-report.json",
            "call_resolution_schema": "schemas/extensions/blueprint-lens-call-resolution-v1.schema.json",
            "contextual_slice_schema": "schemas/extensions/blueprint-lens-contextual-slice-v1.schema.json",
        },
        "next_gate": "commit-bound independent readiness audit and TRUTH_FROZEN decision",
        "not_authorized": [
            "TRUTH_FROZEN before a commit-bound independent readiness audit",
            "visual conditions, effect images or Slate portal",
            "generalization beyond LC5_INTRA_BP_PURE_CALL_V1",
        ],
    }
    return {
        "BP_SlicingProbe.contextual-slice.v1.json": contextual_bytes,
        "mutation-report.json": mutation_bytes,
        "schema-gate.json": pretty_bytes(gate),
    }
