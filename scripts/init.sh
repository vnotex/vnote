#!/bin/bash
#
# Initialization script for VNote development environment
# Run this script once after cloning the repository
#

set -e

echo "========================================"
echo "Initializing VNote development environment"
echo "========================================"

# Get the script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# Step 1: Initialize and update git submodules
echo ""
echo "[1/3] Initializing and updating git submodules..."
if ! git submodule update --init --recursive; then
    echo "Error: Failed to initialize submodules"
    exit 1
fi
echo "✓ Submodules initialized successfully"

# Step 2: Install pre-commit hook for main repository
echo ""
echo "[2/3] Installing pre-commit hook for main repository..."
HOOKS_DIR=".git/hooks"
if [ ! -d "$HOOKS_DIR" ]; then
    echo "Error: .git/hooks directory not found. Are you in a git repository?"
    exit 1
fi

if [ -f "$HOOKS_DIR/pre-commit" ]; then
    echo "Warning: pre-commit hook already exists. Creating backup..."
    cp "$HOOKS_DIR/pre-commit" "$HOOKS_DIR/pre-commit.backup"
fi

cp "scripts/pre-commit" "$HOOKS_DIR/pre-commit"
chmod +x "$HOOKS_DIR/pre-commit"
echo "✓ Main repository pre-commit hook installed"

# Step 3: Install pre-commit hook for vtextedit submodule
echo ""
echo "[3/3] Installing pre-commit hook for vtextedit submodule..."
VTEXTEDIT_HOOKS_DIR="libs/vtextedit/.git/hooks"

# Check if vtextedit is using .git file (submodule) or directory
if [ -f "libs/vtextedit/.git" ]; then
    # It's a submodule, read the actual git directory path
    GIT_DIR=$(grep "gitdir:" "libs/vtextedit/.git" | cut -d' ' -f2)
    VTEXTEDIT_HOOKS_DIR="libs/vtextedit/$GIT_DIR/hooks"
elif [ -d "libs/vtextedit/.git" ]; then
    # It's a regular git directory
    VTEXTEDIT_HOOKS_DIR="libs/vtextedit/.git/hooks"
fi

if [ ! -d "$VTEXTEDIT_HOOKS_DIR" ]; then
    echo "Error: vtextedit hooks directory not found at $VTEXTEDIT_HOOKS_DIR"
    exit 1
fi

if [ -f "$VTEXTEDIT_HOOKS_DIR/pre-commit" ]; then
    echo "Warning: vtextedit pre-commit hook already exists. Creating backup..."
    cp "$VTEXTEDIT_HOOKS_DIR/pre-commit" "$VTEXTEDIT_HOOKS_DIR/pre-commit.backup"
fi

cp "libs/vtextedit/scripts/pre-commit" "$VTEXTEDIT_HOOKS_DIR/pre-commit"
chmod +x "$VTEXTEDIT_HOOKS_DIR/pre-commit"
echo "✓ vtextedit submodule pre-commit hook installed"

# Done
echo ""
echo "========================================"
echo "✓ Initialization complete!"
echo "========================================"
echo ""
echo "Note: The pre-commit hooks require clang-format to be installed."
echo "If you don't have it installed, the hooks will skip formatting with a warning."
echo ""
