# GitHub Copilot Instructions for OBS Audio Sync Plugin

## Project Overview

**OBS Audio Sync Plugin** is a Qt-based plugin for OBS Studio that helps users identify and correct audio/video synchronization issues in recordings.

**Key Features:**
- Scans and lists short recordings (< 15 seconds by default)
- Detects audio spikes using FFmpeg
- Visualizes audio waveforms and video frames on an interactive timeline
- Calculates and displays sync offsets between audio spikes and video frames
- Provides visual feedback with color-coded sync status

## Tech Stack

- **Language**: C++17
- **UI Framework**: Qt6 (Core, Widgets)
- **Build System**: CMake
- **Media Processing**: FFmpeg/libav (libavformat, libavcodec, libavutil, libswscale)
- **OBS Integration**: libobs, obs-frontend-api
- **Testing**: Qt Test framework
- **CI/CD**: GitHub Actions

## Coding Guidelines

### Code Style
- **Formatting**: Use `clang-format` (version 19.1.1 or compatible)
- **Config**: `.clang-format` in project root
- **CMake Formatting**: Use `gersemi` for CMake files
- **Header Guards**: Use `#pragma once` (not include guards)
- **Includes**: Proper include order (system, OBS, Qt, FFmpeg, local)
- **Naming**: PascalCase for classes, camelCase for methods
- **No TODOs**: Remove TODO/FIXME/HACK comments before committing

### Formatting Conflicts

**Important**: If a formatting rule conflicts with what Qt or the build system requires, **the rule should be removed or disabled**, not the code changed to satisfy the rule.

Examples of valid conflicts:
- **Qt MOC requirements**: Qt classes with signals/slots must have `Q_OBJECT` macro
- **Build system requirements**: Platform-specific code patterns
- **FFmpeg API requirements**: C API patterns that conflict with modern C++ formatting

**Never compromise build functionality or Qt requirements for linting rules.**

### Testing
- Tests use Qt Test framework
- Test files in `tests/` directory
- Test executable: `${CMAKE_PROJECT_NAME}-tests`
- Run with `ctest` or directly execute test binary
- All tests must pass before committing

### Pre-Commit Requirements

**All checks must pass before committing code.** Run:

```bash
./build-aux/run-all-checks
```

This runs:
- Code formatting (clang-format) - automatically fixes issues
- CMake formatting (gersemi) - automatically fixes issues
- Test suite - **must exist and pass**

## Issue Tracking with bd

**CRITICAL**: This project uses **bd (beads)** for ALL task tracking. Do NOT create markdown TODO lists.

### Essential Commands

```bash
# Find work
bd ready --json                    # Unblocked issues
bd stale --days 30 --json          # Forgotten issues

# Create and manage
bd create "Title" -t bug|feature|task -p 0-4 --json
bd create "Subtask" --parent <epic-id> --json  # Hierarchical subtask
bd update <id> --status in_progress --json
bd close <id> --reason "Done" --json

# Search
bd list --status open --priority 1 --json
bd show <id> --json

# Sync (CRITICAL at end of session!)
bd sync  # Force immediate export/commit/push
```

### Workflow

1. **Check ready work**: `bd ready --json`
2. **Claim task**: `bd update <id> --status in_progress`
3. **Work on it**: Implement, test, document
4. **Discover new work?** `bd create "Found bug" -p 1 --deps discovered-from:<parent-id> --json`
5. **Complete**: `bd close <id> --reason "Done" --json`
6. **Sync**: `bd sync` (flushes changes to git immediately)
7. **Commit together**: Always commit `.beads/issues.jsonl` with code changes

### Priorities

- `0` - Critical (security, data loss, broken builds)
- `1` - High (major features, important bugs)
- `2` - Medium (default, nice-to-have)
- `3` - Low (polish, optimization)
- `4` - Backlog (future ideas)

## Project Structure

```
obs-audio-sync/
├── src/                    # Source code
│   ├── plugin-main.cpp     # Plugin entry point
│   ├── audio-sync-panel.{h,cpp}  # Main panel widget
│   ├── recording-scanner.{h,cpp}  # Recording discovery
│   ├── audio-analyzer.{h,cpp}    # Audio spike detection
│   ├── video-extractor.{h,cpp}   # Frame extraction
│   └── timeline-widget.{h,cpp}   # Timeline visualization
├── tests/                  # Test files
├── build-aux/              # Build scripts
│   ├── run-clang-format    # Formatting script
│   ├── run-gersemi         # CMake formatting script
│   └── run-all-checks      # Pre-commit checks
├── cmake/                  # CMake modules
└── .beads/
    └── issues.jsonl        # Git-synced issue storage
```

## Build System

### CMake Configuration

- **CMakeLists.txt**: Main build configuration
- **CMakePresets.json**: Platform-specific presets (macos, windows, linux)
- **cmake/**: Platform-specific CMake modules

### Build Commands

```bash
# Configure (platform-specific)
cmake --preset macos        # macOS
cmake --preset windows-x64 # Windows
cmake --preset ubuntu-x86_64 # Linux

# Build
cmake --build build_macos  # or your platform's build directory

# Run tests
ctest
```

### Important CMake Options

- `ENABLE_QT=ON` - Required for UI components
- `ENABLE_FRONTEND_API=ON` - Required for OBS panel integration
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` - Generates compile_commands.json for linting

## Important Considerations

### OBS Plugin Lifecycle

1. **Load**: `obs_module_load()` - Creates and registers panel
2. **Runtime**: Panel is active, user interacts
3. **Unload**: `obs_module_unload()` - Removes panel, cleans up

### Memory Management

- Use Qt's parent-child ownership where possible
- Clean up FFmpeg resources in destructors
- Panel is deleted in `obs_module_unload()`

### Thread Safety

- OBS frontend API calls must be on main thread
- FFmpeg operations can be expensive - consider async/background processing
- Qt signals/slots are thread-safe for cross-thread communication

### Error Handling

- Always check FFmpeg return values
- Handle missing/corrupted files gracefully
- Provide user-friendly error messages
- Log errors using `obs_log()` with appropriate log levels

## CLI Help

Run `bd <command> --help` to see all available flags for any command.
For example: `bd create --help` shows `--parent`, `--deps`, `--assignee`, etc.

## Important Rules

- ✅ Use bd for ALL task tracking
- ✅ Always use `--json` flag for programmatic use
- ✅ Run `bd sync` at end of sessions
- ✅ Run `bd <cmd> --help` to discover available flags
- ✅ Store AI planning docs in `history/` directory
- ✅ All checks must pass before committing
- ❌ Do NOT create markdown TODO lists
- ❌ Do NOT use external issue trackers
- ❌ Do NOT duplicate tracking systems
- ❌ Do NOT clutter repo root with planning documents

---

**For detailed workflows and advanced features, see [AGENTS.md](../AGENTS.md)**
