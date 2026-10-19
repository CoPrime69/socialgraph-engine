#ifndef USER_H
#define USER_H

#include <string>

using namespace std;

class User
{
public:
    // Constructor to initialize user attributes
    User(const string &name, const string &id, const string &password,
         const string &category, double influence, const string &branch)
         : name(name), id(id), password(password), category(category),
           influence(influence), branch(branch) {}

    // Getter methods for user attributes
    string getName() const { return name; }
    string getID() const { return id; }
    string getPassword() const { return password; }
    string getCategory() const { return category; }
    double getInfluence() const { return influence; }
    string getBranch() const { return branch; }

private:
    // User attributes
    string name;
    string id;
    string password;
    string category;
    double influence;
    string branch;
};

#endif // USER_H
