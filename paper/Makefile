.PHONY: pdf docx all clean

all: pdf docx

pdf:
	latexmk -pdf -interaction=nonstopmode -halt-on-error main.tex
	cp main.pdf Reality_Probe_Manuscript.pdf

docx:
	python3 build_word.py

clean:
	latexmk -C main.tex
	rm -f Reality_Probe_Manuscript.pdf Reality_Probe_Manuscript.docx MANUSCRIPT_SHA256.txt
