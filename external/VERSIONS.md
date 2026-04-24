# External Dependencies — Version Record

Snapshot date: 2026-04-24 (arc-engine extraction from gunslinger)

## Management

| Library | Form | Pin |
|---------|------|-----|
| jtask | git submodule | `67f2659a085840b7b524581e5abed8a0d997e3ae` |
| ltask | git submodule | `7c0123f71f9222a9e69831f74771ad1c1c2f0c8d` |
| quickjs | git submodule | `eb63c1d1329dd38694d4dcb5a1bfb07c5da8b0c2` |
| Tina | git submodule | `8a12c87e2f2dbf3c4e4d219b2372761326f2dd3d` |
| cimgui | source in-tree | 1.92.6 snapshot |
| sokol | source in-tree | Oct 2025 snapshot |
| stb | source in-tree | image 2.30 / ds 0.67 |
| fmod | source in-tree | Core + Studio binary SDK |

Submodule pins match arc-mapgen exactly (verified content-identical with
gunslinger's copy-form before extraction).

## License Summary

| Library | License | Source |
|---------|---------|--------|
| sokol | zlib | https://github.com/floooh/sokol |
| QuickJS-ng | MIT | https://github.com/lumin3000/quickjs/tree/coroutine-support |
| jtask | custom | Internal task scheduler built on ltask/Tina/QuickJS |
| ltask | MIT | https://github.com/cloudwu/ltask |
| Tina | MIT | https://github.com/slembcke/Tina |
| Dear ImGui (cimgui) | MIT | https://github.com/cimgui/cimgui |
| stb_image | Public domain | https://github.com/nothings/stb |
| stb_ds | Public domain | https://github.com/nothings/stb |
| FMOD | Proprietary (free for indie, revenue threshold) | https://www.fmod.com/ |

## Notes

- **sokol**: No formal version tags. Identified by CHANGELOG.md first entry (10-Oct-2025).
- **QuickJS-ng**: Version from `QJS_VERSION_MAJOR/MINOR/PATCH` defines in `quickjs.h`.
- **jtask**: Custom library, architecturally aligned with ltask but uses QuickJS instead of Lua.
  Depends on ltask (header-only: queue.h, atomic.h, service.h) and Tina (coroutines).
- **FMOD**: Binary-only distribution. dylib built from FMOD teamcity CI.
  Check fmod.com for version matching your dylib size/date.
- **cimgui**: C wrapper around Dear ImGui. Version from `IMGUI_VERSION` define in `imgui/imgui.h`.
