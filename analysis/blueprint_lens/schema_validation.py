"""Dependency-free validator for the JSON Schema subset used by M2 fixtures.

This is deliberately small rather than a general Draft 2020-12 implementation.
It evaluates every instance-validation keyword used by the three Blueprint Lens
v1 schemas; projects needing arbitrary schemas should use a full validator.
"""

from __future__ import annotations

import json
from pathlib import Path
import re
from typing import Any, Mapping


class SchemaValidationError(ValueError):
    """Raised when an instance violates a Blueprint Lens project schema."""


def _type_matches(value: Any, expected: str) -> bool:
    if expected == "object":
        return isinstance(value, dict)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "null":
        return value is None
    raise SchemaValidationError(f"unsupported schema type: {expected}")


def _resolve_ref(root: Mapping[str, Any], reference: str) -> Mapping[str, Any]:
    if not reference.startswith("#/"):
        raise SchemaValidationError(f"only local schema references are supported: {reference}")
    current: Any = root
    for segment in reference[2:].split("/"):
        segment = segment.replace("~1", "/").replace("~0", "~")
        if not isinstance(current, dict) or segment not in current:
            raise SchemaValidationError(f"unresolvable schema reference: {reference}")
        current = current[segment]
    if not isinstance(current, dict):
        raise SchemaValidationError(f"schema reference is not an object: {reference}")
    return current


def _validate(value: Any, schema: Mapping[str, Any], root: Mapping[str, Any], path: str) -> None:
    if "$ref" in schema:
        _validate(value, _resolve_ref(root, str(schema["$ref"])), root, path)
        return

    if "oneOf" in schema:
        matches = 0
        for candidate in schema["oneOf"]:
            try:
                _validate(value, candidate, root, path)
            except SchemaValidationError:
                continue
            matches += 1
        if matches != 1:
            raise SchemaValidationError(f"{path}: expected exactly one oneOf match, got {matches}")

    if "const" in schema and value != schema["const"]:
        raise SchemaValidationError(f"{path}: expected constant {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        raise SchemaValidationError(f"{path}: value {value!r} is not in {schema['enum']!r}")

    expected_type = schema.get("type")
    if expected_type is not None and not _type_matches(value, str(expected_type)):
        raise SchemaValidationError(f"{path}: expected {expected_type}, got {type(value).__name__}")

    if isinstance(value, str):
        if len(value) < int(schema.get("minLength", 0)):
            raise SchemaValidationError(f"{path}: string is shorter than minLength")
        if "pattern" in schema and re.fullmatch(str(schema["pattern"]), value) is None:
            raise SchemaValidationError(f"{path}: string does not match {schema['pattern']!r}")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema and value < schema["minimum"]:
            raise SchemaValidationError(f"{path}: value is below minimum")

    if isinstance(value, list):
        if len(value) < int(schema.get("minItems", 0)):
            raise SchemaValidationError(f"{path}: array is shorter than minItems")
        if "maxItems" in schema and len(value) > int(schema["maxItems"]):
            raise SchemaValidationError(f"{path}: array is longer than maxItems")
        if schema.get("uniqueItems"):
            encoded = [json.dumps(item, sort_keys=True, ensure_ascii=False) for item in value]
            if len(encoded) != len(set(encoded)):
                raise SchemaValidationError(f"{path}: array items are not unique")
        item_schema = schema.get("items")
        if item_schema is not None:
            for index, item in enumerate(value):
                _validate(item, item_schema, root, f"{path}[{index}]")

    if isinstance(value, dict):
        if len(value) < int(schema.get("minProperties", 0)):
            raise SchemaValidationError(f"{path}: object has too few properties")
        for field in schema.get("required", ()):
            if field not in value:
                raise SchemaValidationError(f"{path}.{field}: required property is missing")
        properties = schema.get("properties", {})
        for field, field_value in value.items():
            if field in properties:
                _validate(field_value, properties[field], root, f"{path}.{field}")
                continue
            additional = schema.get("additionalProperties", True)
            if additional is False:
                raise SchemaValidationError(f"{path}.{field}: additional property is forbidden")
            if isinstance(additional, dict):
                _validate(field_value, additional, root, f"{path}.{field}")


def validate_instance(instance: Any, schema: Mapping[str, Any]) -> None:
    """Validate an already decoded instance against a project schema."""

    _validate(instance, schema, schema, "$")


def validate_json_file(instance_path: str | Path, schema_path: str | Path) -> None:
    """Load and validate one JSON instance against one project schema."""

    instance = json.loads(Path(instance_path).read_text(encoding="utf-8"))
    schema = json.loads(Path(schema_path).read_text(encoding="utf-8"))
    validate_instance(instance, schema)
