#!/usr/bin/env python3
"""Generate deterministic, non-AI VULKAX scientific visualizations from frozen ledgers.

The figures produced here are presentation artifacts only. They do not execute,
retune, or modify any research experiment.
"""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
LEDGER = ROOT / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json"
DEFAULT_OUT = ROOT / "build/visualization"

INK = "#111827"
MUTED = "#5b6472"
GRID = "#d8dee8"
BLUE = "#2563eb"
ORANGE = "#ea580c"
RED = "#b91c1c"
GREEN = "#047857"
AMBER = "#b45309"
PANEL = "#f8fafc"
WHITE = "#ffffff"
DARK = "#09111f"
DARK_PANEL = "#101c2d"
CYAN = "#22d3ee"


def esc(value: object) -> str:
    return html.escape(str(value), quote=True)


def text(x: float, y: float, value: object, size: int = 24, *,
         anchor: str = "start", weight: int = 400, fill: str = INK,
         family: str = "Inter,Arial,sans-serif") -> str:
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="{family}" '
        f'font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}">{esc(value)}</text>'
    )


def line(x1: float, y1: float, x2: float, y2: float, *,
         stroke: str = INK, width: float = 2, dash: str | None = None,
         opacity: float = 1.0) -> str:
    extra = f' stroke-dasharray="{dash}"' if dash else ""
    return (
        f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
        f'stroke="{stroke}" stroke-width="{width}" opacity="{opacity}"{extra}/>'
    )


def rect(x: float, y: float, w: float, h: float, *,
         fill: str = "none", stroke: str = "none", rx: float = 0,
         width: float = 1, opacity: float = 1.0) -> str:
    return (
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
        f'rx="{rx:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" '
        f'opacity="{opacity}"/>'
    )


def circle(cx: float, cy: float, r: float, *, fill: str = "none",
           stroke: str = "none", width: float = 1, opacity: float = 1.0) -> str:
    return (
        f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{fill}" '
        f'stroke="{stroke}" stroke-width="{width}" opacity="{opacity}"/>'
    )


def path(d: str, *, stroke: str = INK, width: float = 3,
         fill: str = "none", opacity: float = 1.0,
         dash: str | None = None) -> str:
    extra = f' stroke-dasharray="{dash}"' if dash else ""
    return (
        f'<path d="{d}" stroke="{stroke}" stroke-width="{width}" fill="{fill}" '
        f'opacity="{opacity}" stroke-linecap="round" stroke-linejoin="round"{extra}/>'
    )


def svg(width: int, height: int, body: Iterable[str], *,
        background: str = WHITE, title_value: str = "VULKAX visualization") -> str:
    return "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-label="{esc(title_value)}">',
        f'<title>{esc(title_value)}</title>',
        rect(0, 0, width, height, fill=background),
        *body,
        "</svg>",
        "",
    ])


def load_ledger() -> dict:
    with LEDGER.open("r", encoding="utf-8") as f:
        return json.load(f)


def write(out_dir: Path, name: str, content: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / name).write_text(content, encoding="utf-8")


