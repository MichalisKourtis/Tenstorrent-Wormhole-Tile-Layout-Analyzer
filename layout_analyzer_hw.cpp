#include <iostream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>


using namespace std;

//=========================================================
// Tenstorrent Wormhole Tile Layout Analyzer (Prototype)
//
// Purpose:
//   Analyze how a tensor would be partitioned into 32x32
//   tiles for a single Wormhole ASIC.
//
// NOTE:
//   This is an architectural analysis tool.
//   It does NOT execute TT-Metal kernels or communicate
//   with real Tenstorrent hardware.
//=========================================================

struct LayoutStatistics
{
    //-----------------------------
    // Input Tensor
    //-----------------------------
    uint32_t tensor_rank;
    vector <uint32_t> shape;
    uint32_t tensor_rows;
    uint32_t tensor_cols;


    //-----------------------------
    // Tile Configuration
    //-----------------------------
    uint32_t tile_height;
    uint32_t tile_width;

    //-----------------------------
    // Datatype Information
    //-----------------------------
    string datatype;
    uint32_t bytes_per_element;

    //-----------------------------
    // Tiling Results
    //-----------------------------
    uint32_t tile_rows;
    uint32_t tile_cols;
    uint32_t matrices;
    uint32_t tiles_per_matrix;
    uint64_t total_tiles;

    //-----------------------------
    // Memory Statistics
    //-----------------------------
    uint64_t raw_bytes;
    uint64_t padded_bytes;
    double padding_percent;

    //-----------------------------
    // Workload Statistics
    //-----------------------------
    uint32_t active_cores;
    uint32_t idle_cores;
    double avg_tiles_per_active_core;


    //-----------------------------
    // L1 Memory Estimates
    //-----------------------------
    double l1_usage_kb;
    double l1_percent;

    // Workload Efficiency
    double workload_efficiency;
};

//----------------------------
// Dynamic Hardware Constants (populated from JSON)aOL
//----------------------------
uint32_t GRID_ROWS = 8;
uint32_t GRID_COLS = 8;
uint32_t TOTAL_CORES = GRID_ROWS * GRID_COLS;
string ARCH_NAME = "Wormhole";

// Approximate Tensix L1 SRAM
uint32_t L1_SIZE_KB = 1317.52;

//=========================================================

bool load_hardware_specs(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "\n[Warning] Could not open " << filename << ". Using default Wormhole constants.\n";
        return false;
    }

    string line;
    while (getline(file, line)) {
        if (line.find("\"grid_rows\"") != string::npos) {
            GRID_ROWS = stoi(line.substr(line.find(":") + 1));
        } else if (line.find("\"grid_cols\"") != string::npos) {
            GRID_COLS = stoi(line.substr(line.find(":") + 1));
        } else if (line.find("\"total_cores\"") != string::npos) {
            TOTAL_CORES = stoi(line.substr(line.find(":") + 1));
        } else if (line.find("\"l1_usable_kb\"") != string::npos) {
            L1_SIZE_KB = stod(line.substr(line.find(":") + 1));
        } else if (line.find("\"primary_asic_arch\"") != string::npos) {
            size_t start = line.find(":") + 2;
            size_t end = line.find_last_of("\"");
            if (start < end) ARCH_NAME = line.substr(start, end - start);

            // Clean quotes IF present (Inside the primary_asic_arch block)
            if (!ARCH_NAME.empty() && ARCH_NAME.front() == '"') ARCH_NAME.erase(0, 1);
            if (!ARCH_NAME.empty() && ARCH_NAME.back() == '"') ARCH_NAME.pop_back();
        }
    }
    file.close();

    cout << "\n[Loaded Hardware] Architecture: " << ARCH_NAME
         << " | Grid: " << GRID_ROWS << "x" << GRID_COLS
         << " | Active Cores: " << TOTAL_CORES
         << " | Usable L1/Core: " << static_cast<uint32_t>(L1_SIZE_KB) << " KB\n\n";
    return true;
}

void print_header()
{
    cout << "=============================================================\n";
    cout << "        Tenstorrent Wormhole Tile Layout Analyzer\n";
    cout << "=============================================================\n";
    cout << "Prototype educational / architecture analysis tool\n";
    cout << "Models ONE Wormhole ASIC (64 Tensix cores)\n";
    cout << "Does NOT execute TT-Metal kernels.\n";
    cout << "=============================================================\n\n";
}

//=========================================================

void draw_tile_grid(uint32_t rows, uint32_t cols)
{
    cout << "\n[TILE GRID]\n\n";

    uint32_t id = 0;

    for(uint32_t r = 0; r < rows; r++)
    {
        for(uint32_t c = 0; c < cols; c++)
            cout << "+------";
        cout << "+\n";

        for(uint32_t c = 0; c < cols; c++)
        {
            cout << "|T"
                 << setw(3)
                 << setfill('0')
                 << id++;
        }

        cout << " |\n";
    }

    for(uint32_t c = 0; c < cols; c++)
        cout << "+------";
    cout << "+\n";

    cout << setfill(' ');
}

//=========================================================

