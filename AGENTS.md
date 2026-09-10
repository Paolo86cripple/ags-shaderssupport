# AGENTS.md

## Project

This repository is the working fork for **Adventure Game Studio shader support**.

Current integration state:

- `master` contains the merged native librashader runtime.
- PR #1 — **Integrate librashader into native AGS OpenGL pipeline** — was squash-merged on 2026-09-10.
- Merge commit: `960f9630a9390e52cd9d8dcd0d4ba058fcf2315b`.

The goal is to add RetroArch-style `.slangp` shader preset support to the native AGS OpenGL renderer through **librashader**, while preserving AGS' existing behavior when shaders are not requested or cannot be used.

---

## Core design rules

These rules are deliberate project decisions and should not be changed casually.

1. **Do not globally migrate AGS to OpenGL 3.3.**
   - Legacy AGS OpenGL behavior remains the default.
   - OpenGL 2.1 compatibility remains the normal no-shader path.

2. **Request OpenGL 3.3 compatibility only on Linux desktop and only when an external shader is configured.**
   - Shader selection currently comes from `AGS_SHADER_CHAIN`.
   - `AGS_SHADER` remains a compatibility fallback for the earlier prototype.

3. **Failure to create the requested OpenGL 3.3 path must not prevent the game from starting.**
   - Retry using the legacy OpenGL 2.1 request.
   - Continue running AGS without the external shader.
   - Never try to initialize librashader after such a fallback.

4. **Keep the change isolated to the OpenGL renderer.**
   - Main integration path: `Engine/gfx/ali3dogl.cpp`.
   - Do not alter unrelated renderers merely to support librashader.

5. **librashader stays dynamically loaded.**
   - AGS must still start normally if librashader is unavailable and no shader is required.
   - The integration must not create a mandatory runtime dependency for normal AGS use.

6. **Preserve AGS rendering state.**
   - AGS renders the completed frame first.
   - librashader runs immediately before `SDL_GL_SwapWindow`.
   - Host OpenGL state disturbed by librashader must be restored before returning to AGS.

7. **Prefer minimal, reviewable changes.**
   - Avoid unrelated cleanup or refactors in shader commits.
   - Keep compatibility behavior explicit and testable.

8. **Do not compile or build this project locally.**
   - AGS, librashader integration builds, automated tests, packaging builds and reproducible artifacts must be produced through GitHub Actions workflows.
   - Local machines are for running and validating workflow-produced binaries/artifacts on real hardware, not for compiling the project.
   - Do not ask the user to run local `cmake`, compiler, Cargo, linker or packaging commands for this project.
   - When hardware testing is required, provide a workflow that produces the exact test artifact and then give instructions only for downloading/running that artifact and collecting logs/results.

---

## Current architecture

The OpenGL display/context path is:

`OGLGraphicsDriver::SetDisplayMode()`
→ `InitGlScreen()`
→ `CreateWindowAndGlContext()`
→ SDL OpenGL window/context creation

When a shader is requested on Linux desktop:

1. AGS requests an OpenGL 3.3 compatibility context.
2. If window or context creation fails, AGS retries with OpenGL 2.1.
3. The driver records whether the shader-capable context path actually succeeded.
4. `FirstTimeInit()` loads the librashader pipeline only when that recorded state permits it.
5. If fallback occurred, AGS continues without the external shader.

The shader bridge resolves the GL 3.x entry points it needs through `SDL_GL_GetProcAddress`; AGS' existing OpenGL 2.1 GLAD usage is intentionally left intact.

---

## Validation status

Last roadmap update: **2026-09-10**

### Completed

