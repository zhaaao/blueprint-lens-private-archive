"""Reproduce the frozen M7 slice comparison from the repository root.

The command uses the accepted slicing and truth-comparison implementation and
M4/M5-backed M7 correctness harness.  It only adapts that report to a compact
CLI result with one row per query.  No fixture or evidence file is written.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any, Mapping


_CORPUS_PATH = "fixtures/m7/m7-corpus-manifest.v1.json"
_ORIGINAL_QUERY_PATH = "fixtures/m7/m7-queries.v1.json"
_SUPPLEMENT_QUERY_PATH = "fixtures/m7/m7-queries-supplement.v1.json"
_REGISTRY_PATH = "fixtures/m7/m7-truth-registry.v1.json"
_CONTROLLER_TRUTH_PATH = "artifacts/m7/truth/controller"
_INDEPENDENT_TRUTH_PATH = "artifacts/m7/truth/independent"
_ADJUDICATION_PATH = "artifacts/m7/truth/adjudication.v1.json"
_G7_EVIDENCE_PATH = "artifacts/m7/reproduction/examiner-reproduction-index.v1.json"

# These are the frozen pins carried by the accepted M7 query and corpus
# products.  Checking the values independently of the files prevents a
# scratch copy from changing a file and then merely updating its self-pin.
_CANONICAL_CORPUS_SHA256 = (
    "45f8e9b894b8f36a95ff20a0633e39c665d9b273a3025c8b3ff836e7a71d2fef"
)
_CANONICAL_ORIGINAL_QUERY_SHA256 = (
    "8279ad3638c564f98eda8072d6f4bb769b576fb3589177b5a74dc7b7d1d75cf4"
)
_CANONICAL_SUPPLEMENT_QUERY_SHA256 = (
    "e8957681982e706990318a82ecfe9f1bce08062df0779df5cb84a0573e5ccb0a"
)
_CANONICAL_G7_EVIDENCE_SHA256 = (
    "7A6BDB73161A2B0297D052A0577D72775B5D243BCAD8475FC487C37D9D232518"
)
_CANONICAL_TYPED_IR_INVENTORY = (
    {
        "path": "artifacts/m7/export/run1/typed-ir/0e00a99a51d384eda19095e556f214deebf797d1d8008371f071e872b6cb187b.blueprint-lens-v1.json",
        "sha256": "8259e7bb889934e9ad97e64ff3d53d8bda840d5131a2f689da8c63b713caf82e",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/13f86e0003e83ed8669789051c1108eee78f51aa32b0095100b68bca2382799f.blueprint-lens-v1.json",
        "sha256": "1a0853abcb4b03ac5051de9158094ce2e38e654b2bd27dbb08dd0d646c2f9d45",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/5bebefa4aec82c95a10e88b13e18d2c8939618ff49e06fdb9e92d3d008901263.blueprint-lens-v1.json",
        "sha256": "2137fae1d155823e529402b102a613109d910807a034ab6826bd3edd6909ae12",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/60fb3c769ebf10e0a24a0217d14cfd19c168b7e294ca7d9f9c26cb10b59e6386.blueprint-lens-v1.json",
        "sha256": "0841255d9d523bda0ecae105a2ec2cba39f28fb2f4afe99dbed1cd8e2c4b5ccd",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/638fdded9113279cefc9e97f78a6c6fab40909a4f05bc6e5ab813392e70ae5fd.blueprint-lens-v1.json",
        "sha256": "57597332d2c08e1a968ffe8cdb231d372bcda74514d2c1a05c00e6bedde047e0",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/6a735b729235ab8b19c55422026f47b04694dd214a06d07f9b2194862372a2ea.blueprint-lens-v1.json",
        "sha256": "64e99bf58190afed770fa1e1474672fb0492beadb06ffa0fce7504c52981f35a",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/750a8bc86d98b397b419580312baa8a187e58de7e9f12eb5e461cd39be768e8e.blueprint-lens-v1.json",
        "sha256": "582a07de8191be3ec7aa8d0a0d458bb9e739e01b42e93d372f20e7b3229ef61c",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/8ca0b6a00f2943b8672a1308934442b81e1e88fb2cc366c0805668e872ba7661.blueprint-lens-v1.json",
        "sha256": "ae3081275ada00b7370f17695b111c13f86e29a0707ba3b722e48ac48ded9e8f",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/a6fea0235eb7390b0bd6f3d8ca4813dea7f0256e044a6d30eda4e112c10f8083.blueprint-lens-v1.json",
        "sha256": "17b56de55bd564965eeabf82345e4e2b501db9688007eac0e0151e633d8e645a",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/af88808755640042329d2c05f0cd80b49ad186548244e8b33baad2c1ce1e5267.blueprint-lens-v1.json",
        "sha256": "8abeacfbb7061f44bbf2e60293ae1e62195a3fd62e140a6abcd14463878775eb",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/f030e7a1fbbf23ff1acfb2ee1cdd4d970a4cdd6a8bbac34951abc97c8357217c.blueprint-lens-v1.json",
        "sha256": "498effe9831ade7ced981b6b78bdc1b35a430396ef1cf747b1780fdeb9ee205d",
    },
    {
        "path": "artifacts/m7/export/run1/typed-ir/f8c288dcc257ab43e3ad4e76092a15e9eec58fa20b2a2b912b670ab381922054.blueprint-lens-v1.json",
        "sha256": "06c2e95c0d2f65c83c4e2bbc65ca7076d918c7ccaac5b93a597fda9de89d3d6c",
    },
)


def _parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Reproduce the frozen M7 execution/data slices and compare truth."
    )
    parser.add_argument(
        "--root",
        type=Path,
        required=True,
        help="repository root containing the M7 fixtures and retained products",
    )
    parser.add_argument(
        "--truth-dir",
        type=Path,
        help=(
            "controller truth directory; defaults to "
            "ROOT/artifacts/m7/truth/controller"
        ),
    )
    return parser.parse_args(argv)


def _portable_path(path: Path, root: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def _load_json(path: Path) -> Mapping[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, Mapping):
        raise ValueError(f"JSON artefact must be an object: {path}")
    return value


def _sha256(path: Path) -> str | None:
    try:
        # Project authorities use the accepted portable digest helper so a
        # checkout's LF/CRLF rendering does not change the provenance result.
        from blueprint_lens.digests import file_sha256

        return file_sha256(path)
    except OSError:
        return None


def _raw_sha256(path: Path) -> str | None:
    """Hash the frozen external packet bytes without checkout normalisation."""

    import hashlib

    try:
        return hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError:
        return None


def _root_relative_path(root: Path, relative: str) -> Path:
    """Resolve a repository-relative authority without allowing traversal."""

    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"authority path is not portable and relative: {relative!r}")
    return root / path


def _canonical_pin_errors(
    original: Mapping[str, Any], supplement: Mapping[str, Any], root: Path
) -> list[str]:
    """Check the immutable path/digest pins before running any slicer."""

    errors: list[str] = []
    corpus_pin = original.get("corpus_manifest")
    if (
        not isinstance(corpus_pin, Mapping)
        or corpus_pin.get("path") != _CORPUS_PATH
        or corpus_pin.get("sha256") != _CANONICAL_CORPUS_SHA256
    ):
        errors.append(
            "M7_TRUTH_CORPUS_PIN_DRIFT: original query corpus pin does not match "
            "the frozen corpus"
        )
    corpus_path = root / _CORPUS_PATH
    actual_corpus_digest = _sha256(corpus_path)
    if actual_corpus_digest != _CANONICAL_CORPUS_SHA256:
        errors.append(
            "M7_TRUTH_CORPUS_PIN_DRIFT: corpus manifest bytes differ from the "
            f"frozen digest (actual={actual_corpus_digest!r})"
        )

    if original.get("query_set_sha256") != _CANONICAL_ORIGINAL_QUERY_SHA256:
        errors.append(
            "M7_TRUTH_QUERY_SET_DRIFT: original query file does not carry the "
            "frozen query-set digest"
        )

    supplement_pin = supplement.get("corpus_manifest")
    supplement_basis = supplement.get("supplements")
    if (
        not isinstance(supplement_pin, Mapping)
        or supplement_pin.get("path") != _CORPUS_PATH
        or supplement_pin.get("sha256") != _CANONICAL_CORPUS_SHA256
        or not isinstance(supplement_basis, Mapping)
        or supplement_basis.get("path") != _ORIGINAL_QUERY_PATH
        or supplement_basis.get("query_set_sha256") != _CANONICAL_ORIGINAL_QUERY_SHA256
        or supplement.get("query_set_sha256") != _CANONICAL_SUPPLEMENT_QUERY_SHA256
    ):
        errors.append(
            "M7_SUPPLEMENT_DRIFT: supplement freeze pins do not match the "
            "canonical corpus/query products"
        )
    return errors


def _typed_ir_inventory() -> list[dict[str, str]]:
    """Return the independent, canonical run1 typed-IR inventory."""

    return [dict(row) for row in _CANONICAL_TYPED_IR_INVENTORY]


def _g7_evidence_hash_errors(g7_evidence_path: Path) -> list[str]:
    """Check the packet bytes before using any packet-contained pins."""

    actual_packet_digest = _raw_sha256(g7_evidence_path)
    if actual_packet_digest == _CANONICAL_G7_EVIDENCE_SHA256.lower():
        return []
    return [
        "M7_G7_EVIDENCE_HASH_DRIFT: frozen G7 evidence bytes differ from "
        f"the canonical digest (actual={actual_packet_digest!r})"
    ]


def _canonical_g7_evidence_path() -> Path:
    """Locate the checkout's packet for diagnostic inventory fallback only."""

    return Path(__file__).resolve().parents[1] / _G7_EVIDENCE_PATH


