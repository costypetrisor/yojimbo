/*
    Yojimbo Client/Server Network Library.

    Copyright © 2016, The Network Protocol Company, Inc.
    
    All rights reserved.
*/

#ifndef YOJIMBO_H
#define YOJIMBO_H

#include "yojimbo_config.h"
#include "yojimbo_common.h"
#include "yojimbo_types.h"
#include "yojimbo_memory.h"
#include "yojimbo_packet.h"
#include "yojimbo_network.h"
#include "yojimbo_platform.h"
#include "yojimbo_allocator.h"
#include "yojimbo_encryption.h"
#include "yojimbo_packet_processor.h"
#include "yojimbo_network_interface.h"
#include "yojimbo_socket_interface.h"
#include "yojimbo_simulator_interface.h"
#include "yojimbo_client_server.h"

bool InitializeYojimbo();

void ShutdownYojimbo();

#endif // #ifndef YOJIMBO_H
