# `tests/support/`

> [Source](../../../tests/support/) · Parent: [`tests/`](tests.md)

## Summary
Test helpers shared by the suites (on the include path as `support/...`).

## Files
| File | Contents |
|---|---|
| `test_systems.h/.cpp` | `CallLog` (context variable, call log shared by stubs), `StubSys` (each tick appends `<label>@<tick>` to the `CallLog` if present), `registerTestSystems()` (`TestHost`, `TestHost2`, `TestInput`, `TestUI` host; `TestWorld` world; `TestBoth` both; `TestRender` host needing the renderer), `registerViewSystemStubs()` (host, needs-renderer stubs named like the view systems so rendered scenes such as `base` resolve without the viewer) |
| `mock_trainer.h/.cpp` | `MockTrainer`: in-process gRPC `UnityToExternalProto` server on a free port; `initialize`, `exchange`, `send`, `close` (header 400) mirror `rpc_communicator.py`. Built only with `DYNAMICAL_TRAIN` |

## Systems defined here
None (test stubs only, not part of the engine).

## Components defined here
None in the engine; `CallLog` is test-only.

## Subfolders
None.
