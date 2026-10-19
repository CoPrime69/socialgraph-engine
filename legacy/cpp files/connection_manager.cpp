#include "connection_manager.h"
#include <bits/stdc++.h>
#include <omp.h>
#include "graph_visualizer.h"

using namespace std;

class GraphTraversal
{
public:
    template <typename ProcessNode, typename ProcessEdge>
    static void bfs(
        const unordered_map<string, vector<string>> &graph,
        const string &start_id,
        ProcessNode process_node,
        ProcessEdge process_edge)
    {
        unordered_map<string, bool> visited;
        queue<string> queue;

        visited[start_id] = true;
        queue.push(start_id);

        while (!queue.empty())
        {
            string current = queue.front();
            queue.pop();

            process_node(current);

            if (graph.find(current) != graph.end())
            {
                for (const auto &neighbor : graph.at(current))
                {
                    process_edge(current, neighbor);

                    if (!visited[neighbor])
                    {
                        visited[neighbor] = true;
                        queue.push(neighbor);
                    }
                }
            }
        }
    }

    static unordered_map<string, double> bfsDistance(
        const unordered_map<string, vector<string>> &graph,
        const string &start_id)
    {
        unordered_map<string, double> distances;

        bfs(
            graph,
            start_id,
            [&](const string &node)
            {
                if (distances.find(node) == distances.end())
                {
                    distances[node] = node == start_id ? 0.0 : numeric_limits<double>::max();
                }
            },
            [&](const string &from, const string &to)
            {
                double new_dist = distances[from] + 1;
                if (new_dist < distances[to])
                {
                    distances[to] = new_dist;
                }
            });

        return distances;
    }

    static vector<string> bfsPath(
        const unordered_map<string, vector<string>> &graph,
        const string &start_id,
        const string &end_id)
    {
        unordered_map<string, string> predecessors;
        bool found = false;

        bfs(
            graph,
            start_id,
            [&](const string &node)
            {
                if (node == end_id)
                {
                    found = true;
                }
            },
            [&](const string &from, const string &to)
            {
                if (predecessors.find(to) == predecessors.end())
                {
                    predecessors[to] = from;
                }
            });

        if (!found)
        {
            return vector<string>();
        }

        vector<string> path;
        string current = end_id;
        while (current != start_id)
        {
            path.push_back(current);
            current = predecessors[current];
        }
        path.push_back(start_id);
        reverse(path.begin(), path.end());

        return path;
    }
};

ConnectionManager::ConnectionManager(const string &user_data_file)
    : adjacency_matrix(new AdjacencyMatrix("adjacency_matrix.csv"))
{
    loadUserData(user_data_file);
    initializeAdjacencyMatrix();
    establishConnections();
}

// ConnectionManager::~ConnectionManager() {
//     delete adjacency_matrix;
// }

void ConnectionManager::initializeAdjacencyMatrix()
{
    // Only initialize the matrix for the first InitialNum users
    vector<User *> initial_users;
    for (int i = 0; i < min(InitialNum, (int)users.size()); i++)
    {
        initial_users.push_back(users[i]);
    }

    vector<vector<User *>> communities = detectCommunities();
    adjacency_matrix->initializeBaseMatrix(initial_users, communities);
}

void ConnectionManager::addConnection(User *user1, User *user2)
{
    // First ensure both users are in the adjacency matrix
    adjacency_matrix->addNewUser(user1->getID());
    adjacency_matrix->addNewUser(user2->getID());

    // Add the connection to the connection map
    connections[user1->getID()].push_back(user2->getID());
    connections[user2->getID()].push_back(user1->getID());

    // Update the adjacency matrix with the new connection and weight
    vector<vector<User *>> communities = detectCommunities();
    adjacency_matrix->updateConnection(user1->getID(), user2->getID(), communities);
}

void ConnectionManager::removeConnection(User *user1, User *user2)
{
    if (!isValidConnection(user1->getID(), user2->getID()))
    {
        cout << "No valid connection exists between these users." << endl;
        return;
    }

    // Remove the connection from the adjacency matrix
    adjacency_matrix->removeConnection(user1->getID(), user2->getID());

    // Update the connections map
    connections[user1->getID()].erase(
        remove(connections[user1->getID()].begin(),
               connections[user1->getID()].end(),
               user2->getID()),
        connections[user1->getID()].end());

    connections[user2->getID()].erase(
        remove(connections[user2->getID()].begin(),
               connections[user2->getID()].end(),
               user1->getID()),
        connections[user2->getID()].end());
}

