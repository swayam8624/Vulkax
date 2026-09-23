#!/usr/bin/env python3
"""Assemble a reproducible, paper-facing Vulkax evidence bundle.

This script copies canonical repository result documents and generated experiment
artifacts into one self-contained directory, computes SHA-256 hashes, and writes a
machine-readable manifest. It never changes scientific decisions.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
from datetime import datetime, timezone


CANONICAL = [
    ("research/results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md", "canonical/result-summary", True),
    ("research/results/DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv", "canonical/result-table", True),
    ("research/results/DCS_FINAL_RESULTS_2026-09-20.json", "canonical/dcs-result-ledger", True),
    ("research/results/VULKAX_FINAL_RESULTS_2026-09-21.json", "canonical/final-result-ledger", True),
    ("research/results/VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md", "canonical/final-research-summary", True),
    ("research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.md", "canonical/orthogonal-force-result", True),
    ("research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.json", "canonical/orthogonal-force-json", True),
    ("research/results/DCS_INFORMATION_FRONTIER_2026-09-20.md", "canonical/information-frontier-summary", True),
    ("research/results/DCS_INFORMATION_FRONTIER_2026-09-20.json", "canonical/information-frontier-json", True),
    ("research/results/DCS_INFORMATION_FRONTIER_2026-09-20.csv", "canonical/information-frontier-table", True),
    ("research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv", "canonical/gauge-paired-diagnostics", True),
    ("research/status/CURRENT_RESEARCH_STATE_2026-09-21.md", "canonical/current-state", True),
    ("research/status/CURRENT_RESEARCH_STATE_2026-09-20.md", "canonical/historical-current-state", False),
    ("research/status/DCS_IMPLEMENTATION_COMPLETE_2026-09-20.md", "canonical/implementation", True),
    ("research/status/DCS_D2_VALIDATION_RESULT.md", "canonical/d2-result", True),
    ("research/status/DCS_D3_RESULT_2026-09-20.md", "canonical/d3-result", True),
    ("research/status/DCS_D4V_RESULT_2026-09-20.md", "canonical/d4v-result", True),
    ("research/status/DCS_GAUGE_RETROSPECTIVE_RESULT_2026-09-20.md", "canonical/gauge-result", True),
    ("research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md", "canonical/limitations", True),
    ("research/literature/CLAIM_GUARD.md", "canonical/claim-guard", True),
    ("research/literature/DCS_NOVELTY_THREAT_MAP.md", "canonical/novelty-map", True),
    ("research/benchmarks/DCS_D2_VALIDATION_PROTOCOL.md", "canonical/d2-protocol", True),
    ("research/benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md", "canonical/d3-protocol", True),
    ("research/benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md", "canonical/d4v-protocol", True),
    ("research/benchmarks/DCS_BENCHMARK_PLAN.md", "canonical/benchmark-plan", True),
    ("research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md", "canonical/orthogonal-force-protocol", True),
    ("research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md", "canonical/math-code-audit", True),
    ("research/status/PAPER_FREEZE_2026-09-21.md", "canonical/paper-freeze", True),
    ("research/paper_data/PUBLICATION_ASSET_LOCK.md", "canonical/publication-asset-lock", True),
    ("research/paper_data/PAPER_POSITIONING.md", "canonical/paper-positioning", True),
    ("research/paper_data/README.md", "canonical/paper-data-readme", True),
    ("research/paper_data/PAPER_DATA_MANIFEST.json", "canonical/paper-data-manifest", True),
    ("research/paper_data/FIGURE_TABLE_SOURCE_MAP.md", "canonical/figure-table-map", True),
    ("research/paper_data/EXPERIMENT_MATRIX.csv", "canonical/experiment-matrix", True),
    ("research/paper_data/ABLATION_MATRIX.md", "canonical/ablation-matrix", True),
    ("research/paper_data/REPRODUCIBILITY_CHECKLIST.md", "canonical/reproducibility-checklist", True),
    ("research/validation/protocol_v1.json", "canonical/publication-validation-protocol", True),
    ("research/validation/record_schema_v1.json", "canonical/publication-validation-schema", True),
    ("research/validation/iris_pendulum_final_test_v1.json", "canonical/iris-pendulum-final-test-config", True),
    ("research/validation/iris_pendulum_final_world_manifest_v1.csv", "canonical/iris-pendulum-final-world-manifest", True),
    ("research/validation/README.md", "canonical/publication-validation-guide", True),
    ("research/validation/DATASET_ADAPTER_CONTRACT.md", "canonical/publication-validation-adapter-contract", True),
    ("research/validation/world_manifest.example.csv", "canonical/publication-validation-manifest-template", True),
    ("docs/PUBLICATION_VALIDATION.md", "canonical/publication-validation-execution-guide", True),
    ("research/benchmarks/REAL_MEASURED_VALIDATION_PROTOCOL_2026-09-23.md", "canonical/measured-validation-protocol", True),
    ("research/benchmarks/IRIS_PENDULUM_PROSPECTIVE_VALIDATION_PROTOCOL_2026-09-23.md", "canonical/iris-pendulum-validation-protocol", True),
    ("research/status/GAUGE_PROSPECTIVE_VALIDATION_RESULT_2026-09-23.md", "canonical/gauge-prospective-validation-result", True),
    ("research/status/IRIS_PENDULUM_PROSPECTIVE_VALIDATION_RESULT_2026-09-23.md", "canonical/iris-pendulum-validation-result", True),
    ("research/literature/VALIDATION_RELATED_WORK_MAP_2026-09-23.md", "canonical/validation-related-work-map", True),
    ("docs/PAPER_EVIDENCE_REPRODUCTION.md", "canonical/reproduction-guide", True),
]

GENERATED = [
    ("dcs-positive-control/analysis.json", "generated/d1-analysis", True),
    ("dcs-positive-control/cases.csv", "generated/d1-cases", False),
    ("dcs-solver-native/analysis.json", "generated/solver-native-analysis", True),
    ("dcs-solver-native/cases.csv", "generated/solver-native-cases", False),
    ("dcs-active-selection/analysis.json", "generated/active-selection-analysis", True),
    ("dcs-active-selection/cases.csv", "generated/active-selection-cases", False),
    ("dcs-d2-validation/analysis.json", "generated/d2-analysis", True),
    ("dcs-d2-validation/cases.csv", "generated/d2-cases", True),
    ("dcs-d2-validation/stencils.csv", "generated/d2-stencils", True),
    ("dcs-d3-discovery/analysis.json", "generated/d3-analysis", True),
    ("dcs-d3-discovery/cases.csv", "generated/d3-cases", True),
    ("dcs-d3-discovery/stencils.csv", "generated/d3-stencils", True),
    ("dcs-d3-discovery/method_summary.csv", "generated/d3-method-summary", True),
    ("dcs-d3-discovery/truth_summary.csv", "generated/d3-truth-summary", True),
    ("dcs-d4v-discovery/analysis.json", "generated/d4v-analysis", True),
    ("dcs-d4v-discovery/proposals.csv", "generated/d4v-proposals", True),
    ("orthogonal-force-compliance/analysis.json", "generated/orthogonal-force-analysis", True),
    ("orthogonal-force-compliance/proposals.csv", "generated/orthogonal-force-proposals", True),
    ("orthogonal-force-compliance/summary.json", "generated/orthogonal-force-summary", True),
    ("paper-captured-world-run/certificate.json", "generated/captured-world-certificate", True),
    ("paper-reproduction-validation.json", "generated/reproduction-validation", True),
    ("paper-dcs-evidence-index.csv", "generated/dcs-evidence-index", True),
    ("paper-diagnostics/information_frontier.json", "generated/information-frontier", True),
    ("paper-diagnostics/d4v_information_frontier.csv", "generated/d4v-information-frontier", True),
    ("paper-diagnostics/gauge_paired_diagnostics.csv", "generated/gauge-paired-diagnostics", True),
    ("paper-performance/captured_world_performance.csv", "generated/performance-csv", False),
    ("paper-performance/captured_world_performance_summary.json", "generated/performance-summary", False),
    ("gauge-effective-span/validation.json", "generated/gauge-effective-span-validation", True),
    ("gauge-effective-span/effective_span.csv", "generated/gauge-effective-span-table", False),
    ("gauge-dcs-retrospective/summary.json", "generated/gauge-retrospective-summary", True),
    ("gauge-dcs-retrospective/per_trial.csv", "generated/gauge-retrospective-per-trial", True),
    ("publication-validation/records.csv", "generated/publication-validation-records", True),
    ("publication-validation/analysis/summary.json", "generated/publication-validation-summary", True),
    ("publication-validation/analysis/method_summary.csv", "generated/publication-validation-method-summary", True),
    ("publication-validation/analysis/risk_coverage.csv", "generated/publication-validation-risk-coverage", True),
    ("publication-validation/analysis/failure_cases.csv", "generated/publication-validation-failures", True),
    ("publication-validation/analysis/transaction_utility.csv", "generated/publication-validation-transaction-utility", True),
    ("publication-validation/sample_size_plan.json", "generated/publication-validation-sample-size", True),
    ("publication-validation/public-data/dataset_inventory.csv", "generated/public-data-inventory", False),
    ("publication-validation/public-data/world_manifest.csv", "generated/public-data-world-manifest", False),
    ("publication-validation/public-data/dataset_truth_index.json", "generated/public-data-truth-index", False),
    ("publication-validation/public-data/preparation_report.json", "generated/public-data-preparation-report", False),
    ("publication-validation/public-data/trial_plan.csv", "generated/public-data-trial-plan", False),
    ("publication-validation/public-data/adapted/adapter_summary.json", "generated/public-data-adapter-summary", False),
    ("publication-validation/gauge-prospective-validation/forensics/summary.json", "generated/gauge-validation-failure-forensics", False),
    ("publication-validation/gauge-prospective-validation/forensics/per_case.csv", "generated/gauge-validation-failure-per-case", False),
    ("publication-validation/iris-pendulum-development/summary.json", "generated/iris-pendulum-development-summary", False),
    ("publication-validation/iris-pendulum-validation/summary.json", "generated/iris-pendulum-validation-summary", False),
    ("publication-validation/iris-pendulum-validation/validation_records.csv", "generated/iris-pendulum-validation-records", False),
    ("publication-validation/iris-pendulum-validation/forensics/summary.json", "generated/iris-pendulum-validation-forensics", False),
    ("publication-validation/iris-pendulum-validation/forensics/per_factor.csv", "generated/iris-pendulum-validation-per-factor", False),
    ("publication-validation/iris-pendulum-validation/forensics/paired_comparison.csv", "generated/iris-pendulum-validation-paired-comparison", False),
    ("publication-validation/combined-iris-validation/analysis/summary.json", "generated/combined-iris-validation-summary", False),
    ("publication-validation/iris-pendulum-final-lock.json", "generated/iris-pendulum-final-lock", False),
    ("publication-validation/iris-pendulum-final-test/final_summary.json", "generated/iris-pendulum-final-summary", False),
    ("publication-validation/iris-pendulum-final-test/final_per_factor.csv", "generated/iris-pendulum-final-per-factor", False),
    ("publication-validation/iris-pendulum-final-test/validation_records.csv", "generated/iris-pendulum-final-records", False),
    ("publication-validation/combined-iris-final/analysis/summary.json", "generated/combined-iris-final-summary", False),
]

def sha256(path: Path) -> str:
    h=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1024*1024),b""):
            h.update(chunk)
    return h.hexdigest()

def git_text(root: Path, *args: str) -> str:
    try:
        return subprocess.check_output(["git", *args], cwd=root, text=True, stderr=subprocess.DEVNULL).strip()
    except Exception:
        return ""

def add_file(src: Path, dest: Path, source_label: str, category: str, required: bool, artifacts: list[dict]) -> None:
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(src,dest)
    artifacts.append({
        "source": source_label,
        "bundle_path": str(dest),
        "category": category,
        "required": required,
        "bytes": dest.stat().st_size,
        "sha256": sha256(dest),
    })

def assemble(repo_root: Path, build_root: Path, out: Path, allow_missing_gauge: bool) -> dict:
    out=out.resolve()
    out.mkdir(parents=True,exist_ok=True)
    canonical_root=out/"canonical"
    generated_root=out/"generated"
    runtime_root=out/"runtime"
    for d in (canonical_root,generated_root,runtime_root):
        d.mkdir(parents=True,exist_ok=True)

    artifacts=[]
    missing=[]

    for rel,category,required in CANONICAL:
        src=repo_root/rel
        if not src.is_file():
            if required: missing.append(rel)
            continue
        add_file(src,canonical_root/rel,rel,category,required,artifacts)

    for rel,category,required in GENERATED:
        gauge=rel.startswith("gauge-") or rel.endswith("gauge_paired_diagnostics.csv")
        effective_required=required and not (allow_missing_gauge and gauge)
        src=build_root/rel
        if not src.is_file():
            if effective_required: missing.append("build/"+rel)
            continue
        add_file(src,generated_root/rel,"build/"+rel,category,effective_required,artifacts)

    paper_figures=build_root/"paper-figures"
    if not (paper_figures/"figure_manifest.json").is_file():
        missing.append("build/paper-figures/figure_manifest.json")
    elif paper_figures.is_dir():
        for src in sorted(p for p in paper_figures.rglob("*") if p.is_file()):
            rel=src.relative_to(paper_figures)
            add_file(
                src,
                generated_root/"paper-figures"/rel,
                "build/paper-figures/"+str(rel),
                "generated/paper-asset",
                src.name=="figure_manifest.json",
                artifacts,
            )

    # Copy system provenance/logs when the runner produced them outside this out dir.
    target_sys=out/"system/system-info.txt"
    candidate_sys=target_sys if target_sys.is_file() else build_root/"paper-evidence/system/system-info.txt"
    if candidate_sys.is_file():
        if candidate_sys.resolve()!=target_sys.resolve():
            add_file(candidate_sys,target_sys,str(candidate_sys),"runtime/system-info",False,artifacts)
        else:
            artifacts.append({
                "source": str(candidate_sys),
                "bundle_path": str(target_sys),
                "category": "runtime/system-info",
                "required": False,
                "bytes": target_sys.stat().st_size,
                "sha256": sha256(target_sys),
            })

    target_logs=out/"logs"
    candidate_logs=target_logs if target_logs.is_dir() else build_root/"paper-evidence/logs"
    if candidate_logs.is_dir():
        for src in sorted(candidate_logs.glob("*.log")):
            dest=target_logs/src.name
            if src.resolve()!=dest.resolve():
                add_file(src,dest,str(src),"runtime/log",False,artifacts)
            else:
                artifacts.append({
                    "source": str(src),
                    "bundle_path": str(dest),
                    "category": "runtime/log",
                    "required": False,
                    "bytes": dest.stat().st_size,
                    "sha256": sha256(dest),
                })

    for item in artifacts:
        try:
            item["bundle_path"]=str(Path(item["bundle_path"]).resolve().relative_to(out))
        except ValueError:
            item["bundle_path"]=str(item["bundle_path"])

    with (out/"SHA256SUMS").open("w") as sums:
        for item in sorted(artifacts,key=lambda x:x["bundle_path"]):
            sums.write(f"{item['sha256']}  {item['bundle_path']}\n")

    complete=not missing
    manifest={
        "schema":"vulkax.paper_evidence_bundle",
        "version":1,
        "created_utc":datetime.now(timezone.utc).isoformat(),
        "repo_commit":git_text(repo_root,"rev-parse","HEAD"),
        "repo_branch":git_text(repo_root,"branch","--show-current"),
        "repo_dirty":bool(git_text(repo_root,"status","--porcelain")),
        "platform":platform.platform(),
        "python":platform.python_version(),
        "allow_missing_gauge":allow_missing_gauge,
        "complete":complete,
        "missing_required":missing,
        "scientific_disposition":"research_program_frozen_information_limit_result",
        "claim_guard":"D2/D3/D4V remain frozen negative; the fresh force-compliance channel improves signal but remains unresolved; GAUGE is retrospective only.",
        "artifacts":artifacts,
    }
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2,sort_keys=True)+"\n")

    with (out/"artifact_index.csv").open("w",newline="") as f:
        fieldnames=["source","bundle_path","category","required","bytes","sha256"]
        w=csv.DictWriter(f,fieldnames=fieldnames)
        w.writeheader()
        for row in artifacts:
            w.writerow({k:row[k] for k in fieldnames})

    readme=(
        "# Vulkax Paper Evidence Bundle\n\n"
        f"Generated from commit: {manifest['repo_commit'] or 'unknown'}\n\n"
        f"Complete for requested profile: {str(complete).lower()}\n\n"
        "## Scientific status\n\n"
        "This bundle reproduces and packages the current Vulkax/DCS evidence. It does not\n"
        "convert negative or retrospective results into a positive prospective claim.\n\n"
        "- D2: frozen negative validation\n"
        "- D3: negative adaptive-order discovery\n"
        "- D4V: negative repair-veto discovery\n"
        "- GAUGE: retrospective mechanism-channel contradiction\n"
        "- D5: replay infrastructure exists, but no fresh positive measured confirmation is claimed\n\n"
        "## Layout\n\n"
        "- canonical/ — committed protocols, result ledgers, claim guards and limitations\n"
        "- generated/ — fresh outputs from the current machine/run\n"
        "- logs/ — command logs when available\n"
        "- system/ — hardware/software provenance when available\n"
        "- manifest.json — SHA-256 indexed artifact manifest\n"
        "- artifact_index.csv — flat artifact table\n"
        "- SHA256SUMS — portable checksums for every indexed artifact\n\n"
        f"Missing required artifacts: {len(missing)}\n"
    )
    if missing:
        readme+="\n## Missing\n\n" + "\n".join("- "+m for m in missing) + "\n"
    (out/"README.md").write_text(readme)

    if not complete:
        raise RuntimeError("paper evidence bundle incomplete: "+", ".join(missing))
    return manifest

def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root=Path(td)
        repo=root/"repo"; out=root/"out"
        repo.mkdir()
        a=repo/"x.txt"; a.write_text("abc\n")
        artifacts=[]
        add_file(a,out/"canonical/x.txt","x.txt","canonical/test",True,artifacts)
        assert artifacts[0]["sha256"]==hashlib.sha256(b"abc\n").hexdigest()
        assert (out/"canonical/x.txt").read_text()=="abc\n"
        print("VALID paper-evidence assembler self-test")

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--repo-root",default=".")
    ap.add_argument("--build-root",default="build")
    ap.add_argument("--out",default="build/paper-evidence")
    ap.add_argument("--allow-missing-gauge",action="store_true")
    ap.add_argument("--self-test",action="store_true")
    args=ap.parse_args()
    if args.self_test:
        self_test(); return 0
    repo=Path(args.repo_root).resolve()
    build=(repo/args.build_root).resolve() if not Path(args.build_root).is_absolute() else Path(args.build_root).resolve()
    out=(repo/args.out).resolve() if not Path(args.out).is_absolute() else Path(args.out).resolve()
    m=assemble(repo,build,out,args.allow_missing_gauge)
    print("VALID paper evidence bundle")
    print("OUT",out)
    print("ARTIFACTS",len(m["artifacts"]))
    print("COMMIT",m["repo_commit"])
    return 0

if __name__=="__main__":
    raise SystemExit(main())
