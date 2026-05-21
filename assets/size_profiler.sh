#!/bin/bash
# Binary Size Comparison Script for mini-loc
# Tests different compilation profiles and optimization levels

set -e

TARGET_NAME="loc"
BUILD_DIR="build"
BIN_DIR="bin"
RESULTS_FILE="/tmp/loc_size_comparison.txt"

# ANSI colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Function to print headers
print_header() {
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

# Function to print results
print_result() {
    local name=$1
    local size=$2
    local stripped=$3
    local time=$4
    
    if [ -n "$stripped" ]; then
        printf "  %-25s %10s (stripped)\n" "$name:" "$size"
    else
        printf "  %-25s %10s\n" "$name:" "$size"
    fi
}

# Function to get file size in human readable format and bytes
get_size() {
    local file=$1
    if [ -f "$file" ]; then
        echo "$(du -h "$file" | cut -f1)"
    else
        echo "N/A"
    fi
}

get_bytes() {
    local file=$1
    if [ -f "$file" ]; then
        du -b "$file" | cut -f1
    else
        echo "0"
    fi
}

# Function to build with a specific profile
build_profile() {
    local profile=$1
    local name=$2
    
    echo -e "${CYAN}Building $name...${NC}"
    
    # Clean
    rm -rf "$BUILD_DIR" "$BIN_DIR" 2>/dev/null || true
    mkdir -p "$BIN_DIR" "$BUILD_DIR"
    
    # Build
    if make PROFILE="$profile" -j$(nproc) > /dev/null 2>&1; then
        echo -e "${GREEN}✓ Build successful${NC}"
        return 0
    else
        echo -e "${RED}✗ Build failed${NC}"
        return 1
    fi
}

# Function to benchmark a binary
benchmark_binary() {
    local binary=$1
    local runs=${2:-3}
    
    if [ ! -f "$binary" ]; then
        echo "N/A"
        return
    fi
    
    # Run a few times and take average
    local total_time=0
    for i in $(seq 1 $runs); do
        local time=$( { time -p "$binary" -r . > /dev/null 2>&1; } 2>&1 | grep real | awk '{print $2}')
        total_time=$(echo "$total_time + ${time%s}" | bc)
    done
    
    local avg=$(echo "scale=3; $total_time / $runs" | bc)
    echo "${avg}s"
}

# Function to analyze binary sections
analyze_binary() {
    local binary=$1
    
    if [ ! -f "$binary" ] || ! command -v readelf > /dev/null; then
        return
    fi
    
    echo -e "${CYAN}  Binary sections:${NC}"
    readelf -S "$binary" 2>/dev/null | grep -E '\.text|\.rodata|\.data|\.debug' | \
        awk '{printf "    %-20s %10s\n", $2, $7}' || true
}

# Main script starts here
print_header "mini-loc Binary Size Optimization Comparison"

echo -e "\n${YELLOW}System Information:${NC}"
echo "  CPU cores: $(nproc)"
echo "  Current directory: $(pwd)"
echo -e "\n"

# Clean up any existing builds
rm -rf "$BUILD_DIR" "$BIN_DIR" 2>/dev/null || true

# Array to store results for comparison
declare -a PROFILE_NAMES=()
declare -a PROFILE_SIZES=()
declare -a PROFILE_SIZES_STRIPPED=()

# Build dev profile
print_header "1. DEV Profile (Default - Optimized for Speed)"
echo "Flags: -O3 -march=native -flto"
echo "Best for: Development and maximum speed"
echo ""

if build_profile "dev" "DEV profile"; then
    BINARY="$BIN_DIR/${TARGET_NAME}-multi"
    SIZE=$(get_size "$BINARY")
    BYTES=$(get_bytes "$BINARY")
    
    print_result "Multi (unstripped)" "$SIZE"
    echo ""
    
    # Make a stripped copy
    BINARY_STRIPPED="${BINARY}.dev.stripped"
    cp "$BINARY" "$BINARY_STRIPPED"
    strip "$BINARY_STRIPPED"
    SIZE_STRIPPED=$(get_size "$BINARY_STRIPPED")
    BYTES_STRIPPED=$(get_bytes "$BINARY_STRIPPED")
    
    print_result "Multi (stripped)" "$SIZE_STRIPPED"
    
    # Store for comparison
    PROFILE_NAMES+=("DEV (native)")
    PROFILE_SIZES+=("$BYTES")
    PROFILE_SIZES_STRIPPED+=("$BYTES_STRIPPED")
    
    echo ""
fi

# Build release profile
print_header "2. RELEASE Profile (Balanced - Recommended)"
echo "Flags: -Os -march=x86-64 -flto=thin"
echo "Best for: Distribution - balanced size and speed"
echo ""

if build_profile "release" "RELEASE profile"; then
    BINARY="$BIN_DIR/${TARGET_NAME}-multi"
    SIZE=$(get_size "$BINARY")
    BYTES=$(get_bytes "$BINARY")
    
    print_result "Multi (stripped)" "$SIZE"
    
    # Store for comparison
    PROFILE_NAMES+=("RELEASE (x86-64)")
    PROFILE_SIZES+=("$BYTES")
    PROFILE_SIZES_STRIPPED+=("$BYTES")  # Already stripped
    
    echo ""
fi

# Build fast profile
print_header "3. FAST Profile (Maximum Speed)"
echo "Flags: -O3 -march=native -flto"
echo "Best for: Maximum performance on your machine"
echo ""

if build_profile "fast" "FAST profile"; then
    BINARY="$BIN_DIR/${TARGET_NAME}-multi"
    SIZE=$(get_size "$BINARY")
    BYTES=$(get_bytes "$BINARY")
    
    print_result "Multi (unstripped)" "$SIZE"
    echo ""
    
    BINARY_STRIPPED="${BINARY}.fast.stripped"
    cp "$BINARY" "$BINARY_STRIPPED"
    strip "$BINARY_STRIPPED"
    SIZE_STRIPPED=$(get_size "$BINARY_STRIPPED")
    BYTES_STRIPPED=$(get_bytes "$BINARY_STRIPPED")
    
    print_result "Multi (stripped)" "$SIZE_STRIPPED"
    
    PROFILE_NAMES+=("FAST (native)")
    PROFILE_SIZES+=("$BYTES")
    PROFILE_SIZES_STRIPPED+=("$BYTES_STRIPPED")
    
    echo ""
fi

# Build tiny profile
print_header "4. TINY Profile (Minimum Size)"
echo "Flags: -Os -flto=thin (no -march=native)"
echo "Best for: Embedded systems and size-critical applications"
echo ""

if build_profile "tiny" "TINY profile"; then
    BINARY="$BIN_DIR/${TARGET_NAME}-multi"
    SIZE=$(get_size "$BINARY")
    BYTES=$(get_bytes "$BINARY")
    
    print_result "Multi (stripped)" "$SIZE"
    
    PROFILE_NAMES+=("TINY (generic)")
    PROFILE_SIZES+=("$BYTES")
    PROFILE_SIZES_STRIPPED+=("$BYTES")
    
    echo ""
fi

# Summary and comparison
print_header "SUMMARY: Size Comparison"

echo -e "\n${CYAN}Unstripped Sizes:${NC}"
for i in "${!PROFILE_NAMES[@]}"; do
    size_bytes=${PROFILE_SIZES[$i]}
    size_human=$(numfmt --to=iec-i --suffix=B "$size_bytes" 2>/dev/null || echo "$size_bytes bytes")
    printf "  %-25s %15s (%12d bytes)\n" "${PROFILE_NAMES[$i]}" "$size_human" "$size_bytes"
done

echo -e "\n${CYAN}Stripped Sizes:${NC}"
for i in "${!PROFILE_NAMES[@]}"; do
    size_bytes=${PROFILE_SIZES_STRIPPED[$i]}
    size_human=$(numfmt --to=iec-i --suffix=B "$size_bytes" 2>/dev/null || echo "$size_bytes bytes")
    printf "  %-25s %15s (%12d bytes)\n" "${PROFILE_NAMES[$i]}" "$size_human" "$size_bytes"
done

# Calculate and show savings
echo -e "\n${CYAN}Size Reduction Compared to DEV (unstripped):${NC}"
dev_size=${PROFILE_SIZES[0]}
for i in "${!PROFILE_NAMES[@]}"; do
    current_size=${PROFILE_SIZES_STRIPPED[$i]}
    if [ $i -eq 0 ]; then
        # Compare stripped vs unstripped for DEV
        current_size=${PROFILE_SIZES_STRIPPED[$i]}
        reduction=$((dev_size - current_size))
        percent=$((reduction * 100 / dev_size))
        printf "  %-25s %10s bytes (%3d%%)\n" "${PROFILE_NAMES[$i]} (stripped)" "-$reduction" "$percent"
    else
        reduction=$((dev_size - current_size))
        percent=$((reduction * 100 / dev_size))
        printf "  %-25s %10s bytes (%3d%%)\n" "${PROFILE_NAMES[$i]}" "-$reduction" "$percent"
    fi
done

# Final recommendation
print_header "RECOMMENDATIONS"

echo -e "\n${GREEN}For Distribution:${NC}"
echo "  • Use RELEASE profile (recommended)"
echo "  • Balanced between size and speed"
echo "  • Portable (x86-64 instead of native)"
echo ""

echo -e "${GREEN}For Development:${NC}"
echo "  • Use DEV profile (default)"
echo "  • Optimized for your machine"
echo "  • Keep symbols for debugging"
echo ""

echo -e "${GREEN}For Minimal Size:${NC}"
echo "  • Use TINY profile"
echo "  • Further move language data to external JSON file"
echo "  • Expected: binary ~300KB + data ~500KB"
echo ""

# Show how to rebuild with recommended settings
echo -e "${YELLOW}Quick Commands:${NC}"
echo "  Development build:   make PROFILE=dev"
echo "  Release build:       make PROFILE=release"
echo "  Size comparison:     make size-comparison"
echo "  Benchmark:           make bench NAME=multi COUNT=5"
echo ""

# Cleanup
rm -f "$BIN_DIR"/*.stripped 2>/dev/null || true

print_header "Analysis Complete"
echo -e "\nBinaries available in: ${BIN_DIR}/"
echo "For detailed results, check the files in ${BIN_DIR}/"
echo ""
