"""Closed public failure contract for M6 Session workflows."""

from __future__ import annotations

from types import MappingProxyType
from typing import Final, Mapping


M6_ERROR_CODES: Final[frozenset[str]] = frozenset(
    {
        "M6_PRECONDITION_NO_BLUEPRINT",
        "M6_PRECONDITION_GRAPH_INVALID",
        "M6_PRECONDITION_DIRTY_SOURCE",
        "M6_PRECONDITION_COMPILE_FAILED",
        "M6_PRECONDITION_QUERY_INVALID",
        "M6_PRECONDITION_STAGING_UNAVAILABLE",
        "M6_EXPORT_FAILED",
        "M6_EXPORT_SOURCE_MISMATCH",
        "M6_PIPELINE_TYPED_DOCUMENT_INVALID",
        "M6_PIPELINE_SLICE_FAILED",
        "M6_PIPELINE_EXPLANATION_FAILED",
        "M6_PIPELINE_SHARED_FACTS_INVALID",
        "M6_PIPELINE_BUDGET_EXCEEDED",
        "M6_PACKET_SCHEMA_INVALID",
        "M6_PACKET_VERSION_UNSUPPORTED",
        "M6_PACKET_HASH_MISMATCH",
        "M6_PACKET_CANONICAL_INVALID",
        "M6_PACKET_REFERENCE_INVALID",
        "M6_PACKET_SOURCE_STALE",
        "M6_PACKET_PUBLISH_FAILED",
        "M6_PACKET_OUTPUT_EXISTS",
        "M6_RUNNER_LAUNCH_FAILED",
        "M6_RUNNER_NONZERO_EXIT",
        "M6_RUNNER_TIMEOUT",
        "M6_RUNNER_CANCELLED",
        "M6_RUNNER_CLEANUP_FAILED",
        "M6_VIEW_PROFILE_UNSUPPORTED",
        "M6_VIEW_ENTITY_UNMAPPED",
        "M6_VIEW_SELECTION_SYNC_FAILED",
        "M6_VIEW_SOURCE_NAVIGATION_FAILED",
        "M6_TELEMETRY_SCHEMA_INVALID",
        "M6_TELEMETRY_SEQUENCE_INVALID",
        "M6_TELEMETRY_REPLAY_MISMATCH",
    }
)


class M6Error(ValueError):
    """Stable, structured public M6 failure."""

    def __init__(
        self,
        code: str,
        message: str,
        *,
        phase: str,
        retryable: bool,
        diagnostics: Mapping[str, object] | None = None,
        cause: Exception | None = None,
    ) -> None:
        if code not in M6_ERROR_CODES:
            raise ValueError(f"unknown M6 error code: {code}")
        if not phase:
            raise ValueError("M6 error phase must be non-empty")
        self.code = code
        self.phase = phase
        self.retryable = retryable
        self.diagnostics = MappingProxyType(dict(diagnostics or {}))
        super().__init__(f"{code}: {message}")
        if cause is not None:
            self.__cause__ = cause
