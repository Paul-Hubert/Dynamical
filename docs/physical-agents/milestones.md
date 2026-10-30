# Milestones

Each milestone ends with a **gate**; the next one does not start until the gate passes or the plan is
revised. Gates come from [`../physical-agents-plan.md`](../physical-agents-plan.md); tasks here are the
engine work needed to reach them.

## M0 — Bridge skeleton, null environment

- [ ] CMake split: `dynamical_sim` (no Vulkan/SDL/ImGui), `dynamical` (game), `dynamical_train`.
      Remove `vulkan.h` from `system.h`; split `system_list.h`; split `TimeSys` into clock + panel;
      move `getMousePosition` out of `MapManager`.
- [ ] Rename the CMake preset to `default`; add `train-release`.
- [ ] vcpkg/flake: `grpc`, `protobuf`; vendor the fork's protos; generate C++.
- [ ] `MlAgentsClient`: handshake, empty exchange, RESET/STEP/QUIT, brain parameters, action routing.
- [ ] Side channels: framing, Engine configuration (ignored), Environment parameters, Stats.
- [ ] `--mode null-env`, launch-argument parsing, `-logFile`.
- [ ] `MockTrainer` protocol test.

**Gate:** trainer steps/s at 6 and 20 envs recorded in `benchmarks.md`. If it stays near ~424 steps/s,
the trainer is the bottleneck and engine speed is not the priority.

## M1 — Fixed step, physics, body, debug draw

- [ ] `SystemSet::fixed_add` / `fixed_tick`; accumulator in `Game::start`; `ArenaClock`.
- [ ] Jolt via vcpkg/flake; `JoltRuntime`; `PhysicsWorld`; layers; `PhysicsStepSys`, `BodySyncSys`.
- [ ] `BodyDef` loader; Unity exporter v1 (body file, rest pose track, limit probes).
- [ ] `RagdollBuilder` (segments, mass/COM/inertia, swing-twist constraints, self-collision filter).
- [ ] `Arena`, `ArenaSet`, `step_parallel`; `--mode bench`.
- [ ] `BodyRenderSys` sphere chains through `MapUploadData`; `dynamical --viewer`.
- [ ] `dynamical_tests` target with frames, body loading, ragdoll build, determinism tests; header check.

**Gate:** a ragdoll loaded from the Unity body file stands in its rest pose (held kinematically or with
zero gravity) and is drawn.

## M2 — Limits, drives, contacts, floor

- [ ] Limit mapping incl. twist sign; limit-probe tests.
- [ ] `JointDriveSys` drive mode A (Jolt motors, stiffness/damping, torque limit); applied torque readback.
- [ ] `ContactRecorder`, `ContactSys`, `SegmentContacts`; Unity friction/restitution combine.
- [ ] Box ground (flat and tilted); parity checklist values from the body file applied.
- [ ] `EnergySys`; ImGui Joints/Energy/Physics panels.

**Gate:** limit envelopes and drive step responses match Unity's contract; the three Jolt energy checks
pass.

## M3 — Agent runtime, parity mode

- [ ] Agent components, `BehaviorRegistry`, agent factory, `AgentId` allocator.
- [ ] Port the observation layout, action mapping, reward terms, terminations, randomisation, stats
      (Unity plan §3.3–3.8), one file per concept.
- [ ] `EpisodeSys`, `Outbox`, `EnvParams`, `ArenaRng`.
- [ ] Golden-file exporter in Unity; parity tests.

**Gate:** observations, rewards and terminations match Unity on identical poses (tolerances in
[testing.md](testing.md#parity-tests-against-unity-m3)).

## M4 — Full bridge, real runs

- [ ] Bridge gathers real agents from all arenas; `num_areas` honoured; NaN handling; arena rebuild on
      failure.
- [ ] `start-run` launches `dynamical_train` (wrapper/name so configs stay unmodified); `env_args`
      carry `--scenario`.
- [ ] Stats keys identical to Unity's; W&B dashboards unchanged.
- [ ] Performance tuning: Release Jolt, `--threads`, arenas per process.

**Gate:** an unmodified config trains, stats reach W&B, and `stop-run` exports the ONNX.

## M5 — Demo recorder

- [ ] Pose-track exporter in Unity for all demo clips; `PoseTrack` loader.
- [ ] `--mode record-demo`, `ActionLayout::inverse`, `DemoWriter`; `tools/check_demo.py`.

**Gate:** `.demo` files from Unity pose tracks load in GAIL.

## M6 — First GAIL walker from scratch

- [ ] Train the walker with the Unity line's config on `dynamical_train`.
- [ ] Eval script comparing behavioural stats with the frozen Unity reference at matched updates.

**Gate:** matches the frozen Unity reference on behavioural stats at matched updates.

## M7 — Exact energy, DeVita stats, slope

- [ ] Energy channel feeding observations/rewards per the Unity plan; drive mode B if the energetics
      line needs explicit torques.
- [ ] DeVita stats; slope randomisation via env params.

**Gate:** the energetics line reruns on the new simulation.

## M8 — Viewer, replay, video, eval

- [ ] `ReplayRecorder` / `ReplaySys`; timeline UI; synced ImPlot.
- [ ] Capsule impostor pipeline; perspective camera mode.
- [ ] Offscreen target, readback, ffmpeg pipe; `--capture`.
- [ ] `dynamical_onnx`, `PolicyRunner`, `PolicySys`; `--mode eval`.

**Gate:** videos and evals produced with no Unity build.

## M9 — Game integration, terrain, strike and duel

- [ ] Decide the time-scale approach ([game-integration.md](game-integration.md#the-time-scale-problem));
      add the real-time game speed.
- [ ] Heightfield ground from FastNoise2 / map chunks; terrain training line.
- [ ] `PhysicalWalker`, `WalkerLodSys`, `WalkerCommandSys`, `WalkerMapSyncSys`, `WalkerRecoverySys`;
      `PatherSys` exclusion.
- [ ] Strike and duel lines (multi-agent groups, self-play teams).

**Gate:** each line gated like M3 + M6; game frame time within budget at `max_physical_walkers`.
