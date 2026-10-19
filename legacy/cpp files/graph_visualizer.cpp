#include "graph_visualizer.h"
#include <bits/stdc++.h>
// #include <filesystem>

using namespace std;

GraphVisualizer::GraphVisualizer(double threshold) : weight_threshold(threshold) {}

// Function to read the adjacency matrix from a CSV file and populate user IDs
vector<vector<double>> GraphVisualizer::readAdjacencyMatrix(
    const string& matrix_file, vector<string>& user_ids) {
    
    ifstream file(matrix_file);
    if (!file.is_open()) {
        cerr << "Could not open matrix file: " << matrix_file << endl;
        return vector<vector<double>>();
    }

    vector<vector<double>> matrix;
    string line;

    // Read header to get user IDs
    if (getline(file, line)) {
        stringstream ss(line);
        string cell;
        getline(ss, cell, ',');  // Skip empty cell

        while (getline(ss, cell, ',')) {
            if (!cell.empty()) {
                user_ids.push_back(cell);
            }
        }
    }

    // Read matrix data
    while (getline(file, line)) {
        vector<double> row;
        stringstream ss(line);
        string cell;

        getline(ss, cell, ',');  // Skip row header

        while (getline(ss, cell, ',')) {
            if (!cell.empty()) {
                try {
                    row.push_back(stod(cell));
                } catch (const exception& e) {
                    cerr << "Error converting value: " << cell << endl;
                    row.push_back(0.0);
                }
            }
        }

        if (!row.empty()) {
            matrix.push_back(row);
        }
    }

    file.close();
    return matrix;
}

// Function to create a graph from the adjacency matrix and communities
void GraphVisualizer::createGraph(const string& matrix_file,
                                   const vector<vector<User*>>& communities,
                                   const string& output_file) {
    
    vector<string> user_ids;
    vector<vector<double>> matrix = readAdjacencyMatrix(matrix_file, user_ids);

    if (matrix.empty()) {
        cerr << "Failed to read adjacency matrix" << endl;
        return;
    }

    string dot_file = "temp_graph.dot";
    ofstream dot_out(dot_file);
    dot_out << generateDotFormat(matrix, user_ids, communities);
    dot_out.close();

    // Construct the output path by combining png_graphs directory with output filename
    string output_path = "png_graphs/" + output_file;

    // Modified command for 4K resolution (3840 x 3840) with path to png_graphs folder
    string command = "dot -Kneato"
                     " -Tpng"
                     " -Gdpi=96"
                     " -Gsize=\"40,40!\""  // 3840/96 = 40 inches
                     " -Goverlap=scale"
                     " -Gsplines=true"
                     " -Gstart=random"
                     " -Gepsilon=0.0001"
                     " -Gmaxiter=1000"
                     " " + dot_file + " -o " + output_path;

    int result = system(command.c_str());

    if (result == 0) {
        cout << "Graph visualization created successfully: " << output_path << endl;
        remove(dot_file.c_str());
    } else {
        cerr << "Failed to create graph visualization" << endl;
    }
}

