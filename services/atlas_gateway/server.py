#!/usr/bin/env python3
"""Vulkax Atlas normalized HTTP/JSON gateway with deterministic replay mode."""

from __future__ import annotations

import argparse
import hashlib
import json
import mimetypes
import os
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlencode, urlparse
from urllib.request import Request, urlopen


def _distance_squared(position: list[float], latitude: float, longitude: float) -> float:
    return (position[0] - latitude) ** 2 + (position[1] - longitude) ** 2


def decode_polyline6(value: str) -> list[list[float]]:
    coordinates: list[list[float]] = []
    latitude = 0
    longitude = 0
    index = 0
    while index < len(value):
        deltas = []
        for _ in range(2):
            result = 0
            shift = 0
            while True:
                if index >= len(value):
                    raise ValueError("truncated Valhalla polyline")
                byte = ord(value[index]) - 63
                index += 1
                result |= (byte & 0x1F) << shift
                shift += 5
                if byte < 0x20:
                    break
            deltas.append(~(result >> 1) if result & 1 else result >> 1)
        latitude += deltas[0]
        longitude += deltas[1]
        coordinates.append([latitude / 1_000_000, longitude / 1_000_000, 0.0])
    return coordinates


def _fetch_json(
    url: str, payload: dict[str, Any] | None = None
) -> dict[str, Any]:
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = Request(
        url,
        data=body,
        headers={
            "Accept": "application/json",
            "Content-Type": "application/json",
            "User-Agent": "VulkaxAtlasGateway/1.0",
        },
        method="POST" if payload is not None else "GET",
    )
    try:
        with urlopen(request, timeout=15) as response:
            if response.length is not None and response.length > 16 * 1024 * 1024:
                raise RuntimeError("upstream response exceeds 16 MiB")
            return json.load(response)
    except HTTPError as error:
        error.close()
        raise RuntimeError(f"upstream request failed: {error}") from error
    except (URLError, TimeoutError, json.JSONDecodeError) as error:
        raise RuntimeError(f"upstream request failed: {error}") from error


