# AGENTS.md - AI Agent Guide for OBS Audio Sync Plugin

This document provides essential information for AI agents working on this codebase. It covers architecture, conventions, common tasks, and important considerations.

## Project Overview

**OBS Audio Sync Plugin** is a Qt-based plugin for OBS Studio that helps users identify and correct audio/video synchronization issues in recordings. The plugin:

- Scans and lists short recordings (< 30 seconds by default)
- Detects audio spikes using FFmpeg
- Visualizes audio waveforms and video frames on an interactive timeline
- Calculates and displays sync offsets between audio spikes and video frames
- Provides visual feedback with color-coded sync status

## Architecture

### Core Components

1. **AudioSyncPanel** (`src/audio-sync-panel.{h,cpp}`)
   - Main Qt widget panel for OBS
   - Orchestrates all other components
   - Handles UI interactions and state management

2. **RecordingScanner** (`src/recording-scanner.{h,cpp}`)
   - Discovers recordings in OBS recording directory
   - Filters by duration threshold (default: < 30 seconds)
   - Returns sorted list of matching recordings with metadata

3. **AudioAnalyzer** (`src/audio-analyzer.{h,cpp}`)
   - Extracts audio samples using FFmpeg/libav
   - Calculates amplitude envelope (RMS or peak)
   - Detects largest audio spike
   - Returns spike timestamp and amplitude data

4. **TimelineWidget** (`src/timeline-widget.{h,cpp}`)
   - Custom Qt widget for timeline visualization
   - Displays audio waveform, frame markers, time markers
   - Handles interactive spike position selection (click/drag)

5. **VideoExtractor** (`src/video-extractor.{h,cpp}`)
   - Extracts video frames using FFmpeg/libav
   - Caches frames for navigation
   - Provides frame-by-frame access within 4-second window

6. **Plugin Entry Point** (`src/plugin-main.cpp`)
   - OBS module load/unload handlers
   - Registers panel with OBS frontend API

### Dependencies

- **OBS Studio**: `libobs`, `obs-frontend-api` (for panel integration)
- **Qt6**: Core and Widgets (for UI)
- **FFmpeg/libav**: 
  - `libavformat` - Container format reading
  - `libavcodec` - Audio/video decoding
  - `libavutil` - Utilities
  - `libswscale` - Image scaling/conversion

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
# or directly
./build/obs-audio-sync-tests
```

### Important CMake Options

- `ENABLE_QT=ON` - Required for UI components
- `ENABLE_FRONTEND_API=ON` - Required for OBS panel integration
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` - Generates compile_commands.json for linting

### Linting Mode

When `CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set, the build system operates in "linting mode":
- Dependencies become optional (won't fail if not found)
- Allows generating compile_commands.json without full OBS build environment
- Useful for CI/CD and IDE integration

## Code Style & Conventions

### Formatting

- **Tool**: `clang-format` (version 19.1.1 or compatible)
- **Config**: `.clang-format` in project root
- **Script**: `./build-aux/run-clang-format`
  - Format: `./build-aux/run-clang-format`
  - Check: `./build-aux/run-clang-format --check`

### Handling Formatting Conflicts

**Important**: If a formatting rule conflicts with what Qt or the build system requires, **the rule should be removed or disabled**, not the code changed to satisfy the rule.

Examples of valid conflicts:
- **Qt MOC requirements**: Qt classes with signals/slots must have `Q_OBJECT` macro and use Qt's specific syntax. Don't change Qt-specific code to satisfy generic C++ formatting rules.
- **Build system requirements**: If the build system or platform-specific code requires certain patterns that conflict with formatting rules, adjust the formatting configuration accordingly.
- **FFmpeg API requirements**: FFmpeg's C API may require patterns that conflict with modern C++ formatting. Adjust formatting rules as needed.

**Never compromise build functionality or Qt requirements for linting rules.**

### Code Standards

- **Header Guards**: Use `#pragma once` (not include guards)
- **Includes**: 
  - No duplicate includes
  - Proper include order (system, OBS, Qt, FFmpeg, local)
