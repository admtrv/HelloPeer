/*
 * main.cpp
 */

#include "entities/node.h"
#include "tools/cli.h"

int main()
{
    Node _node;

    CLI _cli(&_node);
    _cli.run();

    return 0;
}
