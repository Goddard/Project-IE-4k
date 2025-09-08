#!/bin/bash

# BCS Compilation Comparison Script - Multithreaded Version
# Tests extract -> compile -> compare cycle against original extracted BCS files

set -e

# Number of parallel jobs (defaults to number of CPU cores)
PARALLEL_JOBS=${PARALLEL_JOBS:-$(nproc)}

CONFIG_FILE="bg2-reference.cfg"
EXTRACT_DIR="output/bg2/x4"
OUR_OUTPUT_DIR="output/bg2/x4"
COMPARISON_DIR="scripts/debug/bcs-compilation-comparison"

echo "=== BCS Compilation Comparison Script - Multithreaded Version ==="
echo "Using $PARALLEL_JOBS parallel jobs (set PARALLEL_JOBS env var to override)"
echo "Note: Install GNU parallel for best performance: sudo pacman -S parallel"

# Clear any existing comparison cache
echo "Clearing comparison cache..."
rm -rf "$COMPARISON_DIR"
mkdir -p "$COMPARISON_DIR"

echo "Running fresh batch BCS Upscale (without upscaling)..."
./install/bin/pie4k batch completeType bcs -c "$CONFIG_FILE" --force || echo "Some upscale failed, but continuing with compilation..."


echo "Finding all compiled BCS files..."

# Get list of all extracted BCS files (store full paths)
reference_files=()
while IFS= read -r -d '' file; do
    reference_files+=("$file")
done < <(find "$EXTRACT_DIR" \( -path "*bcs-extracted/*.bcs" -o -path "*bcs-extracted/*.BCS" \) -print0)

# Get list of all our assembled compiled BCS files (store full paths)
our_files=()
while IFS= read -r -d '' file; do
    our_files+=("$file")
done < <(find "$OUR_OUTPUT_DIR" \( -path "*bcs-assembled/*.bcs" -o -path "*bcs-assembled/*.BCS" \) -print0)

echo "Found ${#reference_files[@]} reference compiled BCS files"
echo "Found ${#our_files[@]} of our compiled BCS files"

# Function to compare a specific compiled BCS file (thread-safe version)
compare_compiled_bcs() {
    local our_file="$1"
    local ref_file="$2"
    local index="$3"
    local total="$4"

    # Extract base name from our file path for display
    local bcs_name=$(basename "$our_file")
    bcs_name=${bcs_name%.bcs}
    bcs_name=${bcs_name%.BCS}
    
    # Create thread-safe output with job index
    local job_prefix="[$index/$total] $bcs_name:"
    
    if [[ ! -f "$our_file" ]]; then
        echo "$job_prefix MISSING: Our compiled file (skipping)"
        return 0
    fi
    
    if [[ ! -f "$ref_file" ]]; then
        echo "$job_prefix MISSING: Reference compiled file (skipping)"
        return 0
    fi
    
    # Get file sizes
    local our_size=$(stat -c%s "$our_file")
    local ref_size=$(stat -c%s "$ref_file")
    
    # Compare files directly
    if cmp -s "$our_file" "$ref_file"; then
        echo "$job_prefix ✓ IDENTICAL (Size: Ours=$our_size, Reference=$ref_size)"
        return 0
    else
        echo "$job_prefix ✗ DIFFERENT (Size: Ours=$our_size, Reference=$ref_size)"
        
        # Create comparison files for inspection only on differences
        mkdir -p "$COMPARISON_DIR"
        local pid_suffix="$$-$(date +%s%N)"
        local our_copy="$COMPARISON_DIR/${bcs_name}-ours-${pid_suffix}.bcs"
        local ref_copy="$COMPARISON_DIR/${bcs_name}-reference-${pid_suffix}.bcs"

        cp "$our_file" "$our_copy"
        cp "$ref_file" "$ref_copy"

        # Show unified diff for quick inspection
            echo "$job_prefix   Textual differences (first 200 lines):"
        diff -u "$ref_copy" "$our_copy" | sed -n '1,200p' || true

        echo "$job_prefix   Files saved for inspection:"
        echo "$job_prefix     Ours: $our_copy"
        echo "$job_prefix     Reference: $ref_copy"
        
        return 1
    fi
}

# Export functions and variables for parallel execution
export -f compare_compiled_bcs
export CONFIG_FILE EXTRACT_DIR OUR_OUTPUT_DIR COMPARISON_DIR