- **Q_OBJECT**: All Qt classes with signals/slots must have `Q_OBJECT` macro
- **Naming**: Follow existing conventions (PascalCase for classes, camelCase for methods)
- **No TODOs**: Remove TODO/FIXME/HACK comments before committing

### CMake Formatting

- **Tool**: `gersemi` (for CMake files)
- **Script**: `./build-aux/run-gersemi`

## File Structure

```
src/
  ├── plugin-main.cpp          # Plugin entry point (OBS module load/unload)
  ├── plugin-support.h         # Plugin support headers
  ├── audio-sync-panel.{h,cpp} # Main panel widget
  ├── recording-scanner.{h,cpp} # Recording discovery
  ├── audio-analyzer.{h,cpp}   # Audio spike detection
  ├── video-extractor.{h,cpp}  # Frame extraction
  └── timeline-widget.{h,cpp}  # Timeline visualization

tests/
  ├── test-main.cpp            # Test entry point
  ├── test-recording-scanner-standalone.{h,cpp}
  ├── test-timeline-widget.{h,cpp}
  └── test-audio-analyzer.{h,cpp}

build-aux/
  ├── run-clang-format         # Formatting script
  ├── run-gersemi              # CMake formatting script
  └── run-all-checks           # Run all checks and tests (pre-commit)
```

## Common Tasks

### Adding a New Component

1. Create header file (`src/component-name.h`) with:
   - `#pragma once`
   - Class declaration
   - `Q_OBJECT` macro if Qt class with signals/slots

2. Create implementation file (`src/component-name.cpp`) with:
   - Includes (system, OBS, Qt, FFmpeg, local)
   - Implementation

3. Add to `CMakeLists.txt`:
   ```cmake
   target_sources(
     ${CMAKE_PROJECT_NAME}
     PRIVATE
       src/component-name.cpp
   )
   ```

4. If Qt class: Ensure `AUTOMOC ON` is set (already configured)

### Modifying UI Components

- All UI components are Qt widgets
- Use Qt Designer or manual Qt code
- Connect signals/slots for interactions
- Update `AudioSyncPanel` to integrate new UI elements

### Working with FFmpeg

- Always check return values from FFmpeg functions
- Properly allocate/free FFmpeg resources (use RAII where possible)
- Handle errors gracefully with user-friendly messages
- FFmpeg functions are C APIs - wrap in C++ classes for safety

### Testing

- Tests use Qt Test framework
- Test files in `tests/` directory
- Test executable: `${CMAKE_PROJECT_NAME}-tests`
- Run with `ctest` or directly execute test binary
- Tests may require OBS libraries (optional in build mode)

### Output Management

The check scripts (`run-all-checks`, `run-clang-format`, `run-gersemi`) automatically manage verbose output:
- Verbose output (formatting issues) is captured to temporary files
- If output is ≤ 20 lines, it's displayed at the end
- If output is > 20 lines, only the file path is shown
- **AI agents do not need to use `tail` or pipe commands** - the scripts handle output appropriately

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

### Platform Differences

- **macOS**: May need AGL framework stub (handled in CMakeLists.txt)
- **Windows**: FFmpeg DLLs must be bundled with plugin
- **Linux**: System FFmpeg or bundled libraries

### Error Handling

- Always check FFmpeg return values
- Handle missing/corrupted files gracefully
- Provide user-friendly error messages
- Log errors using `obs_log()` with appropriate log levels

## Development Workflow

1. **Make Changes**: Edit source files
2. **Run Docker Checks**: Run `./build-aux/run-docker --all` to verify builds across all platforms
3. **Build**: `cmake --build build_macos` (or your platform)
4. **Verify**: Check that plugin loads in OBS Studio
5. **Commit and Push**: Commit changes and push to the repository
6. **Watch CI**: After pushing, use `gh` to watch the CI workflow and fix any errors that arise
7. **Notify on Success**: When CI checks complete successfully, notify the user with an alert popup

