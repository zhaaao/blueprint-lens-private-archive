"""Portable file digests for text artefacts with checkout-dependent endings."""

from __future__ import annotations

from hashlib import sha256
from pathlib import Path


def file_sha256(path: Path) -> str:
    """Hash file content after normalising CRLF pairs to LF."""

    return sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


__all__ = ["file_sha256"]
