// connection_handler.h
#ifndef CONNECTION_HANDLER_H
#define CONNECTION_HANDLER_H

#include "connection_manager.h"
#include <bits/stdc++.h>

class ConnectionHandler
{
public:
    static void handleConnectionManagement(ConnectionManager &cm, User *user);

private:
    static int getMenuChoice();
    static void viewConnections(ConnectionManager &cm, User *user);
    static void addNewConnections(ConnectionManager &cm, User *user);
    static void removeConnection(ConnectionManager &cm, User *user);
};

#endif