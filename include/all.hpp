#ifndef ALL_HPP
#define ALL_HPP
#include "common.hpp"     // 1. Macros and Forward Decs
#include "utils.hpp"      // 2. Logic helpers (stringmatchlen)
#include "Client.hpp"     // 3. Base Data Objects
#include "Storage.hpp"
#include "AofManager.hpp"
#include "SlabSlotManager.hpp"
#include "ServerContext.hpp" // 4. Context (depends on Aof/Slab)
#include "CommandExecutor.hpp" // 5. Executor (depends on Context)
#include "ProtocolHandler.hpp"
#include <iostream>
#include <cstdlib>

#endif