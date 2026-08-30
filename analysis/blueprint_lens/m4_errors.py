"""Shared fail-closed errors for the M4 execution-slice pipeline."""

from __future__ import annotations

from typing import Final


M4_ERROR_CODES: Final[frozenset[str]] = frozenset(
    {
        "M4_REGISTRY_INVALID",
        "M4_SOURCE_INVALID",
        "M4_TYPED_DOCUMENT_INVALID",
        "M4_CRITERION_INVALID",
        "M4_SLICE_INVARIANT_FAILED",
        "M4_SLICE_SCHEMA_INVALID",
        "M4_EVIDENCE_MISMATCH",
        "M4_PUBLISH_FAILED",
        "M4_OUTPUT_EXISTS",
    }
)


class M4ExecutionError(ValueError):
    """The single public M4 failure with a closed stable code."""

    def __init__(
        self,
        code: str,
        message: str,
        *,
        cause: Exception | None = None,
    ) -> None:
        if code not in M4_ERROR_CODES:
            raise ValueError(f"unknown M4 error code: {code}")
        self.code = code
        super().__init__(f"{code}: {message}")
        if cause is not None:
            self.__cause__ = cause
