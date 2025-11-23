# Contributing to OBS Audio Sync Plugin

Thank you for your interest in contributing! This document provides guidelines for code formatting and linting.

## Code Formatting

This project uses `clang-format` for C/C++ code formatting. The configuration is in `.clang-format`.

### Formatting Your Code

To format your code:

```bash
# Format all source files
./build-aux/run-clang-format

# Format specific files
./build-aux/run-clang-format src/your-file.cpp src/your-file.h

# Check formatting without making changes
./build-aux/run-clang-format --check
```

### Formatting Requirements

- All code must be formatted with `clang-format` before committing
- The CI will check formatting and fail if code is not properly formatted
- Use clang-format version 19.1.1 or compatible

## Code Linting

This project uses `clang-tidy` for static analysis and linting. The configuration is in `.clang-tidy`.

### Running Linting Checks

```bash
# Lint all source files
./build-aux/run-clang-tidy

# Lint specific files
./build-aux/run-clang-tidy src/your-file.cpp src/your-file.h

# Check linting without making changes
./build-aux/run-clang-tidy --check
```

### Generating compile_commands.json

`clang-tidy` requires `compile_commands.json` to understand your build configuration. Generate it with:

```bash
mkdir -p build_lint
cd build_lint
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cp compile_commands.json ..
cd ..
```

Or let the script generate it automatically (it will create `build_lint/` directory).

### Linting Requirements

- Code should pass `clang-tidy` checks before committing
- The CI will check linting and report issues
- Some warnings may be acceptable, but errors should be fixed

## CMake Formatting

CMake files are formatted with `gersemi`. The configuration is in `.gersemirc`.

```bash
# Format CMake files
./build-aux/run-gersemi

# Check formatting
./build-aux/run-gersemi --check
```

## Pre-commit Hooks (Optional)

You can set up pre-commit hooks to automatically format and lint your code:

```bash
# Create .git/hooks/pre-commit
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/sh
./build-aux/run-clang-format
./build-aux/run-clang-tidy --check
EOF
chmod +x .git/hooks/pre-commit
```

## CI/CD

The project uses GitHub Actions to automatically check:
- Code formatting (clang-format)
- Code linting (clang-tidy)
- CMake formatting (gersemi)

All checks must pass before code can be merged.

## Questions?

If you have questions about formatting or linting, please open an issue or check the existing codebase for examples.