// Function to generate the DOT format string for graph visualization
string GraphVisualizer::generateDotFormat(
    const vector<vector<double>>& matrix,
    const vector<string>& user_ids,
    const vector<vector<User*>>& communities) {

    stringstream dot;
    dot << "graph Network {\n";

    // Graph attributes - Modified to constrain layout area
    dot << "    graph [\n"
        << "        overlap=false\n"
        << "        splines=true\n"
        << "        K=2\n"
        << "        layout=neato\n"
        << "        mindist=1.0\n"
        << "        outputorder=nodesfirst\n"
        << "        pad=0.5\n"
        << "        margin=2.0\n"  // Increased margin
        << "        size=\"35,24!\"\n"  // Restrict main graph size to leave room for legend
        << "    ];\n\n";

    // Node attributes
    dot << "    node [\n"
        << "        shape=circle\n"
        << "        style=filled\n"
        << "        width=0.6\n"
        << "        height=0.6\n"
        << "        penwidth=1.5\n"
        << "        fontname=\"Arial\"\n"
        << "        fontsize=10\n"
        << "    ];\n\n";

    // Edge attributes
    dot << "    edge [\n"
        << "        penwidth=1.5\n"
        << "        color=\"#333333\"\n"
        << "    ];\n\n";

    // Define colors for different communities
    const array<string, 20> colors = {{
        "\"#ff7f7f\"", "\"#7fbfff\"", "\"#7fff7f\"", "\"#ff7fff\"", "\"#ffff7f\"",
        "\"#7fffff\"", "\"#ff9966\"", "\"#cccc66\"", "\"#9966ff\"", "\"#ff9966\"",
        "\"#ff6666\"", "\"#66ccff\"", "\"#ffcc66\"", "\"#66ffcc\"", "\"#cc66ff\"",
        "\"#ff66cc\"", "\"#ffccff\"", "\"#66cc66\"", "\"#cccc66\"", "\"#ccff66\""
    }};

    // Add legend container first
    dot << "    // Legend box\n";
    dot << "    subgraph cluster_legend {\n";
    dot << "        margin=20;\n"
        << "        label=\"\";\n"
        << "        fontsize=14;\n"
        << "        style=filled;\n"
        << "        color=white;\n"
        << "        bgcolor=white;\n"
        << "        pos=\"35.5,18!\";\n";  // Adjusted position further right

    // Add title for the legend
    dot << "        \"legend_title\" [shape=none;\n"
        << "            label=\"Communities\";\n"
        << "            fontsize=12;\n"
        << "            fontname=\"Arial Bold\";\n"
        << "            pos=\"36.5,17!\";\n"
        << "        ];\n";

    // Create vertical legend entries with adjusted positioning
    float y_pos = 16.5;  // Start below the title
    for (size_t i = 0; i < communities.size(); i++) {
        string legendNodeId = "legend_" + to_string(i);
        string legendLabelId = "legend_label_" + to_string(i);

        // Add legend node (colored square)
        dot << "        \"" << legendNodeId << "\" [\n"
            << "            shape=square;\n"
            << "            style=filled;\n"
            << "            fillcolor=" << colors[i % colors.size()] << ";\n"
            << "            width=0.3;\n"
            << "            height=0.3;\n"
            << "            label=\"\";\n"
            << "            pos=\"35.8," << y_pos << "!\";\n"
            << "        ];\n";

        // Add legend label with adjusted position
        dot << "        \"" << legendLabelId << "\" [\n"
            << "            shape=none;\n"
            << "            fontsize=10;\n"
            << "            label=\" Community " << i + 1 << "\";\n"
            << "            pos=\"37.2," << y_pos << "!\";\n"
            << "        ];\n";

        y_pos -= 0.6;
    }
    dot << "    }\n\n";

    // Create a map to store user_id to community index mapping
    unordered_map<string, size_t> userCommunityMap;
    for (size_t communityIdx = 0; communityIdx < communities.size(); communityIdx++) {
        for (const User* user : communities[communityIdx]) {
            userCommunityMap[user->getID()] = communityIdx;
        }
    }

    // Add graph nodes with community colors
    for (size_t i = 0; i < communities.size(); i++) {
        for (const User* user : communities[i]) {
            dot << "    \"" << user->getID() << "\" ["
                << "fillcolor=" << colors[i % colors.size()]
                << ", label=\"" << user->getName() << "\"];\n";
        }
    }
    dot << "\n";

    // Add edges with constraints to keep within main graph area
    int edge_count = 0;
    for (size_t i = 0; i < matrix.size(); i++) {
        for (size_t j = i + 1; j < matrix[i].size(); j++) {
            bool exceedsThreshold = (matrix[i][j] >= weight_threshold);

            if (exceedsThreshold) {
                double len = min(0.3, 19.0 / matrix[i][j]);
                dot << "    \"" << user_ids[i] << "\" -- \"" << user_ids[j] 
                    << "\" [len=" << len
                    << ", penwidth=2.5];\n";
                edge_count++;
            }
        }
    }

    // Add constraint to keep nodes within main graph area
    dot << "    {rank=same; ";
    for (const auto& user_id : user_ids) {
        dot << "\"" << user_id << "\"; ";
    }
    dot << "}\n";
    cout << "Number of edges: " << edge_count << endl <<endl;


    dot << "}\n";
    return dot.str();
}
