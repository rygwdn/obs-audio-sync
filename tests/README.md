# Tests

This directory contains unit tests for the OBS Audio Sync plugin.

## Running Tests

After building the project, you can run the tests using:

```bash
# Using CTest
ctest

# Or directly
./build/obs-audio-sync-tests
```

## Test Coverage

- **test-recording-scanner-standalone.cpp**: Tests for `RecordingScanner::isValidVideoFile()`
- **test-timeline-widget.cpp**: Tests for `TimelineWidget` UI component
- **test-audio-analyzer.cpp**: Tests for `AudioAnalyzer` basic functionality

## Test Framework

Tests use Qt Test framework (QTest) which is integrated with CMake's CTest.

## Note

Some tests may require OBS dependencies to be available. Tests that require OBS frontend API are marked accordingly.
