/*
 * main.cpp
 */

#include "entities/node.h"
#include "tools/cli.h"
#include "tools/logger.h"

int main()
{
    auto logger = Logger::get_instance();
    Logger::get_instance()->clear_logs();

    Node _node;

    CLI _cli(&_node);
    _cli.run();

    return 0;
}