void draw_core_grid(uint32_t total_tiles)
{
    cout << "\n[WORMHOLE 8x8 CORE GRID]\n\n";

    uint32_t tile = 0;

    for(uint32_t r = 0; r < GRID_ROWS; r++)
    {
        for(uint32_t c = 0; c < GRID_COLS; c++)
        {
            if(tile < total_tiles)
            {
                cout << "[T"
                     << setw(2)
                     << setfill('0')
                     << tile
                     << "]";
            }
            else
            {
                cout << "[--]";
            }

            tile++;
        }

        cout << "\n";
    }

    cout << setfill(' ');
}

void update_tensor_totals(LayoutStatistics& stats){ 

    stats.total_tiles *= stats.matrices;
    stats.raw_bytes *= stats.matrices;
    stats.padded_bytes *= stats.matrices;

    }

//=========================================================

LayoutStatistics calculate_layout_statistics(const vector <uint32_t>& shape, uint32_t tile_height, uint32_t tile_width, uint32_t bytes_per_element, const string &type, uint32_t tensor_rank)
{
    //-----------------------------------------
    // Tile decomposition
    //-----------------------------------------
    LayoutStatistics stats{};
    stats.tensor_rank = tensor_rank;
    stats.shape = shape;
    stats.matrices = 1;

    uint32_t tile_bytes=
    tile_height *
    tile_width *
    bytes_per_element;


    uint32_t matrix_rows, matrix_cols;

    switch (stats.tensor_rank)
    {
    case 2:
        matrix_rows = stats.shape[0];
        matrix_cols = stats.shape[1];
        stats.matrices = 1;
        break;

    case 3:
        matrix_rows = stats.shape[1];
        matrix_cols = stats.shape[2];
        stats.matrices = stats.shape[0];
        break;

    case 4:
        matrix_rows = stats.shape[2];
        matrix_cols = stats.shape[3];
        stats.matrices = stats.shape[0] * stats.shape[1];
        break;

    default:
        cout << "Unsupported tensor rank.\n";
        return stats;
    }

    stats.tensor_rows = matrix_rows;
    stats.tensor_cols = matrix_cols;

    stats.tile_height = tile_height;
    stats.tile_width = tile_width;

    stats.datatype = type;
    stats.bytes_per_element = bytes_per_element;

     stats.tile_rows =
        static_cast<uint32_t>(
            ceil(static_cast<double>(stats.tensor_rows ) / tile_height));

     stats.tile_cols =
        static_cast<uint32_t>(
            ceil(static_cast<double>(stats.tensor_cols) / tile_width));

     stats.total_tiles =
        stats.tile_rows * stats.tile_cols;
    
    stats.tiles_per_matrix = stats.total_tiles;

    //-----------------------------------------
    // Memory calculations
    //-----------------------------------------

     stats.raw_bytes =
        static_cast<uint64_t>(stats.tensor_rows) *
        stats.tensor_cols *
        bytes_per_element;

    stats.padded_bytes =
        static_cast<uint64_t>(stats.total_tiles) * tile_bytes;

     stats.padding_percent = 0.0;

    if(stats.padded_bytes > 0)
    {
        stats.padding_percent =
            (double)(stats.padded_bytes - stats.raw_bytes) /
            stats.padded_bytes * 100.0;
    }

    //-----------------------------------------
    // Workload analysis
    //-----------------------------------------

     stats.active_cores =
        min(stats.tiles_per_matrix, TOTAL_CORES);

     stats.idle_cores =
        TOTAL_CORES - stats.active_cores;

     stats.avg_tiles_per_active_core = 0.0;

    if(stats.active_cores > 0)
    {
        stats.avg_tiles_per_active_core =
            static_cast<double>(stats.tiles_per_matrix) /
            stats.active_cores;
    }

    stats.workload_efficiency = static_cast<double>(stats.active_cores) / TOTAL_CORES * 100.0;

    //-----------------------------------------
    // Estimated L1 requirement
    //-----------------------------------------

     stats.l1_usage_kb =
        stats.avg_tiles_per_active_core *
        tile_bytes /
        1024.0;

     stats.l1_percent =
        (stats.l1_usage_kb /
        L1_SIZE_KB) * 100.0;
        
    update_tensor_totals(stats);

        return stats;
        }

    
    
    //-----------------------------------------
    // Report
    //-----------------------------------------