bool ConnectionManager::isValidConnection(const string &user1_id, const string &user2_id)
{
    // Use the public getConnectionWeight method instead of accessing private matrix directly
    double weight = adjacency_matrix->getConnectionWeight(user1_id, user2_id);
    return weight >= 13.0;
}

void ConnectionManager::viewConnections(User *user)
{
    cout << "Connections for " << user->getName() << ":" << endl;
    bool hasConnections = false;

    // Get all users and check connections with weight threshold
    for (const auto &other_user : users)
    {
        if (other_user->getID() != user->getID())
        { // Don't check connection with self
            double weight = adjacency_matrix->getConnectionWeight(user->getID(), other_user->getID());

            if (weight >= 13.0)
            {
                cout << other_user->getName()
                     << " (ID: " << other_user->getID()
                     << ", Weight: " << weight << ")" << endl;
                hasConnections = true;
            }
        }
    }

    if (!hasConnections)
    {
        cout << "No valid connections found (weight >= 15.0)." << endl;
    }
}

void ConnectionManager::addUser(User *new_user)
{
    // Add user to the adjacency matrix first
    adjacency_matrix->addNewUser(new_user->getID());

    // Add connections based on category and branch
    connections[new_user->getID()] = {};
    vector<vector<User *>> communities = detectCommunities();

    for (User *user : users)
    {
        if (user != new_user &&
            user->getCategory() == new_user->getCategory() &&
            user->getBranch() == new_user->getBranch())
        {

            if (connections[new_user->getID()].size() < 5 &&
                connections[user->getID()].size() < 5)
            {

                connections[new_user->getID()].push_back(user->getID());
                connections[user->getID()].push_back(new_user->getID());

                // Update the connection weight in the adjacency matrix
                adjacency_matrix->updateConnection(new_user->getID(), user->getID(), communities);
            }
        }
    }

    users.push_back(new_user);
}

double ConnectionManager::getConnectionWeight(const string &user1_id, const string &user2_id) const
{
    return adjacency_matrix->getConnectionWeight(user1_id, user2_id);
}

unordered_map<string, double> ConnectionManager::calculateBetweennessCentrality()
{
    unordered_map<string, double> betweenness;

#pragma omp parallel
    {
        unordered_map<string, double> local_betweenness;

#pragma omp for schedule(dynamic)
        for (size_t i = 0; i < users.size(); i++)
        {
            const auto &user = users[i];
            queue<string> queue;
            unordered_map<string, double> distance;
            unordered_map<string, vector<string>> predecessors;
            stack<string> stack;
            unordered_map<string, double> dependency;

            for (const auto &u : users)
            {
                distance[u->getID()] = numeric_limits<double>::max();
            }

            distance[user->getID()] = 0;
            queue.push(user->getID());

            // Forward pass - shortest paths calculation
            while (!queue.empty())
            {
                string curr = queue.front();
                queue.pop();
                stack.push(curr);

                for (const auto &neighbor_id : connections[curr])
                {
                    if (distance[neighbor_id] == numeric_limits<double>::max())
                    {
                        queue.push(neighbor_id);
                        distance[neighbor_id] = distance[curr] + 1;
                    }
                    if (distance[neighbor_id] == distance[curr] + 1)
                    {
                        predecessors[neighbor_id].push_back(curr);
                    }
                }
            }

            // Backward pass - dependency accumulation
            while (!stack.empty())
            {
                string w = stack.top();
                stack.pop();

                for (const auto &v : predecessors[w])
                {
                    double factor = (1.0 + dependency[w]) / predecessors[w].size();
                    dependency[v] += factor;
                }

                if (w != user->getID())
                {
                    local_betweenness[w] += dependency[w];
                }
            }
        }

#pragma omp critical
        {
            for (const auto &pair : local_betweenness)
            {
                betweenness[pair.first] += pair.second;
            }
        }
    }

    return betweenness;
}

