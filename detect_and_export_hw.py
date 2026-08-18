import json
import sys
import pyluwen

# Hardware Specifications
ARCH_SPECS = {
    "wormhole": {
        "name": "Wormhole",
        "grid_x": 8,
        "grid_y": 8,
        "l1_physical_bytes": 1499136,      # Physical L1 SRAM
        "fw_reserved_l1_bytes": 150000,    # L1 reserved for FW, mailboxes, and kernels
        "dram_channels": 6,
        "dram_chan_bytes": 2 * (1024 ** 3),
    },
    "blackhole": {
        "name": "Blackhole",
        "grid_x": 14,
        "grid_y": 10,
        "l1_physical_bytes": 1572864,
        "fw_reserved_l1_bytes": 200000,
        "dram_channels": 8,
        "dram_chan_bytes": 4 * (1024 ** 3),
    },
    "grayskull": {
        "name": "Grayskull",
        "grid_x": 10,
        "grid_y": 12,
        "l1_physical_bytes": 1048576,
        "fw_reserved_l1_bytes": 120000,
        "dram_channels": 8,
        "dram_chan_bytes": 1 * (1024 ** 3),
    }
}

PCI_DEVICE_IDS = {
    0x4010: "grayskull", 0x4011: "grayskull",
    0x4012: "wormhole",  0x4013: "wormhole", 0x401E: "wormhole",
    0x4014: "blackhole", 0x4015: "blackhole",
}

def detect_arch_type(chip, asic_idx=0):
    # Primary Method: pyluwen Downcasting
    try:
        chip.as_wh()
        print(f"[ASIC {asic_idx}] Architecture detected via pyluwen Downcasting -> Wormhole")
        return "wormhole"
    except Exception:
        pass
    try:
        chip.as_bh()
        print(f"[ASIC {asic_idx}] Architecture detected via pyluwen Downcasting -> Blackhole")
        return "blackhole"
    except Exception:
        pass

    # Fallback Method: PCIe Device ID Lookup
    dev_id = getattr(chip, "device_id", None)
    if dev_id in PCI_DEVICE_IDS:
        arch = PCI_DEVICE_IDS[dev_id]
        print(f"[ASIC {asic_idx}] Architecture detected via PCIe Device ID Lookup (0x{dev_id:04X}) -> {arch.capitalize()}")
        return arch

    print(f"[ASIC {asic_idx}] Architecture detection failed -> Defaulting to Wormhole fallback")
    return "wormhole"  # Default fallback

def run_hardware_analysis(json_output_file="hardware_specs.json"):
    chips = pyluwen.detect_chips()
    if not chips:
        print("Error: No Tenstorrent devices detected via pyluwen.", file=sys.stderr)
        return

    print("=======================================================================")
    print("    TENSTORRENT WORKLOAD & MEMORY TILING HARDWARE ANALYZER            ")
    print("=======================================================================\n")

    json_export_data = None

    for i, chip in enumerate(chips):
        arch_key = detect_arch_type(chip, asic_idx=i)
        specs = ARCH_SPECS[arch_key]
        telem = chip.get_telemetry()

        # Check column harvesting bitmask
        col_mask = getattr(telem, "tensix_enabled_col", 0)
        
        if col_mask > 0:
            active_cols = bin(col_mask).count('1')
            harvesting_msg = f"Detected (col_mask: {bin(col_mask)}) -> {active_cols}/{specs['grid_x']} columns active"
        else:
            active_cols = specs["grid_x"]
            harvesting_msg = f"Not detected or unpopulated -> defaulting to full grid ({active_cols} columns)"

        active_rows = specs["grid_y"]
        active_compute_cores = active_cols * active_rows

        # Usable L1 SRAM calculations
        l1_phys = specs["l1_physical_bytes"]
        l1_reserved = specs["fw_reserved_l1_bytes"]
        l1_usable_per_core = l1_phys - l1_reserved
        total_usable_l1_chip = active_compute_cores * l1_usable_per_core

        # Terminal Output
        board_id = hex(telem.board_id) if hasattr(telem, "board_id") and telem.board_id else "N/A"
        print(f"Details for [ASIC {i}] ({specs['name']} | Board ID: {board_id}):")
        print(f"   ├─ Column Harvesting Status : {harvesting_msg}")
        print(f"   ├─ Compute Grid Topology   : {active_cols}x{active_rows} ({active_compute_cores} Active Compute Cores)")
        print(f"   ├─ Physical L1 SRAM / Core : {l1_phys / (1024**2):.2f} MB ({l1_phys / 1024:.1f} KB)")
        print(f"   ├─ Usable L1 SRAM / Core   : {l1_usable_per_core / (1024**2):.2f} MB ({l1_usable_per_core / 1024:.1f} KB)")
        print(f"   │                            └─ [Reserved for FW/Buffers: {l1_reserved / 1024:.1f} KB]")
        print(f"   └─ Total Usable Chip L1    : {total_usable_l1_chip / (1024**2):.2f} MB")
        print("-----------------------------------------------------------------------")

        # Capture primary ASIC configuration for C++ JSON
        if i == 0:
            json_export_data = {
                "system_info": {
                    "detected_asics": len(chips),
                    "primary_asic_arch": specs["name"],
                    "board_id": board_id
                },
                "hardware_specs": {
                    "grid_rows": active_rows,
                    "grid_cols": active_cols,
                    "total_cores": active_compute_cores,
                    "l1_physical_kb": l1_phys / 1024.0,
                    "l1_fw_reserved_kb": l1_reserved / 1024.0,
                    "l1_usable_kb": l1_usable_per_core / 1024.0,
                    "dram_channels": specs["dram_channels"],
                    "dram_total_gb": (specs["dram_channels"] * specs["dram_chan_bytes"]) / (1024.0 ** 3)
                }
            }

    # Write out JSON file for C++ tool
    if json_export_data:
        with open(json_output_file, "w") as f:
            json.dump(json_export_data, f, indent=4)
        print(f"\n[+] Successfully exported primary ASIC configuration to '{json_output_file}'\n")

if __name__ == "__main__":
    run_hardware_analysis()
