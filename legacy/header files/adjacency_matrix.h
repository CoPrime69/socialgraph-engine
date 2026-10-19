#ifndef ADJACENCY_MATRIX_H
#define ADJACENCY_MATRIX_H

#include <bits/stdc++.h>
#include "user.h"

using namespace std;

class AdjacencyMatrix
{
public:
    // Constructor and Destructor
    AdjacencyMatrix(const string &matrix_file);
    ~AdjacencyMatrix();

    // Initialization
    void initializeBaseMatrix(const vector<User *> &users, const vector<vector<User *>> &communities);
    void addNewUser(const string &userId); // Add new user to matrix with zero connections

    // Connection Management
    void updateConnection(const string &user1_id, const string &user2_id, const vector<vector<User *>> &communities);
    void removeConnection(const string &user1_id, const string &user2_id);
    double calculateConnectionWeight(User *user1, User *user2, const vector<vector<User *>> &communities);
    double getConnectionWeight(const string &user1_id, const string &user2_id) const;

    // File Operations
    void saveToFile(); // Save matrix to CSV
    void loadFromFile(); // Load matrix from CSV
    void forceSave(); // Force save changes to file

    // Utility Functions
    const unordered_map<string, int> &getUserIndexMap() const;
    const vector<vector<double>> &getMatrix() const;
    void printMatrix() const; // Print matrix (for debugging)

private:
    // Data Members
    string matrix_file_path;
    vector<string> user_ids; // Order of users
    unordered_map<string, int> user_index_map; // Maps user IDs to matrix indices
    vector<vector<double>> matrix; // The adjacency matrix

    // Helper Functions
    int findCommunityIndex(const string &user_id, const vector<vector<User *>> &communities) const;
    bool areInSameCommunity(const string &user1_id, const string &user2_id, const vector<vector<User *>> &communities) const;

    bool hasChanges = false; // Track changes for saving
};

#endif // ADJACENCY_MATRIX_H
