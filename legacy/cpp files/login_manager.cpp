#include "login_manager.h"
#include <bits/stdc++.h>

using namespace std;

LoginManager::LoginManager(ConnectionManager &cm) : cm(cm)
{
    loadUsers();
}

void LoginManager::loadUsers()
{
    vector<User *> all_users = cm.getAllUsers();
    for (auto user : all_users)
    {
        users[user->getID()] = user;
    }
}

void LoginManager::saveUsers()
{
    ofstream file("user_data.csv", ios::app); // Open in append mode
    if (!file.is_open())
    {
        cout << "Error opening file for saving user data!" << endl;
        return;
    }

    // Find and append only new users (if not already saved in the file)
    for (const auto &user_pair : users)
    {
        User *user = user_pair.second;

        // Check if the user already exists in the CSV file
        if (!isUserExist(user->getID()))
        { // Adjust if necessary to prevent duplicate entries
            file << user->getName() << ","
                 << user->getID() << ","
                 << user->getPassword() << ","
                 << user->getCategory() << ","
                 << user->getInfluence() << ","
                 << user->getBranch() << "\n";
        }
    }

    file.close();
}

User *LoginManager::login(const string &id, const string &password)
{
    if (users.find(id) != users.end())
    {
        User *user = users[id];
        if (user->getPassword() == password)
        {
            cout << "Login successful!" << endl;
            return user;
        }
        else
        {
            cout << "Incorrect password!" << endl;
        }
    }
    else
    {
        cout << "No user found with that ID!" << endl;
    }
    return nullptr;
}

bool LoginManager::registerUser(const string &name, const string &id, const string &password, const string &category, double influence, const string &branch)
{
    if (users.find(id) != users.end())
    {
        cout << "User ID already exists!" << endl;
        return false;
    }

    // Create the new user
    User *new_user = new User(name, id, password, category, influence, branch);

    // Add the user to the system
    cm.addUser(new_user);
    users[id] = new_user;

    // Append only the new user data to the file
    ofstream file("user_data.csv", ios::app);
    if (file.is_open())
    {
        file << new_user->getName() << ","
             << new_user->getID() << ","
             << new_user->getPassword() << ","
             << new_user->getCategory() << ","
             << new_user->getInfluence() << ","
             << new_user->getBranch() << "\n";
        file.close();
        cout << "User registered successfully and saved to file!" << endl;
    }
    else
    {
        cout << "Error opening file to save the new user data!" << endl;
    }

    return true;
}

bool LoginManager::isUserExist(const string &id)
{
    return users.find(id) != users.end();
}

User *LoginManager::getUserById(const string &id)
{
    return users.find(id) != users.end() ? users[id] : nullptr;
}