- [x] Replace the original custom RetroArch-style preset/parser prototype with a native librashader bridge.
- [x] Dynamically load librashader rather than making it a mandatory AGS dependency.
- [x] Check librashader ABI 2 at runtime.
- [x] Pass the completed AGS framebuffer through librashader before `SDL_GL_SwapWindow`.
- [x] Request OpenGL 3.3 compatibility only when a shader is explicitly configured on Linux desktop.
- [x] Preserve legacy OpenGL 2.1 behavior when no shader is configured.
- [x] Retry OpenGL 2.1 if shader-requested OpenGL 3.3 window/context creation fails.
- [x] Persist the result of the context-selection policy so librashader is disabled after a GL 2.1 fallback.
- [x] Add first-frame librashader validation.
- [x] Add bounded runtime timeout and log/error checks.
- [x] Add a dedicated no-shader legacy OpenGL runtime smoke test.
- [x] Run CI from `pull_request` plus manual `workflow_dispatch`, avoiding duplicate branch push + PR runs.
- [x] Build AGS successfully in CI after the fallback-state fix.
- [x] Build external librashader `0.12.0` successfully in CI.
- [x] Pass Mesa software OpenGL verification under Xvfb.
- [x] Pass legacy OpenGL runtime smoke with no shader configured.
- [x] Pass librashader runtime smoke, including shader loading and first completed filter call.
- [x] Restore six unrelated workflow files that had accidentally appeared as deletions in PR #1.
- [x] Verify that the four `Engine/CMakeLists.txt` source entries retained by PR #1 are required: removing them caused the AGS build to fail.
- [x] Complete final code/scope review of PR #1.
- [x] Pass final CI run #20 on head `6c4079691aecbb4defee734a64fc94492831d44e`.
- [x] Squash-merge PR #1 into `master` as `960f9630a9390e52cd9d8dcd0d4ba058fcf2315b`.

### Current CI baseline

The final validated PR run successfully completed all of these steps:

- AGS configure
- AGS build
- librashader 0.12.0 build
- pinned smoke assets
- Mesa/OpenGL verification
- legacy no-shader OpenGL smoke
- librashader runtime smoke
- diagnostic artifact upload

This is the minimum regression baseline future shader-related changes should preserve.

**Build policy:** all future builds and automated validation for this project must run in GitHub Actions. Real-hardware testing must use workflow-produced artifacts rather than locally compiled binaries.

---

## Roadmap

### Phase 1 — Core librashader integration

Status: **merged and functionally complete in CI; post-merge hardware validation still required before release/upstream submission**.

Remaining work:

- [ ] Produce a downloadable real-hardware test artifact through GitHub Actions.
- [ ] Test that workflow-produced artifact on a real Linux desktop GPU rather than only Mesa software rendering/Xvfb.
- [ ] Verify AMD/Mesa hardware behavior on CachyOS/KDE.
- [ ] Verify no-shader behavior on real hardware.
- [ ] Verify a simple single-pass shader on real hardware.
- [ ] Run the definitive complex stress preset: **ScaleFX + RAA + AA**.
- [ ] Verify windowed mode.
- [ ] Verify fullscreen mode.
- [ ] Verify resize and display-mode changes.
- [ ] Verify repeated mode/context recreation does not leave stale shader resources.
- [ ] Verify repeated Alt-Tab/context transitions under KDE Wayland.
- [ ] Confirm shader-disabled fallback behavior on a system/context where OpenGL 3.3 creation is intentionally unavailable or forced to fail, if a deterministic hardware test is practical.
- [ ] Test at least one real AGS game in addition to the automated test game.

### Phase 2 — Shader preset compatibility

- [ ] Test multi-pass `.slangp` presets.
- [ ] Test presets using relative shader/resource paths.
- [ ] Test common RetroArch slang shader families.
- [ ] Validate feedback/history/pass dependencies used by more complex presets.
- [ ] Define clear behavior for unsupported presets/features.
- [ ] Improve error reporting without making shader failures fatal to the game.

### Phase 3 — Correct source/output geometry

The current bridge filters the completed backbuffer and currently uses the same dimensions for source and output.

- [ ] Separate native game/source resolution from final drawable/output resolution.
- [ ] Pass accurate viewport/output sizes to librashader.
- [ ] Verify aspect-ratio handling.
- [ ] Verify integer scaling interactions.
- [ ] Verify letterboxing/pillarboxing behavior.
- [ ] Verify high-DPI drawable size behavior.

### Phase 4 — Runtime integration and configuration

The environment variables are currently a development/bootstrap interface.