def _authority_inventory(
    root: Path,
    g7_evidence_path: Path,
    typed_ir_inventory: list[dict[str, str]],
) -> tuple[list[dict[str, str]], list[str]]:
    """Select the 98 reproduction inputs and bind them to packet SHA values."""

    try:
        packet = _load_json(g7_evidence_path)
        registry = _load_json(root / _REGISTRY_PATH)
        entries = registry.get("entries")
        if not isinstance(entries, list):
            raise ValueError("truth registry has no entries array")
        registry_paths = {
            entry["path"]
            for entry in entries
            if isinstance(entry, Mapping) and isinstance(entry.get("path"), str)
        }
        expected_paths = {
            _ADJUDICATION_PATH,
            _CORPUS_PATH,
            _ORIGINAL_QUERY_PATH,
            _SUPPLEMENT_QUERY_PATH,
            _REGISTRY_PATH,
            *registry_paths,
            *(row["path"] for row in typed_ir_inventory),
        }
        packet_rows = packet.get("dataset", {}).get("files")
        if not isinstance(packet_rows, list):
            raise ValueError("G7 evidence has no dataset.files array")
        packet_files = {
            row["path"]: row
            for row in packet_rows
            if isinstance(row, Mapping) and isinstance(row.get("path"), str)
        }
        errors: list[str] = []
        missing = sorted(expected_paths - packet_files.keys())
        if missing:
            errors.append(
                "M7_G7_AUTHORITY_HASH_DRIFT: packet dataset is missing "
                f"reproduction authorities {missing!r}"
            )
        inventory: list[dict[str, str]] = []
        for path in sorted(expected_paths):
            row = packet_files.get(path)
            digest = row.get("sha256") if isinstance(row, Mapping) else None
            if not isinstance(digest, str) or not digest:
                errors.append(
                    "M7_G7_AUTHORITY_HASH_DRIFT: packet has no SHA for "
                    f"{path}"
                )
                continue
            inventory.append({"path": path, "sha256": digest})
        return inventory, errors
    except Exception as error:
        return [], [f"M7_G7_AUTHORITY_HASH_DRIFT: unable to read packet inventory: {error}"]


