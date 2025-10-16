#include <string>

#include "osclink.hpp"
#include "profile.hpp"
#include "topo.hpp"
#include "base64.hpp"
#include "hwloc.h"

static Profile_Config config;
static Topology       topo;

static void build_config();
static void build_topo();
static void send_config(OSCLink_Server &link);
static void send_topo(OSCLink_Server &link);
static void send_heatmap(OSCLink_Server &link);

int main(void) {
    if (!isatty(STDIN_FILENO)) {
        printf("input must be from a PTY\n");
        return 1;
    }

    build_config();
    build_topo();

    auto &link = OSCLink_Server::get();
    link.start();

    printf("Server started. Reaching out to client.\n");

    link.send("SERVER-CONNECT");

    while (true) {
        std::string message = link.pull_next();

        printf("%s\n", message.c_str());

        if      (message == "REQUEST/TOPOLOGY")     { send_topo(link);    }
        else if (message == "REQUEST/CONFIG")       { send_config(link);  }
        else if (message == "REQUEST/HEATMAP-DATA") { send_heatmap(link); }
    }

    return 0;
}

static void build_config() {

}

static void topo_from_hwloc(hwloc_obj_t obj, Topology_Node &parent) {
    char type[32];
    unsigned i;

    hwloc_obj_type_snprintf(type, sizeof(type), obj, 0);

    std::string node_name = "";

    node_name += type;

    if (obj->os_index != (unsigned) -1) {
        node_name += "#";
        node_name += std::to_string(obj->os_index);
    }

    auto &sub = parent.get_subnode(node_name);

    for (i = 0; i < obj->arity; i++) {
        topo_from_hwloc(obj->children[i], sub);
    }
}

static void build_topo() {
    hwloc_topology_t t;

    hwloc_topology_init(&t);
    hwloc_topology_load(t);

    hwloc_obj_t root = hwloc_get_root_obj(t);

    topo_from_hwloc(root, topo);

    hwloc_topology_destroy(t);
}

static void send_config(OSCLink_Server &link) {
    std::string message = "CONFIG;" + config.to_serialized();
    link.send(std::move(message));
}

static void send_topo(OSCLink_Server &link) {
    std::string message = "TOPOLOGY;" + topo.to_serialized();
    link.send(std::move(message));
}

static void send_heatmap(OSCLink_Server &link) {
    std::string out = "HEATMAP-DATA";
    for (int i = 0; i < 500; i += 1) {
        out += ";";
        int n = random() % 100000;
        out += std::to_string(n);
    }
    link.send(std::move(out));
}
