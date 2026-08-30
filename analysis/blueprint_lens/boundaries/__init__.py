"""Evidence-backed boundary and bounded-query primitives."""

from .upstream_query import (
    BoundedUpstreamResult,
    UpstreamFrontier,
    build_bounded_upstream_query,
)

__all__ = [
    "BoundedUpstreamResult",
    "UpstreamFrontier",
    "build_bounded_upstream_query",
]