class AtlasGateway:
    def __init__(
        self,
        replay_path: Path,
        content_root: Path | None = None,
        pelias_url: str | None = None,
        valhalla_url: str | None = None,
    ) -> None:
        self.replay_path = replay_path.resolve()
        self.content_root = content_root.resolve() if content_root else None
        self.pelias_url = (pelias_url or os.getenv("PELIAS_URL", "")).rstrip("/")
        self.valhalla_url = (
            valhalla_url or os.getenv("VALHALLA_URL", "")
        ).rstrip("/")
        self.replay = json.loads(self.replay_path.read_text(encoding="utf-8"))
        if self.replay.get("format") != "Vulkax-Atlas-navigation-replay-1":
            raise ValueError("unsupported Atlas navigation replay format")

    def status(self) -> dict[str, Any]:
        return {
            "service": "Vulkax Atlas Gateway",
            "apiVersion": "v1",
            "mode": "deterministic-replay",
            "capabilities": [
                "search",
                "reverse",
                "route",
                "transit",
                "traffic",
                "range-content",
            ],
            "upstreams": {
                "pelias": bool(self.pelias_url),
                "valhalla": bool(self.valhalla_url),
                "otp": bool(os.getenv("OTP_URL")),
                "traffic": bool(os.getenv("TOMTOM_API_KEY")),
            },
        }

    @staticmethod
    def _pelias_result(feature: dict[str, Any]) -> dict[str, Any]:
        properties = feature.get("properties", {})
        coordinates = feature.get("geometry", {}).get("coordinates", [0.0, 0.0])
        return {
            "id": properties.get("gid", properties.get("id", "")),
            "name": properties.get("name", properties.get("label", "")),
            "subtitle": properties.get("label", ""),
            "position": [coordinates[1], coordinates[0], 0.0],
            "confidence": properties.get("confidence", 0.0),
            "category": properties.get("layer", ""),
        }

    def search(
        self,
        query: str,
        limit: int,
        focus_latitude: float | None = None,
        focus_longitude: float | None = None,
        locale: str = "en",
    ) -> list[dict[str, Any]]:
        limit = max(1, min(limit, 50))
        if self.pelias_url:
            parameters: dict[str, Any] = {
                "text": query,
                "size": limit,
                "lang": locale,
            }
            if focus_latitude is not None and focus_longitude is not None:
                parameters["focus.point.lat"] = focus_latitude
                parameters["focus.point.lon"] = focus_longitude
            response = _fetch_json(
                f"{self.pelias_url}/v1/autocomplete?{urlencode(parameters)}"
            )
            return [
                self._pelias_result(feature)
                for feature in response.get("features", [])
            ]
        normalized = query.casefold()
        matches = [
            item
            for item in self.replay.get("search", [])
            if not normalized
            or normalized in item.get("name", "").casefold()
            or normalized in item.get("subtitle", "").casefold()
        ]
        matches.sort(key=lambda item: item.get("confidence", 0.0), reverse=True)
        return matches[:limit]

    def reverse(
        self, latitude: float, longitude: float, locale: str = "en"
    ) -> dict[str, Any] | None:
        if self.pelias_url:
            response = _fetch_json(
                f"{self.pelias_url}/v1/reverse?"
                + urlencode(
                    {
                        "point.lat": latitude,
                        "point.lon": longitude,
                        "lang": locale,
                        "size": 1,
                    }
                )
            )
            features = response.get("features", [])
            return self._pelias_result(features[0]) if features else None
        candidates = self.replay.get("search", [])
        if not candidates:
            return None
        return min(
            candidates,
            key=lambda item: _distance_squared(
                item["position"], latitude, longitude
            ),
        )

    def routes(
        self,
        mode: str,
        alternatives: int,
        origin: list[float] | None = None,
        destination: list[float] | None = None,
        intermediate_stops: list[list[float]] | None = None,
        locale: str = "en",
    ) -> list[dict[str, Any]]:
        if self.valhalla_url:
            if origin is None or destination is None:
                raise ValueError("origin and destination are required")
            costing = {
                "driving": "auto",
                "walking": "pedestrian",
                "cycling": "bicycle",
            }.get(mode)
            if costing is None:
                raise ValueError(f"Valhalla does not support route mode {mode}")
            positions = [origin, *(intermediate_stops or []), destination]
            response = _fetch_json(
                f"{self.valhalla_url}/route",
                {
                    "locations": [
                        {"lat": position[0], "lon": position[1]}
                        for position in positions
                    ],
                    "costing": costing,
                    "units": "kilometers",
                    "language": locale,
                    "alternates": max(0, min(alternatives, 3)),
                    "directions_options": {"units": "kilometers"},
                },
            )
            trip = response.get("trip", {})
            summary = trip.get("summary", {})
            shape: list[list[float]] = []
            maneuvers: list[dict[str, Any]] = []
            shape_offset = 0
            for leg in trip.get("legs", []):
                leg_shape = decode_polyline6(leg.get("shape", ""))
                if shape and leg_shape:
                    leg_shape = leg_shape[1:]
                shape.extend(leg_shape)
                for maneuver in leg.get("maneuvers", []):
                    begin_index = shape_offset + int(
                        maneuver.get("begin_shape_index", 0)
                    )
                    position = (
                        shape[min(begin_index, len(shape) - 1)]
                        if shape
                        else origin
                    )
                    maneuvers.append(
                        {
                            "instruction": maneuver.get("instruction", ""),
                            "position": position,
                            "distanceMeters": float(
                                maneuver.get("length", 0.0)
                            )
                            * 1000.0,
                            "durationSeconds": maneuver.get("time", 0.0),
                            "routeShapeIndex": begin_index,
                        }
                    )
                shape_offset = max(0, len(shape) - 1)
            return [
                {
                    "id": response.get("id", "valhalla-route-0"),
                    "mode": mode,
                    "distanceMeters": float(summary.get("length", 0.0))
                    * 1000.0,
                    "durationSeconds": summary.get("time", 0.0),
                    "trafficDelaySeconds": 0.0,
                    "trafficAware": False,
                    "shape": shape,
                    "maneuvers": maneuvers,
                }
            ]
        routes = [
            route
            for route in self.replay.get("routes", [])
            if route.get("mode") == mode
        ]
        return routes[: max(1, min(alternatives + 1, 4))]

    def transit(self) -> list[dict[str, Any]]:
        return self.replay.get("transit", [])

    def traffic(self) -> dict[str, Any]:
        return {
            "observedAt": "recorded-fixture",
            "segments": self.replay.get("traffic", []),
        }

    def content(self, relative_path: str) -> tuple[Path, str]:
        if self.content_root is None:
            raise FileNotFoundError("content serving is disabled")
        candidate = (self.content_root / relative_path).resolve()
        if self.content_root not in candidate.parents and candidate != self.content_root:
            raise PermissionError("content path escapes configured root")
        if not candidate.is_file():
            raise FileNotFoundError(relative_path)
        digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
        return candidate, f'"{digest}"'