def _authority_hash_errors(
    root: Path, authority_inventory: list[dict[str, str]]
) -> list[str]:
    """Compare every selected input's current bytes to its packet SHA."""

    errors: list[str] = []
    for row in authority_inventory:
        try:
            path = _root_relative_path(root, row["path"])
        except (KeyError, ValueError) as error:
            errors.append(
                "M7_G7_AUTHORITY_HASH_DRIFT: invalid authority path "
                f"{row.get('path')!r}: {error}"
            )
            continue
        actual_digest = _sha256(path)
        if actual_digest != row["sha256"].lower():
            errors.append(
                "M7_G7_AUTHORITY_HASH_DRIFT: "
                f"{row['path']} recorded={row['sha256']} actual={actual_digest!r}"
            )
    return errors


def _g7_provenance_errors(
    root: Path,
    typed_ir_dir: Path,
    g7_evidence_errors: list[str],
    authority_inventory: list[dict[str, str]],
    authority_inventory_errors: list[str],
) -> list[str]:
    """Check the examiner reproduction index and its non-self-authenticating file set."""

    errors: list[str] = list(g7_evidence_errors)
    errors.extend(authority_inventory_errors)
    errors.extend(_authority_hash_errors(root, authority_inventory))

    expected_paths = {row["path"] for row in _CANONICAL_TYPED_IR_INVENTORY}
    actual_paths: set[str] = set()
    try:
        if typed_ir_dir.is_dir():
            actual_paths = {
                _portable_path(path, root)
                for path in typed_ir_dir.rglob("*")
                if path.is_file()
            }
    except OSError as error:
        errors.append(
            "M7_G7_TYPED_IR_HASH_DRIFT: unable to enumerate typed-IR files: "
            f"{error}"
        )
    missing = sorted(expected_paths - actual_paths)
    extra = sorted(actual_paths - expected_paths)
    if missing or extra:
        errors.append(
            "M7_G7_TYPED_IR_HASH_DRIFT: typed-IR inventory set differs "
            f"(missing={missing!r}, extra={extra!r})"
        )
    for row in _CANONICAL_TYPED_IR_INVENTORY:
        path = root / row["path"]
        actual_digest = _sha256(path)
        if actual_digest != row["sha256"]:
            errors.append(
                "M7_G7_TYPED_IR_HASH_DRIFT: "
                f"{row['path']} recorded={row['sha256']} actual={actual_digest!r}"
            )
    return errors


