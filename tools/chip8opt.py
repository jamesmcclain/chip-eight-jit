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
EXTERNAL_I = -1  # CHIP-8 font memory; never part of an assembled ROM payload.


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


def dynamic_base(base: int) -> int:
    return -(base + 2)


def is_dynamic_base(value: Optional[int]) -> bool:
    return value is not None and value <= -2


def decode_dynamic_base(value: int) -> int:
    return -value - 2


def add_access(a: Analysis, pc: int, kind: str, start: Optional[int], length: Optional[int], reason: str) -> None:
    # Fx29 points into the interpreter font below 0x200.  It is dynamic, but
    # cannot alias/refer to relocatable ROM data.
    if start == EXTERNAL_I:
        return
    if is_dynamic_base(start):
        a.accesses.append(Access(pc, f"dynamic-{kind}", decode_dynamic_base(start), length, reason))
        return
    a.accesses.append(Access(pc, kind, start, length, reason))
    if start is None:
        a.hazards.add(f"0x{pc:03X}: dynamic I for {reason}")


def call_i_summaries(insns: dict[int, Statement], labels: dict[str, int]) -> dict[int, Optional[int]]:
    """Very conservative summaries for straight-line leaf routines.

    ``None`` means the routine's I effect is unknown; a missing ``LD I`` means
    it preserves I.  Branching routines deliberately receive no summary.
    """
    summaries: dict[int, Optional[int]] = {}
    for call in insns.values():
        if call.op != "CALL":
            continue
        start = target(call, labels)
        if start is None or start in summaries:
            continue
        pc, last_i, safe = start, "preserve", True
        while pc in insns:
            stmt = insns[pc]
            if stmt.op == "LD" and len(stmt.args) == 2 and stmt.args[0] == "I":
                value = resolve(stmt.args[1], labels)
                if value is None:
                    safe = False
                    break
                last_i = value
            elif stmt.op == "ADD" and len(stmt.args) == 2 and stmt.args[0] == "I":
                if not isinstance(last_i, int) or last_i == EXTERNAL_I:
                    safe = False
                    break
                last_i = last_i if is_dynamic_base(last_i) else dynamic_base(last_i)
            elif stmt.op in {"JP", "CALL"}:
                safe = False
                break
            if stmt.op == "RET":
                break
            pc += 2
        if safe and pc in insns and insns[pc].op == "RET":
            summaries[start] = last_i  # type: ignore[assignment]
        else:
            summaries[start] = None
    return summaries


def analyze(statements: list[Statement], labels: dict[str, int]) -> Analysis:
    result = Analysis(labels, statements)
    insns = instruction_map(statements)
    summaries = call_i_summaries(insns, labels)
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
                i_value = EXTERNAL_I
            elif args[0] == "B" and args[1].startswith("V"):
                add_access(result, pc, "write", i_value, 3, "BCD store")
            elif args[0] == "[I]" and args[1].startswith("V"):
                add_access(result, pc, "write", i_value, int(args[1][1], 16) + 1, "register store")
            elif args[1] == "[I]" and args[0].startswith("V"):
                add_access(result, pc, "read", i_value, int(args[0][1], 16) + 1, "register load")
        elif op == "ADD" and len(args) == 2:
            if args[0] == "I":
                value = regs[int(args[1][1], 16)] if args[1].startswith("V") else None
                if i_value is None:
                    i_value = None
                elif is_dynamic_base(i_value):
                    i_value = i_value
                elif value is None:
                    i_value = dynamic_base(i_value)
                else:
                    i_value = i_value + value
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
                    effect = summaries.get(target(stmt, labels))
                    if effect == "preserve":
                        propagated = next_state
                    elif isinstance(effect, int):
                        propagated = (effect, (None,) * 16)
                    else:
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


def symbolize_in_rom_addresses(statements: list[Statement], labels: dict[str, int]) -> tuple[list[Statement], dict[str, int]]:
    """Replace direct numeric in-ROM references with generated symbolic labels.

    A label is inserted at the referenced statement boundary, so a compacting
    pass can move a sprite/data table along with every direct I reference.
    """
    payload_end = max((s.pc + s.size for s in statements), default=ENTRY)
    by_pc = {s.pc: s for s in statements}
    generated: dict[int, str] = {}
    for s in statements:
        if not (s.op in {"JP", "CALL"} or (s.op == "LD" and len(s.args) == 2 and s.args[0] == "I")):
            continue
        value = target(s, labels)
        if value is None or not ENTRY <= value < payload_end or value not in by_pc:
            continue
        if parse_number(s.args[-1]) is not None:
            generated.setdefault(value, f"ADDR{value:03X}")
    output: list[Statement] = []
    new_labels = dict(labels)
    for s in statements:
        name = generated.get(s.pc)
        if name:
            # A source label already supplies the symbolic relocation anchor.
            existing = next((label for label, address in labels.items() if address == s.pc), None)
            name = existing or name
            new_labels[name] = s.pc
            if not existing:
                output.append(Statement(s.line, name, None, [], s.pc, 0))
        copied = Statement(s.line, s.label, s.op, list(s.args), s.pc, s.size)
        # The reference target, not the instruction PC, selects the generated name.
        if copied.op and copied.args and parse_number(copied.args[-1]) is not None:
            value = target(s, labels)
            if value in generated:
                copied.args[-1] = next((label for label, address in new_labels.items() if address == value), generated[value])
        output.append(copied)
    return output, new_labels