def make_handler(gateway: AtlasGateway) -> type[BaseHTTPRequestHandler]:
    class Handler(BaseHTTPRequestHandler):
        server_version = "VulkaxAtlasGateway/1.0"

        def log_message(self, format_string: str, *args: object) -> None:
            if os.getenv("ATLAS_GATEWAY_QUIET") != "1":
                super().log_message(format_string, *args)

        def _json(self, status: HTTPStatus, payload: Any) -> None:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _body(self) -> dict[str, Any]:
            try:
                size = int(self.headers.get("Content-Length", "0"))
            except ValueError as error:
                raise ValueError("invalid Content-Length") from error
            if size > 1024 * 1024:
                raise ValueError("request body exceeds 1 MiB")
            return json.loads(self.rfile.read(size) or b"{}")

        def do_GET(self) -> None:
            parsed = urlparse(self.path)
            query = parse_qs(parsed.query)
            try:
                if parsed.path == "/v1/status":
                    self._json(HTTPStatus.OK, gateway.status())
                elif parsed.path == "/v1/search":
                    self._json(
                        HTTPStatus.OK,
                        {
                            "results": gateway.search(
                                query.get("q", [""])[0],
                                int(query.get("limit", ["10"])[0]),
                                float(query["focus.lat"][0])
                                if "focus.lat" in query
                                else None,
                                float(query["focus.lon"][0])
                                if "focus.lon" in query
                                else None,
                                query.get("locale", ["en"])[0],
                            )
                        },
                    )
                elif parsed.path == "/v1/reverse":
                    result = gateway.reverse(
                        float(query["lat"][0]),
                        float(query["lon"][0]),
                        query.get("locale", ["en"])[0],
                    )
                    self._json(
                        HTTPStatus.OK if result else HTTPStatus.NOT_FOUND,
                        {"result": result},
                    )
                elif parsed.path == "/v1/traffic":
                    self._json(HTTPStatus.OK, gateway.traffic())
                elif parsed.path.startswith("/v1/content/"):
                    self._serve_content(parsed.path.removeprefix("/v1/content/"))
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})
            except (KeyError, ValueError) as error:
                self._json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            except RuntimeError as error:
                self._json(HTTPStatus.BAD_GATEWAY, {"error": str(error)})
            except PermissionError as error:
                self._json(HTTPStatus.FORBIDDEN, {"error": str(error)})
            except FileNotFoundError as error:
                self._json(HTTPStatus.NOT_FOUND, {"error": str(error)})

        def do_POST(self) -> None:
            parsed = urlparse(self.path)
            try:
                payload = self._body()
                if parsed.path == "/v1/route":
                    self._json(
                        HTTPStatus.OK,
                        {
                            "routes": gateway.routes(
                                payload.get("mode", "driving"),
                                int(payload.get("alternatives", 2)),
                                payload.get("origin"),
                                payload.get("destination"),
                                payload.get("intermediateStops", []),
                                payload.get("locale", "en"),
                            )
                        },
                    )
                elif parsed.path == "/v1/transit":
                    self._json(
                        HTTPStatus.OK, {"itineraries": gateway.transit()}
                    )
                else:
                    self._json(HTTPStatus.NOT_FOUND, {"error": "unknown endpoint"})
            except (json.JSONDecodeError, ValueError) as error:
                self._json(HTTPStatus.BAD_REQUEST, {"error": str(error)})
            except RuntimeError as error:
                self._json(HTTPStatus.BAD_GATEWAY, {"error": str(error)})

        def _serve_content(self, relative_path: str) -> None:
            path, etag = gateway.content(relative_path)
            if self.headers.get("If-None-Match") == etag:
                self.send_response(HTTPStatus.NOT_MODIFIED)
                self.send_header("ETag", etag)
                self.end_headers()
                return
            size = path.stat().st_size
            start, end = 0, size - 1
            status = HTTPStatus.OK
            range_header = self.headers.get("Range")
            if range_header:
                if not range_header.startswith("bytes=") or "," in range_header:
                    self._json(
                        HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE,
                        {"error": "only one byte range is supported"},
                    )
                    return
                first, last = range_header[6:].split("-", 1)
                start = int(first) if first else 0
                end = int(last) if last else size - 1
                if start < 0 or end < start or start >= size:
                    self._json(
                        HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE,
                        {"error": "range outside content"},
                    )
                    return
                end = min(end, size - 1)
                status = HTTPStatus.PARTIAL_CONTENT
            length = end - start + 1
            self.send_response(status)
            self.send_header(
                "Content-Type",
                mimetypes.guess_type(path.name)[0] or "application/octet-stream",
            )
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("ETag", etag)
            self.send_header("Content-Length", str(length))
            if status == HTTPStatus.PARTIAL_CONTENT:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            with path.open("rb") as stream:
                stream.seek(start)
                self.wfile.write(stream.read(length))

    return Handler


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--listen", default=os.getenv("ATLAS_GATEWAY_LISTEN", "127.0.0.1")
    )
    parser.add_argument(
        "--port", type=int, default=int(os.getenv("ATLAS_GATEWAY_PORT", "8080"))
    )
    parser.add_argument(
        "--replay",
        type=Path,
        default=Path(
            os.getenv(
                "ATLAS_REPLAY",
                "data/atlas/navigation_replay.json",
            )
        ),
    )
    parser.add_argument(
        "--content-root",
        type=Path,
        default=Path(os.environ["ATLAS_CONTENT_ROOT"])
        if os.getenv("ATLAS_CONTENT_ROOT")
        else None,
    )
    arguments = parser.parse_args()
    gateway = AtlasGateway(arguments.replay, arguments.content_root)
    server = ThreadingHTTPServer(
        (arguments.listen, arguments.port), make_handler(gateway)
    )
    print(
        f"Vulkax Atlas Gateway listening on "
        f"http://{arguments.listen}:{arguments.port}"
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