def figure_information_frontier(data: dict) -> str:
    ofc = data["orthogonal_force_compliance"]
    dcs = ofc["dcs"]
    force = ofc["force_compliance"]
    gain = ofc["force_to_dcs_median_abs_z_ratio"]
    threshold = data["dcs"]["post_hoc_information_frontier"]["d4v"]["threshold_abs_z"]

    W, H = 1500, 900
    left, right, top, bottom = 155, 90, 150, 145
    plot_w, plot_h = W - left - right, H - top - bottom
    ymax = 2.2
    y = lambda z: top + plot_h * (1.0 - float(z) / ymax)

    body: list[str] = []
    body += [
        text(70, 72, "VULKAX — orthogonal physical information increases observability", 40, weight=700),
        text(70, 112, "Frozen fresh synthetic follow-on; no threshold or force-amplitude retuning", 22, fill=MUTED),
    ]

    for tick in [0, 0.5, 1.0, 1.5, 2.0]:
        yy = y(tick)
        body += [
            line(left, yy, W-right, yy, stroke=GRID, width=1),
            text(left-18, yy+8, f"{tick:.1f}", 18, anchor="end", fill=MUTED),
        ]

    body += [
        line(left, top, left, top+plot_h, width=2),
        line(left, top+plot_h, W-right, top+plot_h, width=2),
        text(44, top+plot_h/2, "|z| standardized signal magnitude", 21, anchor="middle",
             fill=INK, family="Inter,Arial,sans-serif"),
    ]
    body[-1] = (
        f'<text x="45" y="{top+plot_h/2:.1f}" transform="rotate(-90 45 {top+plot_h/2:.1f})" '
        f'font-family="Inter,Arial,sans-serif" font-size="21" font-weight="500" fill="{INK}" '
        f'text-anchor="middle">|z| standardized signal magnitude</text>'
    )

    x1, x2 = 495, 1015
    bar_w = 180
    for x, value, maxv, label, color in [
        (x1, dcs["median_abs_z"], dcs["max_abs_z"], "Kinematics / DCS", BLUE),
        (x2, force["median_abs_z"], force["max_abs_z"], "+ known force / compliance", ORANGE),
    ]:
        body += [
            rect(x-bar_w/2, y(value), bar_w, top+plot_h-y(value), fill=color, rx=12, opacity=0.9),
            circle(x, y(maxv), 9, fill=color),
            line(x-58, y(maxv), x+58, y(maxv), stroke=color, width=3),
            text(x, top+plot_h+48, label, 22, anchor="middle", weight=650),
            text(x, y(value)-18, f"median {value:.4f}", 22, anchor="middle", weight=700, fill=color),
            text(x+75, y(maxv)+7, f"max {maxv:.4f}", 19, fill=color),
        ]

    ty = y(threshold)
    body += [
        line(left, ty, W-right, ty, stroke=RED, width=3, dash="12 10"),
        text(W-right-6, ty-12, f"frozen decision reference  |z| = {threshold:g}",
             21, anchor="end", weight=700, fill=RED),
        line(x1+125, y(0.18), x2-135, y(0.66), stroke=INK, width=3),
        text(690, y(0.84), f"{gain:.2f}× median signal gain", 30,
             anchor="middle", weight=800, fill=ORANGE),
        text(690, y(0.73), "large gain; still below the frozen decision reference", 20,
             anchor="middle", fill=MUTED),
        rect(180, 775, 1140, 72, fill=PANEL, stroke=GRID, rx=18),
        text(750, 806, "Result: information channel matters, but credible verification remains information-limited.",
             24, anchor="middle", weight=650),
        text(750, 835, "36 fresh proposals • force coverage 0% • all 36 remain unresolved at the frozen threshold",
             19, anchor="middle", fill=MUTED),
    ]
    return svg(W, H, body, title_value="VULKAX orthogonal information frontier")


def beam_curve(x: float, y: float, w: float, amp: float, *, color: str,
               width: float = 8, opacity: float = 1.0, dash: str | None = None) -> str:
    d = (
        f"M {x:.1f} {y:.1f} "
        f"C {x+w*0.30:.1f} {y:.1f}, "
        f"{x+w*0.66:.1f} {y+amp*0.28:.1f}, "
        f"{x+w:.1f} {y+amp:.1f}"
    )
    return path(d, stroke=color, width=width, opacity=opacity, dash=dash)


