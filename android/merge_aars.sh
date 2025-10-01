#!/bin/bash
# Simple AAR merger - extracts both AARs and combines them

VIROCORE_AAR="$1"
VIROAR_AAR="$2"
OUTPUT_AAR="$3"

if [ -z "$VIROCORE_AAR" ] || [ -z "$VIROAR_AAR" ] || [ -z "$OUTPUT_AAR" ]; then
    echo "Usage: $0 <virocore.aar> <viroar.aar> <output.aar>"
    exit 1
fi

TEMP_DIR=$(mktemp -d)
trap "rm -rf $TEMP_DIR" EXIT

echo "Extracting AARs to $TEMP_DIR..."

# Extract virocore
mkdir -p "$TEMP_DIR/virocore"
cd "$TEMP_DIR/virocore"
unzip -q "$(realpath "$VIROCORE_AAR")"

# Extract viroar into same location (will merge)
cd "$TEMP_DIR/virocore"
unzip -o -q "$(realpath "$VIROAR_AAR")"

# Repackage
echo "Creating merged AAR..."
cd "$TEMP_DIR/virocore"
zip -q -r "$(realpath "$OUTPUT_AAR")" .

echo "Merged AAR created: $OUTPUT_AAR"