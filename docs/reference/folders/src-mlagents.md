# `src/mlagents/`

> [Source](../../../src/mlagents/) · Parent: [`src/`](src.md)

## Summary
The engine side of the ML-Agents protocol (communication version 1.5.0, package 4.0.0), compiled into the `dynamical_mlagents` library with gRPC and protobuf (only when `DYNAMICAL_TRAIN` is on). The Python trainer is the gRPC server on `localhost:<port>`; the engine is the client and calls `UnityToExternalProto.Exchange` repeatedly. Side channels (environment parameters, engine configuration, stats) are encoded and decoded byte for byte as the fork's Python and C# classes do. No registry access: [`TrainHost`](../systems/TrainHost.md) drives it.

## Files
| File | Contents |
|---|---|
| `client.h/.cpp` | `MlAgentsClient` (namespace `dy::mlagents`, alias `co` = `communicator_objects`): channel with unlimited message sizes; `call` issues one `AsyncExchange` with `wait_for_ready`, awaits it in 1 s `AsyncNext` slices and cancels it once on a stop request; `Status { Ok, Closed (reply header != 200), Stopped, Failed (gRPC error, see error) }`; `initialize` (init output with header 200, deadline `connect_timeout` 60 s, then an empty message whose answer is the first command), `exchange` (no deadline) |
| `side_channels.h/.cpp` | `ChannelId` (`parse` from UUID text into .NET `bytes_le` order), `envParamsChannel` / `engineConfigChannel` / `statsChannel` ids, `ChannelMessage`, `readChannelMessages` / `writeChannelMessages` (`[16-byte id][int32 length][payload]`, false on truncation), `MessageReader` / `MessageWriter` (little-endian int32, float32, length-prefixed strings, float lists), `EnvParamMessage` + `readEnvParam` (Float, or Sampler: seed + Uniform/Gaussian (2 floats) / MultiRangeUniform (float list)), `EngineConfigMessage` + `readEngineConfig`, `StatAggregation`, `writeStat` (key, value, aggregation) |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
[`proto/`](src-mlagents-proto.md)