unordered_map<string, double> ConnectionManager::calculateClosenessCentrality()
{
    unordered_map<string, double> closeness;

#pragma omp parallel
    {
        unordered_map<string, double> local_closeness;

#pragma omp for schedule(dynamic)
        for (size_t i = 0; i < users.size(); i++)
        {
            const auto &user = users[i];
            auto distances = GraphTraversal::bfsDistance(connections, user->getID());

            double total_distance = 0.0;
            int reachable_nodes = 0;

            for (const auto &dist : distances)
            {
                if (dist.second != numeric_limits<double>::max())
                {
                    total_distance += dist.second;
                    reachable_nodes++;
                }
            }

            // Normalize by number of reachable nodes
            if (reachable_nodes > 1)
            { // Exclude self from count
                local_closeness[user->getID()] = (reachable_nodes - 1) / total_distance;
            }
            else
            {
                local_closeness[user->getID()] = 0.0;
            }
        }

#pragma omp critical
        {
            closeness.insert(local_closeness.begin(), local_closeness.end());
        }
    }

    return closeness;
}

// unordered_map<string, double> ConnectionManager::calculatePageRank()
// {
//     unordered_map<string, double> pagerank;
//     unordered_map<string, double> out_degree;

//     for (const auto &user : users)
//     {
//         pagerank[user->getID()] = 1.0 / users.size();
//         out_degree[user->getID()] = connections[user->getID()].size();
//     }

//     double damping_factor = 0.85;
//     double tolerance = 1e-6;

//     bool converged = false;
//     while (!converged)
//     {
//         unordered_map<string, double> new_pagerank;
//         double total_pagerank = 0.0;

//         for (const auto &user : users)
//         {
//             double sum = 0.0;
//             for (const auto &neighbor_id : connections[user->getID()])
//             {
//                 sum += pagerank[neighbor_id] / out_degree[neighbor_id];
//             }
//             new_pagerank[user->getID()] = (1.0 - damping_factor) / users.size() + damping_factor * sum;
//             total_pagerank += new_pagerank[user->getID()];
//         }

//         converged = true;
//         for (const auto &user : users)
//         {
//             new_pagerank[user->getID()] /= total_pagerank;
//             if (abs(new_pagerank[user->getID()] - pagerank[user->getID()]) > tolerance)
//             {
//                 converged = false;
//                 break;
//             }
//         }

//         pagerank = move(new_pagerank);
//     }

//     return pagerank;
// }

unordered_map<string, double> ConnectionManager::calculatePageRank()
{
    unordered_map<string, double> pagerank;
    unordered_map<string, double> out_degree;
    vector<string> dangling_nodes; // Stores nodes with no outgoing edges

    // Initialize pagerank and calculate out-degrees
    for (const auto &user : users)
    {
        string user_id = user->getID();
        pagerank[user_id] = 1.0 / users.size();
        out_degree[user_id] = connections[user_id].size();

        if (out_degree[user_id] == 0)
        {
            dangling_nodes.push_back(user_id); // Mark dangling nodes
        }
    }

    double damping_factor = 0.85;
    double tolerance = 1e-4;      // Start with a larger tolerance for faster initial iterations
    double min_tolerance = 1e-6;  // Minimum tolerance to tighten as convergence improves
    double tolerance_decay = 0.9; // Factor to reduce tolerance per iteration
    bool converged = false;

    while (!converged)
    {
        unordered_map<string, double> new_pagerank;
        double total_pagerank = 0.0;
        double dangling_sum = 0.0; // Contribution from dangling nodes

        // Sum contributions from dangling nodes
        for (const auto &dangling_id : dangling_nodes)
        {
            dangling_sum += pagerank[dangling_id];
        }
        dangling_sum *= damping_factor / users.size();

// Parallelize the computation of new pagerank values
#pragma omp parallel for reduction(+ : total_pagerank)
        for (size_t i = 0; i < users.size(); ++i)
        {
            User *user = users[i];
            string user_id = user->getID();
            double sum = 0.0;

            // Sum up the contributions from neighbors
            for (const auto &neighbor_id : connections[user_id])
            {
                sum += pagerank[neighbor_id] / out_degree[neighbor_id];
            }

            // Calculate new PageRank for the current user, including dangling node contribution
            new_pagerank[user_id] = (1.0 - damping_factor) / users.size() + damping_factor * sum + dangling_sum;
            total_pagerank += new_pagerank[user_id];
        }

        // Normalize new PageRank values and check for convergence
        converged = true;
#pragma omp parallel for
        for (size_t i = 0; i < users.size(); ++i)
        {
            User *user = users[i];
            string user_id = user->getID();

            new_pagerank[user_id] /= total_pagerank;

            // Check convergence
            if (fabs(new_pagerank[user_id] - pagerank[user_id]) > tolerance)
            {
#pragma omp atomic write
                converged = false;
            }
        }

        pagerank = move(new_pagerank);

        // Tighten the tolerance for the next iteration if not yet at min_tolerance
        tolerance = max(min_tolerance, tolerance * tolerance_decay);
    }

    return pagerank;
}