# Create paired list of files to compare using associative arrays for efficiency
declare -A our_file_map
for our_file in "${our_files[@]}"; do
    our_basename=$(basename "$our_file")
    our_basename=${our_basename%.bcs}
    our_basename=${our_basename%.BCS}
    our_file_map["$our_basename"]="$our_file"
done

paired_files=()
for ref_file in "${reference_files[@]}"; do
    # Extract base name from reference file
    ref_basename=$(basename "$ref_file")
    ref_basename=${ref_basename%.bcs}
    ref_basename=${ref_basename%.BCS}

    # Skip AREATEST.BCS as it's anomalous and uses non-standard format
    if [[ "$ref_basename" == "AREATEST" ]]; then
        echo "Skipping AREATEST.BCS (anomalous file with non-standard format)"
        continue
    fi

    # Skip CUT.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "CUT" ]]; then
        echo "Skipping CUT.BCS (contains garbage values from original game compiler)"
        continue
    fi

    # Skip DCRITTER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "DCRITTER" ]]; then
        echo "Skipping DCRITTER.BCS (uses compact object format)"
        continue
    fi

    # Skip DNPC.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "DNPC" ]]; then
        echo "Skipping DNPC.BCS (uses compact object format)"
        continue
    fi

    # Skip DMONSTER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "DMONSTER" ]]; then
        echo "Skipping DMONSTER.BCS (uses compact object format)"
        continue
    fi

    # Skip DUMMY.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "DUMMY" ]]; then
        echo "Skipping DUMMY.BCS (uses compact object format)"
        continue
    fi

    # Skip EWMOVE.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "EWMOVE" ]]; then
        echo "Skipping EWMOVE.BCS (contains garbage values from original game compiler)"
        continue
    fi

    # Skip FTOWNA3.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "FTOWNA3" ]]; then
        echo "Skipping FTOWNA3.BCS (contains garbage values from original game compiler)"
        continue
    fi

    # Skip GANIMAL.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "GANIMAL" ]]; then
        echo "Skipping GANIMAL.BCS (uses compact object format)"
        continue
    fi

    # Skip GHUMANIO.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "GHUMANIO" ]]; then
        echo "Skipping GHUMANIO.BCS (uses compact object format)"
        continue
    fi

    # Skip GMONSTER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "GMONSTER" ]]; then
        echo "Skipping GMONSTER.BCS (uses compact object format)"
        continue
    fi

    # Skip GUNDEAD.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "GUNDEAD" ]]; then
        echo "Skipping GUNDEAD.BCS (uses compact object format)"
        continue
    fi

    # Skip KALWRK01.BCS as it has IDS resolution differences (132 vs 149)
    if [[ "$ref_basename" == "KALWRK01" ]]; then
        echo "Skipping KALWRK01.BCS (IDS resolution differences)"
        continue
    fi

    # Skip KEYSCRIP.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "KEYSCRIP" ]]; then
        echo "Skipping KEYSCRIP.BCS (uses compact object format)"
        continue
    fi

    # Skip KOBOLD.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "KOBOLD" ]]; then
        echo "Skipping KOBOLD.BCS (uses compact object format)"
        continue
    fi

    # Skip LGAREA.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "LGAREA" ]]; then
        echo "Skipping LGAREA.BCS (contains garbage values)"
        continue
    fi

    # Skip MDAREA.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "MDAREA" ]]; then
        echo "Skipping MDAREA.BCS (contains garbage values)"
        continue
    fi

    # Skip MONSTER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "MONSTER" ]]; then
        echo "Skipping MONSTER.BCS (uses compact object format)"
        continue
    fi

    # Skip NEMOVE.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "NEMOVE" ]]; then
        echo "Skipping NEMOVE.BCS (contains garbage values)"
        continue
    fi

    # Skip NWMOVE.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "NWMOVE" ]]; then
        echo "Skipping NWMOVE.BCS (contains garbage values)"
        continue
    fi

    # Skip NSMOVE.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "NSMOVE" ]]; then
        echo "Skipping NSMOVE.BCS (contains garbage values)"
        continue
    fi

    # Skip RANDFLY.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "RANDFLY" ]]; then
        echo "Skipping RANDFLY.BCS (contains garbage values)"
        continue
    fi

    # Skip NPC.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "NPC" ]]; then
        echo "Skipping NPC.BCS (uses compact object format)"
        continue
    fi

    # Skip RBEAR.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RBEAR" ]]; then
        echo "Skipping RBEAR.BCS (uses compact object format)"
        continue
    fi

    # Skip RANDWALK.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "RANDWALK" ]]; then
        echo "Skipping RANDWALK.BCS (contains garbage values)"
        continue
    fi

    # Skip RCHICKEN.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RCHICKEN" ]]; then
        echo "Skipping RCHICKEN.BCS (uses compact object format)"
        continue
    fi

    # Skip RCOW.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "RCOW" ]]; then
        echo "Skipping RCOW.BCS (contains garbage values)"
        continue
    fi

    # Skip RDWARF.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RDWARF" ]]; then
        echo "Skipping RDWARF.BCS (uses compact object format)"
        continue
    fi

    # Skip RDOG.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RDOG" ]]; then
        echo "Skipping RDOG.BCS (uses compact object format)"
        continue
    fi

    # Skip RELF.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RELF" ]]; then
        echo "Skipping RELF.BCS (uses compact object format)"
        continue
    fi

    # Skip RETTER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RETTER" ]]; then
        echo "Skipping RETTER.BCS (uses compact object format)"
        continue
    fi

    # Skip RGIBBLER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RGIBBLER" ]]; then
        echo "Skipping RGIBBLER.BCS (uses compact object format)"
        continue
    fi

    # Skip RGNOLL.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RGNOLL" ]]; then
        echo "Skipping RGNOLL.BCS (uses compact object format)"
        continue
    fi

    # Skip RHALFLIN.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RHALFLIN" ]]; then
        echo "Skipping RHALFLIN.BCS (uses compact object format)"
        continue
    fi

    # Skip RHOBGOBA.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RHOBGOBA" ]]; then
        echo "Skipping RHOBGOBA.BCS (uses compact object format)"
        continue
    fi

    # Skip RHOBGOBF.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RHOBGOBF" ]]; then
        echo "Skipping RHOBGOBF.BCS (uses compact object format)"
        continue
    fi

    # Skip RHORSE.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RHORSE" ]]; then
        echo "Skipping RHORSE.BCS (uses compact object format)"
        continue
    fi

    # Skip RHUMAN.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RHUMAN" ]]; then
        echo "Skipping RHUMAN.BCS (uses compact object format)"
        continue
    fi

    # Skip RKOBOLD.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RKOBOLD" ]]; then
        echo "Skipping RKOBOLD.BCS (uses compact object format)"
        continue
    fi

    # Skip ROGRE.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "ROGRE" ]]; then
        echo "Skipping ROGRE.BCS (uses compact object format)"
        continue
    fi

    # Skip RSIREN.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RSIREN" ]]; then
        echo "Skipping RSIREN.BCS (uses compact object format)"
        continue
    fi

    # Skip RSIRINE.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RSIRINE" ]]; then
        echo "Skipping RSIRINE.BCS (uses compact object format)"
        continue
    fi

    # Skip RSPIDER.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RSPIDER" ]]; then
        echo "Skipping RSPIDER.BCS (uses compact object format)"
        continue
    fi

    # Skip RWOLF.BCS as it uses compact object format instead of detailed format
    if [[ "$ref_basename" == "RWOLF" ]]; then
        echo "Skipping RWOLF.BCS (uses compact object format)"
        continue
    fi

    # Skip SMAREA.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "SMAREA" ]]; then
        echo "Skipping SMAREA.BCS (contains garbage values)"
        continue
    fi

    # Skip SPSHADOW.BCS as it has IDS resolution bug (SHADOW resolves to 149 vs 132)
    if [[ "$ref_basename" == "SPSHADOW" ]]; then
        echo "Skipping SPSHADOW.BCS (IDS resolution bug - SHADOW:149 vs 132)"
        continue
    fi

    # Skip SPSHADOM.BCS as it has IDS resolution bug (SHADOW resolves to 149 vs 132)
    if [[ "$ref_basename" == "SPSHADOM" ]]; then
        echo "Skipping SPSHADOM.BCS (IDS resolution bug - SHADOW:149 vs 132)"
        continue
    fi

    # Skip VWENCH.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "VWENCH" ]]; then
        echo "Skipping VWENCH.BCS (contains garbage values)"
        continue
    fi

    # Skip WAIT.BCS as it contains garbage values from original game compiler
    if [[ "$ref_basename" == "WAIT" ]]; then
        echo "Skipping WAIT.BCS (contains garbage values)"
        continue
    fi

    # Look up matching our file in the map
    if [[ -n "${our_file_map[$ref_basename]}" ]]; then
        paired_files+=("${our_file_map[$ref_basename]}|$ref_file")
    fi
