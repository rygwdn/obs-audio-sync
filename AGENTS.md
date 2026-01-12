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
2. **RecordingScanner** (`src/recording-scanner.{h,cpp}`)
   - Discovers recordings in OBS recording directory, filters, returns sorted list
3. **AudioAnalyzer** (`src/audio-analyzer.{h,cpp}`)
   - Detects largest audio spike
4. **TimelineWidget** (`src/timeline-widget.{h,cpp}`)
   - Custom Qt widget for timeline visualization
   - Displays audio waveform, frame markers, time markers
5. **VideoExtractor** (`src/video-extractor.{h,cpp}`)
   - Extracts video frames using FFmpeg/libav
6. **Plugin Entry Point** (`src/plugin-main.cpp`)
   - OBS module load/unload handlers

### Dependencies

- **OBS Studio**: `libobs`, `obs-frontend-api` (for panel integration)
- **Qt6**: Core and Widgets (for UI)
- **FFmpeg/libav**

## Build System

### CMake Configuration

- **CMakeLists.txt**: Main build configuration
- **CMakePresets.json**: Platform-specific presets (macos, windows, linux)
- **cmake/**: Platform-specific CMake modules


### Code Standards

- **Header Guards**: Use `#pragma once` (not include guards)
- **Q_OBJECT**: All Qt classes with signals/slots must have `Q_OBJECT` macro
- **Naming**: Follow existing conventions (PascalCase for classes, camelCase for methods)
- **No TODOs**: Remove TODO/FIXME/HACK comments before committing

## Common Tasks

### Adding a New Component

1. Create header file (`src/component-name.h`) with:
2. Create implementation file (`src/component-name.cpp`) with:
3. Add to `CMakeLists.txt`:

### Testing

- Tests use Qt Test framework
- Test files in `tests/` directory

### Thread Safety

- OBS frontend API calls must be on main thread
- FFmpeg operations can be expensive - consider async/background processing
- Qt signals/slots are thread-safe for cross-thread communication

### Error Handling

- Always check FFmpeg return values
- Handle missing/corrupted files gracefully
- Provide user-friendly error messages
- Log errors using `obs_log()` with appropriate log levels

## Development Workflow

1. **Make Changes**: Edit source files
2. **Run Docker Checks**: Run `./build-aux/run-docker --all` to verify builds across all platforms
5. **Commit and Push**: Commit changes and push to the repository
6. **Watch CI**: After pushing, use `gh` to watch the CI workflow and fix any errors that arise
7. **Notify on Success**: When CI checks complete successfully, notify the user with an alert popup

### Pre-Commit Requirements

**All checks must pass before committing code.** Agents should always run Docker checks before committing:

```bash
./build-aux/run-docker --all
```

**Important**: If any check fails, fix the issues before committing. The script will exit with a non-zero status if any checks fail.

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

### Installing Artifacts from CI (macOS)

The project includes a script to easily download and install the latest macOS build from GitHub Actions:

```bash
./scripts/install-macos-from-ci.sh [run_id]
```

**Usage:**
- **Without arguments**: Downloads and installs the latest successful build from the current branch
- **With run_id**: Downloads and installs a specific CI run (e.g., `./scripts/install-macos-from-ci.sh 20931605419`)

**What it does:**
1. Fetches the specified CI run (or latest from current branch)
2. Downloads the macOS universal binary artifact
3. Extracts the plugin bundle
4. Removes the old plugin from OBS plugins directory
5. Installs the new plugin
6. Reminds user to restart OBS Studio

**Location**: Plugin is installed to `~/Library/Application Support/obs-studio/plugins/obs-audio-sync.plugin`

**Example workflow:**
```bash
# Make changes
git add -u && git commit -m "Fix bug"
git push

# Watch CI build
gh pr checks <pr-number> --watch

# Install the new build (once CI completes)
./scripts/install-macos-from-ci.sh

# Restart OBS to load the updated plugin
```

## Documentation

- **README.md**: User-facing documentation
- **CONTRIBUTING.md**: Contribution guidelines
- **CHECK_STATUS.md**: Status of static checks and CI

## Getting Help

- Review existing similar code in the codebase
- Check OBS Studio plugin documentation
- Review FFmpeg/libav documentation for media processing
- Check Qt documentation for UI components
