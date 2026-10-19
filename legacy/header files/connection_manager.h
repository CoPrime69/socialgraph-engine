#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <bits/stdc++.h>
#include "adjacency_matrix.h"
#include "user.h"

using namespace std;

class ConnectionManager
{
public:
    // Constructor
    ConnectionManager(const string &user_data_file);

    // Centrality Calculation Functions
    unordered_map<string, double> calculateBetweennessCentrality();
    unordered_map<string, double> calculateClosenessCentrality();
    unordered_map<string, double> calculatePageRank();

    // User Management
    vector<User *> getAllUsers();
    void addUser(User *new_user);
    User *getUser(const string &id) const;
    void loadUserData(const string &file_path);
    void saveUserData(const string &file_path);
    int getRandomPosition(int max); // For random selection of users
    double getLuminance(const string &color); // Utility to calculate luminance of a color

    // Connection Management
    vector<pair<User *, User *>> recommendConnectionsForNewUser(User *new_user);
    void addConnection(User *user1, User *user2);
    void removeConnection(User *user1, User *user2);
    void viewConnections(User *user);
    bool isValidConnection(const string &user1_id, const string &user2_id);
    double getConnectionWeight(const string &user1_id, const string &user2_id) const;

    // Community Detection
    vector<vector<User *>> detectCommunities();

    // Graph Visualization
    void visualizeGraph(const string &output_file, const unordered_map<string, double> &betweenness, const vector<vector<User *>> &communities);

    // Initialize the Adjacency Matrix
    void initializeAdjacencyMatrix();
    void establishConnections();

private:
    // User and Connection Data
    unordered_map<string, vector<string>> connections;
    vector<User *> users;
    
    // Adjacency Matrix and Constants
    AdjacencyMatrix *adjacency_matrix; // Adjacency matrix for connections
    const int InitialNum = 100; // Initial number of users or connections

};

#endif // CONNECTION_MANAGER_H
