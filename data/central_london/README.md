# Central London GeoBEACON Dataset

This is the second complete Vulkax city slice. It covers a compact Central London study area:

```text
south = 51.5030
west  = -0.1395
north = 51.5160
east  = -0.1160
```

The checked `source.osm` extract is OpenStreetMap data licensed under ODbL 1.0.

Attribution: © OpenStreetMap contributors  
License and attribution details: https://www.openstreetmap.org/copyright

Regenerate the extract, semantic LOD tiles, and local navigation graph explicitly:

```bash
python3 tools/fetch_osm_extract.py \
  --bbox 51.5030 -0.1395 51.5160 -0.1160 \
  --output data/central_london/source.osm \
  --force

python3 tools/build_geobeacon_tiles.py \
  --source data/central_london/source.osm \
  --output data/central_london/generated \
  --bbox 51.5030 -0.1395 51.5160 -0.1160 \
  --dataset-id central-london \
  --display-name "Central London"

python3 tools/build_connaught_navigation.py \
  --source data/central_london/source.osm \
  --output data/central_london/navigation.json \
  --region central-london \
  --display-name "Central London" \
  --subtitle "Central London, United Kingdom"
```

The renderer never contacts OpenStreetMap or Overpass during build or runtime.
