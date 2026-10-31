#!/usr/bin/env python3
"""Captures side-channel payloads produced by the fork's Python classes, for P3.2 (byte-exact tests).

    build/venv/Scripts/python tools/capture_sidechannels.py [--out tests/data/sidechannel]

Needs the fork's mlagents_envs installed (build/venv). Writes <name>.bin (the framed payload exactly as
SideChannelManager.generate_side_channel_messages produces it) and expected.json (what each blob holds).
The stats blob is built with OutgoingMessage the way StatsSideChannel.cs writes it (key, value, aggregation),
since the Python side only reads stats.
"""

import argparse
import json
import pathlib
import uuid

from mlagents_envs.side_channel.engine_configuration_channel import EngineConfigurationChannel
from mlagents_envs.side_channel.environment_parameters_channel import EnvironmentParametersChannel
from mlagents_envs.side_channel.outgoing_message import OutgoingMessage
from mlagents_envs.side_channel.side_channel import SideChannel
from mlagents_envs.side_channel.side_channel_manager import SideChannelManager
from mlagents_envs.side_channel.stats_side_channel import StatsAggregationMethod, StatsSideChannel

ROOT = pathlib.Path(__file__).resolve().parent.parent


class StatsWriter(SideChannel):
    """Same id as the trainer's StatsSideChannel; writes what the engine sends."""

    def __init__(self):
        super().__init__(uuid.UUID("a1d8f7b7-cec8-50f9-b78b-d3e165a78520"))

    def on_message_received(self, msg):
        pass

    def add(self, key, value, aggregation):
        msg = OutgoingMessage()
        msg.write_string(key)
        msg.write_float32(value)
        msg.write_int32(aggregation.value)
        self.queue_message_to_send(msg)


def framed(channels):
    return bytes(SideChannelManager(channels).generate_side_channel_messages())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default=str(ROOT / "tests" / "data" / "sidechannel"))
    args = parser.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    blobs, expected = {}, {}

    env = EnvironmentParametersChannel()
    env.set_float_parameter("targetSpeed", 1.36)
    blobs["env_float"] = framed([env])
    expected["env_float"] = [{"key": "targetSpeed", "type": "float", "value": 1.36}]

    env = EnvironmentParametersChannel()
    env.set_uniform_sampler_parameters("randomSpeed", 0.5, 1.5, 7)
    blobs["env_uniform"] = framed([env])
    expected["env_uniform"] = [{"key": "randomSpeed", "type": "sampler", "sampler": "uniform", "seed": 7, "args": [0.5, 1.5]}]

    env = EnvironmentParametersChannel()
    env.set_gaussian_sampler_parameters("pushForce", 100.0, 12.5, 3)
    blobs["env_gaussian"] = framed([env])
    expected["env_gaussian"] = [{"key": "pushForce", "type": "sampler", "sampler": "gaussian", "seed": 3, "args": [100.0, 12.5]}]

    env = EnvironmentParametersChannel()
    env.set_multirangeuniform_sampler_parameters("slopeAngle", [(-10.0, -5.0), (5.0, 10.0)], 11)
    blobs["env_multirange"] = framed([env])
    expected["env_multirange"] = [{"key": "slopeAngle", "type": "sampler", "sampler": "multirange", "seed": 11, "args": [-10.0, -5.0, 5.0, 10.0]}]

    engine = EngineConfigurationChannel()
    engine.set_configuration_parameters(width=84, height=84, quality_level=5, time_scale=20.0, target_frame_rate=-1, capture_frame_rate=60)
    blobs["engine_config"] = framed([engine])
    expected["engine_config"] = [{"type": 0, "values": [84, 84]}, {"type": 1, "values": [5]}, {"type": 2, "values": [20.0]},
                                 {"type": 3, "values": [-1]}, {"type": 4, "values": [60]}]

    stats = StatsWriter()
    stats.add("Root/Speed", 1.25, StatsAggregationMethod.AVERAGE)
    stats.add("Death/Rate", 0.5, StatsAggregationMethod.MOST_RECENT)
    stats.add("Collision/Count", 3.0, StatsAggregationMethod.SUM)
    stats.add("Bot Joint/RLeg", 0.75, StatsAggregationMethod.HISTOGRAM)
    blobs["stats"] = framed([stats])
    expected["stats"] = [{"key": "Root/Speed", "value": 1.25, "aggregation": 0}, {"key": "Death/Rate", "value": 0.5, "aggregation": 1},
                         {"key": "Collision/Count", "value": 3.0, "aggregation": 2}, {"key": "Bot Joint/RLeg", "value": 0.75, "aggregation": 3}]

    # Several channels in one payload, in the order the trainer sends them
    env = EnvironmentParametersChannel()
    env.set_float_parameter("targetSpeed", 1.36)
    engine = EngineConfigurationChannel()
    engine.set_configuration_parameters(time_scale=20.0)
    blobs["mixed"] = framed([engine, env])
    expected["mixed"] = [{"channel": "engine", "type": 2, "values": [20.0]}, {"channel": "env", "key": "targetSpeed", "type": "float", "value": 1.36}]

    # The trainer's reader accepts what the engine writes (round trip through StatsSideChannel)
    reader = StatsSideChannel()
    SideChannelManager([reader]).process_side_channel_message(blobs["stats"])
    got = reader.get_and_reset_stats()
    assert set(got) == {"Root/Speed", "Death/Rate", "Collision/Count", "Bot Joint/RLeg"}, got

    for name, blob in blobs.items():
        (out / f"{name}.bin").write_bytes(blob)
    with open(out / "expected.json", "w", newline="\n") as f:
        json.dump(expected, f, indent=1)
        f.write("\n")
    print(f"wrote {len(blobs)} blobs to {out}")


if __name__ == "__main__":
    main()