def _provenance_errors(
    root: Path,
    typed_ir_dir: Path,
    query_paths: list[Path],
    g7_evidence_errors: list[str],
    authority_inventory: list[dict[str, str]],
    authority_inventory_errors: list[str],
) -> list[str]:
    """Audit every frozen authority using paths rooted at ``--root``.

    The accepted M7 helpers predate the reproduction CLI and keep their project
    root as a module constant.  Their root is patched only for this bounded,
    synchronous audit so the same helper semantics and stable error codes work
    for the scratch roots used by the RED.
    """

    errors: list[str] = []
    corpus_path = root / _CORPUS_PATH
    registry_path = root / _REGISTRY_PATH
    adjudication_path = root / _ADJUDICATION_PATH
    controller_dir = root / _CONTROLLER_TRUTH_PATH
    independent_dir = root / _INDEPENDENT_TRUTH_PATH

    try:
        errors.extend(
            _g7_provenance_errors(
                root,
                typed_ir_dir,
                g7_evidence_errors,
                authority_inventory,
                authority_inventory_errors,
            )
        )
        original = _load_json(query_paths[0])
        supplement = _load_json(query_paths[1])
        registry = _load_json(registry_path)
        adjudication = _load_json(adjudication_path)
        corpus = _load_json(corpus_path)
        errors.extend(_canonical_pin_errors(original, supplement, root))

        from blueprint_lens.production import (
            TypedIRDirectoryProvider,
            audit_query_list,
            audit_truth_registry,
            verify_adjudication,
        )
        import blueprint_lens.production.m7_query_selection as m7_query_selection
        import blueprint_lens.production.m7_truth as m7_truth

        provider = TypedIRDirectoryProvider(typed_ir_dir)
        old_truth_root = m7_truth._ROOT
        old_selection_root = m7_query_selection._ROOT
        old_selection_sha256 = m7_query_selection._sha256
        try:
            # Both helpers resolve registry/query pins through their module root.
            m7_truth._ROOT = root
            m7_query_selection._ROOT = root
            # The accepted supplement verifier has a private raw-byte digest
            # helper.  Patch it only for this synchronous rooted audit so its
            # corpus pin follows the same portable authority semantics, then
            # restore the helper together with the private root below.
            m7_query_selection._sha256 = _sha256

            original_audit = audit_query_list(original, corpus, provider)
            errors.extend(original_audit.errors)
            errors.extend(
                m7_query_selection.verify_supplement(supplement, corpus, provider)
            )
            registry_audit = audit_truth_registry(registry)
            errors.extend(registry_audit.errors)
            errors.extend(
                verify_adjudication(
                    adjudication,
                    registry,
                    controller_dir,
                    independent_dir,
                )
            )
        finally:
            m7_truth._ROOT = old_truth_root
            m7_query_selection._ROOT = old_selection_root
            m7_query_selection._sha256 = old_selection_sha256
    except Exception as error:
        errors.append(f"M7_REPRODUCTION_PROVENANCE_INVALID: {error}")
    return sorted(set(errors))