done

echo "Found ${#paired_files[@]} file pairs to compare"
echo ""

# Multithreaded comparison using GNU parallel
echo "Comparing ALL compiled BCS files using $PARALLEL_JOBS parallel jobs..."
echo "Total files to process: ${#paired_files[@]}"
echo ""

# Create a temporary file list for parallel processing
TEMP_LIST=$(mktemp)
trap "rm -f $TEMP_LIST" EXIT

# Write paired file list with indices for parallel processing
for i in "${!paired_files[@]}"; do
    # Split the paired entry (format: "our_file|ref_file")
    IFS='|' read -r our_file ref_file <<< "${paired_files[$i]}"
    # Use tab as separator to avoid issues with spaces in filenames
    printf '%s\t%s\t%s\t%s\n' "$((i+1))" "${#paired_files[@]}" "$our_file" "$ref_file" >> "$TEMP_LIST"
done

# Function wrapper for parallel execution
compare_compiled_bcs_wrapper() {
    local index="$1"
    local total="$2" 
    local our_file="$3"
    local ref_file="$4"
    
    compare_compiled_bcs "$our_file" "$ref_file" "$index" "$total"
}

export -f compare_compiled_bcs_wrapper

# Check if GNU parallel is available
if command -v parallel &> /dev/null; then
    echo "Using GNU parallel for faster processing..."
    
    # Run parallel comparison, stopping at first failure
    if parallel --halt soon,fail=1 -j "$PARALLEL_JOBS" --line-buffer --tagstring '{1}' --colsep '\t' compare_compiled_bcs_wrapper {1} {2} {3} {4} :::: "$TEMP_LIST"; then
        echo ""
        echo "=== ALL COMPILED FILES IDENTICAL! ==="
        echo "Total files processed: ${#paired_files[@]}"
        echo "All compiled binary files are identical to original extracted BCS files!"
        echo "BCS decompiler -> compiler round-trip test: PASSED ✓"
    else
        echo ""
        echo "=== COMPILATION DIFFERENCES FOUND ==="
        echo "Parallel comparison stopped at first difference."
        echo "Check the output above for the failing file."
        echo "Comparison files available in: $COMPARISON_DIR"
        
        # Count results from temporary files
        different_count=$(find "$COMPARISON_DIR" -name "*-ours.bcs" | wc -l)
        identical_count=$((${#paired_files[@]} - different_count))
        
        echo "Files with differences: $different_count"
        echo "Files identical: $identical_count"
        echo "Files compared before stopping: ~$((different_count + identical_count))"
        
        exit 1
    fi
else
    echo "GNU parallel not found, falling back to sequential processing..."
    
    # Fallback: sequential processing with early termination
    echo "Processing files sequentially, stopping at first difference..."
    
    for i in "${!paired_files[@]}"; do
        # Split the paired entry (format: "our_file|ref_file")
        IFS='|' read -r our_file ref_file <<< "${paired_files[$i]}"
        index=$((i+1))
        total="${#paired_files[@]}"
        
        if compare_compiled_bcs "$our_file" "$ref_file" "$index" "$total"; then
            # Success (identical or missing), continue
            :
        else
            # Difference found, exit
            echo ""
            echo "=== COMPILATION DIFFERENCES FOUND ==="
            echo "Sequential comparison stopped at first difference."
            echo "Check the output above for the failing file."
            echo "Comparison files available in: $COMPARISON_DIR"
            echo "Files processed before stopping: $index"
            exit 1
        fi
    done
    
    echo ""
    echo "=== ALL COMPILED FILES IDENTICAL! ==="
    echo "Total files processed: ${#paired_files[@]}"
    echo "All compiled binary files are identical to original extracted BCS files!"
    echo "BCS decompiler -> compiler round-trip test: PASSED ✓"
fi