# Contributing to OBS Audio Sync Plugin

Thank you for your interest in contributing! This document provides guidelines for code formatting.

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
- Code formatting (clang-format) - automatically fixes formatting issues
- CMake formatting (gersemi) - automatically fixes CMake formatting issues
- Test suite - **must exist and pass** (missing tests are considered a failure)

**All checks must pass before committing.** The script will exit with a non-zero status if any checks fail.

**Note**: The script uses fix mode, so it will automatically correct formatting issues where possible. However, missing/failing tests will cause the script to fail.

You can skip specific checks if needed:
```bash
./build-aux/run-all-checks --skip-tests          # Skip tests
./build-aux/run-all-checks --skip-formatting     # Skip formatting checks
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
- CMake formatting (gersemi)

All checks must pass before code can be merged.

## Questions?

If you have questions about formatting, please open an issue or check the existing codebase for examples.
