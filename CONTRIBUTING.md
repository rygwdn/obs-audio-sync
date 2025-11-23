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

## Running All Checks

Before committing, run all checks to ensure everything passes:

```bash
./build-aux/run-all-checks
```

This script runs:
- Code formatting check (clang-format)
- CMake formatting check (gersemi)
- Code linting check (clang-tidy)
- Test suite (if build directory exists)

**All checks must pass before committing.** The script will exit with a non-zero status if any checks fail.

You can skip specific checks if needed:
```bash
./build-aux/run-all-checks --skip-tests          # Skip tests
./build-aux/run-all-checks --skip-formatting     # Skip formatting checks
./build-aux/run-all-checks --skip-linting        # Skip linting checks
./build-aux/run-all-checks --skip-cmake-formatting  # Skip CMake formatting
```

## Pre-commit Hooks (Optional)

You can set up pre-commit hooks to automatically run all checks:

```bash
# Create .git/hooks/pre-commit
cat > .git/hooks/pre-commit << 'EOF'
#!/bin/sh
./build-aux/run-all-checks
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