// vector<vector<User *>> ConnectionManager::detectCommunities()
// {
//     vector<vector<User *>> communities;
//     unordered_map<string, int> community_map;
//     int num_communities = 0;

//     for (User *user : users)
//     {
//         bool found_community = false;
//         for (int i = 0; i < communities.size() && i < 20; ++i)
//         {
//             bool can_join = true;
//             for (User *neighbor : communities[i])
//             {
//                 if (find(connections[user->getID()].begin(), connections[user->getID()].end(), neighbor->getID()) == connections[user->getID()].end())
//                 {
//                     can_join = false;
//                     break;
//                 }
//             }
//             if (can_join)
//             {
//                 communities[i].push_back(user);
//                 community_map[user->getID()] = i;
//                 found_community = true;
//                 break;
//             }
//         }
//         if (!found_community)
//         {
//             if (num_communities < 20)
//             {
//                 communities.emplace_back(vector<User *>{user});
//                 community_map[user->getID()] = num_communities++;
//             }
//             else
//             {
//                 // Assign user to the community with the least members
//                 int min_community = 0;
//                 for (int i = 1; i < 20; ++i)
//                 {
//                     if (communities[i].size() < communities[min_community].size())
//                     {
//                         min_community = i;
//                     }
//                 }
//                 communities[min_community].push_back(user);
//                 community_map[user->getID()] = min_community;
//             }
//         }
//     }

//     for (int i = 0; i < communities.size(); ++i)
//     {
//         string community_name = "Community " + to_string(i + 1);
//         for (User *user : communities[i])
//         {
//             if (user->getBranch() == "electrical")
//             {
//                 community_name = "Electrical Engineers";
//                 break;
//             }
//         }
//         for (User *user : communities[i])
//         {
//             community_map[user->getID()] = i;
//         }
//     }

//     return communities;
// }

vector<vector<User *>> ConnectionManager::detectCommunities()
{
    const int TARGET_COMMUNITY_SIZE = users.size() / 10;       // Aim for roughly 10 communities
    const int MAX_COMMUNITY_SIZE = floor(users.size() * 0.14); // Allow some flexibility

    vector<vector<User *>> communityGroups;
    unordered_map<string, int> communities; // maps user ID to community ID

    // Sort users by branch and category to help initial grouping
    vector<User *> sortedUsers = users;
    sort(sortedUsers.begin(), sortedUsers.end(),
         [](const User *a, const User *b)
         {
             if (a->getBranch() != b->getBranch())
                 return a->getBranch() < b->getBranch();
             return a->getCategory() < b->getCategory();
         });

    // Initial assignment based on similar attributes
    int currentCommunity = 0;
    vector<User *> currentGroup;

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dis(static_cast<int>(MAX_COMMUNITY_SIZE) - 4, static_cast<int>(MAX_COMMUNITY_SIZE) + 4);

    for (User *user : sortedUsers)
    {
        int random_max_size = dis(gen); // Generate a random number within the range

        if (currentGroup.size() >= random_max_size)
        {
            if (!currentGroup.empty())
            {
                communityGroups.push_back(currentGroup);
                currentGroup.clear();
                currentCommunity++;
            }
        }

        currentGroup.push_back(user);
        communities[user->getID()] = currentCommunity;
    }

    // Don't forget the last group
    if (!currentGroup.empty())
    {
        communityGroups.push_back(currentGroup);
    }

    // Refinement phase - iterate a few times to balance communities
    for (int iteration = 0; iteration < 3; iteration++)
    {
        bool changed = false;

        for (User *user : users)
        {
            int currentComm = communities[user->getID()];
            unordered_map<int, int> neighborCommunities;

            // Count neighbor communities
            for (const string &neighborId : connections[user->getID()])
            {
                neighborCommunities[communities[neighborId]]++;
            }

            // Find best community based on connections and size
            int bestCommunity = currentComm;
            double bestScore = 0;

            for (const auto &pair : neighborCommunities)
            {
                // Calculate score based on:
                // 1. Number of connections in that community
                // 2. Current size of that community (prefer smaller communities)
                double communitySize = communityGroups[pair.first].size();
                double sizeScore = 1.0 - (communitySize / MAX_COMMUNITY_SIZE);
                double connectionScore = pair.second / (double)connections[user->getID()].size();
                double score = connectionScore * 0.7 + sizeScore * 0.3; // Weight factors

                if (score > bestScore && communityGroups[pair.first].size() < MAX_COMMUNITY_SIZE)
                {
                    bestScore = score;
                    bestCommunity = pair.first;
                }
            }

            // Move user if better community found
            if (bestCommunity != currentComm && bestScore > 0.3)
            { // Threshold to prevent random moves
                // Remove from current community
                auto &currentVec = communityGroups[currentComm];
                currentVec.erase(remove(currentVec.begin(), currentVec.end(), user), currentVec.end());

                // Add to new community
                communityGroups[bestCommunity].push_back(user);
                communities[user->getID()] = bestCommunity;
                changed = true;
            }
        }

        if (!changed)
            break;
    }

    // Remove empty communities
    communityGroups.erase(
        remove_if(communityGroups.begin(), communityGroups.end(),
                  [](const vector<User *> &community)
                  { return community.empty(); }),
        communityGroups.end());

    return communityGroups;
}

