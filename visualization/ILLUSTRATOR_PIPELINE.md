# Illustrator-first paper visual pipeline

The production split is deliberate:

- **Blender owns pixels:** solver-driven 3D plates, lighting, material, X-ray overlays, and motion.
- **SVG / Illustrator owns communication:** typography, figure grid, arrows, callouts, captions, and verdicts.

Primary outputs are generated under \`build/paper-visuals/\`:

\`\`\`text
plates/observe.png
plates/repair.png
plates/interrogate.png
plates/xray.png
plates/wireframe.png
vector/vulkax_hero_composite.svg
vector/vulkax_mechanism_xray.svg
vector/vulkax_method_explainer.svg
video/vulkax_verification_animation.blend
video/vulkax_verification_animation.mp4   # optional
\`\`\`

The SVG masters embed the plate pixels and keep layout/type/vector annotations editable. Open directly in Adobe Illustrator and save an AI working copy.

The Stanford Bunny remains a visualization carrier rather than frozen benchmark geometry. Its deformation comes from interpolation of the exported raw VULKAX MPM field. Cinematic timing and display magnification are presentation choices and are disclosed.
