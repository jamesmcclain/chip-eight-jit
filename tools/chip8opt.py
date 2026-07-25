#!/usr/bin/env python3
"""Analysis and canonicalization support for chip8-asm source.

This deliberately does not rewrite programs.  Its analysis is conservative:
when it cannot establish an address or a control-flow target, it records a
hazard rather than guessing.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

ENTRY = 0x200
LIMIT = 0x1000


@dataclass
class Statement:
    line: int
    label: Optional[str]
    op: Optional[str]
    args: list[str]
    pc: int = 0
    size: int = 0


@dataclass
class Access:
    pc: int
    kind: str                 # read, write, or dynamic
    start: Optional[int]
    length: Optional[int]
    reason: str


@dataclass
class Analysis:
    labels: dict[str, int]
    statements: list[Statement]
    reachable: set[int] = field(default_factory=set)
    leaders: set[int] = field(default_factory=lambda: {ENTRY})
    edges: set[tuple[int, int]] = field(default_factory=set)
    accesses: list[Access] = field(default_factory=list)
    hazards: set[str] = field(default_factory=set)


def tokens(line: str) -> list[str]:
    """Match asm.c's comma/comment token rules closely."""
    line = line.split(";", 1)[0].split("#", 1)[0]
    return line.replace(",", " ").split()


def parse_number(word: str) -> Optional[int]:
    try:
        # strtoul(base=0), as used by asm.c, accepts a leading-zero octal form.
        if word.lower().startswith("0x"):
            return int(word[2:], 16)
        if len(word) > 1 and word.startswith("0"):
            return int(word, 8)
        return int(word, 10)
    except ValueError:
        return None


def parse_source(text: str) -> tuple[list[Statement], dict[str, int]]:
    statements: list[Statement] = []
    labels: dict[str, int] = {}
    pc = ENTRY
    for line_no, raw in enumerate(text.splitlines(), 1):
        words = tokens(raw)
        if not words:
            continue
        label = None
        if words[0].endswith(":"):
            label = words.pop(0)[:-1].upper()
            if not label:
                raise ValueError(f"line {line_no}: empty label")
            if label in labels:
                raise ValueError(f"line {line_no}: duplicate label {label}")
            labels[label] = pc
        op = words.pop(0).upper() if words else None
        args = [word.upper() for word in words]
        stmt = Statement(line_no, label, op, args, pc)
        if op == ".ORG":
            if len(args) != 1 or parse_number(args[0]) is None:
                raise ValueError(f"line {line_no}: invalid .org")
            target = parse_number(args[0])
            if target is None or target < pc or not ENTRY <= target < LIMIT:
                raise ValueError(f"line {line_no}: invalid .org target")
            stmt.size = target - pc
        elif op == ".BYTE":
            stmt.size = len(args)
        elif op == ".WORD":
            stmt.size = 2 * len(args)
        elif op:
            # The assembler owns detailed operand validation. Every instruction is two bytes.
            stmt.size = 2
        statements.append(stmt)
        pc += stmt.size
        if pc > LIMIT:
            raise ValueError(f"line {line_no}: program exceeds CHIP-8 memory")
    return statements, labels


def resolve(word: str, labels: dict[str, int]) -> Optional[int]:
    return labels.get(word.upper(), parse_number(word))


def target(stmt: Statement, labels: dict[str, int]) -> Optional[int]:
    if not stmt.args:
        return None
    return resolve(stmt.args[-1], labels)


def is_skip(stmt: Statement) -> bool:
    return stmt.op in {"SE", "SNE", "SKP", "SKNP"}