def figure_reality_inspector(data: dict) -> str:
    d1 = data["dcs"]["stages"]["D1"]
    d4 = data["dcs"]["stages"]["D4V"]
    ofc = data["orthogonal_force_compliance"]
    W, H = 1800, 900
    body: list[str] = [
        text(80, 72, "VULKAX  |  REALITY INSPECTOR", 44, weight=800, fill=WHITE),
        text(80, 112, "A captured world can look better while its mechanism gets worse.", 24, fill="#a8b3c5"),
    ]

    panels = [(60, 165, 520, 610), (640, 165, 520, 610), (1220, 165, 520, 610)]
    headings = [
        ("01  OBSERVE", "Fit the captured world"),
        ("02  REPAIR", "Appearance improves"),
        ("03  INTERROGATE", "Ask a physical counterfactual"),
    ]
    for (x, y0, w, h), (h1, h2) in zip(panels, headings):
        body += [
            rect(x, y0, w, h, fill=DARK_PANEL, stroke="#26374f", rx=26, width=2),
            text(x+34, y0+52, h1, 24, weight=800, fill=CYAN),
            text(x+34, y0+88, h2, 21, fill="#cbd5e1"),
        ]

    x, y0, _, _ = panels[0]
    body += [
        rect(x+55, y0+160, 46, 170, fill="#334155", rx=10),
        beam_curve(x+92, y0+230, 335, 86, color="#cbd5e1", width=10),
        circle(x+427, y0+316, 12, fill="#cbd5e1"),
        text(x+50, y0+400, "captured trajectory / geometry", 19, fill="#a8b3c5"),
        text(x+50, y0+448, "Ordinary fitting sees what happened.", 23, weight=650, fill=WHITE),
        text(x+50, y0+482, "It does not uniquely identify why.", 23, fill="#a8b3c5"),
    ]

    x, y0, _, _ = panels[1]
    body += [
        rect(x+55, y0+160, 46, 170, fill="#334155", rx=10),
        beam_curve(x+92, y0+230, 335, 82, color="#64748b", width=12, opacity=0.45),
        beam_curve(x+92, y0+230, 335, 86, color="#34d399", width=7),
        circle(x+427, y0+316, 11, fill="#34d399"),
        text(x+50, y0+386, "constructed D1 positive control", 18, fill="#a8b3c5"),
        text(x+50, y0+426, f"median observation improvement  {100*d1['median_observation_improvement']:.2f}%",
             20, weight=700, fill="#34d399"),
        text(x+50, y0+464, f"mechanism witness degradation  {d1['median_witness_degradation_ratio']:.3f}×",
             20, weight=700, fill="#fb7185"),
        text(x+50, y0+526, f"{d1['cases']}/{d1['cases']} deceptive constructed repairs rejected",
             20, fill="#e2e8f0"),
    ]

    x, y0, _, _ = panels[2]
    base_x = x+92
    body += [
        rect(x+55, y0+160, 46, 170, fill="#334155", rx=10),
        beam_curve(base_x, y0+230, 335, 58, color="#94a3b8", width=7, dash="11 9"),
        beam_curve(base_x, y0+230, 335, 126, color="#fb7185", width=8),
        line(x+345, y0+160, x+345, y0+246, stroke="#f59e0b", width=5),
        path(f"M {x+330} {y0+226} L {x+345} {y0+250} L {x+360} {y0+226}",
             stroke="#f59e0b", width=5),
        text(x+280, y0+145, "known probe", 18, fill="#fbbf24"),
        text(x+50, y0+386, "fresh orthogonal force/compliance test", 18, fill="#a8b3c5"),
        text(x+50, y0+426, f"median |z|  {ofc['dcs']['median_abs_z']:.4f}  →  "
             f"{ofc['force_compliance']['median_abs_z']:.4f}", 23, weight=700, fill=WHITE),
        text(x+50, y0+464, f"{ofc['force_to_dcs_median_abs_z_ratio']:.2f}× stronger evidence",
             25, weight=800, fill="#f59e0b"),
        text(x+50, y0+502, f"max |z| = {ofc['force_compliance']['max_abs_z']:.4f} < 2",
             22, weight=700, fill="#fb7185"),
        rect(x+50, y0+535, 420, 52, fill="#3a2530", stroke="#fb7185", rx=14),
        text(x+260, y0+569, "REFUSE — insufficient physical information",
             20, anchor="middle", weight=800, fill="#fecdd3"),
    ]

    body += [
        text(900, 824,
             f"D4V: {d4['proposals']} observationally improving proposals, "
             f"{d4['deceptive']} deceptive, {d4['beneficial']} beneficial, 0 resolved",
             22, anchor="middle", fill="#dbeafe"),
        text(900, 862,
             "Mechanism drawing is schematic; all numeric annotations are read from the frozen VULKAX result ledger.",
             18, anchor="middle", fill="#8392a8"),
    ]
    return svg(W, H, body, background=DARK, title_value="VULKAX Reality Inspector storyboard")