void print_report(const LayoutStatistics& stats){
    cout << "\n=============================================================\n";
    cout << "INPUT TENSOR : \n";

    cout<<"Tensor Rank       : "<<stats.tensor_rank<<"D\n";
    cout<<"Tensor shape      :";

    for (size_t i=0;i<stats.shape.size();i++){
        cout << stats.shape[i];

    if (i + 1 < stats.shape.size())
        cout << " x ";}

    cout << "\n";
    cout << "=============================================================\n\n";

    cout << fixed << setprecision(2);

    //---------------------
    cout << "[TILING]\n";
    
    cout<<"Matrices                  : "<<stats.matrices<<"\n";
    
    cout << "Tile Size                 : "
         << stats.tile_height
         << " x "
         << stats.tile_width
         << "\n";

    cout << "Tile Grid                 : "
         << stats.tile_rows
         << " x "
         << stats.tile_cols
         << "\n";

    cout << "Tiles per Matrix          : "
         << stats.tiles_per_matrix
         << "\n";

    cout << "Total Tiles               : "
         << stats.total_tiles
         << "\n\n";

    //---------------------
    cout << "[MEMORY]\n";

    cout << "Datatype                  : "
         << stats.datatype
         << "\n";
    
     cout << "Bytes per Element         : "
         << stats.bytes_per_element
         << "\n";

    cout << "Original Tensor Size      : "
         << stats.raw_bytes / 1024.0
         << " KB\n";

    cout << "Tiled Memory Footprint    : "
         << stats.padded_bytes / 1024.0
         << " KB\n";

    cout << "Padding Overhead          : "
         << stats.padding_percent
         << " %\n\n";

    //---------------------
    cout << "[WORKLOAD DISTRIBUTION (PER MATRIX)]\n";

    cout << "Available Tensix Cores    : "
         << TOTAL_CORES
         << "\n";

    cout << "Active Cores              : "
         << stats.active_cores
         << " / "
         << TOTAL_CORES
         << "\n";

    cout << "Idle Cores                : "
         << stats.idle_cores
         << "\n";

    cout << "Average Tiles per Active Core : "
         << stats.avg_tiles_per_active_core
         << "\n";
    
     cout << "Workload Efficiency       : "
         << stats.workload_efficiency
         <<"%"<< "\n\n";

    //---------------------
    cout << "[L1 MEMORY ESTIMATE (PER MATRIX)]\n";

    cout << "Estimated L1 Needed/Core  : "
         << stats.l1_usage_kb
         << " KB\n";

    cout << "Approximate L1 Capacity   : "
         << L1_SIZE_KB
         << " KB\n";

    cout << "Estimated L1 Utilization  : "
         << stats.l1_percent
         << " %\n\n";
    
    
    //---------------------
    cout << "[WARNINGS]\n";

    if (stats.active_cores < TOTAL_CORES)    {
    cout << "Workload does not fully utilize available cores. "
         <<"\n";
    }

    if (stats.padding_percent > 20.0){
    cout << "Warning: High padding overhead detected.\n";
    }

    if (stats.l1_percent > 80.0){
    cout << "Warning: Estimated L1 usage is high.\n";
    }
 

    //---------------------
    
    cout << "\n[NOTES]\n";

    cout << "- Models one Wormhole ASIC only.\n";
    cout << "- Data types of tensors are user-configurable.\n";
    cout<< "- Tile dimensions are user-configurable.\n";
    cout << "- Uses simplified one-tile-per-core style mapping.\n";
    cout << "- L1 values are estimates, not hardware measurements.\n";
    cout << "- Intended for architecture understanding and workload analysis.\n";

    cout << "\n=============================================================\n";
    }

//=========================================================

int main()
{
    load_hardware_specs("hardware_specs.json");
    print_header();
 
    int datatype,tensor_rank;
    string type;
    uint32_t bytes_per_element, tile_height, tile_width;
    
    cout<<"Select tensor dimensionality"<<endl;
    cout<<"2)2D\n3)3D\n4)4D\nYour choice:";
    cin>> tensor_rank;

    if (tensor_rank < 2 || tensor_rank > 4)
    {
        cout << "Only 2D, 3D and 4D tensors are supported.\n";
        return 0;
    }

    vector<uint32_t> shape;

    for (uint32_t i = 0; i < tensor_rank; i++){
        uint32_t value;

        if (i == tensor_rank - 2)
            cout << "Rows: ";
        else if (i == tensor_rank - 1)
            cout << "Columns: ";
        else
            cout << "Dimension " << i << ": ";

        cin >> value;

        shape.push_back(value);
    }
   

    cout << "\nEnter tile height : ";
    cin >> tile_height;

    cout << "Enter tile width : ";
    cin >> tile_width;

    cout<<"\nSelect datatype \n1) FP32 \n2) FP16 \n3) BF16 \n4) INT8 \n5) INT32 \n6) INT64\n";
    cout<< "Your choice:";
    cin>> datatype;
    switch (datatype){
    case 1: bytes_per_element = 4; type = "FP32";  break;   // FP32
    case 2: bytes_per_element = 2; type = "FP16"; break;   // FP16
    case 3: bytes_per_element = 2; type = "BF16"; break;   // BF16
    case 4: bytes_per_element = 1; type = "INT8"; break;   // INT8
    case 5: bytes_per_element = 4; type = "INT32"; break;   // INT32
    case 6: bytes_per_element = 8; type = "INT64"; break;   // INT64
    default:
        cout << "Invalid selection.\n";
        return 0;
    }
  

    LayoutStatistics stats = calculate_layout_statistics(shape, tile_height, tile_width,bytes_per_element,type,tensor_rank);


    print_report(stats);
    draw_tile_grid(stats.tile_rows, stats.tile_cols);
    draw_core_grid(stats.tiles_per_matrix);

    return 0;
}