vector<User *> ConnectionManager::getAllUsers()
{
    return users; // Assuming 'users' is a vector that stores all User pointers
}

// vector<pair<User *, User *>> ConnectionManager::recommendConnectionsForNewUser(User *new_user)
// {
//     vector<pair<User *, User *>> recommendations;
//     unordered_map<string, int> community_map;
//     vector<vector<User *>> communities = detectCommunities();

//     for (int i = 0; i < communities.size(); ++i)
//     {
//         for (User *user : communities[i])
//         {
//             community_map[user->getID()] = i;
//         }
//     }

//     vector<pair<double, pair<User *, User *>>> sortedRecommendations;
//     for (User *user : users)
//     {
//         if (user != new_user && community_map[user->getID()] != community_map[new_user->getID()])
//         {
//             double score = 0.0;
//             if (user->getCategory() == new_user->getCategory())
//             {
//                 score += 0.5;
//             }
//             if (user->getBranch() == new_user->getBranch())
//             {
//                 score += 0.5;
//             }
//             sortedRecommendations.emplace_back(-score, make_pair(new_user, user));
//         }
//     }

//     sort(sortedRecommendations.begin(), sortedRecommendations.end());
//     for (const auto &pair : sortedRecommendations)
//     {
//         recommendations.emplace_back(pair.second);
//     }

//     return recommendations;
// }

