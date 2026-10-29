# ESP-Mosaico application rules

These rules supplement the [repository rules](../AGENTS.md) for applications in
`projects/`. Apply them to new or changed application-owned ESP-IDF modules;
preserve existing public API compatibility and dependency-owned code conventions.

## Development Guidelines

1. **Organize components by responsibility and keep the entry point minimal.** `app_main()` should only handle initialization, dependency wiring, and startup sequencing. Keep component dependency directions clear; circular dependencies are prohibited.

2. **Define contracts for public interfaces.** Keep public API comments concise; put detailed ownership, lifecycle, calling context, thread safety, timeouts and error contracts in the component README, with units and ranges beside fields. Each README must describe responsibilities, dependencies and minimal usage. Comments should explain design rationale and constraints.

3. **Distinguish application components from third-party components.** Keep application components under `components/app_*`; `main` composes them. Preserve the original names of third-party components and manage them centrally. Do not directly modify components in `managed_components/`; apply changes through patches.

4. **Separate public interfaces from internal implementations.** Place public headers in `include/` and expose internal headers only through private include paths. Components must not include other components’ private headers or access their internal structures.

5. **Encapsulate module state.** Manage state through private contexts or classes. Avoid scattered global variables. Exchanging application state through cross-module `extern` global variables is prohibited.

6. **Prefer composition and limit abstractions.** Compose functionality through members, contexts, and small interfaces. Avoid inheritance hierarchies and generic frameworks that serve no practical purpose.

7. **Actively use Git for code management.** Before starting, check the current branch, `git status`, and existing diffs. Use a separate branch for each independent task, such as `feat/xxx` or `fix/xxx`. For parallel tasks, use separate worktrees under `Projects` to avoid modifying the same working directory concurrently.

8. **Keep commits focused and reviewable.** Each commit should serve one clear purpose. Commit functional changes, refactoring, and formatting-only changes separately.

Project creation initializes an independent Git repository on `main` and supplies
`tools/quality/` configuration. It does not stage files, create commits, configure
remotes or install hooks. Follow the generated quality guide to enable checks;
the shared checker belongs to utils and is referenced by relative path. Preserve
template-owned settings and keep application-specific policy in the application.


## Debugging Guidelines

1. **Prefer structured interfaces.** Use CLI/RPC interfaces with clear semantics to query connection status, firmware identity, memory usage, and operation results.

2. **Read diagnostic information within a defined scope.** Filter logs by time range, keyword, or failure. Save complete logs and core dumps as files, and let the model read relevant sections as needed.

3. **Delegate repetitive workflows to programs.** Prefer scripts, simulators, and automated assertions. Configure timeouts, state checks, and bounded retries, and return summaries with evidence of failures.

4. **Take screenshots as needed.** Use screenshots to diagnose layout, text, and rendering issues and to verify key steps. Avoid taking screenshots after every action or repeatedly inspecting unchanged screens.

5. **Interact with the screen with a clear purpose.** Use touch, swipe, and coordinate-based clicks to verify real interactions or perform functions that lack usable interfaces. Avoid long sequences of blind actions. When testing UI flows, do not replace interaction validation with direct calls to application logic.


## Stack and allocation

- Avoid local arrays or aggregate objects larger than 128 bytes on task stacks
  unless justified by the task's stack budget, including the call chain.
  Prefer instance-owned buffers; do not move mutable buffers to hidden globals.

- Prefer reusable instance buffers, memory pools or ring buffers in high-frequency
  or latency-sensitive paths. Avoid repeated heap allocation in those paths.