- [ ] Define the permanent AGS-side shader configuration interface.
- [ ] Decide how shader presets are selected per game/profile.
- [ ] Keep shaders optional and OFF by default unless explicitly configured.
- [ ] Add clean runtime diagnostics for selected preset, backend and fallback state.
- [ ] Decide whether shader reload/change can safely occur without restarting the engine.

### Phase 5 — AGS Shader Launcher

- [ ] Implement the standalone launcher/profile layer.
- [ ] Detect AGS games and associate per-game shader profiles.
- [ ] Browse/select `.slangp` presets.
- [ ] Store per-game shader configuration persistently.
- [ ] Launch AGS with the correct shader configuration.
- [ ] Expose safe fallback/no-shader behavior clearly to the user.
- [ ] Add preset discovery/management without modifying upstream shader files unnecessarily.

### Phase 6 — Packaging and release

Only begin release packaging after the core runtime path is validated on real hardware.

- [x] Finish review of PR #1.
- [x] Mark PR #1 ready for review.
- [x] Resolve merge-blocking review findings.
- [x] Merge the librashader runtime work.
- [ ] Complete post-merge AMDGPU/CachyOS hardware validation using workflow-produced artifacts.
- [ ] Define supported librashader runtime/package strategy for Linux distributions.
- [ ] Add Arch/CachyOS-oriented packaging through GitHub Actions.
- [ ] Document runtime dependencies and optional shader assets.
- [ ] Produce a reproducible release build through GitHub Actions.
- [ ] Add release notes describing compatibility and fallback behavior.

---

## Known limitations

- The current automated first-frame marker proves CPU-side completion of the filter call; it is not pixel-correctness validation and does not by itself prove GPU completion.
- CI currently uses Mesa software rendering/Xvfb, not a physical GPU/display session.
- Native-resolution source and final output dimensions are not yet modeled separately in the bridge.
- Complex multi-pass preset coverage is not yet complete.
- The environment-variable configuration is temporary bootstrap plumbing, not the final launcher-facing configuration API.

---

## Files of interest

Primary renderer integration:

- `Engine/gfx/ali3dogl.cpp`
- `Engine/gfx/ali3dogl.h`

librashader bridge:

- `Engine/gfx/ags_shader_pipeline.cpp`
- `Engine/gfx/ags_shader_pipeline.h`

CI/runtime validation:

- `.github/workflows/librashader-ci.yml`

When changing these files, check whether this roadmap and validation status need updating in the same work session.

---

## Development workflow

For shader-related work:

1. Work on focused feature branches.
2. Keep individual commits narrow and reversible.
3. Review the resulting diff for unrelated changes before relying on CI.
4. Preserve the no-shader OpenGL path.
5. Build AGS only through GitHub Actions workflows.
6. Run the legacy no-shader automated smoke only through GitHub Actions.
7. Run the librashader automated shader smoke only through GitHub Actions.
8. Do not require or instruct the user to compile AGS, librashader, Rust/Cargo dependencies or packaging locally.
9. For graphics/context changes, have GitHub Actions produce a downloadable test artifact and validate that artifact on real hardware before declaring the work release-ready.
10. Update this `AGENTS.md` whenever roadmap status, architecture decisions, validation coverage, build policy, or known limitations materially change.

Do not mark roadmap items complete from code inspection alone when they require runtime or hardware validation.

---

## Post-merge definition of done for the librashader runtime

The core integration is merged into the user's fork. Before release or upstream submission, require:

- [x] AGS builds successfully in GitHub Actions.
- [x] No-shader legacy OpenGL smoke passes in GitHub Actions.
- [x] librashader single-pass runtime smoke passes in GitHub Actions.
- [x] Shader-requested OpenGL 3.3 path is isolated to Linux desktop.
- [x] OpenGL 2.1 fallback disables external shader initialization.
- [x] Final PR code/scope review completed.
- [x] PR #1 merged into `master`.
- [ ] Workflow-produced real Linux hardware test artifact passes on AMDGPU/CachyOS.
- [ ] Windowed/fullscreen/resize/Alt-Tab behavior is validated on hardware.
- [ ] The **ScaleFX + RAA + AA** stress preset is validated on hardware.
- [ ] At least one representative real AGS game is validated.

Hardware validation is a release/upstream gate, not a prerequisite for the already-completed merge into the user's fork.
