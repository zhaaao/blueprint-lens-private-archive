"""Derive the narrow M3 export ingress from the authoritative M7 registry."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping

from .m7_corpus import load_m7_corpus_manifest
from .pipeline import PipelineItem, PipelineResult, build_batch
from .project_documents import ProductionManifestProvider


def _typed_name(object_path: str) -> str:
    digest = hashlib.sha256(object_path.encode("utf-8")).hexdigest()
    return f"{digest}.blueprint-lens-v1.json"


def derive_m3_export_manifest(manifest: Mapping[str, Any]) -> dict[str, Any]:
    """Project an M7 manifest onto the frozen M3 batch-ingress contract."""

    if not isinstance(manifest, Mapping):
        raise ValueError("M7_EXPORT_MANIFEST_INVALID: manifest must be an object")
    regression_assets = manifest.get("regression_assets")
    candidate_graphs = manifest.get("candidate_graphs")
    if not isinstance(regression_assets, list) or not isinstance(candidate_graphs, list):
        raise ValueError(
            "M7_EXPORT_MANIFEST_INVALID: regression_assets and candidate_graphs must be arrays"
        )

    derived_regressions: list[dict[str, str]] = []
    for index, row in enumerate(regression_assets):
        if not isinstance(row, Mapping):
            raise ValueError(
                f"M7_EXPORT_MANIFEST_INVALID: regression_assets[{index}] must be an object"
            )
        row_id = row.get("id")
        object_path = row.get("object_path")
        if not isinstance(row_id, str) or not row_id or not isinstance(object_path, str) or not object_path:
            raise ValueError(
                f"M7_EXPORT_MANIFEST_INVALID: regression_assets[{index}] requires id/object_path"
            )
        derived_regressions.append({"id": row_id, "object_path": object_path})

    derived_candidates: list[dict[str, Any]] = []
    for index, row in enumerate(candidate_graphs):
        if not isinstance(row, Mapping):
            raise ValueError(
                f"M7_EXPORT_MANIFEST_INVALID: candidate_graphs[{index}] must be an object"
            )
        row_id = row.get("id")
        object_path = row.get("object_path")
        graph_id = row.get("graph_id")
        band = row.get("band")
        risk_dimensions = row.get("risk_dimensions")
        if (
            not isinstance(row_id, str)
            or not row_id
            or not isinstance(object_path, str)
            or not object_path
            or not isinstance(graph_id, str)
            or not graph_id
            or not isinstance(band, str)
            or not band
            or not isinstance(risk_dimensions, list)
            or not risk_dimensions
        ):
            raise ValueError(
                f"M7_EXPORT_MANIFEST_INVALID: candidate_graphs[{index}] has invalid ingress fields"
            )
        dimensions: list[str] = []
        for dimension_index, declaration in enumerate(risk_dimensions):
            if not isinstance(declaration, Mapping):
                raise ValueError(
                    "M7_EXPORT_MANIFEST_INVALID: "
                    f"candidate_graphs[{index}].risk_dimensions[{dimension_index}] must be an object"
                )
            dimension = declaration.get("dimension")
            if not isinstance(dimension, str) or not dimension:
                raise ValueError(
                    "M7_EXPORT_MANIFEST_INVALID: "
                    f"candidate_graphs[{index}].risk_dimensions[{dimension_index}] requires dimension"
                )
            dimensions.append(dimension)
        if len(dimensions) != len(set(dimensions)):
            raise ValueError(
                f"M7_EXPORT_MANIFEST_INVALID: candidate_graphs[{index}] repeats a risk dimension"
            )
        derived_candidates.append(
            {
                "band": band,
                "graph_id": graph_id,
                "id": row_id,
                "object_path": object_path,
                "risk_dimensions": sorted(dimensions),
            }
        )

    return {
        "candidate_graphs": derived_candidates,
        "regression_assets": derived_regressions,
        "schema_name": "blueprint-lens-m3-corpus",
        "schema_version": "1.0.0",
    }


def write_m3_export_manifest(source: str | Path, destination: str | Path) -> Path:
    """Generate canonical M3 ingress bytes from the validated M7 registry."""

    source_path = Path(source)
    destination_path = Path(destination)
    manifest = load_m7_corpus_manifest(source_path)
    derived = derive_m3_export_manifest(manifest)
    payload = (json.dumps(derived, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode(
        "utf-8"
    )
    destination_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination_path.with_name(destination_path.name + ".tmp")
    try:
        temporary.write_bytes(payload)
        temporary.replace(destination_path)
    finally:
        temporary.unlink(missing_ok=True)
    return destination_path


def materialize_typed_ir(output_root: str | Path) -> tuple[PipelineResult, ...]:
    """Build canonical typed IR beside one M3 batch capture.

    The batch result is the exporter-owned source of asset membership and raw
    paths.  The existing production pipeline then performs the unchanged raw
    0.2 to typed-IR 1.0.0 transformation.
    """

    root = Path(output_root)
    result_path = root / "batch-result.v1.json"
    provider = ProductionManifestProvider(result_path)
    typed_root = root / "typed-ir"
    typed_root.mkdir(parents=True, exist_ok=True)
    items = tuple(
        PipelineItem(
            asset_id=asset_id,
            blueprint_object_path=asset_id,
            raw_path=provider.load(asset_id).raw_path,
            typed_ir_path=typed_root / _typed_name(asset_id),
        )
        for asset_id in provider.list_asset_ids()
    )
    return build_batch(items)


def _main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    path = write_m3_export_manifest(args.manifest, args.output)
    print(f"M7_EXPORT_INPUT_GENERATED path={path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())


__all__ = [
    "derive_m3_export_manifest",
    "materialize_typed_ir",
    "write_m3_export_manifest",
]
