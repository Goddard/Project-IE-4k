#!/bin/bash

# BAF Comparison Script with Parallel Processing
# Compares our extracted BAF files with reference files

set -e

# Number of parallel jobs (defaults to number of CPU cores)
PARALLEL_JOBS=${PARALLEL_JOBS:-$(nproc)}

REFERENCE_DIR=".reference/bcs-reference"
OUR_EXTRACTED_DIR="output/bg2/x4"

echo "=== BAF Comparison Script ==="
echo "Using $PARALLEL_JOBS parallel jobs (set PARALLEL_JOBS env var to override)"
echo "Comparing extracted BAF files with reference files..."
echo ""

# Function to strip inline comments from a BAF file
strip_comments() {
    local input_file="$1"
    local output_file="$2"

    # Strip everything after "  // " (two spaces followed by comment)
    sed 's/  \/\/.*$//' "$input_file" > "$output_file"
}

# Function to compare a specific BAF file (parallel-safe version)
compare_baf() {
    local our_file="$1"
    local ref_file="$2"
    local index="$3"
    local total="$4"

    # Extract basename for display
    local baf_name=$(basename "$our_file" | sed 's/\.baf$//i')

    # Create job prefix for parallel output
    local job_prefix="[$index/$total] $baf_name:"

    if [[ ! -f "$our_file" ]]; then
        echo "$job_prefix MISSING: Our extracted file"
        return 1
    fi

    if [[ ! -f "$ref_file" ]]; then
        echo "$job_prefix MISSING: Reference file"
        return 1
    fi

    # Create unique temporary stripped files for this job
    local pid_suffix="$$-$(date +%s%N)"
    local our_stripped="/tmp/${baf_name}-ours-stripped-${pid_suffix}.txt"
    local ref_stripped="/tmp/${baf_name}-ref-stripped-${pid_suffix}.txt"

    # Strip comments for comparison
    strip_comments "$our_file" "$our_stripped"
    strip_comments "$ref_file" "$ref_stripped"

    # Compare stripped versions for structural differences
    if cmp -s "$our_stripped" "$ref_stripped"; then
        echo "$job_prefix ✓ IDENTICAL"
        rm -f "$our_stripped" "$ref_stripped"
        return 0
    else
        echo "$job_prefix ✗ DIFFERENT"

        # Show the differences
        echo "$job_prefix   Differences:"
        diff -u "$ref_stripped" "$our_stripped" | sed 's/^/'"$job_prefix"'    /' | head -20 || true

        # Clean up temp files
        rm -f "$our_stripped" "$ref_stripped"

        return 1
    fi
}

# Find all BAF file pairs for comparison
echo "Finding BAF file pairs..."
echo "Looking in: $OUR_EXTRACTED_DIR"
echo "Reference dir: $REFERENCE_DIR"

# Test if directories exist
if [[ ! -d "$OUR_EXTRACTED_DIR" ]]; then
    echo "ERROR: OUR_EXTRACTED_DIR does not exist: $OUR_EXTRACTED_DIR"
    exit 1
fi
if [[ ! -d "$REFERENCE_DIR" ]]; then
    echo "ERROR: REFERENCE_DIR does not exist: $REFERENCE_DIR"
    exit 1
fi

file_pairs=()

# Find all our extracted BAF files
while IFS= read -r -d '' our_file; do
    # Extract basename without extension
    basename=$(basename "$our_file" | sed 's/\.baf$//i')

    # Try to find matching reference file
    ref_file=""
    for ext in BAF baf; do
        candidate="$REFERENCE_DIR/${basename}.${ext}"
        if [[ -f "$candidate" ]]; then
            ref_file="$candidate"
            echo "Found pair: $our_file -> $ref_file"
            break
        fi
    done

    if [[ -n "$ref_file" ]]; then
        file_pairs+=("$our_file|$ref_file")
    else
        echo "No reference found for: $our_file (looked for $REFERENCE_DIR/${basename}.BAF and $REFERENCE_DIR/${basename}.baf)"
    fi
done < <(find "$OUR_EXTRACTED_DIR" -name "*-bcs-extracted" -type d -exec find {} \( -name "*.baf" -o -name "*.BAF" \) -print0 \; 2>/dev/null)

echo "Found ${#file_pairs[@]} BAF file pairs to compare"
echo ""

# Export functions and variables for parallel execution
export -f compare_baf
export -f strip_comments
export REFERENCE_DIR OUR_EXTRACTED_DIR

# Create a temporary file list for parallel processing
TEMP_LIST=$(mktemp)
trap "rm -f $TEMP_LIST" EXIT

# Write file pairs with indices for parallel processing
for i in "${!file_pairs[@]}"; do
    echo "$((i+1)) ${#file_pairs[@]} ${file_pairs[$i]}" >> "$TEMP_LIST"
done

# Function wrapper for parallel execution
compare_baf_wrapper() {
    local index="$1"
    local total="$2"
    local pair="$3"

    # Split the pair into our_file and ref_file
    IFS='|' read -r our_file ref_file <<< "$pair"
    compare_baf "$our_file" "$ref_file" "$index" "$total"
}

export -f compare_baf_wrapper

# Use sequential processing for debugging
echo "Using sequential processing for debugging..."

differences_found=0
files_compared=0

for pair in "${file_pairs[@]}"; do
    files_compared=$((files_compared + 1))

    # Split the pair into our_file and ref_file
    IFS='|' read -r our_file ref_file <<< "$pair"

    if ! compare_baf "$our_file" "$ref_file" "$files_compared" "${#file_pairs[@]}"; then
        differences_found=$((differences_found + 1))
        echo ""
        echo "=== DIFFERENCE FOUND ==="
        echo "Stopped at first difference."
        echo "Files compared: $files_compared"
        echo "Differences found: $differences_found"
        exit 1
    fi
done

echo ""
echo "=== ALL FILES IDENTICAL ==="
echo "Total files compared: $files_compared"
echo "All BAF files are structurally identical to references!"
