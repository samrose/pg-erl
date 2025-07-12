#!/bin/bash

# Build script for erlang_cnode PostgreSQL extension

set -e

echo "Building erlang_cnode PostgreSQL extension..."

# Check if we're in the right directory
if [ ! -f "erlang_cnode.c" ]; then
    echo "Error: erlang_cnode.c not found. Please run this script from the extension directory."
    exit 1
fi

# Check for required dependencies
echo "Checking dependencies..."

# Check for PostgreSQL development headers
if ! pg_config --version > /dev/null 2>&1; then
    echo "Error: pg_config not found. Please install PostgreSQL development headers."
    echo "  Ubuntu/Debian: sudo apt-get install postgresql-server-dev-all"
    echo "  macOS: brew install postgresql"
    echo "  Fedora/RHEL: sudo dnf install postgresql-server-devel"
    exit 1
fi

# Check for Erlang interface
ERL_INCLUDE_DIRS=(
    "/usr/lib/erlang/usr/include"
    "/usr/local/lib/erlang/usr/include"
    "/opt/homebrew/lib/erlang/usr/include"
)

ERL_LIB_DIRS=(
    "/usr/lib/erlang/usr/lib"
    "/usr/local/lib/erlang/usr/lib"
    "/opt/homebrew/lib/erlang/usr/lib"
)

ERL_INCLUDE_FOUND=false
ERL_LIB_FOUND=false

for dir in "${ERL_INCLUDE_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        export ERL_INTERFACE_INCLUDE_DIR="$dir"
        ERL_INCLUDE_FOUND=true
        echo "Found Erlang include directory: $dir"
        break
    fi
done

for dir in "${ERL_LIB_DIRS[@]}"; do
    if [ -d "$dir" ]; then
        export ERL_INTERFACE_LIB_DIR="$dir"
        ERL_LIB_FOUND=true
        echo "Found Erlang lib directory: $dir"
        break
    fi
done

if [ "$ERL_INCLUDE_FOUND" = false ] || [ "$ERL_LIB_FOUND" = false ]; then
    echo "Error: Erlang interface libraries not found."
    echo "Please install Erlang development libraries:"
    echo "  Ubuntu/Debian: sudo apt-get install liberlinterface-dev"
    echo "  macOS: brew install erlang"
    echo "  Fedora/RHEL: sudo dnf install erlang-erlang-interface"
    exit 1
fi

# Clean previous builds
echo "Cleaning previous builds..."
make clean 2>/dev/null || true

# Build the extension
echo "Building extension..."
make

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "To install the extension:"
    echo "  sudo make install"
    echo ""
    echo "To test the extension:"
    echo "  psql -d your_database -f test_extension.sql"
    echo ""
    echo "To enable the extension in a database:"
    echo "  CREATE EXTENSION erlang_cnode;"
else
    echo "Build failed!"
    exit 1
fi 