# Social Network Manager

## Overview

This project is a C++ application designed to analyze social networks by representing them as graphs. In this system, nodes represent users, and edges represent relationships between users. The application includes algorithms to identify key influencers, detect communities within the network, and recommend new connections. It aims to provide valuable insights into social networks by analyzing the structure and relationships between users, allowing for better network management and user interaction recommendations. The project structure is organized into source files, header files, and other resources like CSV data and PNG graphs.

## Build Instructions

To build the project, you can use the provided `build.bat` script. This script compiles the source files and links them to create the executable.

### Steps to Build

1. Open a command prompt.
2. Navigate to the `DSA_ProjectCM_42_54_55_56` directory.
3. Run the `build.bat` script:

    ```sh
    build.bat
    ```

The script will create a `build` directory (if it doesn't exist), compile the source files, and link them to produce the `program.exe` executable.

## Project Structure
### Source Files

- `cpp files/adjacency_matrix.cpp`: Implements the adjacency matrix functionality.
- `cpp files/connection_handler.cpp`: Handles connections.
- `cpp files/connection_manager.cpp`: Manages connections.
- `cpp files/graph_visualizer.cpp`: Visualizes graphs.
- `cpp files/login_manager.cpp`: Manages user logins.
- `cpp files/main.cpp`: The main entry point of the application.
- `cpp files/print_utilities.cpp`: Utility functions for printing.

### Header Files

- `header files/adjacency_matrix.h`: Header for adjacency matrix functionality.
- `header files/connection_handler.h`: Header for connection handler.
- `header files/connection_manager.h`: Header for connection manager.
- `header files/graph_visualizer.h`: Header for graph visualizer.
- `header files/login_manager.h`: Header for login manager.
- `header files/print_utilities.h`: Header for print utilities.
- `header files/user.h`: Header for user-related functionality.

### CSV Files

- `adjacency_matrix.csv`: CSV file containing adjacency matrix data.
- `user_data.csv`: CSV file containing user data.

### Output

- `program.exe`: The executable generated after building the project.

### Additional Resources

- `png_graphs/`: Directory containing PNG graphs.

