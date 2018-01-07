#include <stdlib.h>
#include <iostream>

#include "errors.h"

void params_invalid() {
    std::cout << "Usage: altminix snapshot (create|remove|rollback|list) volume_path [snapshot_name]" << std::endl;
    exit(EXIT_FAILURE);
}

void source_volume_invalid() {
    std::cout << "The source volume is invalid" << std::endl;
    exit(EXIT_FAILURE);
}

void snapshot_name_invalid() {
    std::cout << "The snapshot name is invalid" << std::endl;
    exit(EXIT_FAILURE);
}