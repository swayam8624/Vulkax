#!/usr/bin/env python3

import json
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "services" / "atlas_gateway"))

from server import AtlasGateway, ThreadingHTTPServer, decode_polyline6, make_handler


class AtlasGatewayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        class UpstreamHandler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def response(self, payload):
                body = json.dumps(payload).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                if self.path.startswith("/v1/autocomplete"):
                    self.response(
                        {
                            "features": [
                                {
                                    "geometry": {
                                        "coordinates": [77.2295, 28.6129]
                                    },
                                    "properties": {
                                        "gid": "osm:w1950397",
                                        "name": "India Gate",
                                        "label": "India Gate, New Delhi",
                                        "confidence": 0.99,
                                        "layer": "venue",
                                    },
                                }
                            ]
                        }
                    )
                elif self.path.startswith("/v1/reverse"):
                    self.response(
                        {
                            "features": [
                                {
                                    "geometry": {
                                        "coordinates": [77.2295, 28.6129]
                                    },
                                    "properties": {
                                        "gid": "osm:w1950397",
                                        "name": "India Gate",
                                        "label": "India Gate, New Delhi",
                                        "confidence": 0.99,
                                        "layer": "venue",
                                    },
                                }
                            ]
                        }
                    )

            def do_POST(self):
                size = int(self.headers.get("Content-Length", "0"))
                request = json.loads(self.rfile.read(size))
                if self.path == "/route":
                    if request["costing"] != "auto":
                        self.send_error(400)
                        return
                    self.response(
                        {
                            "id": "route-delhi-1",
                            "trip": {
                                "summary": {"length": 3.2, "time": 720},
                                "legs": [
                                    {
                                        "shape": "_ojxoAnmny}Cig}e@qc~rG",
                                        "maneuvers": [
                                            {
                                                "instruction": "Continue",
                                                "length": 3.2,
                                                "time": 720,
                                                "begin_shape_index": 0,
                                            }
                                        ],
                                    }
                                ],
                            },
                        }
                    )

        cls.upstream = ThreadingHTTPServer(
            ("127.0.0.1", 0), UpstreamHandler
        )
        cls.upstream_thread = threading.Thread(
            target=cls.upstream.serve_forever, daemon=True
        )
        cls.upstream_thread.start()
        upstream_base = f"http://127.0.0.1:{cls.upstream.server_port}"

        cls.temporary = tempfile.TemporaryDirectory()
        content = Path(cls.temporary.name)
        (content / "tile.bin").write_bytes(bytes(range(32)))
        gateway = AtlasGateway(
            ROOT / "data" / "atlas" / "navigation_replay.json",
            content,
            pelias_url=upstream_base,
            valhalla_url=upstream_base,
        )
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(gateway))
        cls.thread = threading.Thread(
            target=cls.server.serve_forever, daemon=True
        )
        cls.thread.start()
        cls.base = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)
        cls.upstream.shutdown()
        cls.upstream.server_close()
        cls.upstream_thread.join(timeout=2)
        cls.temporary.cleanup()

    def json_request(self, path: str, payload=None):
        body = None if payload is None else json.dumps(payload).encode()
        request = Request(
            self.base + path,
            data=body,
            headers={"Content-Type": "application/json"},
        )
        with urlopen(request, timeout=2) as response:
            return response.status, json.load(response)

    def test_contract_endpoints(self):
        status, payload = self.json_request("/v1/status")
        self.assertEqual(status, 200)
        self.assertIn("route", payload["capabilities"])
        self.assertTrue(payload["upstreams"]["pelias"])
        self.assertTrue(payload["upstreams"]["valhalla"])

        _, payload = self.json_request("/v1/search?q=gate")
        self.assertEqual(payload["results"][0]["name"], "India Gate")

        _, payload = self.json_request("/v1/reverse?lat=28.613&lon=77.23")
        self.assertEqual(payload["result"]["name"], "India Gate")

        _, payload = self.json_request(
            "/v1/route",
            {
                "mode": "driving",
                "alternatives": 0,
                "origin": [28.6315, 77.2167, 215.0],
                "destination": [28.6129, 77.2295, 216.0],
            },
        )
        self.assertEqual(payload["routes"][0]["id"], "route-delhi-1")

        _, payload = self.json_request("/v1/transit", {})
        self.assertTrue(payload["itineraries"][0]["realtimeTransit"])

        _, payload = self.json_request("/v1/traffic")
        self.assertEqual(payload["segments"][0]["currentSpeedKph"], 18.0)

    def test_range_and_etag_content(self):
        request = Request(
            self.base + "/v1/content/tile.bin", headers={"Range": "bytes=4-9"}
        )
        with urlopen(request, timeout=2) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.read(), bytes(range(4, 10)))
            etag = response.headers["ETag"]

        request = Request(
            self.base + "/v1/content/tile.bin",
            headers={"If-None-Match": etag},
        )
        with self.assertRaises(HTTPError) as context:
            urlopen(request, timeout=2)
        self.assertEqual(context.exception.code, 304)
        context.exception.close()

    def test_valhalla_polyline6_decoder(self):
        decoded = decode_polyline6("_ojxoAnmny}Cig}e@qc~rG")
        self.assertEqual(len(decoded), 2)
        self.assertAlmostEqual(decoded[0][0], 42.358528, places=6)
        self.assertAlmostEqual(decoded[0][1], -83.2714, places=6)
        self.assertAlmostEqual(decoded[1][0], 42.996613, places=6)
        self.assertAlmostEqual(decoded[1][1], -78.749855, places=6)


if __name__ == "__main__":
    unittest.main()
