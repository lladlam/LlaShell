#include "CoreIPC/pipe_channel.h"

// Client-side connection logic is in PipeChannel::ConnectToServer()
// which is defined in pipe_server.cpp alongside the server implementation.
// This file exists to separate logical concerns in the build system.

// Future: if we need to add a dedicated reconnection manager or
// connection pool for outbound connections, it goes here.
