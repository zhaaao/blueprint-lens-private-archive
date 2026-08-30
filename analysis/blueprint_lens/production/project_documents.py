"""Validated project-document providers for production and frozen M3 inputs."""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import json
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
from types import MappingProxyType
from typing import Any, Literal, Mapping, Protocol

from ..raw_probe import BlueprintDocument, Graph, Node, Pin, load_raw_probe


_RESULT_SCHEMA_NAME = "blueprint-lens-m3-batch-result"
_RESULT_SCHEMA_VERSION = "1.0.0"
_RAW_FORMAT = "blueprint-lens-raw-probe"
_RAW_VERSION = "0.2"
_SHA256 = re.compile(r"[0-9a-f]{64}")


@dataclass(frozen=True, slots=True)
class AssetProvenance:
    blueprint_object_path: str
    package_persistent_guid: str
    package_source_sha256: str
    generated_class_path: str
    compile_status: Literal["up_to_date"]


@dataclass(frozen=True, slots=True)
class ProjectDocument:
    document: BlueprintDocument
    raw_path: Path
    raw_sha256: str
    provenance: AssetProvenance


class ProjectDocumentProvider(Protocol):
    def list_asset_ids(self) -> tuple[str, ...]: ...

    def load(self, asset_id: str) -> ProjectDocument: ...


@dataclass(frozen=True, slots=True)
class FrozenFixture:
    """One explicitly declared frozen raw fixture and its provenance."""

    raw_path: Path
    raw_sha256: str
    provenance: AssetProvenance


class ProjectDocumentError(ValueError):
    """A fail-closed provider error with a stable machine-readable code."""

    def __init__(self, code: str, message: str, *, cause: Exception | None = None) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")
        if cause is not None:
            self.__cause__ = cause


def _fail(code: str, message: str, *, cause: Exception | None = None) -> None:
    raise ProjectDocumentError(code, message, cause=cause)


def _non_empty_string(value: Any, context: str) -> str:
    if not isinstance(value, str) or not value.strip():
        _fail("M3_PROVIDER_PROVENANCE_INVALID", f"{context} must be a non-empty string")
    return value


def _sha256_string(value: Any, context: str, *, code: str) -> str:
    if not isinstance(value, str) or _SHA256.fullmatch(value) is None:
        _fail(code, f"{context} must be a lowercase SHA-256")
    return value


def _validate_provenance(provenance: AssetProvenance) -> None:
    _non_empty_string(provenance.blueprint_object_path, "blueprint_object_path")
    _non_empty_string(provenance.package_persistent_guid, "package_persistent_guid")
    _sha256_string(
        provenance.package_source_sha256,
        "package_source_sha256",
        code="M3_PROVIDER_PROVENANCE_INVALID",
    )
    _non_empty_string(provenance.generated_class_path, "generated_class_path")
    if provenance.compile_status != "up_to_date":
        _fail(
            "M3_PROVIDER_PROVENANCE_INVALID",
            "compile_status must be 'up_to_date'",
        )


def _parse_provenance(value: Mapping[str, Any], context: str) -> AssetProvenance:
    try:
        provenance = AssetProvenance(
            blueprint_object_path=value["object_path"],
            package_persistent_guid=value["package_guid"],
            package_source_sha256=value["package_source_sha256"],
            generated_class_path=value["generated_class_path"],
            compile_status=value["compile_status"],
        )
    except KeyError as error:
        _fail(
            "M3_PROVIDER_PROVENANCE_INVALID",
            f"{context} is missing provenance field {error.args[0]!r}",
            cause=error,
        )
    _validate_provenance(provenance)
    return provenance


def _resolve_persisted_path(root: Path, value: Any, context: str) -> Path:
    if not isinstance(value, str) or not value:
        _fail("M3_PROVIDER_PATH_INVALID", f"{context} must be a non-empty relative path")
    windows = PureWindowsPath(value)
    posix = PurePosixPath(value)
    if (
        "\\" in value
        or windows.is_absolute()
        or bool(windows.drive)
        or posix.is_absolute()
        or ".." in posix.parts
    ):
        _fail("M3_PROVIDER_PATH_INVALID", f"{context} must be a portable relative path")

    resolved_root = root.resolve()
    resolved = resolved_root.joinpath(*posix.parts).resolve()
    try:
        resolved.relative_to(resolved_root)
    except ValueError as error:
        _fail(
            "M3_PROVIDER_PATH_INVALID",
            f"{context} escapes declared root {resolved_root}",
            cause=error,
        )
    return resolved


