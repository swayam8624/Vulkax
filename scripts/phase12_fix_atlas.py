from pathlib import Path

path = Path('src/atlas/streaming/tile_source.cpp')
text = path.read_text()
old = 'if (relativeToRoot.empty() || relativeToRoot.native().starts_with("..")) {'
new = (
    'const auto firstComponent = relativeToRoot.begin();\n'
    '        if (relativeToRoot.empty() ||\n'
    '            (firstComponent != relativeToRoot.end() &&\n'
    '             *firstComponent == std::filesystem::path{".."})) {'
)
if old not in text:
    raise SystemExit('Atlas root-containment marker missing')
path.write_text(text.replace(old, new, 1))