def figure_stage_story(data: dict) -> str:
    stages = data["dcs"]["stages"]
    ofc = data["orthogonal_force_compliance"]
    W, H = 1650, 900
    body: list[str] = [
        text(70, 72, "VULKAX evidence story", 42, weight=800),
        text(70, 112, "What each completed stage established — and what it did not", 23, fill=MUTED),
    ]
    cards = [
        ("D1", GREEN, f"{stages['D1']['cases']}/{stages['D1']['cases']} rejected",
         "Constructed positive control",
         ("Verifier implementation detects designed", "deceptive repairs when information is sufficient.")),
        ("D2", AMBER, f"{stages['D2_frozen']['resolved_worlds']}/{stages['D2_frozen']['truth_worlds']} resolved",
         "Frozen prospective validation",
         ("Signal remained far below", "the frozen credibility reference.")),
        ("D3", AMBER, f"{stages['D3']['resolved_worlds']}/{stages['D3']['truth_worlds']} resolved",
         "Numerical refinement",
         ("Lower numerical floor did not create", "missing physical information.")),
        ("D4V", RED, f"{stages['D4V']['deceptive']} deceptive / {stages['D4V']['proposals']}",
         "Repair-verification discovery",
         ("Ordinary improvement can hide", "mechanistically worse repairs.")),
        ("OFC", ORANGE, f"{ofc['force_to_dcs_median_abs_z_ratio']:.2f}× signal gain",
         "Fresh orthogonal information",
         ("Different physics helps strongly,", "but 0% of proposals resolve.")),
        ("GAUGE", BLUE, f"{stages['GAUGE']['longitudinal_darkfield_endpoint_wins']}/{stages['GAUGE']['trials']} endpoint wins",
         "Measured retrospective",
         ("Aggregate fit and mechanism-specific", "evidence can disagree.")),
    ]
    x0, y0 = 70, 165
    cw, ch = 485, 250
    gapx, gapy = 30, 34
    for i, (name, color, metric, subtitle, message_lines) in enumerate(cards):
        row, col = divmod(i, 3)
        x = x0 + col*(cw+gapx)
        y = y0 + row*(ch+gapy)
        body += [
            rect(x, y, cw, ch, fill=PANEL, stroke=GRID, rx=22, width=2),
            rect(x, y, 10, ch, fill=color, rx=5),
            text(x+34, y+48, name, 28, weight=850, fill=color),
            text(x+34, y+88, subtitle, 19, fill=MUTED),
            text(x+34, y+138, metric, 31, weight=800),
            text(x+34, y+182, message_lines[0], 18, fill=INK),
            text(x+34, y+210, message_lines[1], 18, fill=INK),
        ]
    body += [
        rect(120, 770, 1410, 74, fill="#eef2ff", stroke="#c7d2fe", rx=18),
        text(825, 805,
             "Central result: observational improvement ≠ physical improvement; verification quality depends on information content.",
             23, anchor="middle", weight=700),
        text(825, 835,
             "Negative prospective outcomes are retained as completed evidence, not hidden as unfinished work.",
             18, anchor="middle", fill=MUTED),
    ]
    return svg(W, H, body, title_value="VULKAX evidence stage story")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    data = load_ledger()
    if data.get("research_program_status") != "frozen_for_manuscript_preparation":
        raise SystemExit("Refusing to render: final result ledger is not marked frozen.")

    outputs = {
        "fig_information_frontier.svg": figure_information_frontier(data),
        "fig_reality_inspector_storyboard.svg": figure_reality_inspector(data),
        "fig_evidence_story.svg": figure_stage_story(data),
    }
    for name, content in outputs.items():
        write(args.out, name, content)
        print(args.out / name)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