vector<pair<User *, User *>> ConnectionManager::recommendConnectionsForNewUser(User *new_user)
{
    vector<pair<User *, User *>> recommendations;
    unordered_map<string, int> community_map;
    vector<vector<User *>> communities = detectCommunities();

    // Map users to their communities
    for (int i = 0; i < communities.size(); ++i)
    {
        for (User *user : communities[i])
        {
            community_map[user->getID()] = i;
        }
    }

    // Helper function to get user attributes as a set
    auto getUserAttributes = [](User *user) -> unordered_set<string>
    {
        unordered_set<string> attributes;

        // Add available user attributes
        attributes.insert(user->getCategory());
        attributes.insert(user->getBranch());
        attributes.insert(user->getName());

        // Convert influence to a discretized string representation
        // This helps include influence in similarity calculation
        int influence_level = static_cast<int>(user->getInfluence() * 10);
        attributes.insert("influence_" + to_string(influence_level));

        return attributes;
    };

    // Calculate Jaccard similarity between two sets
    auto calculateJaccardSimilarity = [](const unordered_set<string> &set1,
                                         const unordered_set<string> &set2) -> double
    {
        if (set1.empty() && set2.empty())
            return 0.0;

        size_t intersection_size = 0;
        for (const auto &elem : set1)
        {
            if (set2.find(elem) != set2.end())
            {
                intersection_size++;
            }
        }

        size_t union_size = set1.size() + set2.size() - intersection_size;
        return static_cast<double>(intersection_size) / union_size;
    };

    vector<pair<double, pair<User *, User *>>> sortedRecommendations;
    unordered_set<string> new_user_attributes = getUserAttributes(new_user);

    // Calculate similarity scores for all users in different communities
    for (User *user : users)
    {
        // Skip if same user or already connected
        if (user == new_user)
        {
            continue;
        }

        // Check existing connections using adjacency matrix
        double connection_weight = getConnectionWeight(new_user->getID(), user->getID());
        if (connection_weight != 0)
        {
            continue;
        }

        if (community_map[user->getID()] != community_map[new_user->getID()])
        {
            unordered_set<string> user_attributes = getUserAttributes(user);

            // Calculate Jaccard similarity
            double jaccard_score = calculateJaccardSimilarity(new_user_attributes, user_attributes);

            // Calculate influence similarity (0 to 1 scale)
            double influence_diff = abs(user->getInfluence() - new_user->getInfluence());
            double influence_similarity = 1.0 - min(influence_diff, 1.0);

            // Weighted combination of similarities
            double final_score = jaccard_score * 0.6 +       // Base attribute similarity
                                 influence_similarity * 0.2; // Influence similarity

            // Add bonus for exact matches on important attributes
            if (user->getCategory() == new_user->getCategory())
            {
                final_score += 0.1;
            }
            if (user->getBranch() == new_user->getBranch())
            {
                final_score += 0.1;
            }
            if (community_map[user->getID()] == community_map[new_user->getID()])
            {
                final_score += 0.3;
            }

            // Use negative score for sorting in descending order
            sortedRecommendations.emplace_back(-final_score, make_pair(new_user, user));
        }
    }

    // Sort recommendations by similarity score
    sort(sortedRecommendations.begin(), sortedRecommendations.end());

    // Extract final recommendations
    for (const auto &pair : sortedRecommendations)
    {
        recommendations.emplace_back(pair.second);
    }

    return recommendations;
}

void ConnectionManager::visualizeGraph(const string &output_file,
                                       const unordered_map<string, double> &betweenness,
                                       const vector<vector<User *>> &communities)
{
    GraphVisualizer visualizer(15.0);
    visualizer.createGraph("adjacency_matrix.csv", communities, output_file);
}

int ConnectionManager::getRandomPosition(int max)
{
}

double ConnectionManager::getLuminance(const string &color)
{
}

User *ConnectionManager::getUser(const string &id) const
{
    for (User *user : users)
    {
        if (user->getID() == id)
        {
            return user;
        }
    }
    return nullptr;
}

void ConnectionManager::loadUserData(const string &file_path)
{
    ifstream file(file_path);
    string line;
    getline(file, line); // Skip header row

    while (getline(file, line))
    {
        stringstream ss(line);
        string name, id, password, category, branch;
        double influence;
        getline(ss, name, ',');
        getline(ss, id, ',');
        getline(ss, password, ',');
        getline(ss, category, ',');
        ss >> influence;
        ss.ignore();
        getline(ss, branch, ',');

        users.push_back(new User(name, id, password, category, influence, branch));
    }
}

void ConnectionManager::establishConnections()
{
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<> dis(0.0, 1.0);

    vector<vector<User *>> communities = detectCommunities();

    // Only establish connections for the first InitialNum users
    for (int i = 0; i < min(InitialNum, (int)users.size()); i++)
    {
        User *user1 = users[i];
        for (int j = 0; j < min(InitialNum, (int)users.size()); j++)
        {
            User *user2 = users[j];
            if (user1 != user2)
            {
                double connection_probability =
                    (user1->getCategory() == user2->getCategory() &&
                     user1->getBranch() == user2->getBranch())
                        ? 0.5
                        : 0.2;

                if (dis(gen) < connection_probability)
                {
                    connections[user1->getID()].push_back(user2->getID());
                    connections[user2->getID()].push_back(user1->getID());

                    // Update the connection weight in the adjacency matrix
                    // adjacency_matrix->updateConnection(user1->getID(), user2->getID(), communities);
                }
            }
        }
    }
}

void ConnectionManager::saveUserData(const string &file_path)
{
    ofstream file(file_path);
    file << "name,id,password,category,influence,branch\n";
    for (User *user : users)
    {
        file << user->getName() << "," << user->getID() << "," << user->getPassword() << "," << user->getCategory() << "," << user->getInfluence() << "," << user->getBranch() << "\n";
    }
    file.close();
}