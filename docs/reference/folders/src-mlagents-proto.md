# `src/mlagents/proto/`

> [Source](../../../src/mlagents/proto/) · Parent: [`src/mlagents/`](src-mlagents.md)

## Summary
The ML-Agents protobuf definitions, vendored unchanged from the ML-Agents fork used by the trainer (`VERSION` records the fork commit and the protocol: communication version 1.5.0, package version 4.0.0). CMake globs `mlagents_envs/communicator_objects/*.proto` and generates C++ and gRPC sources with `protobuf_generate` into `<build>/proto` for the `dynamical_mlagents` target; the generated headers are included as `mlagents_envs/communicator_objects/<name>.pb.h`. The C++ namespace is `communicator_objects` (aliased `co` in the engine). Main messages used by the engine: `UnityMessageProto` (header + `UnityOutputProto` / `UnityInputProto`), `UnityRLInitializationOutputProto` / `InputProto` (handshake, capabilities, seed), `UnityRLInputProto` (agent actions per behaviour, `CommandProto` STEP/RESET/QUIT, side-channel bytes), `UnityRLOutputProto` (`AgentInfoProto` lists per behaviour, side-channel bytes), `ObservationProto`, `BrainParametersProto` / `ActionSpecProto`, and the `UnityToExternalProto.Exchange` service.

## Files
| File | Contents |
|---|---|
| `VERSION` | provenance of the vendored protos (fork commit, path, protocol versions) |
| `mlagents_envs/communicator_objects/*.proto` | 21 unmodified ML-Agents message and service definitions (no per-file detail here) |

## Systems defined here
None.

## Components defined here
None.

## Subfolders
`mlagents_envs/communicator_objects/` (the `.proto` files; no separate doc).