### Pre-Commit Requirements

**All checks must pass before committing code.** Agents should always run Docker checks before committing:

```bash
./build-aux/run-docker --all
```

This verifies builds across all platforms and runs all necessary checks (formatting, linting, and tests).

**Important**: If any check fails, fix the issues before committing. The script will exit with a non-zero status if any checks fail.

## Issue Tracking with bd (beads)

**IMPORTANT**: This project uses **bd (beads)** for ALL issue tracking. Do NOT use markdown TODOs, task lists, or other tracking methods.

### Why bd?

- Dependency-aware: Track blockers and relationships between issues
- Git-friendly: Auto-syncs to JSONL for version control
- Agent-optimized: JSON output, ready work detection, discovered-from links
- Prevents duplicate tracking systems and confusion

### Quick Start

**Check for ready work:**
```bash
bd ready --json
```

**Create new issues:**
```bash
bd create "Issue title" -t bug|feature|task -p 0-4 --json
bd create "Issue title" -p 1 --deps discovered-from:bd-123 --json
bd create "Subtask" --parent <epic-id> --json  # Hierarchical subtask (gets ID like epic-id.1)
```

**Claim and update:**
```bash
bd update bd-42 --status in_progress --json
bd update bd-42 --priority 1 --json
```

**Complete work:**
```bash
bd close bd-42 --reason "Completed" --json
```

### Issue Types

- `bug` - Something broken
- `feature` - New functionality
- `task` - Work item (tests, docs, refactoring)
- `epic` - Large feature with subtasks
- `chore` - Maintenance (dependencies, tooling)

### Priorities

- `0` - Critical (security, data loss, broken builds)
- `1` - High (major features, important bugs)
- `2` - Medium (default, nice-to-have)
- `3` - Low (polish, optimization)
- `4` - Backlog (future ideas)

### Workflow for AI Agents

1. **Check ready work**: `bd ready` shows unblocked issues
2. **Claim your task**: `bd update <id> --status in_progress`
3. **Work on it**: Implement, test, document
4. **Discover new work?** Create linked issue:
   - `bd create "Found bug" -p 1 --deps discovered-from:<parent-id>`
5. **Complete**: `bd close <id> --reason "Done"`
6. **Commit together**: Always commit the `.beads/issues.jsonl` file together with the code changes so issue state stays in sync with code state

### Auto-Sync

bd automatically syncs with git:
- Exports to `.beads/issues.jsonl` after changes (5s debounce)
- Imports from JSONL when newer (e.g., after `git pull`)
- No manual export/import needed!

### GitHub Copilot Integration

If using GitHub Copilot, also create `.github/copilot-instructions.md` for automatic instruction loading.
Run `bd onboard` to get the content, or see step 2 of the onboard instructions.

### MCP Server (Recommended)

If using Claude or MCP-compatible clients, install the beads MCP server:

```bash
pip install beads-mcp
```

Add to MCP config (e.g., `~/.config/claude/config.json`):
```json
{
  "beads": {
    "command": "beads-mcp",
    "args": []
  }
}
```

Then use `mcp__beads__*` functions instead of CLI commands.

### Managing AI-Generated Planning Documents

AI assistants often create planning and design documents during development:
- PLAN.md, IMPLEMENTATION.md, ARCHITECTURE.md
- DESIGN.md, CODEBASE_SUMMARY.md, INTEGRATION_PLAN.md
- TESTING_GUIDE.md, TECHNICAL_DESIGN.md, and similar files

**Best Practice: Use a dedicated directory for these ephemeral files**

**Recommended approach:**
- Create a `history/` directory in the project root
- Store ALL AI-generated planning/design docs in `history/`
- Keep the repository root clean and focused on permanent project files
- Only access `history/` when explicitly asked to review past planning

