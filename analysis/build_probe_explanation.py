"""Build the canonical Blueprint Lens explanation fixture."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from blueprint_lens import (
    build_probe_explanation,
    canonical_explanation_bytes,
    validate_explanation_model,
)


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_IR = ROOT / "fixtures/v1/BP_SlicingProbe.v1.json"
DEFAULT_SLICE = (
    ROOT / "artifacts/v1/BP_SlicingProbe.execution.slice.v1.json"
)
DEFAULT_ASSET = (
    ROOT / "unreal/BlueprintLensProbe/Content/Probe/BP_SlicingProbe.uasset"
)
DEFAULT_SCHEMA = ROOT / "schemas/blueprint-lens-explanation-v1.schema.json"
DEFAULT_OUTPUT = (
    ROOT
    / "unreal/BlueprintLensProbe/Plugins/BlueprintLensExporter"
    / "Resources/Explanation"
    / "BP_SlicingProbe.set-health.explanation.v1.json"
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the canonical Blueprint Lens explanation fixture."
    )
    parser.add_argument("--ir", type=Path, default=DEFAULT_IR)
    parser.add_argument("--slice", type=Path, default=DEFAULT_SLICE)
    parser.add_argument("--asset", type=Path, default=DEFAULT_ASSET)
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    model = build_probe_explanation(args.ir, args.slice, args.asset)
    validate_explanation_model(
        model, args.ir, args.slice, args.asset, args.schema
    )
    payload = canonical_explanation_bytes(model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    digest = hashlib.sha256(payload).hexdigest().upper()
    counts = model["counts"]
    print(
        f'EXPLANATION_MODEL_SUCCESS output="{args.output.resolve()}" '
        f'units={counts["units"]} relations={counts["relations"]} '
        f'source_nodes={counts["source_nodes"]} '
        f'source_edges={counts["source_edges"]} sha256={digest}'
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
