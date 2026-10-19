#include "print_utilities.h"
#include <bits/stdc++.h>

using namespace std;

vector<int> PrintUtilities::g_validIndices;
int PrintUtilities::g_currentStart = 0;

// Function to print top influencers
void PrintUtilities::printResults(ConnectionManager &cm,
                                  const unordered_map<string, double> &pagerank,
                                  const vector<vector<User *>> &communities)
{
    cout << "Top 5 Influencers:" << endl;
    vector<pair<string, double>> top_influencers;

    // Collecting pagerank data into a vector
    for (const auto &pair : pagerank)
    {
        top_influencers.emplace_back(pair.first, pair.second);
    }

    // Sorting the influencers by rank
    sort(top_influencers.begin(), top_influencers.end(),
         [](const auto &a, const auto &b) { return a.second > b.second; });

    // Displaying the top influencers
    for (size_t i = 0; i < 5 && i < top_influencers.size(); ++i)
    {
        cout << "User: " << cm.getUser(top_influencers[i].first)->getName()
             << ", Influence: " << top_influencers[i].second << endl;
    }

    cout << "\nCommunities:" << endl;

    // Displaying the communities
    for (size_t i = 0; i < communities.size(); ++i)
    {
        cout << "Community " << i + 1 << ": ";
        for (User *user : communities[i])
        {
            cout << user->getName() << ", ";
        }
        cout << endl;
    }
}

// Function to print recommended communities
void PrintUtilities::printRecommendedCommunities(const vector<vector<User *>> &communities,
                                                 User *new_user)
{
    int matchingCommunityCount = 0;
    for (const auto &community : communities)
    {
        if (matchingCommunityCount >= 3)
            break;

        bool is_match = false;
        for (User *user : community)
        {
            if (user->getCategory() == new_user->getCategory() &&
                user->getBranch() == new_user->getBranch())
            {
                is_match = true;
                break;
            }
        }

        if (is_match)
        {
            matchingCommunityCount++;
            cout << "Community " << matchingCommunityCount << ": ";
            for (User *user : community)
            {
                cout << user->getName() << ", ";
            }
            cout << endl;
        }
    }
}

// Function to print recommended connections
void PrintUtilities::printRecommendedConnections(const vector<pair<User *, User *>> &recommendations,
                                                 User *new_user)
{
    cout << "\nRecommended connections based on category and influence:" << endl;
    int validIndex = 0;
    g_validIndices.clear();

    int skipped = 0;
    for (int i = 0; i < recommendations.size() && validIndex < 5; i++)
    {
        const auto &pair = recommendations[i];

        if ((pair.first->getCategory() == new_user->getCategory() &&
             pair.first->getBranch() == new_user->getBranch()) ||
            (pair.second->getCategory() == new_user->getCategory() &&
             pair.second->getBranch() == new_user->getBranch()))
        {

            if (skipped < g_currentStart)
            {
                skipped++;
                continue;
            }

            cout << validIndex << ": " << pair.first->getName() << " <-> "
                 << pair.second->getName() << " (Category: " << pair.second->getCategory()
                 << ", Branch: " << pair.second->getBranch() << ")" << endl;

            g_validIndices.push_back(i);
            validIndex++;
        }
    }

    if (validIndex == 0)
    {
        if (g_currentStart == 0)
        {
            cout << "No matching recommendations found." << endl;
        }
        else
        {
            cout << "No more recommendations available." << endl;
            g_currentStart = 0;
        }
    }
}

// Function to select a connection from recommendations
int PrintUtilities::selectConnection(const vector<pair<User *, User *>> &recommendations)
{
    if (g_validIndices.empty())
    {
        return -1;
    }

    cout << "\nSelect a connection to add (enter number 0-" << g_validIndices.size() - 1
         << ", or -1 to skip, or any other negative number to exit): ";
    int displayIndex;
    cin >> displayIndex;

    if (displayIndex == -1)
    {
        g_currentStart += 5;
        printRecommendedConnections(recommendations, recommendations[0].first);
        return selectConnection(recommendations);
    }
    else if (displayIndex < -1)
    {
        g_currentStart = 0;
        return -1;
    }
    else if (displayIndex >= 0 && displayIndex < g_validIndices.size())
    {
        g_currentStart = 0;
        return g_validIndices[displayIndex];
    }

    cout << "Invalid selection. Please try again." << endl;
    return selectConnection(recommendations);
}