**Example .gitignore entry (optional):**
```
# AI planning documents (ephemeral)
history/
```

**Benefits:**
- ✅ Clean repository root
- ✅ Clear separation between ephemeral and permanent documentation
- ✅ Easy to exclude from version control if desired
- ✅ Preserves planning history for archeological research
- ✅ Reduces noise when browsing the project

### CLI Help

Run `bd <command> --help` to see all available flags for any command.
For example: `bd create --help` shows `--parent`, `--deps`, `--assignee`, etc.

### Important Rules

- ✅ Use bd for ALL task tracking
- ✅ Always use `--json` flag for programmatic use
- ✅ Link discovered work with `discovered-from` dependencies
- ✅ Check `bd ready` before asking "what should I work on?"
- ✅ Store AI planning docs in `history/` directory
- ✅ Run `bd <cmd> --help` to discover available flags
- ❌ Do NOT create markdown TODO lists
- ❌ Do NOT use external issue trackers
- ❌ Do NOT duplicate tracking systems
- ❌ Do NOT clutter repo root with planning documents

For more details, see README.md and QUICKSTART.md.

## Debugging Tips

- Use `obs_log()` for logging (LOG_INFO, LOG_WARNING, LOG_ERROR)
- Check OBS log file for plugin messages
- FFmpeg errors: Check return codes and error messages
- Qt issues: Use Qt Creator or gdb/lldb with Qt symbols
- Memory issues: Use valgrind (Linux) or Address Sanitizer

## CI/CD

- Formatting checks run in GitHub Actions
- Linting checks run in GitHub Actions
- Build verification for multiple platforms
- Tests run in CI environment

### Watching Build Status

After pushing changes, agents should watch the CI workflow using `gh`:

```bash
gh run watch $(gh run list --commit $(git rev-parse HEAD) --json=databaseId --jq='.[0].databaseId') --exit-status --compact | cat
```

This command will:
- Find the most recent workflow run for the current commit
- Watch it in real-time with compact output
- Exit with the workflow's exit status
- Pipe through `cat` to avoid pager issues

**Important**: 
- If CI checks fail, fix the errors and push again
- When CI checks complete successfully, notify the user with an alert popup

## Documentation

- **README.md**: User-facing documentation
- **CONTRIBUTING.md**: Contribution guidelines
- **CHECK_STATUS.md**: Status of static checks and CI

## Key Files to Understand

1. **src/plugin-main.cpp**: Entry point, panel registration
2. **src/audio-sync-panel.{h,cpp}**: Main UI orchestration
3. **CMakeLists.txt**: Build configuration, dependencies

## When Making Changes

1. **Read existing code** to understand patterns
2. **Follow naming conventions** used in the codebase
3. **Maintain consistency** with existing code style
4. **Run Docker checks** using `./build-aux/run-docker --all` before committing
5. **Test thoroughly** - ensure all tests pass
6. **Update documentation** if adding features or changing behavior

**Important**: All Docker checks (builds, formatting, linting, CMake formatting, and tests) must pass before committing. The CI will reject commits that fail these checks.

## Common Pitfalls

- **Missing Q_OBJECT**: Qt classes with signals/slots must have `Q_OBJECT` macro
- **FFmpeg resource leaks**: Always free allocated resources
- **Thread safety**: OBS API calls must be on main thread
- **Include order**: System → OBS → Qt → FFmpeg → Local
- **CMake changes**: Remember to add new source files to `target_sources`
- **Platform-specific code**: Test on target platform or check CI

## Getting Help

- Review existing similar code in the codebase
- Check OBS Studio plugin documentation
- Review FFmpeg/libav documentation for media processing
- Check Qt documentation for UI components

---

**Last Updated**: 2025-01-27
**Project**: OBS Audio Sync Plugin
**Author**: Ryan Wooden
