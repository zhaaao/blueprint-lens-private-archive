"""Closed error contract for M5 variable/data slicing."""

from __future__ import annotations

from typing import Final


M5_ERROR_CODES: Final[frozenset[str]] = frozenset(
    {
        "M5_REGISTRY_INVALID",
        "M5_SOURCE_INVALID",
        "M5_TYPED_DOCUMENT_INVALID",
        "M5_CRITERION_INVALID",
        "M5_SLICE_INVARIANT_FAILED",
        "M5_SLICE_SCHEMA_INVALID",
        "M5_EVIDENCE_MISMATCH",
        "M5_PUBLISH_FAILED",
        "M5_OUTPUT_EXISTS",
    }
)


class M5DataError(ValueError):
    """Stable public M5 failure with an approved machine-readable code."""

    def __init__(
        self,
        code: str,
        message: str,
        *,
        cause: Exception | None = None,
    ) -> None:
        if code not in M5_ERROR_CODES:
            raise ValueError(f"unknown M5 error code: {code}")
        self.code = code
        super().__init__(f"{code}: {message}")
        if cause is not None:
            self.__cause__ = cause
