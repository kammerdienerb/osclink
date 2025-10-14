#pragma once

#include <map>

#include "common.hpp"
#include "serio/serio.h"

namespace {

struct Topology_Edge {
private:
    std::vector<std::string> endpoints;

public:
    SERIO_REGISTER(endpoints);
};


struct Topology_Node {
    Topology_Node() : name("<unknown>") { }
    Topology_Node(std::string &&name) : name(name) { }

    Topology_Node &get_subnode(std::string name) {
        return this->subnodes[name];
    }

private:
    std::string                          name;
    std::map<std::string, Topology_Node> subnodes;
    std::map<std::string, Topology_Edge> edges;

public:
    SERIO_REGISTER(name, subnodes, edges);
};


struct Topology : Topology_Node {
    SERIO_REGISTER();
};

}