def _load_queries(query_paths: list[Path]) -> list[Mapping[str, Any]]:
    queries: list[Mapping[str, Any]] = []
    for path in query_paths:
        document = _load_json(path)
        raw_queries = document.get("queries")
        if not isinstance(raw_queries, list):
            raise ValueError(f"query artefact has no queries array: {path}")
        for index, query in enumerate(raw_queries):
            if not isinstance(query, Mapping):
                raise ValueError(f"query {index} in {path} is not an object")
            queries.append(query)
    return queries


def _row_from_measurement(row: Mapping[str, Any]) -> dict[str, Any]:
    nodes = row["nodes"]
    edges = row["edges"]
    node_false_positive = list(nodes["false_positive_ids"])
    node_false_negative = list(nodes["false_negative_ids"])
    edge_false_positive = list(edges["false_positive_ids"])
    edge_false_negative = list(edges["false_negative_ids"])
    exact = not (
        node_false_positive
        or node_false_negative
        or edge_false_positive
        or edge_false_negative
    )
    return {
        "query_id": row["query_id"],
        "slice_kind": row["slice_kind"],
        "exact": exact,
        "false_positive_node_ids": node_false_positive,
        "false_negative_node_ids": node_false_negative,
        "false_positive_edge_ids": edge_false_positive,
        "false_negative_edge_ids": edge_false_negative,
        "produced_node_count": nodes["true_positive"] + nodes["false_positive"],
        "truth_node_count": nodes["true_positive"] + nodes["false_negative"],
        "produced_edge_count": edges["true_positive"] + edges["false_positive"],
        "truth_edge_count": edges["true_positive"] + edges["false_negative"],
    }