def successors(stmt: Statement, labels: dict[str, int], hazards: set[str]) -> list[int]:
    fall = stmt.pc + 2
    if stmt.op == "RET":
        return []
    if stmt.op == "JP":
        if len(stmt.args) == 2:  # JP V0, address
            hazards.add(f"0x{stmt.pc:03X}: computed JP V0")
            return []
        dest = target(stmt, labels)
        if dest is None:
            hazards.add(f"0x{stmt.pc:03X}: unresolved JP target")
            return []
        return [dest]
    if stmt.op == "CALL":
        dest = target(stmt, labels)
        if dest is None:
            hazards.add(f"0x{stmt.pc:03X}: unresolved CALL target")
            return [fall]
        # The call edge and continuation are both useful conservative CFG edges.
        return [dest, fall]
    if is_skip(stmt):
        return [fall, fall + 2]
    return [fall]


def instruction_map(statements: list[Statement]) -> dict[int, Statement]:
    return {s.pc: s for s in statements if s.op and not s.op.startswith(".")}


def add_access(a: Analysis, pc: int, kind: str, start: Optional[int], length: Optional[int], reason: str) -> None:
    a.accesses.append(Access(pc, kind, start, length, reason))
    if start is None:
        a.hazards.add(f"0x{pc:03X}: dynamic I for {reason}")


def analyze(statements: list[Statement], labels: dict[str, int]) -> Analysis:
    result = Analysis(labels, statements)
    insns = instruction_map(statements)
    # I and Vx constants form a deliberately tiny abstract domain.  It is enough
    # to identify the common LD I; DRW / Fx55 patterns without claiming proof.
    State = tuple[Optional[int], tuple[Optional[int], ...]]
    initial: State = (None, (None,) * 16)
    # Join incoming states at each PC.  This keeps loops finite: a disagreeing
    # constant becomes unknown, rather than creating one state per iteration.
    incoming: dict[int, State] = {ENTRY: initial}
    work: deque[int] = deque([ENTRY])

    def join(old: State, new: State) -> State:
        old_i, old_regs = old
        new_i, new_regs = new
        return (old_i if old_i == new_i else None,
                tuple(a if a == b else None for a, b in zip(old_regs, new_regs)))

    while work:
        pc = work.popleft()
        if pc not in insns:
            continue
        state = incoming[pc]
        result.reachable.add(pc)
        stmt = insns[pc]
        i_value, regs = state
        regs = list(regs)
        op, args = stmt.op, stmt.args
        if op == "LD" and len(args) == 2:
            if args[0] == "I":
                i_value = resolve(args[1], labels)
            elif len(args[0]) == 2 and args[0][0] == "V" and args[0][1] in "0123456789ABCDEF":
                x = int(args[0][1], 16)
                regs[x] = resolve(args[1], labels) if not args[1].startswith("V") else regs[int(args[1][1], 16)]
            elif args[0] == "F":
                i_value = None
            elif args[0] == "B" and args[1].startswith("V"):
                add_access(result, pc, "write", i_value, 3, "BCD store")
            elif args[0] == "[I]" and args[1].startswith("V"):
                add_access(result, pc, "write", i_value, int(args[1][1], 16) + 1, "register store")
            elif args[1] == "[I]" and args[0].startswith("V"):
                add_access(result, pc, "read", i_value, int(args[0][1], 16) + 1, "register load")
        elif op == "ADD" and len(args) == 2:
            if args[0] == "I":
                value = regs[int(args[1][1], 16)] if args[1].startswith("V") else None
                i_value = None if i_value is None or value is None else i_value + value
            elif args[0].startswith("V"):
                x = int(args[0][1], 16)
                value = resolve(args[1], labels) if not args[1].startswith("V") else regs[int(args[1][1], 16)]
                regs[x] = None if regs[x] is None or value is None else (regs[x] + value) & 0xFF
        elif op == "DRW" and len(args) == 3:
            add_access(result, pc, "read", i_value, resolve(args[2], labels), "sprite read")
        # Instructions with an unknown/side-effectful register result invalidate it.
        if op in {"RND", "OR", "AND", "XOR", "SUB", "SUBN", "SHR", "SHL"} and args and args[0].startswith("V"):
            regs[int(args[0][1], 16)] = None
        next_state: State = (i_value, tuple(regs))
        for dest in successors(stmt, labels, result.hazards):
            if ENTRY <= dest < LIMIT:
                result.edges.add((pc, dest))
                result.leaders.add(dest)
                # A call's return continuation has the callee's unknown effects.
                # The target itself receives the caller state.
                propagated = next_state
                if op == "CALL" and dest == stmt.pc + 2:
                    propagated = (None, (None,) * 16)
                previous = incoming.get(dest)
                merged = propagated if previous is None else join(previous, propagated)
                if previous != merged:
                    incoming[dest] = merged
                    work.append(dest)
    # Writes into the assembled payload make its apparent data mutable; writes
    # overlapping reachable instructions are the stricter self-modifying case.
    payload_end = max((s.pc + s.size for s in statements), default=ENTRY)
    for access in result.accesses:
        if access.start is not None and access.length is not None and access.kind == "write":
            if access.start < payload_end and access.start + access.length > ENTRY:
                result.hazards.add(f"0x{access.pc:03X}: write into ROM payload at 0x{access.start:03X}")
            for code_pc in result.reachable:
                if access.start <= code_pc < access.start + access.length:
                    result.hazards.add(f"0x{access.pc:03X}: possible self-modifying write at 0x{code_pc:03X}")
    return result


