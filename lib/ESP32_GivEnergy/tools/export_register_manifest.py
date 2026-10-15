#!/usr/bin/env python3
"""Export GivTCP's named model/register definitions without importing GivTCP."""

from __future__ import annotations

import argparse
import ast
import json
import subprocess
from pathlib import Path
from typing import Any


MODEL_FILES = (
    "baseinverter.py",
    "inverter.py",
    "threephase.py",
    "ems.py",
    "gateway.py",
    "battery.py",
    "meter.py",
    "hvbcu.py",
    "hvbmu.py",
)


def git_commit(repository: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repository), "rev-parse", "HEAD"],
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def dotted_name(node: ast.AST) -> str:
    try:
        return ast.unparse(node)
    except Exception:
        return node.__class__.__name__


def literal(node: ast.AST) -> Any:
    try:
        return ast.literal_eval(node)
    except (ValueError, TypeError):
        return dotted_name(node)


def register_calls(node: ast.AST) -> list[dict[str, Any]]:
    registers: list[dict[str, Any]] = []
    for child in ast.walk(node):
        if not isinstance(child, ast.Call) or len(child.args) != 1:
            continue
        name = dotted_name(child.func)
        if name not in {"HR", "IR", "MR"}:
            continue
        index = literal(child.args[0])
        if isinstance(index, int):
            registers.append({"bank": name, "address": index})
        else:
            registers.append(
                {"bank": name, "address_expression": dotted_name(child.args[0])}
            )
    return registers


def parse_definition(name: str, call: ast.Call, source_file: str) -> dict[str, Any]:
    valid = None
    for keyword in call.keywords:
        if keyword.arg == "valid":
            valid = literal(keyword.value)
    return {
        "name": name,
        "converter": dotted_name(call.args[0]) if call.args else None,
        "post_converter": dotted_name(call.args[1]) if len(call.args) > 1 else None,
        "registers": register_calls(call),
        "valid_write_range": valid,
        "source_file": source_file,
    }


def parse_model_file(path: Path) -> dict[str, list[dict[str, Any]]]:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    classes: dict[str, list[dict[str, Any]]] = {}
    for class_node in (n for n in tree.body if isinstance(n, ast.ClassDef)):
        definitions: list[dict[str, Any]] = []
        for statement in ast.walk(class_node):
            if not isinstance(statement, ast.Assign):
                continue
            if not any(
                (
                    isinstance(target, ast.Name)
                    and target.id in {"REGISTER_LUT", "NEW_LUT"}
                )
                or (
                    isinstance(target, ast.Attribute)
                    and target.attr in {"REGISTER_LUT", "NEW_LUT"}
                )
                for target in statement.targets
            ):
                continue
            if not isinstance(statement.value, ast.Dict):
                continue
            for key, value in zip(statement.value.keys, statement.value.values):
                if (
                    isinstance(key, ast.Constant)
                    and isinstance(key.value, str)
                    and isinstance(value, ast.Call)
                    and dotted_name(value.func) in {"Def", "RegisterDefinition"}
                ):
                    definitions.append(
                        parse_definition(key.value, value, path.name)
                    )
        if definitions:
            classes[class_node.name] = definitions
    return classes


def parse_control_registers(commands_file: Path) -> list[dict[str, Any]]:
    tree = ast.parse(
        commands_file.read_text(encoding="utf-8"),
        filename=str(commands_file),
    )
    controls: list[dict[str, Any]] = []
    for node in tree.body:
        if not isinstance(node, ast.ClassDef) or node.name != "RegisterMap":
            continue
        for statement in node.body:
            if (
                isinstance(statement, ast.Assign)
                and len(statement.targets) == 1
                and isinstance(statement.targets[0], ast.Name)
                and isinstance(statement.value, ast.Constant)
                and isinstance(statement.value.value, int)
            ):
                controls.append(
                    {
                        "name": statement.targets[0].id,
                        "holding_register": statement.value.value,
                    }
                )
    return controls


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "givtcp",
        type=Path,
        help="Path to a GivTCP checkout",
    )
    parser.add_argument(
        "output",
        type=Path,
        help="Destination JSON file",
    )
    args = parser.parse_args()

    package = args.givtcp / "GivTCP" / "givenergy_modbus_async"
    model_root = package / "model"
    models: dict[str, list[dict[str, Any]]] = {}
    for filename in MODEL_FILES:
        models.update(parse_model_file(model_root / filename))

    manifest = {
        "schema": 1,
        "source": "britkat1980/giv_tcp",
        "source_commit": git_commit(args.givtcp),
        "scope": (
            "Every named Def(...) register entry found in the pinned model "
            "files. Unnamed/reserved raw addresses remain accessible through "
            "RegisterStore."
        ),
        "models": models,
        "control_registers": parse_control_registers(
            package / "client" / "commands.py"
        ),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
