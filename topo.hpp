#pragma once

#include <map>

#include "common.hpp"

namespace {

struct Topology_Edge {
private:
    std::vector<std::string> endpoints;
};


struct Topology_Node {

private:
    std::string                          name;
    std::map<std::string, Topology_Node> subnodes;
    std::map<std::string, Topology_Edge> edges;
};


struct Topology : Topology_Node {

};

}
