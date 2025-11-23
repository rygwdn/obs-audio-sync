# OBS Audio Sync Plugin

A plugin for OBS Studio that helps users identify and correct audio/video synchronization issues in their recordings by analyzing audio spikes and correlating them with video frames.

## Features

- **Recording Discovery**: Automatically scans and lists recordings under a configurable duration threshold (default: 15 seconds)
- **Audio Spike Detection**: Automatically finds the largest audio spike in recordings using FFmpeg
- **Timeline Visualization**: Interactive timeline showing:
  - Audio waveform visualization
  - Frame markers based on video FPS
  - Time markers
  - Clickable/draggable spike position indicator
- **Video Frame Navigation**: Browse video frames in a 4-second window around the audio spike
- **Sync Calculation**: Real-time display of time and frame difference between audio spike and selected video frame
- **Visual Feedback**: Color-coded sync status (green/yellow/red) based on offset magnitude

## Requirements

- OBS Studio 31.1.1 or later
- Qt6
- FFmpeg/libav libraries (libavformat, libavcodec, libavutil, libswscale)

## Building

### Prerequisites

- CMake 3.28 or later
- OBS Studio development libraries
- Qt6 development libraries
- FFmpeg/libav development libraries
- Platform-specific build tools:
  - **macOS**: Xcode 16.0+
  - **Windows**: Visual Studio 2022
  - **Linux**: GCC/Clang, pkg-config, build-essential

### Build Steps

1. Clone the repository:
```bash
git clone https://github.com/rygwdn/obs-audio-sync.git
cd obs-audio-sync
```

2. Configure the build:
```bash
# macOS
cmake --preset macos

# Windows
cmake --preset windows-x64

# Linux
cmake --preset ubuntu-x86_64
```

3. Build:
```bash
cmake --build build_macos  # or your platform's build directory
```

4. Install:
The plugin will be built to `rundir/RelWithDebInfo/` (or your build configuration directory).

## Usage

1. **Open OBS Studio** and ensure the plugin is loaded
2. **Access the panel**: The "Audio Sync" panel should appear in OBS (View → Docks → Audio Sync)
3. **Select a recording**: Double-click on a recording from the list (recordings under 15 seconds)
4. **Review the analysis**:
   - The timeline shows the audio waveform and detected spike
   - Navigate through video frames using Previous/Next buttons
   - The sync offset is displayed in real-time
5. **Adjust spike position**: Click or drag the spike marker on the timeline to fine-tune
6. **Identify sync issues**: Use the color-coded offset display to see if audio and video are in sync

## Development

### Code Formatting

This project uses `clang-format` for code formatting. Format your code before committing:

```bash
./build-aux/run-clang-format
```

Check formatting without making changes:
```bash
./build-aux/run-clang-format --check
```

### Code Linting

This project uses `clang-tidy` for static analysis:

```bash
./build-aux/run-clang-tidy --check
```

### Running Tests

After building, run the test suite:

```bash
ctest
# or directly
./build_macos/obs-audio-sync-tests
```

### Running All Checks

Before committing, run all checks (formatting, linting, CMake formatting, and tests):

```bash
./build-aux/run-all-checks
```

This ensures all pre-commit requirements are met. Individual checks can be skipped with `--skip-*` flags if needed.

### Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines on:
- Code formatting standards
- Linting requirements
- Testing requirements
- Pull request process

## Architecture

### Components

- **AudioSyncPanel**: Main Qt widget panel for OBS
- **RecordingScanner**: Discovers and filters recordings by duration
- **AudioAnalyzer**: Extracts audio samples and detects spikes using FFmpeg
- **TimelineWidget**: Custom widget for timeline visualization
- **VideoExtractor**: Extracts video frames using FFmpeg

### Dependencies

- **Qt6**: Core and Widgets for UI
- **obs-frontend-api**: OBS Studio frontend API for panel integration
- **FFmpeg/libav**: Audio and video processing
  - libavformat: Container format reading
  - libavcodec: Audio/video decoding
  - libavutil: Utilities
  - libswscale: Image scaling/conversion

## Future Enhancements

- Automatic frame detection using computer vision
- Direct audio offset adjustment on OBS audio sources
- Batch processing for multiple recordings
- Advanced analysis with multiple spike detection
- Statistical analysis of sync drift

## License

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

See [LICENSE](LICENSE) for full details.

## Author

Ryan Wooden

## Acknowledgments

- Built using the [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate)
- Uses FFmpeg/libav for media processing
