#!/usr/bin/env python3
"""Build editable Word output from the prose-edited Reality Probe manuscript."""
from pathlib import Path
import subprocess
import tempfile
from docx import Document
from docx.shared import Pt
from docx.enum.text import WD_ALIGN_PARAGRAPH

ROOT = Path(__file__).resolve().parent
TEX = ROOT / "main_humanized.tex"
OUT = ROOT / "Reality_Probe_Manuscript_Prose_Edited.docx"

src = TEX.read_text(encoding="utf-8")

# Equivalent TeX forms that Pandoc translates to Word math more reliably.
repls = {
    r"\[z = \frac{e\left( R_{0},Y \right) - e\left( R_{1},Y \right)}{\sigma}.\]":
        r"\[z = \frac{e(R_0,Y)-e(R_1,Y)}{\sigma}.\]",
    r"\[\mathcal{D}_{\mu}\left\lbrack F_{M} \right\rbrack = \sum_{i}^{}w_{i}F_{M}\left( u_{i} \right).\]":
        r"\[\mathcal{D}_{\mu}[F_M] = \sum_i w_i F_M(u_i).\]",
    r"\[r_{\alpha} = \sum_{i}^{}w_{i}u_{i}^{\alpha} \approx 0,\quad\quad|\alpha| < k.\]":
        r"\[r_{\alpha} = \sum_i w_i u_i^{\alpha} \approx 0,\quad |\alpha| < k.\]",
    r"\[\kappa(S) = \sum_{T \subseteq S}^{}( - 1)^{|S| - |T|}F(T).\]":
        r"\[\kappa(S) = \sum_{T \subseteq S} (-1)^{|S|-|T|}F(T).\]",
    r"\(\sum_{i}^{}w_{i}^{2}\)": r"\(\sum_i w_i^2\)",
    r"\[z_{force} = \frac{e(B,Y) - e(R,Y)}{\sqrt{\sigma_{meas}^{2} + \sigma_{repeat}^{2} + \sigma_{num,B}^{2} + \sigma_{num,R}^{2}}}.\]":
        r"\[z_{force} = \frac{e(B,Y)-e(R,Y)}{\sqrt{\sigma_{meas}^2+\sigma_{repeat}^2+\sigma_{num,B}^2+\sigma_{num,R}^2}}.\\]",
}
for old, new in repls.items():
    src = src.replace(old, new)

with tempfile.NamedTemporaryFile("w", suffix=".tex", dir=ROOT, delete=False, encoding="utf-8") as f:
    f.write(src)
    tmp = Path(f.name)

try:
    subprocess.run(
        ["pandoc", str(tmp), "--from=latex", "--to=docx",
         f"--resource-path={ROOT}", "-o", str(OUT)],
        check=True,
        cwd=ROOT,
    )
finally:
    tmp.unlink(missing_ok=True)

doc = Document(OUT)
for para in doc.paragraphs:
    if "sum_i w_i F_M(u_i)" in para.text:
        para.clear()
        run = para.add_run("𝒟μ[F_M] = Σᵢ wᵢ F_M(uᵢ).")
        run.font.name = "Cambria Math"
        run.font.size = Pt(11)
        para.alignment = WD_ALIGN_PARAGRAPH.CENTER
    elif "sum_i w_i^2" in para.text:
        para.clear()
        para.add_run(
            "The denominator of the standardized evidence keeps measurement, repeat, and numerical uncertainty separate. "
            "Measurement and repeat variance propagate through a signed stencil with the squared ℓ₂ weight gain Σᵢ wᵢ². "
            "Numerical error is handled more cautiously because discretization errors can be correlated across intervention points. "
            "D3 also estimates that contribution directly in witness space by evaluating the same signed witness at nominal and refined solver resolution."
        )

props = doc.core_properties
props.title = "Reality Probe: Information-Limited Physical Verification of Captured Worlds"
props.author = "Swayam Singal"
props.subject = "Prose-edited Reality Probe / Vulkax research manuscript"
props.keywords = "captured worlds; physical verification; counterfactual simulation; MPM; uncertainty; experiment design"
doc.save(OUT)
print(OUT)
