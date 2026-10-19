#include "connection_handler.h"
#include "print_utilities.h"
#include <bits/stdc++.h>

using namespace std;

void ConnectionHandler::handleConnectionManagement(ConnectionManager &cm, User *user)
{
    while (true)
    {
        int choice = getMenuChoice();

        switch (choice)
        {
        case 1:
            viewConnections(cm, user);
            break;
        case 2:
            addNewConnections(cm, user);
            break;
        case 3:
            removeConnection(cm, user);
            break;
        case 4:
            return;
        default:
            cout << "Invalid option. Please try again." << endl;
        }
    }
}

int ConnectionHandler::getMenuChoice()
{
    cout << "\nConnection Management Menu:" << endl;
    cout << "1. View your connections" << endl;
    cout << "2. Add new connection" << endl;
    cout << "3. Remove connection" << endl;
    cout << "4. Exit" << endl;
    cout << "Choose an option: ";

    int choice;
    cin >> choice;
    return choice;
}

void ConnectionHandler::viewConnections(ConnectionManager &cm, User *user)
{
    cm.viewConnections(user);
}

void ConnectionHandler::addNewConnections(ConnectionManager &cm, User *user)
{
    vector<pair<User *, User *>> recommendations = cm.recommendConnectionsForNewUser(user);
    PrintUtilities::printRecommendedConnections(recommendations, user);

    int selected_connection = PrintUtilities::selectConnection(recommendations);
    if (selected_connection >= 0)
    {
        User *selected_user = recommendations[selected_connection].second;
        cm.addConnection(user, selected_user);
        cout << "Successfully connected with " << selected_user->getName() << "!" << endl;
    }

    // The following commented-out lines could be uncommented for additional behavior:
    // cout << "Add another connection? (y/n): ";
    // char more_choice;
    // cin >> more_choice;
    // if (more_choice != 'y' && more_choice != 'Y')
}

void ConnectionHandler::removeConnection(ConnectionManager &cm, User *user)
{
    cout << "\nYour current connections:" << endl;
    cm.viewConnections(user);

    cout << "\nEnter the ID of the connection you want to remove: ";
    string remove_id;
    cin >> remove_id;

    User *connection_to_remove = cm.getUser(remove_id);
    if (connection_to_remove)
    {
        cm.removeConnection(user, connection_to_remove);
        cout << "Successfully removed connection with " << connection_to_remove->getName() << endl;
    }
    else
    {
        cout << "Invalid user ID or not in your connections." << endl;
    }
}
