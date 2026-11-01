#!/usr/bin/env python3
"""Reference outputs of an exported policy from Python's onnxruntime, for the engine's PolicyRunner test (SR.13).

    build/venv/Scripts/python tools/onnx_reference.py tests/data/onnx/sphere_roller.onnx [--count 100]

Writes <model>.reference.json: observations shaped like the sphere roller's (unit normals with y > 0 in the goal
frame, velocities, angular velocities, a target speed) and the model's deterministic and stochastic-free outputs.
"""

import argparse
import json
import pathlib

import numpy as np
import onnxruntime


def observations(count, rng):
    obs = np.zeros((count, 16), dtype=np.float32)
    for i in (0, 3, 6):
        n = rng.normal(size=(count, 3)) * [0.3, 0.0, 0.3] + [0.0, 1.0, 0.0]
        obs[:, i:i + 3] = n / np.linalg.norm(n, axis=1, keepdims=True)
    obs[:, 9:12] = rng.normal(size=(count, 3)) * [1.5, 0.4, 2.0]
    obs[:, 12:15] = rng.normal(size=(count, 3)) * 6.0
    obs[:, 15] = rng.uniform(0.0, 3.0, size=count)
    return obs


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("model")
    parser.add_argument("--count", type=int, default=100)
    args = parser.parse_args()

    session = onnxruntime.InferenceSession(args.model, providers=["CPUExecutionProvider"])
    obs = observations(args.count, np.random.default_rng(20261031))
    actions, version, memory, shape = session.run(
        ["deterministic_continuous_actions", "version_number", "memory_size", "continuous_action_output_shape"], {"obs_0": obs})
    out = pathlib.Path(args.model).with_suffix(".reference.json")
    out.write_text(json.dumps({
        "onnxruntime": onnxruntime.__version__,
        "version_number": float(version[0]), "memory_size": float(memory[0]), "continuous_action_output_shape": float(shape[0]),
        "observations": obs.tolist(), "deterministic_actions": actions.tolist(),
    }))
    print(f"{out}: {args.count} observations, actions in [{actions.min():.3f}, {actions.max():.3f}]")


if __name__ == "__main__":
    main()
