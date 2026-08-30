"""Build the real LC1 typed-IR, slice, explanation, and parity artifacts."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .contract_validation import validate_contract_file
from .execution_slice import compute_execution_slice
from .explanation_model import (
    build_linear_execution_explanation,
    canonical_explanation_bytes,
    validate_explanation_model,
)
from .raw_probe import load_blueprint_lens_v1
from .typed_ir import TypedIRBuildError, build_typed_ir


LC1_BLUEPRINT_PATH = (
    "/Game/LensCorpus/BP_LC1_LongChain.BP_LC1_LongChain"
)
LC1_CRITERION_SYMBOL = "LC1Ready"
LC1_EXPECTED_UNITS = 14
LC1_EXPECTED_RELATIONS = 13

class LC1ArtifactError(ValueError):
    """Raised when the real LC1 source does not satisfy the frozen profile."""


def _canonical_json_bytes(value: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest().upper()


def _sha256_file(path: Path) -> str:
    return _sha256_bytes(path.read_bytes())


def _write_bytes(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)


def _write_json(path: Path, value: Mapping[str, Any]) -> None:
    _write_bytes(path, _canonical_json_bytes(value))


def _build_typed_ir(raw: Mapping[str, Any]) -> dict[str, Any]:
    if raw.get("format") != "blueprint-lens-raw-probe":
        raise LC1ArtifactError("LC1 source must be a raw Blueprint Lens export")
    if raw.get("format_version") != "0.2":
        raise LC1ArtifactError("LC1 source must use raw format version 0.2")
    if raw.get("blueprint", {}).get("path") != LC1_BLUEPRINT_PATH:
        raise LC1ArtifactError("LC1 source Blueprint path does not match contract")

    try:
        return build_typed_ir(
            raw,
            expected_blueprint_path=LC1_BLUEPRINT_PATH,
            canonicalize_entity_order=False,
        )
    except TypedIRBuildError as error:
        raise LC1ArtifactError(str(error)) from error


def _criterion_node_id(ir: Mapping[str, Any]) -> tuple[str, str]:
    matches: list[tuple[str, str]] = []
    for graph in ir["blueprint"]["graphs"]:
        for node in graph["nodes"]:
            symbol = node.get("symbol") or {}
            if (
                symbol.get("kind") == "variable"
                and symbol.get("access") == "set"
                and symbol.get("name") == LC1_CRITERION_SYMBOL
            ):
                matches.append((graph["id"], node["id"]))
    if len(matches) != 1:
        raise LC1ArtifactError(
            "LC1 criterion symbol must resolve exactly once; "
            f"found {len(matches)}"
        )
    return matches[0]


def _build_slice(
    ir_path: Path,
    ir: Mapping[str, Any],
) -> dict[str, Any]:
    document = load_blueprint_lens_v1(ir_path)
    graph_id, criterion_node_id = _criterion_node_id(ir)
    result = compute_execution_slice(document, criterion_node_id)
    if result.graph_id != graph_id:
        raise LC1ArtifactError("LC1 criterion graph changed during slicing")
    if (
        len(result.node_ids) != LC1_EXPECTED_UNITS
        or len(result.edge_ids) != LC1_EXPECTED_RELATIONS
    ):
        raise LC1ArtifactError(
            "real LC1 slice inventory mismatch: "
            f"{len(result.node_ids)} units/{len(result.edge_ids)} relations"
        )

    selected = set(result.node_ids)
    boundaries = [
        {
            "node_id": node.id,
            "status": node.semantic_status,
            "reason": node.semantic_reason,
        }
        for node in sorted(document.nodes, key=lambda item: item.id)
        if node.id in selected and node.semantic_status != "supported"
    ]
    if boundaries:
        raise LC1ArtifactError(
            "real LC1 linear profile requires all selected nodes supported"
        )

    return {
        "format": "blueprint-lens-slice",
        "schema_version": "1.0.0",
        "rules_version": "1.0.0",
        "source_fixture": ir_path.name,
        "source_sha256": _sha256_file(ir_path),
        "slice_kind": "execution_context",
        "criterion": {
            "graph_id": graph_id,
            "node_id": criterion_node_id,
            "description": "Set LC1Ready at the end of the frozen long chain",
        },
        "graph_id": result.graph_id,
        "node_ids": list(result.node_ids),
        "edge_ids": list(result.edge_ids),
        "inclusion_reasons": {
            node_id: list(reasons)
            for node_id, reasons in result.inclusion_reasons.items()
        },
        "boundaries": boundaries,
        "counts": {
            "nodes": len(result.node_ids),
            "edges": len(result.edge_ids),
        },
    }


def _build_manifest(
    explanation_path: Path,
    model: Mapping[str, Any],
) -> dict[str, Any]:
    unit_ids = [str(unit["id"]) for unit in model["units"]]
    relation_ids = [
        str(relation["id"]) for relation in model["relations"]
    ]
    common = {
        "displayed_unit_ids": unit_ids,
        "displayed_relation_ids": relation_ids,
        "source_navigation": True,
        "status_visible": True,
        "boundary_visible": True,
        "adds_source_facts": False,
    }
    return {
        "schema_version": "1.0.0",
        "selection_status": "unselected",
        "fixture": {
            "path": explanation_path.as_posix(),
            "source_nodes": model["counts"]["source_nodes"],
            "source_edges": model["counts"]["source_edges"],
            "unit_ids": unit_ids,
            "relation_ids": relation_ids,
        },
        "default_condition_id": None,
        "conditions": [
            {
                "id": "LC1_INTERVAL_LENS",
                "label": "Semantic Interval Lens",
                "primary_topology": (
                    "continuous_execution_rail_with_measured_interval_lens"
                ),
                **common,
            },
            {
                "id": "LC1_BARCODE_DETAIL",
                "label": "Overview Barcode + Adjacent Detail",
                "primary_topology": (
                    "exact_unit_overview_barcode_with_adjacent_detail_rail"
                ),
                **common,
            },
        ],
    }


def _build_layout_golden(model: Mapping[str, Any]) -> dict[str, Any]:
    units = [str(unit["id"]) for unit in model["units"]]
    relations = [
        str(relation["id"]) for relation in model["relations"]
    ]
    run_units = units[1:-1]
    run_relations = relations[1:-1]
    return {
        "format": "blueprint-lens-frame-flow-golden",
        "schema_version": "0.1.0",
        "graph_expression": "Entry -> StraightRun(12) -> CriterionFocus",
        "truth_counts": {
            "units": len(units),
            "relations": len(relations),
            "unique_source_nodes": len(units),
        },
        "segments": [
            {
                "kind": "Entry",
                "member_unit_ids": units[:1],
                "member_relation_ids": [],
                "incoming_relation_ids": [],
                "outgoing_relation_ids": relations[:1],
            },
            {
                "kind": "StraightRun",
                "member_unit_ids": run_units,
                "member_relation_ids": run_relations,
                "incoming_relation_ids": relations[:1],
                "outgoing_relation_ids": relations[-1:],
            },
            {
                "kind": "CriterionFocus",
                "member_unit_ids": units[-1:],
                "member_relation_ids": [],
                "incoming_relation_ids": relations[-1:],
                "outgoing_relation_ids": [],
            },
        ],
        "initial_detail_window": {
            "capacity": 3,
            "anchor_unit_id": run_units[-1],
            "hidden_prefix_unit_ids": run_units[:-3],
            "visible_unit_ids": run_units[-3:],
            "hidden_suffix_unit_ids": [],
            "hidden_prefix_relation_ids": run_relations[:9],
            "visible_relation_ids": run_relations[9:],
            "hidden_suffix_relation_ids": [],
        },
    }


def build_lc1_artifacts(
    raw_path: Path,
    asset_path: Path,
    output_dir: Path,
    graph_schema_path: Path,
    slice_schema_path: Path,
    explanation_schema_path: Path,
    *,
    renderer_copy_path: Path | None = None,
) -> dict[str, Path]:
    """Build and validate the real LC1 artifact chain."""

    raw_path = Path(raw_path).resolve()
    asset_path = Path(asset_path).resolve()
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    raw = json.loads(raw_path.read_text(encoding="utf-8"))
    ir = _build_typed_ir(raw)
    ir_path = output_dir / "BP_LC1_LongChain.ir.v1.json"
    _write_json(ir_path, ir)
    validate_contract_file(ir_path, graph_schema_path)

    slice_value = _build_slice(ir_path, ir)
    slice_path = output_dir / "BP_LC1_LongChain.execution.slice.v1.json"
    _write_json(slice_path, slice_value)
    validate_contract_file(slice_path, slice_schema_path)

    model = build_linear_execution_explanation(
        ir_path,
        slice_path,
        asset_path,
        question="Why does Set LC1Ready execute?",
    )
    validate_explanation_model(
        model,
        ir_path,
        slice_path,
        asset_path,
        explanation_schema_path,
    )
    if (
        model["counts"]["units"] != LC1_EXPECTED_UNITS
        or model["counts"]["relations"] != LC1_EXPECTED_RELATIONS
        or model["counts"]["source_nodes"] != LC1_EXPECTED_UNITS
        or model["counts"]["source_edges"] != LC1_EXPECTED_RELATIONS
    ):
        raise LC1ArtifactError("LC1 Explanation parity is not 14/13")

    explanation_payload = canonical_explanation_bytes(model)
    explanation_path = (
        output_dir / "BP_LC1_LongChain.explanation.v1.json"
    )
    _write_bytes(explanation_path, explanation_payload)
    if renderer_copy_path is not None:
        renderer_copy_path = Path(renderer_copy_path).resolve()
        _write_bytes(renderer_copy_path, explanation_payload)
        if renderer_copy_path.read_bytes() != explanation_payload:
            raise LC1ArtifactError("renderer fixture copy differs from evidence")

    manifest = _build_manifest(explanation_path, model)
    manifest_path = output_dir / "manifest.v1.json"
    _write_json(manifest_path, manifest)

    layout_golden = _build_layout_golden(model)
    layout_golden_path = output_dir / "layout-golden.v0.json"
    _write_json(layout_golden_path, layout_golden)

    parity = {
        "format": "blueprint-lens-lc1-parity",
        "schema_version": "1.0.0",
        "verdict": "PASS",
        "expected": {
            "units": LC1_EXPECTED_UNITS,
            "relations": LC1_EXPECTED_RELATIONS,
            "source_nodes": LC1_EXPECTED_UNITS,
            "source_edges": LC1_EXPECTED_RELATIONS,
        },
        "actual": dict(model["counts"]),
        "hashes": {
            "raw_export_sha256": _sha256_file(raw_path),
            "ir_sha256": _sha256_file(ir_path),
            "slice_sha256": _sha256_file(slice_path),
            "explanation_sha256": _sha256_file(explanation_path),
            "blueprint_package_sha256": _sha256_file(asset_path),
            "layout_golden_sha256": _sha256_file(layout_golden_path),
        },
        "condition_ids": [
            condition["id"] for condition in manifest["conditions"]
        ],
        "information_matching": {
            "unit_coverage_equal": True,
            "relation_coverage_equal": True,
            "source_navigation_equal": True,
            "status_visibility_equal": True,
            "boundary_visibility_equal": True,
            "adds_source_facts": False,
            "selection_status": "unselected",
        },
    }
    parity_path = output_dir / "parity.v1.json"
    _write_json(parity_path, parity)

    return {
        "ir": ir_path,
        "slice": slice_path,
        "explanation": explanation_path,
        "manifest": manifest_path,
        "layout_golden": layout_golden_path,
        "parity": parity_path,
    }
