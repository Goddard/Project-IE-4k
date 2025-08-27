#pragma once

#include <memory>
#include "core/OperationsMonitor/GpuProvider.h"

namespace ProjectIE4k {

class GpuProviderFactory {
public:
    static std::unique_ptr<GpuProvider> create();
};

} // namespace ProjectIE4k


