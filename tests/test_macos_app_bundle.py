#!/usr/bin/env python3

import json
import pathlib
import plistlib
import sys
import unittest


class MacosAppBundleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.bundle = pathlib.Path(sys.argv[1]).resolve()
        cls.resources = cls.bundle / "Contents" / "Resources"

    def test_native_entrypoint_and_identity(self) -> None:
        info_path = self.bundle / "Contents" / "Info.plist"
        self.assertTrue(info_path.is_file())
        with info_path.open("rb") as stream:
            info = plistlib.load(stream)
        self.assertEqual(info["CFBundleIdentifier"], "dev.swayam.vulkax.atlas")
        executable = (
            self.bundle / "Contents" / "MacOS" / info["CFBundleExecutable"]
        )
        self.assertTrue(executable.is_file())

    def test_required_shaders_are_bundled(self) -> None:
        required = {
            "simple_shader.vert.spv",
            "simple_shader_adaptive.frag.spv",
            "point_light.vert.spv",
            "point_light.frag.spv",
        }
        shader_root = self.resources / "shaders"
        self.assertTrue(shader_root.is_dir())
        missing = sorted(
            name for name in required if not (shader_root / name).is_file()
        )
        self.assertEqual(missing, [])

    def test_all_registered_cities_are_self_contained(self) -> None:
        registry_path = self.resources / "data" / "cities.json"
        with registry_path.open(encoding="utf-8") as stream:
            registry = json.load(stream)
        self.assertGreaterEqual(len(registry["cities"]), 2)

        for city in registry["cities"]:
            manifest = self.resources / "data" / city["manifest"]
            navigation = self.resources / "data" / city["navigation"]
            self.assertTrue(manifest.is_file(), city["displayName"])
            self.assertTrue(navigation.is_file(), city["displayName"])
            city_root = navigation.parent
            self.assertTrue((city_root / "LICENSE-ODbL.md").is_file())
            self.assertTrue((city_root / "README.md").is_file())


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
