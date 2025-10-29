#include <string>
#include <cstdarg>
#include <alloca.h>

#include "ssh_link.hpp"
#include "profile.hpp"
#include "topo.hpp"
#include "base64.hpp"
#include "hwloc.h"
#include "subprocess.hpp"
#include "json.hpp"

using json = nlohmann::json;

static SSH_Link_Server *ssh_link;
static Profile_Config   config;
static Topology         topo;

static void report_warning(const char *fmt, ...);
static void build_config();
static void build_topo();
static void send_config();
static void send_topo();
static void send_heatmap();

int main(void) {
    build_config();
    build_topo();

    ssh_link = &SSH_Link_Server::get();
    ssh_link->start();

    printf("Server started. Reaching out to client.\n");

    ssh_link->send("SERVER-CONNECT");

    while (auto m = ssh_link->pull_next()) {
        std::string &message = *m;

        printf("%s\n", message.c_str());

        if      (message == "REQUEST/TOPOLOGY")     { send_topo();    }
        else if (message == "REQUEST/CONFIG")       { send_config();  }
        else if (message == "REQUEST/HEATMAP-DATA") { send_heatmap(); }
    }

    return 0;
}

static void report_warning(const char *fmt, ...) {
    va_list va;

    va_start(va, fmt);
    int size = vsnprintf(NULL, 0, fmt, va);
    va_end(va);

    char *buff = (char*)alloca(size + 1);

    va_start(va, fmt);
    vsnprintf(buff, size + 1, fmt, va);
    va_end(va);

    printf("WARNING: %s\n", buff);

    std::string message = "SERVER-WARNING;";
    message += buff;

    ssh_link->send(std::move(message));
}

static void build_config() {
    auto &perf = config.source("perf");

    Subprocess perf_list({ "perf", "list", "-j" }, 1s);

    if (perf_list.error() != Subprocess::Error::NONE) {
        report_warning("failed to run 'perf list'");
        goto out;
    }

    perf_list.join();

    if (auto output = perf_list.output()) {
        json events;
        try {
            events = json::parse(*output);
            for (auto &event : events) {
                perf.add_event(event["EventName"]);
            }
        } catch (...) {
            report_warning("failed to parse 'perf list' output");
        }
    } else if (perf_list.exit_status() != 0) {
        report_warning("'perf list' exited with non-zero status %d", perf_list.exit_status());
    } else {
        report_warning("error when running 'perf list'");
    }

out:;
}

static void topo_from_hwloc(hwloc_obj_t obj, Topology_Node *parent) {
    char type[32];
    unsigned i;

    hwloc_obj_type_snprintf(type, sizeof(type), obj, 0);

    std::string node_name = "";

    node_name += type;

    if (obj->os_index != (unsigned) -1) {
        node_name += "#";
        node_name += std::to_string(obj->os_index);
    }

    /* Create a new node under the current parent */
    Topology_Node &sub = parent->get_subnode(node_name);

    /* Decide if we should go down in depth or not */
    Topology_Node *new_parent = &sub;
    if ((obj->arity == 1) && !obj->memory_arity && !obj->io_arity && !obj->misc_arity) {
        new_parent = parent;
    }

    /* Recurse into children */
    for (i = 0; i < obj->arity; i++) {
        topo_from_hwloc(obj->children[i], new_parent);
    }
}

static void build_topo() {
    hwloc_topology_t t;

    hwloc_topology_init(&t);
    hwloc_topology_load(t);

    hwloc_obj_t root = hwloc_get_root_obj(t);

    topo_from_hwloc(root, &topo);

    hwloc_topology_destroy(t);
}

static void send_config() {
    std::string message = "CONFIG;" + config.to_serialized();
    ssh_link->send(std::move(message));
}

static void send_topo() {
    std::string message = "TOPOLOGY;" + topo.to_serialized();
    ssh_link->send(std::move(message));
}

static void send_heatmap() {
    std::string out = "HEATMAP-DATA";
    for (int i = 0; i < 500; i += 1) {
        out += ";";
        int n = random() % 100000;
        out += std::to_string(n);
    }
    ssh_link->send(std::move(out));
}
