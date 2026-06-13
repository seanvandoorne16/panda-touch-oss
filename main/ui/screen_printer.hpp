#pragma once
#include <memory>
#include "bambu_client.hpp"

namespace ScreenPrinter {
    void show(std::shared_ptr<BambuClient> client);
    void destroy();
    void refresh(const PrinterState& state);
}
