from pathlib import Path

path = Path('apps/VulkaxPhysicsStudioMac/Sources/VulkaxPhysicsStudioMac/main.swift')
text = path.read_text()
marker = '''        if CommandLine.arguments.contains("--native-cinematic-capture-smoke") {
            exit(runCinematicCaptureWriterSmoke() ? EXIT_SUCCESS : EXIT_FAILURE)
        }
'''
replacement = marker + '''        if let option = CommandLine.arguments.firstIndex(of: "--native-scene-mesh-gpu-smoke") {
            guard option + 1 < CommandLine.arguments.count else { exit(EXIT_FAILURE) }
            exit(runStudioSceneMeshRendererSmoke(path: CommandLine.arguments[option + 1])
                 ? EXIT_SUCCESS : EXIT_FAILURE)
        }
'''
if marker not in text:
    raise SystemExit('cinematic capture CLI marker missing')
path.write_text(text.replace(marker, replacement, 1))
Path('scripts/phase11_add_scene_cli.py').unlink()
