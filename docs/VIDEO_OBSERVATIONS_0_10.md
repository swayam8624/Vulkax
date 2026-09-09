# Video observations for the post-1.0 reality loop

This slice adds the first real-video observation path to the executable `WorldIR` work without changing any frozen Vulkax 1.0 evidence claim.

## Observation spaces

`WorldIR` schema 3 makes the coordinate/value space of every observation explicit. The current spaces are `Dimensionless`, `PhysicalSI`, `ImagePixels`, `NormalizedImage`, and `LinearRadiance`. The reality loop requires the forward model to predict the same space as the observation, so a pixel-space trajectory cannot be silently subtracted from a metre-space MPM trajectory.

## Video point-track contract

`scripts/extract_video_track.py` converts a static-camera video into a dependency-light single-point motion track using `ffmpeg`, grayscale extraction, a temporal-median background, connected foreground components, and area/proximity tracking. The temporal median is intentional: an arithmetic average can leave a moving-object trail that biases the foreground centroid toward the middle of the trajectory.

The emitted file begins with:

```text
# vulkax_video_point_track_v1
# source=<stable source or provenance URI>
# width_pixels=<processed frame width>
# height_pixels=<processed frame height>
# nominal_fps=<track sampling rate>
frame_index,time_seconds,x_pixels,y_pixels,confidence,split
```

The C++ loader rejects malformed metadata, out-of-frame points, non-monotonic frame/time sequences, invalid confidence, and unknown data splits. Imported tracks become `ObservationSpace::ImagePixels` records. Confidence changes uncertainty as `sigma = base_sigma / sqrt(confidence)`; it does not alter measured coordinates and is not interpreted as a calibrated probability.

## Fit versus validation

The extractor interleaves held-out validation rows using `--validation-stride`. Those rows are imported as `ObservationRole::Validation`, so the generic reality loop reports their residuals after fitting but does not optimize against them under the default settings.

## Pinned development footage

`assets/reality/video_sources.lock.json` pins Wikimedia Commons `Bouncing Ball.webm` by ScienceCOLA. Commons reports a 640x480 original, 26.427 s duration, 1,081,689 bytes, CC BY 3.0 attribution to ScienceCOLA, and SHA-1 `63a7d2d42be52bfc13fcf3643605b39841ae52d5`.

```bash
python3 scripts/fetch_reality_video_assets.py --validate-only
python3 scripts/fetch_reality_video_assets.py
python3 scripts/extract_video_track.py \
  build/reality-assets/video/sciencecola_bouncing_ball_2013.webm \
  build/reality-assets/video/sciencecola_bouncing_ball_2013.track.csv \
  --source https://commons.wikimedia.org/wiki/File:Bouncing_Ball.webm
```

The video and derived track are development observation inputs, not physical ground truth. This slice does not claim camera calibration, 3D reconstruction, monocular material identification, or causal identification.

## Next integration seam

The next stage is a camera-aware image-space forward model that maps a physical/world-space state through an explicit camera model into `ImagePixels` or `NormalizedImage`. That lets the existing reality loop optimize camera/world parameters against video tracks while preserving the same fit/validation and evidence boundaries already used by captured MPM.
