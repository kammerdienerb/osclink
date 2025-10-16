#pragma once

#include <string>
#include <vector>
#include <sstream>

#include <cereal/cereal.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/base_class.hpp>
#include <cereal/archives/binary.hpp>

#include "common.hpp"
#include "topo.hpp"

namespace {

struct Profile_Event {
    std::string   name;
    Resource_Type resource_type;

    template<class Archive>
    void serialize(Archive & archive) {
        archive(name, resource_type);
    }
};

struct Profile_Data_Source {
    std::string                name;
    std::vector<Profile_Event> events;

    template<class Archive>
    void serialize(Archive & archive) {
        archive(name, events);
    }
};

struct Profile_Config {
    std::vector<Profile_Data_Source> sources;

    template<class Archive>
    void serialize(Archive & archive) {
        archive(sources);
    }

    std::string to_serialized() {
        std::stringstream ss;

        {
            cereal::BinaryOutputArchive oarchive(ss);
            oarchive(*this);
        }

        return ss.str();
    }

    static Profile_Config from_serialized(std::string &data) {
        Profile_Config ret;

        std::stringstream ss(data);

        {
            cereal::BinaryInputArchive iarchive(ss);
            iarchive(ret);
        }

        return ret;
    }
};

struct Monitor_Data {

};

struct Profile_Data {

};

}
