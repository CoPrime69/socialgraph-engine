#include "connection_manager.h"
#include "adjacency_matrix.h"
#include "login_manager.h"
#include "graph_visualizer.h"
#include "connection_handler.h"
#include "print_utilities.h"
#include <bits/stdc++.h>

using namespace std;

int main()
{
     string user_data_file = "user_data.csv";
    ConnectionManager cm(user_data_file);
    LoginManager login_manager(cm);

    // Choose between login or registration
     cout << "1. Login\n2. Register\nChoose an option: ";
    int choice;
     cin >> choice;

     string id, password;
     string name, category, branch;
    double influence;

    User *user = nullptr;

    if (choice == 1)
    {
        // Login
         cout << "Enter User ID: ";
         cin >> id;
         cout << "Enter Password: ";
         cin >> password;

        user = login_manager.login(id, password);
        if (!user)
        {
             cout << "Invalid ID or password!" <<  endl;
            return 0;
        }
    }
    else if (choice == 2)
    {
        // Register
         cout << "Enter Name: ";
         cin >> name;
         cout << "Enter User ID: ";
         cin >> id;
         cout << "Enter Password: ";
         cin >> password;

        if (login_manager.isUserExist(id))
        {
             cout << "User ID already exists. Please login." <<  endl;
            return 0;
        }

         cout << "Enter Category: ";
         cin >> category;
         cout << "Enter Influence: ";
         cin >> influence;
         cout << "Enter Branch: ";
         cin >> branch;

        if (!login_manager.registerUser(name, id, password, category, influence, branch))
        {
             cout << "Registration failed!" <<  endl;
            return 0;
        }
        user = login_manager.getUserById(id);
    }
    // else if(choice == 3){
    //      vector< vector<User *>> communities = cm.detectCommunities();
    //     GraphVisualizer visualizer(15.0);
    //     visualizer.createGraph("adjacency_matrix.csv", communities, "social_network_graph.png");
    // }
    else
    {
         cout << "Invalid choice!" <<  endl;
        return 0;
    }

    // Calculate centrality measures
     unordered_map< string, double> betweenness = cm.calculateBetweennessCentrality();
     unordered_map< string, double> closeness = cm.calculateClosenessCentrality();
     unordered_map< string, double> pagerank = cm.calculatePageRank();

    // Detect communities
     vector< vector<User *>> communities = cm.detectCommunities();

    // Print the results using PrintUtilities
    PrintUtilities::printResults(cm, pagerank, communities);

    // Handle connection management using ConnectionHandler
    ConnectionHandler::handleConnectionManagement(cm, user);

    // Visualize the updated graph
    cm.visualizeGraph("updated_social_network_graph.png", betweenness, communities);

    // GraphVisualizer visualizer(15.0);
    // visualizer.createGraph("adjacency_matrix.csv", communities, "social_network_graph.png");

     cout << "Program completed successfully!" <<  endl;
    return 0;
}