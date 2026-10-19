#ifndef PRINT_UTILITIES_H
#define PRINT_UTILITIES_H

#include "connection_manager.h"
#include  <bits/stdc++.h>

using namespace std;

class PrintUtilities
{
public:
    static void printResults(ConnectionManager &cm,
                             const  unordered_map< string, double> &pagerank,
                             const  vector< vector<User *>> &communities);

    static void printRecommendedCommunities(const  vector< vector<User *>> &communities,
                                            User *new_user);

    static void printRecommendedConnections(const  vector< pair<User *, User *>> &recommendations,
                                            User *new_user);

    static int selectConnection(const  vector< pair<User *, User *>> &recommendations);

private:
    static  vector<int> g_validIndices;
    static int g_currentStart;
};

#endif