#pragma once
#include <memory>
#include "bambu_client.hpp"

// Fixes issue #212: AMS filament remaining % per spool
namespace ScreenAms {
    void show(std::shared_ptr<BambuClient> client);
    void refresh(const PrinterState& state);
    void destroy();
}
