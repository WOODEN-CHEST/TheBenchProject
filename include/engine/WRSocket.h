#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include "WRIO.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


// Types.
typedef struct SocketAddressStruct SocketAddress;
typedef struct TcpSocketStruct TcpSocket;
typedef struct TcpListenerStruct TcpListener;
typedef struct UdpSocketStruct UdpSocket;

typedef enum SocketFamilyEnum
{
    SocketFamily_IPv4,
    SocketFamily_IPv6,
} SocketFamily;

Error SocketAddress_FromString(const unsigned char* ip, uint16_t port, SocketAddress** outAddress);

Error SocketAddress_FromHostname(const unsigned char* hostname, uint16_t port, SocketAddress** outAddress);

Error SocketAddress_GetIP(const SocketAddress* self, GenericBuffer* outIP);

uint16_t SocketAddress_GetPort(const SocketAddress* self);

SocketFamily SocketAddress_GetFamily(const SocketAddress* self);

void SocketAddress_Deconstruct(SocketAddress* self);



Error TcpListener_Create(uint16_t port, SocketFamily family, TcpListener** outListener);

Error TcpListener_Accept(TcpListener* self, TcpSocket** outSocket);

Error TcpListener_GetLocalAddress(const TcpListener* self, SocketAddress** outAddress);

Error TcpListener_Deconstruct(TcpListener* self);



Error TcpSocket_Connect(const SocketAddress* address, TcpSocket** outSocket);

Error TcpSocket_GetIOStream(TcpSocket* self, IOStream** outStream);

Error TcpSocket_GetLocalAddress(const TcpSocket* self, SocketAddress** outAddress);

Error TcpSocket_GetRemoteAddress(const TcpSocket* self, SocketAddress** outAddress);

Error TcpSocket_SetReceiveTimeout(TcpSocket* self, size_t milliseconds);

Error TcpSocket_SetSendTimeout(TcpSocket* self, size_t milliseconds);

Error TcpSocket_Shutdown(TcpSocket* self);

Error TcpSocket_Deconstruct(TcpSocket* self);



Error UdpSocket_Create(SocketFamily family, UdpSocket** outSocket);

Error UdpSocket_Bind(UdpSocket* self, uint16_t port);

Error UdpSocket_Send(UdpSocket* self, const SocketAddress* destination, const void* data, size_t size);

Error UdpSocket_Receive(UdpSocket* self, GenericBuffer* buffer, size_t* outBytesRead, SocketAddress** outSender);

Error UdpSocket_SetReceiveTimeout(UdpSocket* self, size_t milliseconds);

Error UdpSocket_Deconstruct(UdpSocket* self);