def _deep_freeze(value: Any) -> Any:
    if isinstance(value, Mapping):
        return MappingProxyType({key: _deep_freeze(item) for key, item in value.items()})
    if isinstance(value, (list, tuple)):
        return tuple(_deep_freeze(item) for item in value)
    return value


def _freeze_pin(pin: Pin) -> Pin:
    return replace(pin, type=_deep_freeze(pin.type), default=_deep_freeze(pin.default))


def _freeze_node(node: Node) -> Node:
    return replace(
        node,
        symbol=None if node.symbol is None else _deep_freeze(node.symbol),
        pins=tuple(_freeze_pin(pin) for pin in node.pins),
    )


def _freeze_graph(graph: Graph) -> Graph:
    return replace(graph, nodes=tuple(_freeze_node(node) for node in graph.nodes))


def _freeze_document(document: BlueprintDocument) -> BlueprintDocument:
    return replace(document, graphs=tuple(_freeze_graph(graph) for graph in document.graphs))


def _load_fixture(asset_id: str, fixture: FrozenFixture) -> ProjectDocument:
    raw_path = fixture.raw_path.resolve()
    try:
        raw_bytes = raw_path.read_bytes()
    except OSError as error:
        _fail(
            "M3_PROVIDER_DOCUMENT_INVALID",
            f"cannot read raw document for {asset_id!r}: {error}",
            cause=error,
        )
    actual_sha256 = hashlib.sha256(raw_bytes).hexdigest()
    if actual_sha256 != fixture.raw_sha256:
        _fail(
            "M3_PROVIDER_RAW_HASH_MISMATCH",
            f"raw SHA-256 mismatch for {asset_id!r}",
        )

    try:
        document = load_raw_probe(raw_path)
    except Exception as error:
        _fail(
            "M3_PROVIDER_DOCUMENT_INVALID",
            f"raw document rejected for {asset_id!r}: {error}",
            cause=error,
        )
    if document.format != _RAW_FORMAT or document.format_version != _RAW_VERSION:
        _fail(
            "M3_PROVIDER_DOCUMENT_INVALID",
            f"raw document for {asset_id!r} must use {_RAW_FORMAT} {_RAW_VERSION}",
        )
    if document.blueprint_path != fixture.provenance.blueprint_object_path:
        _fail(
            "M3_PROVIDER_DOCUMENT_INVALID",
            f"raw blueprint path disagrees with provenance for {asset_id!r}",
        )

    return ProjectDocument(
        document=_freeze_document(document),
        raw_path=raw_path,
        raw_sha256=actual_sha256,
        provenance=fixture.provenance,
    )


