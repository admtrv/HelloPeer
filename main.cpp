/*
 * main.cpp
 */

#include "entities/node.h"
#include "tools/cli.h"

int main()
{
    Node node;

    CLI cli(&node);
    cli.run();

    return 0;
}
