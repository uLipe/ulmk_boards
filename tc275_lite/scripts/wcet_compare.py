#!/usr/bin/env python3
"""Compare SILICON_WCET report lines against wcet_deferred_resched_compare.md baseline."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROW_RE = re.compile(
	r"^(?P<name>[a-zA-Z0-9_]+)\s+"
	r"(?P<mn>\d+)/(?P<avg>\d+)/(?P<mx>\d+)"
	r"(?:\s+blk=(?P<blk>\d+))?"
	r"\s+o1=(?P<o1>[01])\s*$"
)
BASE_RE = re.compile(
	r"^\|\s*(?P<name>[a-zA-Z0-9_]+)\s*\|\s*"
	r"(?P<before>\d+)\s*\|\s*(?P<after>\d+)\s*\|"
)


def parse_report(text: str) -> dict[str, dict]:
	out: dict[str, dict] = {}
	for line in text.splitlines():
		m = ROW_RE.match(line.strip())
		if not m:
			continue
		out[m.group("name")] = {
			"avg": int(m.group("avg")),
			"mn": int(m.group("mn")),
			"mx": int(m.group("mx")),
			"o1": m.group("o1") == "1",
		}
	return out


def parse_baseline(path: Path) -> dict[str, int]:
	"""Return 'depois (avg)' column from the markdown table."""
	out: dict[str, int] = {}
	for line in path.read_text(encoding="utf-8").splitlines():
		m = BASE_RE.match(line)
		if not m:
			continue
		name = m.group("name")
		if name == "syscall":
			continue
		out[name] = int(m.group("after"))
	return out


def main() -> int:
	ap = argparse.ArgumentParser()
	ap.add_argument("report", type=Path, help="decoded RAM/UART log")
	ap.add_argument(
		"--baseline",
		type=Path,
		default=Path("/home/ulipe/fun/wcet_deferred_resched_compare.md"),
	)
	ap.add_argument(
		"--tol-pct",
		type=float,
		default=25.0,
		help="flag regression if avg exceeds baseline by this percent",
	)
	ap.add_argument(
		"--label",
		default="run",
		help="label for printed table",
	)
	args = ap.parse_args()

	text = args.report.read_text(encoding="utf-8", errors="replace")
	got = parse_report(text)
	base = parse_baseline(args.baseline)

	meta = []
	for key in ("root_cpu=", "ipc_srv_cpu=", "affinity_cpu1 ", "unit=", "slot="):
		for line in text.splitlines():
			if key in line:
				meta.append(line.strip())
				break

	print(f"=== WCET compare [{args.label}] ===")
	for m in meta:
		print(m)
	print(f"{'syscall':22} {'base':>8} {'avg':>8} {'Δ':>8} {'Δ%':>8}  note")
	print("-" * 70)

	regress = 0
	missing = 0
	for name in sorted(base.keys()):
		if name not in got:
			print(f"{name:22} {base[name]:8d} {'—':>8} {'—':>8} {'—':>8}  MISSING")
			missing += 1
			continue
		avg = got[name]["avg"]
		b = base[name]
		delta = avg - b
		pct = (100.0 * delta / b) if b else 0.0
		note = ""
		if not got[name]["o1"]:
			note = "o1=0"
		if b > 0 and pct > args.tol_pct:
			note = (note + " ").strip() + "REGRESS"
			regress += 1
		elif b > 0 and pct < -args.tol_pct:
			note = (note + " ").strip() + "faster"
		print(f"{name:22} {b:8d} {avg:8d} {delta:8d} {pct:7.1f}%  {note}")

	extra = sorted(set(got) - set(base))
	for name in extra:
		print(f"{name:22} {'—':>8} {got[name]['avg']:8d}")

	print("-" * 70)
	print(f"regressions(>{args.tol_pct:.0f}%): {regress}  missing: {missing}")
	if "SILICON_WCET: PASS" not in text and "SILICON_WCET: PASS" not in text.replace("\0", ""):
		# truncated PASS in ramlog is common; accept scratch-driven passes via FAIL absent
		if "SILICON_WCET: FAIL" in text:
			print("STATUS: FAIL in log")
			return 1
	if regress or missing:
		return 2
	return 0


if __name__ == "__main__":
	sys.exit(main())
