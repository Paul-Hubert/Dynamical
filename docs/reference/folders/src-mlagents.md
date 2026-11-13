# src/mlagents

The engine side of the ML-Agents protocol (communication version 1.5.0, package 4.0.0): the gRPC client of the
Python trainer and the side channels ([ML-Agents bridge](../../physical-agents/mlagents-bridge.md)). Built as the
`dynamical_mlagents` library (gRPC, protobuf) used by `src/train/`. No registry access.

## Files

| File | What |
|---|---|
| [`client.h`](../../../src/mlagents/client.h), [`client.cpp`](../../../src/mlagents/client.cpp) | `MlAgentsClient`: `UnityToExternalProto.Exchange` calls to localhost:<port>, `initialize` (initialisation output, then the first command), `exchange`; every call awaited in 1 s slices so that a stop request is seen within a second; statuses Ok, Closed (header other than 200), Stopped, Failed |
| [`side_channels.h`](../../../src/mlagents/side_channels.h), [`side_channels.cpp`](../../../src/mlagents/side_channels.cpp) | side channel payloads ([16-byte id][int32 length][bytes]): `ChannelId`, `readChannelMessages`, `writeChannelMessages`, `MessageReader`, `MessageWriter`; the environment parameters channel (`EnvParamMessage`: float or sampler), the engine configuration channel (`EngineConfigMessage`, ignored), the stats channel (`writeStat`, `StatAggregation`) |
| `proto/` | the fork's `mlagents_envs/communicator_objects/*.proto` vendored unchanged (`proto/VERSION` gives the fork, commit and protocol versions), compiled by CMake (`protobuf_generate`, gRPC plugin) into the build folder |

## Systems

None.

## Components and context variables

None.

## Worth knowing

- Channel ids are in .NET `Guid.ToByteArray` / Python `uuid.bytes_le` order; fields are little endian, strings
  int32 length + ASCII, as the Python side's `incoming_message.py` / `outgoing_message.py`.
- A truncated payload is refused (`readChannelMessages` returns false; the host logs and ignores it).

## Dependencies

- gRPC, protobuf, the generated `mlagents_envs/communicator_objects` headers.
- Used by `src/train/` (`TrainHost::serve`, `WorldsEnvironment::write`, `NullEnvironment`) and
  `tests/protocol/` (mock trainer, side channel blobs captured from Python).

## Design docs

- [docs/physical-agents/mlagents-bridge.md](../../physical-agents/mlagents-bridge.md)
