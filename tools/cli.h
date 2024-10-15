/*
 * cli.h
 */

#pragma once

#include <iostream>
#include <string>

#include "../entities/node.h"

#define CLI_HISTORY_FILE_NAME ".cli_history"

class CLI {
public:
    explicit CLI(Node* node);
    ~CLI();

    void run();

private:
    Node* _node;
    static void display_help();
    static void display_header();
};