class ProductionManifestProvider:
    """Load project documents declared by an M3 batch result manifest.

    Task 2 result manifests aggregate unique assets by ``object_path`` and do not
    carry another asset identifier, so this adapter deliberately exposes that
    object path as its provider ``asset_id``.
    """

    def __init__(self, manifest_path: str | Path, *, root: str | Path | None = None) -> None:
        source = Path(manifest_path)
        declared_root = Path(root) if root is not None else source.parent
        try:
            value = json.loads(source.read_text(encoding="utf-8"))
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            _fail(
                "M3_PROVIDER_MANIFEST_INVALID",
                f"cannot read production result manifest {source}: {error}",
                cause=error,
            )
        if not isinstance(value, dict):
            _fail("M3_PROVIDER_MANIFEST_INVALID", "production result root must be an object")
        if (
            value.get("schema_name") != _RESULT_SCHEMA_NAME
            or value.get("schema_version") != _RESULT_SCHEMA_VERSION
            or not isinstance(value.get("assets"), list)
        ):
            _fail("M3_PROVIDER_MANIFEST_INVALID", "unsupported production result manifest tuple")
        _sha256_string(
            value.get("source_manifest_sha256"),
            "source_manifest_sha256",
            code="M3_PROVIDER_MANIFEST_INVALID",
        )

        fixtures: dict[str, FrozenFixture] = {}
        expected_counts: dict[str, tuple[int, int, int, int]] = {}
        for index, raw_asset in enumerate(value["assets"]):
            context = f"assets[{index}]"
            if not isinstance(raw_asset, dict):
                _fail("M3_PROVIDER_MANIFEST_INVALID", f"{context} must be an object")
            provenance = _parse_provenance(raw_asset, context)
            asset_id = provenance.blueprint_object_path
            if asset_id in fixtures:
                _fail(
                    "M3_PROVIDER_DUPLICATE_ASSET_ID",
                    f"duplicate production asset ID {asset_id!r}",
                )
            raw_path = _resolve_persisted_path(
                declared_root, raw_asset.get("raw_relative_path"), f"{context}.raw_relative_path"
            )
            raw_sha256 = _sha256_string(
                raw_asset.get("raw_sha256"),
                f"{context}.raw_sha256",
                code="M3_PROVIDER_MANIFEST_INVALID",
            )
            counts: list[int] = []
            for field in ("graph_count", "node_count", "pin_count", "edge_count"):
                count = raw_asset.get(field)
                if isinstance(count, bool) or not isinstance(count, int) or count < 0:
                    _fail(
                        "M3_PROVIDER_MANIFEST_INVALID",
                        f"{context}.{field} must be a non-negative integer",
                    )
                counts.append(count)
            fixtures[asset_id] = FrozenFixture(
                raw_path=raw_path,
                raw_sha256=raw_sha256,
                provenance=provenance,
            )
            expected_counts[asset_id] = tuple(counts)  # type: ignore[assignment]
        self._fixtures: Mapping[str, FrozenFixture] = MappingProxyType(fixtures)
        self._expected_counts: Mapping[str, tuple[int, int, int, int]] = MappingProxyType(
            expected_counts
        )
        self._asset_ids = tuple(sorted(fixtures))

    def list_asset_ids(self) -> tuple[str, ...]:
        return self._asset_ids

    def load(self, asset_id: str) -> ProjectDocument:
        try:
            fixture = self._fixtures[asset_id]
        except KeyError as error:
            _fail(
                "M3_PROVIDER_UNKNOWN_ASSET_ID",
                f"unknown production asset ID {asset_id!r}",
                cause=error,
            )
        project_document = _load_fixture(asset_id, fixture)
        document = project_document.document
        observed_counts = (
            len(document.graphs),
            len(document.nodes),
            len(document.pins),
            len(document.edges),
        )
        if observed_counts != self._expected_counts[asset_id]:
            _fail(
                "M3_PROVIDER_RESULT_MISMATCH",
                f"result counts disagree with raw document for {asset_id!r}",
            )
        return project_document


class FrozenFixtureProvider:
    """Load project documents from an explicit copied immutable fixture map."""

    def __init__(self, fixtures: Mapping[str, FrozenFixture]) -> None:
        copied: dict[str, FrozenFixture] = {}
        for asset_id, fixture in fixtures.items():
            if not isinstance(asset_id, str) or not asset_id:
                _fail("M3_PROVIDER_MANIFEST_INVALID", "frozen asset ID must be non-empty")
            if not isinstance(fixture, FrozenFixture):
                _fail(
                    "M3_PROVIDER_MANIFEST_INVALID",
                    f"frozen fixture {asset_id!r} has an invalid value",
                )
            if not isinstance(fixture.provenance, AssetProvenance):
                _fail(
                    "M3_PROVIDER_PROVENANCE_INVALID",
                    f"frozen fixture {asset_id!r} is missing valid provenance",
                )
            _sha256_string(
                fixture.raw_sha256,
                f"frozen fixture {asset_id!r} raw_sha256",
                code="M3_PROVIDER_MANIFEST_INVALID",
            )
            _validate_provenance(fixture.provenance)
            copied[asset_id] = FrozenFixture(
                raw_path=Path(fixture.raw_path).resolve(),
                raw_sha256=fixture.raw_sha256,
                provenance=fixture.provenance,
            )
        self._fixtures: Mapping[str, FrozenFixture] = MappingProxyType(copied)
        self._asset_ids = tuple(sorted(copied))

    def list_asset_ids(self) -> tuple[str, ...]:
        return self._asset_ids

    def load(self, asset_id: str) -> ProjectDocument:
        try:
            fixture = self._fixtures[asset_id]
        except KeyError as error:
            _fail(
                "M3_PROVIDER_UNKNOWN_ASSET_ID",
                f"unknown frozen asset ID {asset_id!r}",
                cause=error,
            )
        return _load_fixture(asset_id, fixture)


__all__ = [
    "AssetProvenance",
    "FrozenFixture",
    "FrozenFixtureProvider",
    "ProductionManifestProvider",
    "ProjectDocument",
    "ProjectDocumentError",
    "ProjectDocumentProvider",
]
