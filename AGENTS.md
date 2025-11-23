# AGENTS.md - AI Agent Guide for OBS Audio Sync Plugin

This document provides essential information for AI agents working on this codebase. It covers architecture, conventions, common tasks, and important considerations.

## Project Overview

**OBS Audio Sync Plugin** is a Qt-based plugin for OBS Studio that helps users identify and correct audio/video synchronization issues in recordings. The plugin:

- Scans and lists short recordings (< 15 seconds by default)
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
   - Filters by duration threshold (default: < 15 seconds)
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

### Linting

- **Tool**: `clang-tidy`
- **Config**: `.clang-tidy` in project root
- **Script**: `./build-aux/run-clang-tidy`
  - Check: `./build-aux/run-clang-tidy --check`

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
  ├── run-clang-tidy           # Linting script
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
- Tests may require OBS libraries (optional in linting mode)

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
2. **Run All Checks**: Run `./build-aux/run-all-checks` to verify all checks pass
3. **Build**: `cmake --build build_macos` (or your platform)
4. **Verify**: Check that plugin loads in OBS Studio

### Pre-Commit Requirements

**All checks must pass before committing code.** Run the comprehensive check script:

```bash
./build-aux/run-all-checks
```

This script runs:
- Code formatting check (clang-format)
- CMake formatting check (gersemi)
- Code linting check (clang-tidy)
- Test suite (if build directory exists)

If any check fails, fix the issues before committing. The script will exit with a non-zero status if any checks fail.

You can also run individual checks:
- Format code: `./build-aux/run-clang-format`
- Check formatting: `./build-aux/run-clang-format --check`
- Check linting: `./build-aux/run-clang-tidy --check`
- Format CMake: `./build-aux/run-gersemi`
- Check CMake formatting: `./build-aux/run-gersemi --check`
- Run tests: `ctest` or `./build_macos/obs-audio-sync-tests`

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

To watch the build status for the current commit:

```bash
gh run watch $(gh run list --commit $(git rev-parse HEAD) --json=databaseId --jq='.[0].databaseId') --exit-status --compact | cat
```

This command will:
- Find the most recent workflow run for the current commit
- Watch it in real-time with compact output
- Exit with the workflow's exit status
- Pipe through `cat` to avoid pager issues

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
4. **Run all checks** using `./build-aux/run-all-checks` before committing
5. **Test thoroughly** - ensure all tests pass
6. **Format and lint** - all checks must pass before committing
7. **Update documentation** if adding features or changing behavior

**Important**: All checks (formatting, linting, CMake formatting, and tests) must pass before committing. The CI will reject commits that fail these checks.

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
