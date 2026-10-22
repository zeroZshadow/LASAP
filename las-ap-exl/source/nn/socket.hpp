// Declarations for the SDK's BSD socket wrappers.
// From randovania/open-dread-rando-exlaunch (GPL-2.0).
#pragma once

#include <nn/nn_common.hpp>
#include <sys/socket.h>
#include <poll.h>

namespace nn::socket {

    struct InAddr {
        u32 addr;
    };

    Result Initialize(void* pool, ulong poolSize, ulong allocPoolSize, int concurLimit);
    Result Finalize();

    s32 SetSockOpt(s32 socket, s32 socketLevel, s32 option, void const*, u32 len);
    ssize_t Send(s32 socket, void const* buffer, u64 bufferLength, s32 flags);
    ssize_t Recv(s32 desc, void *buffer, size_t bufferLength, s32 flags);
    s32 Close(s32 desc);

    s32 Socket(s32 domain, s32 type, s32 proto);
    u16 InetHtons(u16);
    u32 InetAton(const char* str, InAddr*);
    u32 Connect(s32 socket, const sockaddr* addr, u32 addrLen);
    s32 Bind(s32 socket, const sockaddr* addr, u32 addrLen);
    s32 Listen(s32 socket, s32 backlog);
    s32 Accept(s32 socket, sockaddr* addrOut, u32* addrLenOut);
    s32 Shutdown(s32 desc, s32 how);
    s32 Poll(pollfd* fds, u64 count, s32 timeoutMs);
    s32 Fcntl(s32 desc, s32 cmd, ...);
};
