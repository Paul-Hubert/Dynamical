# Decision log (autonomous run P0 → P3)

Choices made without a human, with the reason and the alternatives. Newest last.

| Date | Phase | Decision | Why | Alternatives |
|---|---|---|---|---|
| 2026-10-31 | P0 | Until P1 splits the library, `dynamical_tests` links only the sources that build without Vulkan (`util/log`, `scene/run_args`, the `imgdiff` core), not every existing source | Linking the whole tree would pull `main()`, Vulkan, SDL and OpenXR into the tests; step 9 already restricts CI to the non-Vulkan files | Link every source with `main` excluded (needs the Vulkan SDK in CI) |