def dynamic_base_boundaries(statements: list[Statement], labels: dict[str, int]) -> set[int]:
    """Return data-relative dynamic-I bases whose following layout is pinned.

    Deleting bytes before a symbolic base moves both the base and everything
    after it equally.  Deleting bytes at/after that base could change a
    data-relative dynamic offset, so those bytes are kept in place.
    """
    analysis = analyze(statements, labels)
    return {access.start for access in analysis.accesses if access.kind.startswith("dynamic-") and access.start is not None}


def relocation_hazards(statements: list[Statement], labels: dict[str, int]) -> list[str]:
    """Conditions that make deleting bytes unsafe without a whole-ROM relocator."""
    analysis = analyze(statements, labels)
    # Static writes to labelled payload data are relocatable too; only writes
    # that overlap executable bytes remain self-modifying-code blockers.  A
    # merged CFG state can retain an unknown path beside a proven symbolic-base
    # path; the latter pins that instruction's following layout.
    proven_dynamic_pcs = {access.pc for access in analysis.accesses if access.kind.startswith("dynamic-")}
    problems = [h for h in analysis.hazards if ("dynamic I" in h and int(h[2:5], 16) not in proven_dynamic_pcs) or "self-modifying" in h or "computed JP" in h]
    payload_end = max((s.pc + s.size for s in statements), default=ENTRY)
    for s in statements:
        if s.op == ".ORG":
            problems.append(f"0x{s.pc:03X}: .ORG fixes layout")
        if s.op in {"JP", "CALL"} or (s.op == "LD" and len(s.args) == 2 and s.args[0] == "I"):
            value = target(s, labels)
            # A numeric in-ROM address will not track the compacted layout.
            if s.args and parse_number(s.args[-1]) is not None and ENTRY <= (value or 0) < payload_end:
                problems.append(f"0x{s.pc:03X}: numeric in-ROM address")
    return sorted(set(problems))


def protected_pcs(statements: list[Statement], labels: dict[str, int]) -> set[int]:
    """PCs which must remain instruction boundaries for skip/control-flow entry."""
    protected = {ENTRY, *labels.values()}
    for s in statements:
        if s.op in {"JP", "CALL"} and not (s.op == "JP" and len(s.args) == 2):
            value = target(s, labels)
            if value is not None:
                protected.add(value)
        if is_skip(s):
            protected.update((s.pc + 2, s.pc + 4))
    return protected


def is_register(word: str) -> bool:
    return len(word) == 2 and word[0] == "V" and word[1] in "0123456789ABCDEF"


def optimize_peepholes(statements: list[Statement], labels: dict[str, int]) -> tuple[list[Statement], list[str]]:
    """Apply small, proven byte-removing rewrites to relocatable source."""
    statements, labels = symbolize_in_rom_addresses(statements, labels)
    problems = relocation_hazards(statements, labels)
    if problems:
        raise ValueError("cannot compact safely: " + "; ".join(problems))
    protected = protected_pcs(statements, labels)
    for base in dynamic_base_boundaries(statements, labels):
        protected.update(s.pc for s in statements if s.pc >= base)
    result: list[Statement] = []
    changes: list[str] = []
    i = 0
    while i < len(statements):
        s = statements[i]
        next_s = statements[i + 1] if i + 1 < len(statements) else None
        # Fold LD Vx, kk; ADD Vx, kk.  A branch/skip into the ADD would see a
        # different input value, so only remove an unprotected second word.
        if (next_s and s.op == "LD" and next_s.op == "ADD" and len(s.args) == len(next_s.args) == 2
                and is_register(s.args[0]) and s.args[0] == next_s.args[0]
                and parse_number(s.args[1]) is not None and parse_number(next_s.args[1]) is not None
                and next_s.label is None and next_s.pc not in protected):
            total = (parse_number(s.args[1]) + parse_number(next_s.args[1])) & 0xFF
            result.append(Statement(s.line, s.label, "LD", [s.args[0], f"0x{total:02X}"], s.pc, 2))
            changes.append(f"0x{s.pc:03X}: folded LD/ADD for {s.args[0]}")
            i += 2
            continue
        # These instructions have no side effects. Labels are retained as
        # boundaries; a later relocation pass can handle label coalescing.
        noop = (s.op == "LD" and len(s.args) == 2 and s.args[0] == s.args[1]) or (
            s.op == "ADD" and len(s.args) == 2 and is_register(s.args[0]) and parse_number(s.args[1]) == 0)
        if noop and s.label is None and s.pc not in protected:
            changes.append(f"0x{s.pc:03X}: removed no-op {s.op}")
            i += 1
            continue
        # An immediately overwritten pure load is dead.  Do not erase a label
        # or a skip/control-flow entry point.
        if (next_s and s.op == "LD" and next_s.op == "LD" and len(s.args) == len(next_s.args) == 2
                and s.args[0] == next_s.args[0] and (s.args[0] == "I" or is_register(s.args[0]))
                and s.args[1] != "K"
                and s.label is None and s.pc not in protected):
            changes.append(f"0x{s.pc:03X}: removed overwritten LD {s.args[0]}")
            i += 1
            continue
        result.append(s)
        i += 1
    return result, changes


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
    for name in ("analyze", "canonicalize", "optimize"):
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
    elif args.command == "optimize":
        try:
            optimized, changes = optimize_peepholes(statements, labels)
        except ValueError as exc:
            parser.error(str(exc))
        output = canonicalize(optimized)
        print(f"peephole optimization: {len(changes)} change(s)", file=sys.stderr)
        for change in changes:
            print(f"  {change}", file=sys.stderr)
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
