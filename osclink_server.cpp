#include <string>

#include "osclink.hpp"
#include "base64.hpp"
#include "hwloc.h"

void send_topo(OSCLink_Server &link);
void send_heatmap(OSCLink_Server &link);

int main(void) {
    if (!isatty(STDIN_FILENO)) {
        printf("input must be from a PTY\n");
        return 1;
    }

    auto &link = OSCLink_Server::get();
    link.start();

    printf("Server started. Reaching out to client.\n");

    link.send("SERVER-CONNECT");

    while (true) {
        std::string message = link.pull_next();

        printf("%s\n", message.c_str());

        if      (message == "REQUEST/TOPOLOGY")     { send_topo(link);    }
        else if (message == "REQUEST/HEATMAP-DATA") { send_heatmap(link); }
    }

    return 0;
}


void print_child(hwloc_obj_t obj, int depth) {
    char type[32], attr[1024];
    unsigned i;

    hwloc_obj_type_snprintf(type, sizeof(type), obj, 0);

    printf("%*s%s", 2*depth, "", type);

    if (obj->os_index != (unsigned) -1) {
        printf("#%u", obj->os_index);
    }

    hwloc_obj_attr_snprintf(attr, sizeof(attr), obj, " ", 0);

    if (*attr) {
        printf("(%s)", attr);
    }

    printf("\n");

    for (i = 0; i < obj->arity; i++) {
        print_child(obj->children[i], depth + 1);
    }
}

void send_topo(OSCLink_Server &link) {
    hwloc_topology_t t;

    hwloc_topology_init(&t);
    hwloc_topology_load(t);

    hwloc_obj_t root = hwloc_get_root_obj(t);

    print_child(root, 0);

    link.send("TOPOLOGY");

    hwloc_topology_destroy(t);
}

void send_heatmap(OSCLink_Server &link) {
    std::string out = "HEATMAP-DATA";
    for (int i = 0; i < 500; i += 1) {
        out += "\n";
        int n = random() % 100000;
        out += std::to_string(n);
    }
    link.send(std::move(out));
}
