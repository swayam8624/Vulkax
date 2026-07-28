# Midtown Manhattan GeoBEACON Dataset

This is the fourth complete Vulkax city slice. It covers Times Square, Bryant Park, Rockefeller
Center, and the surrounding Midtown street grid:

```text
south = 40.7520
west  = -74.0060
north = 40.7660
east  = -73.9800
```

The checked `source.osm` extract is OpenStreetMap data licensed under ODbL 1.0.

Attribution: © OpenStreetMap contributors  
License and attribution details: https://www.openstreetmap.org/copyright

Regenerate the extract, semantic LOD tiles, and local navigation graph explicitly:

```bash
python3 tools/fetch_osm_extract.py \
  --bbox 40.7520 -74.0060 40.7660 -73.9800 \
  --output data/midtown_manhattan/source.osm \
  --force

python3 tools/build_geobeacon_tiles.py \
  --source data/midtown_manhattan/source.osm \
  --output data/midtown_manhattan/generated \
  --bbox 40.7520 -74.0060 40.7660 -73.9800 \
  --dataset-id midtown-manhattan \
  --display-name "Midtown Manhattan"

python3 tools/build_connaught_navigation.py \
  --source data/midtown_manhattan/source.osm \
  --output data/midtown_manhattan/navigation.json \
  --region midtown-manhattan \
  --display-name "Midtown Manhattan" \
  --subtitle "New York City, United States"
```

The renderer never contacts OpenStreetMap or Overpass during build or runtime.
