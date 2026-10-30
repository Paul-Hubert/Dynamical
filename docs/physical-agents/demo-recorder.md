# Demo recorder

`dynamical_train --mode record-demo --scenario <s> --tracks resources/agents/tracks/ --out demos/`
writes ML-Agents `.demo` files from Unity pose tracks, for GAIL (and BC if the config uses it). M5 gate:
the files load in the fork's `demo_loader.py` and a GAIL run starts on them.

## What gets recorded

For each pose track and each frame at a decision tick:

1. **Set the body kinematically to the frame** (positions, rotations and velocities of every segment,
   `BodyInterface::SetPositionAndRotation` + `SetLinearAndAngularVelocity`), no physics step. Run
   `BodySyncSys` and `ContactSys`-equivalent ground-contact detection (a shape query against the
   ground, since no step means no contact callbacks).
2. Call the scenario's `ObservationLayout::write` — the *same* code training uses. This is the point of
   recording in-engine: demo observations and policy observations come from one implementation.
3. Derive the action that would produce this pose with the scenario's `ActionLayout` inverse
   (`ActionLayout::inverse(registry, agent) → [-1,1] vector`: joint targets = current joint rotations,
   normalised by limits). If the Unity plan records demos differently (e.g. physically tracking the
   clip with a PD controller and recording the controller's actions), follow the Unity plan; the
   inverse mapping is only the default.
4. Reward: 0 (GAIL ignores environment reward in demos) unless the Unity plan says otherwise.
5. `done` on the last frame of the track (or every `max_episode_steps` decisions for long loops).

Optionally (`--physical`) the recorder can instead run physics with PD tracking of the clip and record
only frames where tracking error stays below a threshold, which yields demos the physics can actually
reach. Off by default.

## File format

A `.demo` file is a sequence of length-delimited protobuf messages (each prefixed by its size as a
varint, as `WriteDelimitedTo` / `_DecodeVarint32` produce):

```
offset 0       DemonstrationMetaProto (delimited)       rewritten at close with final counts
offset 33      BrainParametersProto (delimited)         the behaviour's brain parameters
then           AgentInfoActionPairProto (delimited) × N  { agent_info: AgentInfoProto, action_info: AgentActionProto }
```

*Verify*: upstream C# `DemonstrationWriter` reserves `k_MetaDataBytes = 32` bytes for the metadata and
seeks to `k_MetaDataBytes + 1` before writing the brain parameters; Python's `demo_loader.py` uses
`INITIAL_POS = 33`. The metadata must therefore encode (with its varint prefix) in ≤ 33 bytes — keep
`demonstration_name` short (≤ 12 characters) and pad the gap with zeros.

`DemonstrationMetaProto` fields: `api_version` (= 1), `demonstration_name`, `number_steps`,
`number_episodes`, `mean_reward`.

`AgentInfoProto` inside a pair is filled exactly as for the wire ([mlagents-bridge.md](mlagents-bridge.md#step-output)),
so the observation specs Python derives from the demo match the ones from the environment. A mismatch
in observation shapes between demo and env is the most common GAIL setup error; the recorder therefore
refuses to run if the scenario's `BehaviorSpec` differs from the one stored in an existing file it would
append to.

## Implementation

```cpp
// src/mlagents/demo_writer.h
class DemoWriter {
public:
    DemoWriter(const std::filesystem::path&, std::string name, const BehaviorSpec&);
    void record(const AgentStep& step, std::span<const float> continuous_action);
    void close();               // seeks to 0, rewrites the metadata (steps, episodes, mean reward)
};
```

Uses `google::protobuf::io::CodedOutputStream::WriteVarint32` + `SerializeToCodedStream`. One file per
track (`<clip>.demo`) or one per behaviour (`--single-file`); the trainer config's `demo_path` can point
at a directory either way.

## Checks

- Round-trip test: write 3 steps, parse with a C++ reader implementing the same offsets, compare.
- Python check script `tools/check_demo.py` calls `mlagents.trainers.demo_loader.demo_to_buffer(path, 1, spec)`
  and prints step count and observation shapes. Run it in M5 and whenever the observation layout
  changes.