def _build_result(root: Path, truth_dir: Path | None) -> dict[str, Any]:
    root = root.resolve()
    query_paths = [
        root / _ORIGINAL_QUERY_PATH,
        root / _SUPPLEMENT_QUERY_PATH,
    ]
    typed_ir_dir = root / "artifacts/m7/export/run1/typed-ir"
    adjudication_path = root / _ADJUDICATION_PATH
    g7_evidence_path = root / _G7_EVIDENCE_PATH
    controller_truth_dir = (
        truth_dir.resolve()
        if truth_dir is not None
        else root / _CONTROLLER_TRUTH_PATH
    )

    # The script is executable directly from a checkout, so make the local
    # package import explicit rather than relying on a caller-provided path.
    analysis_path = str(root / "analysis")
    if not Path(analysis_path).is_dir():
        analysis_path = str(Path(__file__).resolve().parents[1] / "analysis")
    if analysis_path not in sys.path:
        sys.path.insert(0, analysis_path)

    from blueprint_lens.production import build_correctness_report

    typed_ir_inventory = _typed_ir_inventory()
    g7_evidence_errors = _g7_evidence_hash_errors(g7_evidence_path)
    authority_packet_path = g7_evidence_path
    if g7_evidence_errors:
        canonical_packet = _canonical_g7_evidence_path()
        if not _g7_evidence_hash_errors(canonical_packet):
            authority_packet_path = canonical_packet
    authority_inventory, authority_inventory_errors = _authority_inventory(
        root, authority_packet_path, typed_ir_inventory
    )
    provenance_errors = _provenance_errors(
        root,
        typed_ir_dir,
        query_paths,
        g7_evidence_errors,
        authority_inventory,
        authority_inventory_errors,
    )
    inputs = {
        "adjudication_path": _portable_path(adjudication_path, root),
        "canonical_controller_truth_dir": _portable_path(
            root / _CONTROLLER_TRUTH_PATH, root
        ),
        "corpus_manifest_path": _portable_path(root / _CORPUS_PATH, root),
        "g7_evidence_path": _portable_path(g7_evidence_path, root),
        "independent_truth_dir": _portable_path(
            root / _INDEPENDENT_TRUTH_PATH, root
        ),
        "query_paths": [_portable_path(path, root) for path in query_paths],
        "truth_dir": _portable_path(controller_truth_dir, root),
        "truth_registry_path": _portable_path(root / _REGISTRY_PATH, root),
        "typed_ir_dir": _portable_path(typed_ir_dir, root),
    }
    if provenance_errors:
        return {
            "status": "FAIL",
            "inputs": inputs,
            "authority_inventory": authority_inventory,
            "provenance_errors": provenance_errors,
            "typed_ir_inventory": typed_ir_inventory,
        }

    queries = _load_queries(query_paths)
    report = build_correctness_report(
        queries,
        controller_truth_dir,
        typed_ir_dir,
        adjudication_path,
    )
    rows = [
        _row_from_measurement(row)
        for row in sorted(report["rows"], key=lambda item: item["query_id"])
    ]
    exact_count = sum(1 for row in rows if row["exact"])
    produced_nodes = sum(row["produced_node_count"] for row in rows)
    produced_edges = sum(row["produced_edge_count"] for row in rows)
    return {
        "status": "PASS" if exact_count == len(rows) else "FAIL",
        "authority_inventory": authority_inventory,
        "inputs": inputs,
        "provenance_errors": [],
        "typed_ir_inventory": typed_ir_inventory,
        "counts": {
            "edges": produced_edges,
            "exact": exact_count,
            "nodes": produced_nodes,
            "queries": len(rows),
        },
        "rows": rows,
    }


def main(argv: list[str] | None = None) -> int:
    try:
        args = _parse_args(argv)
        result = _build_result(args.root, args.truth_dir)
        print(json.dumps(result, ensure_ascii=False, sort_keys=True))
        return 0 if result["status"] == "PASS" else 1
    except Exception as error:  # fail closed while preserving machine-readable output
        print(
            json.dumps(
                {
                    "status": "FAIL",
                    "provenance_errors": [
                        f"M7_REPRODUCTION_PROVENANCE_INVALID: {type(error).__name__}: {error}"
                    ],
                    "error": f"{type(error).__name__}: {error}",
                },
                ensure_ascii=False,
                sort_keys=True,
            )
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
