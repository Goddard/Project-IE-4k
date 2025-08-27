#include "ServiceBase.h"
#include "ServiceManager.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace ProjectIE4k {

bool ServiceBase::registerServiceFactory(const std::string& serviceName, 
                                        std::function<std::unique_ptr<ServiceBase>()> factory) {
    return ServiceManager::registerService(serviceName, factory);
}

} // namespace ProjectIE4k 