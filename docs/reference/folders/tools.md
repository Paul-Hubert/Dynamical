# `tools/`

> [Source](../../../tools/) · Parent: repository root

## Summary
Developer tools: two C++ programs built by CMake (`imgdiff` always, `raycast` with `-DDYNAMICAL_BUILD_TOOLS=ON`) and Python scripts for the visual tests, goldens, ML-Agents training runs, benchmarks, reports and the reference docs. Scripts that need the fork's ML-Agents run from `build/venv` (`setup_venv.py`); the visual tests need a GPU and the `dynamical` executable.

## Files
| File | Contents |
|---|---|
| `imgdiff/imgdiff.h/.cpp` | `dy::imgdiff`: `Image` (8-bit RGB), `Thresholds { max_mae 1.5, max_fraction 0.005, pixel_threshold 24 }` (1/255 units), `Result`, `compare`, `heatmap`, `load`, `save`; library `imgdiff_core` (also linked into the tests) |
| `imgdiff/main.cpp` | `imgdiff <capture.png> <golden.png> [--mae] [--fraction] [--pixel] [--heatmap <out>] [--always-heatmap]`: exit 0 within thresholds, 1 outside (heatmap `<capture>_diff.png`), 2 on usage or I/O errors |
| `imgdiff/stb_impl.cpp` | stb image implementation unit |
| `raycast/main.cpp` | bakes `resources/raycast_data.bin` for the grass shader: per tile position and ray angle, distance to the first blade (jittered circles on a 4x4 grid) and its normal; cereal portable binary |
| `visual_test.py` | CTest `gpu` driver: `golden` (capture shots, imgdiff against `tests/golden/images`, `--expect-fail`), `shots`, `repeat` (two runs, MAE only), `probe` (probe CSVs compared across runs and with a golden, max position difference <= tol) |
| `rebaseline.py` | replace a golden (image or file) from the newest artifact or `--from`, appending the reason to `tests/golden/CHANGELOG.md` |
| `phase_report.py` | per-phase sign-off report `build/reports/P<n>/index.html`: CTest per label, test IDs from `phases.md`, captures beside goldens and heatmaps, plots, checklist |
| `gen_reference_docs.py` | generates `docs/reference/components/*.md`, the access matrix and indexes from `docs/reference/data/*.json`; `--check` verifies without writing |
| `setup_venv.py` | creates `build/venv` with the fork's ML-Agents (Python 3.10.1-3.10.12, pinned versions, installed from a `git archive` copy of the fork) |
| `train_smoke.py` | L3.1 [slow]: `mlagents-learn` against TrainingEnv.exe `mode=null` with `tests/data/null_ppo.yaml`; checks exit 0, TensorBoard events, `run_logs/Player-0.log` |
| `train_sphere.py` | PPO run on `sphere_roll` or `wheel_roll` (`training/*.yaml`), environment frozen into `<results>/<run-id>_env`, optional `--init-path` |
| `bench_null.py` | B3.1: trainer steps/s against `mode=null` at several env counts (`build/artifacts/P3/bench_null.csv`) |
| `plot_throughput.py` | plots `bench_null.csv` against the Unity reference of 424 steps/s |
| `tensorboard_shot.py` | screenshot of TensorBoard (headless Edge or Chrome) plus scalar plots of a run |
| `capture_sidechannels.py` | captures side-channel payloads from the fork's Python classes into `tests/data/sidechannel/` (P3.2) |
| `onnx_reference.py` | Python onnxruntime reference outputs for `tests/data/onnx/sphere_roller.onnx` (SR.13) |
| `make_primitives.py` | writes `resources/models/primitives/{box,sphere}.gltf` (unit box copy, UV sphere of diameter 1) |
| `sphere_video.py` | MP4 of `sphere_view` / `wheel_view` (or the heuristic scene) rendered frame by frame through a capture script, encoded with ffmpeg |
| `wandb_offline/sitecustomize.py` | put on `PYTHONPATH` by `train_smoke.py` and `bench_null.py`: forces `wandb.init(mode="offline")` |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
`imgdiff/`, `raycast/`, `wandb_offline/` (described above; no separate docs).
