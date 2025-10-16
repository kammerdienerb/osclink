#pragma once

#include <map>
#include <string>
#include <sstream>

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/archives/binary.hpp>

#include "common.hpp"

namespace {

enum Resource_Type {
    CPU,
    GPU,
};

struct Topology_Edge {
    std::vector<std::string> endpoints;

    template<class Archive>
    void serialize(Archive & archive) {
        archive(endpoints);
    }
};


struct Topology_Node {
    Topology_Node()                   : name("<unknown>") { }
    Topology_Node(std::string &&name) : name(name)        { }

    Topology_Node &get_subnode(std::string name) {
        Topology_Node &ref = this->subnodes[name];
        ref.name = name;
        return ref;
    }

    std::string                          name;
    std::map<std::string, Topology_Node> subnodes;
    std::map<std::string, Topology_Edge> edges;

    template<class Archive>
    void serialize(Archive & archive) {
        archive(name, subnodes, edges);
    }
};


struct Topology : Topology_Node {
    Topology() : Topology_Node("System") { }

    template<class Archive>
    void serialize(Archive & archive) {
        archive(cereal::base_class<Topology_Node>(this));
    }

    std::string to_serialized() {
        std::stringstream ss;

        {
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(*this);
        }

        return ss.str();
    }

    static Topology from_serialized(std::string &data) {
        Topology ret;

        std::stringstream ss(data);

        {
            cereal::BinaryInputArchive iarchive(ss);
            iarchive(ret);
        }

        return ret;
    }
};

}
