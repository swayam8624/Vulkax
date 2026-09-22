.PHONY: pdf docx final all clean

all: pdf docx final

pdf:
	latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
	cp main.pdf Reality_Probe_Manuscript.pdf

docx:
	python3 build_word.py

final:
	latexmk -pdf -interaction=nonstopmode -halt-on-error main_humanized.tex
	cp main_humanized.pdf Reality_Probe_Manuscript_Prose_Edited.pdf
	python3 build_word_humanized.py
	cp Reality_Probe_Manuscript_Prose_Edited.pdf Reality_Probe_Manuscript_Final.pdf
	cp Reality_Probe_Manuscript_Prose_Edited.docx Reality_Probe_Manuscript_Final.docx
	sha256sum Reality_Probe_Manuscript_Final.pdf Reality_Probe_Manuscript_Final.docx > FINAL_MANUSCRIPT_SHA256.txt

clean:
	latexmk -C main.tex
	latexmk -C main_humanized.tex
	rm -f Reality_Probe_Manuscript.pdf Reality_Probe_Manuscript.docx MANUSCRIPT_SHA256.txt
	rm -f Reality_Probe_Manuscript_Prose_Edited.pdf Reality_Probe_Manuscript_Prose_Edited.docx MANUSCRIPT_PROSE_EDITED_SHA256.txt
	rm -f Reality_Probe_Manuscript_Final.pdf Reality_Probe_Manuscript_Final.docx FINAL_MANUSCRIPT_SHA256.txt
