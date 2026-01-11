#!/usr/bin/env bash
set -euo pipefail

# Script to download and install the macOS plugin from GitHub Actions artifacts
# Usage: ./install-from-ci.sh [run_id]
#   If run_id is not provided, uses the latest run from the current branch

REPO="rygwdn/obs-audio-sync"
PLUGIN_INSTALL_DIR="$HOME/Library/Application Support/obs-studio/plugins"
DOWNLOAD_DIR="$(mktemp -d)"

# Cleanup on exit
trap 'rm -rf "$DOWNLOAD_DIR"' EXIT

# Get run ID
if [ $# -eq 0 ]; then
    echo "Getting latest CI run..."
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    RUN_ID=$(gh run list --repo "$REPO" --branch "$CURRENT_BRANCH" --limit 1 --json databaseId --jq '.[0].databaseId')
    echo "Using run ID: $RUN_ID from branch: $CURRENT_BRANCH"
else
    RUN_ID="$1"
    echo "Using provided run ID: $RUN_ID"
fi

# Get artifact name
echo "Fetching artifact list..."
ARTIFACT_NAME=$(gh api "repos/$REPO/actions/runs/$RUN_ID/artifacts" --jq '.artifacts[] | select(.name | contains("macos")) | .name')

if [ -z "$ARTIFACT_NAME" ]; then
    echo "Error: No macOS artifact found for run $RUN_ID"
    exit 1
fi

echo "Found artifact: $ARTIFACT_NAME"

# Download artifact
echo "Downloading artifact..."
gh run download "$RUN_ID" --name "$ARTIFACT_NAME" --dir "$DOWNLOAD_DIR" --repo "$REPO"

# Extract archive
echo "Extracting archive..."
cd "$DOWNLOAD_DIR"
tar -xf obs-audio-sync-*.tar.xz

# Remove old plugin
echo "Removing old plugin..."
rm -rf "$PLUGIN_INSTALL_DIR/obs-audio-sync.plugin"

# Install new plugin
echo "Installing new plugin..."
cp -R obs-audio-sync.plugin "$PLUGIN_INSTALL_DIR/"

echo "✓ Plugin installed successfully to: $PLUGIN_INSTALL_DIR/obs-audio-sync.plugin"
echo "⚠ Please restart OBS Studio for changes to take effect"