def canonicalize(statements: list[Statement]) -> str:
    lines: list[str] = []
    for s in statements:
        if s.label:
            lines.append(f"{s.label}:")
        if s.op:
            args = ", ".join(s.args)
            lines.append(f"        {s.op}" + (f" {args}" if args else ""))
    return "\n".join(lines) + "\n"


def report(analysis: Analysis) -> dict:
    data_stmts = [s for s in analysis.statements if s.op in {".BYTE", ".WORD"}]
    blocks = sorted(leader for leader in analysis.leaders if leader in analysis.reachable)
    return {
        "entrypoint": "0x200",
        "labels": {name: f"0x{addr:03X}" for name, addr in sorted(analysis.labels.items(), key=lambda x: x[1])},
        "reachable_instruction_count": len(analysis.reachable),
        "reachable_bytes": 2 * len(analysis.reachable),
        "basic_block_leaders": [f"0x{x:03X}" for x in blocks],
        "edges": [[f"0x{a:03X}", f"0x{b:03X}"] for a, b in sorted(analysis.edges)],
        "declared_data_bytes": sum(s.size for s in data_stmts),
        "accesses": [{"pc": f"0x{pc:03X}", "kind": kind, "start": None if start is None else f"0x{start:03X}", "length": length, "reason": reason} for pc, kind, start, length, reason in sorted(set((x.pc, x.kind, x.start, x.length, x.reason) for x in analysis.accesses), key=str)],
        "hazards": sorted(analysis.hazards),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("analyze", "canonicalize"):
        p = sub.add_parser(name)
        p.add_argument("source", type=Path, help="assembler-compatible source")
        p.add_argument("-o", "--output", type=Path)
    sub.choices["analyze"].add_argument("--json", action="store_true", help="emit JSON")
    args = parser.parse_args()
    try:
        statements, labels = parse_source(args.source.read_text())
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.command == "canonicalize":
        output = canonicalize(statements)
    else:
        payload = report(analyze(statements, labels))
        if args.json:
            output = json.dumps(payload, indent=2) + "\n"
        else:
            output = "\n".join([
                "CHIP-8 optimizer analysis",
                f"reachable: {payload['reachable_instruction_count']} instructions ({payload['reachable_bytes']} bytes)",
                f"declared data: {payload['declared_data_bytes']} bytes",
                "basic blocks: " + ", ".join(payload["basic_block_leaders"]),
                "hazards: " + ("none" if not payload["hazards"] else "; ".join(payload["hazards"])),
            ]) + "\n"
    if args.output:
        args.output.write_text(output)
    else:
        sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
