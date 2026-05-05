#define _POSIX_C_SOURCE 200112L
#include "WRSocket.h"
#include <limits.h>
#include <stdio.h>
#include <stdatomic.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

typedef SOCKET NativeSocket;
typedef int NativeSocketLength;
typedef int NativeTransferLength;
typedef int NativeTransferResult;

#define NATIVE_SOCKET_INVALID INVALID_SOCKET

#elif defined(__linux__)

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef int NativeSocket;
typedef socklen_t NativeSocketLength;
typedef size_t NativeTransferLength;
typedef ssize_t NativeTransferResult;

#define NATIVE_SOCKET_INVALID (-1)

#else

#error Unsupported platform for WRSocket.

#endif


// Macros.
#define SOCKET_PORT_TEXT_CAPACITY 6
#define SOCKET_ADDRESS_TEXT_CAPACITY INET6_ADDRSTRLEN


// Types.
typedef struct SocketAddressStruct
{
    SocketFamily _family;
    struct sockaddr_storage _storage;
    NativeSocketLength _length;
} SocketAddress;

typedef struct TcpSocketStreamStruct TcpSocketStream;

typedef struct TcpSocketStruct
{
    NativeSocket _handle;
    bool _hasSocket;
    bool _isShutDown;
    TcpSocketStream* _stream;
} TcpSocket;

struct TcpSocketStreamStruct
{
    IOStream Base;
    TcpSocket* _owner;
    bool _isClosed;
    bool _isEOF;
};

typedef struct TcpListenerStruct
{
    NativeSocket _handle;
    bool _hasSocket;
    SocketFamily _family;
} TcpListener;

typedef struct UdpSocketStruct
{
    NativeSocket _handle;
    bool _hasSocket;
    SocketFamily _family;
} UdpSocket;


// Fields.
static const IOStreamVTable SOCKET_STREAM_VTABLE =
{
    .Self = NULL,
    ._getPosition = NULL,
    ._setPosition = NULL,
    ._setPositionSpecial = NULL,
    ._setLength = NULL,
    ._flush = NULL,
    ._writeByte = NULL,
    ._write = NULL,
    ._readByte = NULL,
    ._read = NULL,
    ._close = NULL,
    ._isEOF = NULL,
    ._deconstruct = NULL,
};

#if defined(_WIN32)
static atomic_flag SocketLibraryLock = ATOMIC_FLAG_INIT;
static size_t SocketLibraryReferenceCount = 0;
static bool SocketLibraryStarted = false;
#endif


// Static functions.
#if defined(_WIN32)
static void SocketLibrary_Lock(void)
{
    while (atomic_flag_test_and_set(&SocketLibraryLock))
    {
    }
}

static void SocketLibrary_Unlock(void)
{
    atomic_flag_clear(&SocketLibraryLock);
}
#endif

static int SocketPlatform_GetLastErrorCode(void)
{
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

static Error CreateNullArgumentError(const unsigned char* argumentName)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Socket argument \"%s\" must not be null.",
        argumentName);
}

static Error CreateUnsupportedFamilyError(SocketFamily family)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Unsupported socket family value %d.",
        (int)family);
}

static Error CreateNativeSocketError(ErrorCode code, const unsigned char* operationName, int nativeError)
{
    return Error_Construct3(code,
        u8"Failed to %s. Native socket error code %d.",
        operationName,
        nativeError);
}

static Error CreateAddressInfoError(const unsigned char* operationName, int nativeError)
{
    return Error_Construct3(ErrorCode_IO,
        u8"Failed to %s. Address resolution error code %d.",
        operationName,
        nativeError);
}

static Error CreateByteBufferRequiredError(const unsigned char* argumentName, size_t elementSize)
{
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"Socket argument \"%s\" must be a byte buffer, got element size %zu.",
        argumentName,
        elementSize);
}

static Error CreateTimeoutRangeError(size_t milliseconds)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Socket timeout value %zu milliseconds exceeds the supported range.",
        milliseconds);
}

#if defined(_WIN32)
static Error CreateSizeRangeError(const unsigned char* operationName, size_t size)
{
    return Error_Construct3(ErrorCode_ArgumentOutOfRange,
        u8"Cannot %s %zu bytes because the native socket API does not support that size in a single call.",
        operationName,
        size);
}
#endif

static Error CreateSocketClosedError(const unsigned char* socketName, const unsigned char* operationName)
{
    return Error_Construct3(ErrorCode_InvalidOperation,
        u8"Cannot %s a closed %s.",
        operationName,
        socketName);
}

static Error CreateSocketEOFError(void)
{
    return Error_Construct1(ErrorCode_IO, u8"Cannot read past the end of the socket stream.");
}

static Error SocketLibrary_Acquire(void)
{
#if defined(_WIN32)
    WSADATA Data;
    int Result = 0;

    SocketLibrary_Lock();
    if (!SocketLibraryStarted)
    {
        Result = WSAStartup(MAKEWORD(2, 2), &Data);
        if (Result != 0)
        {
            SocketLibrary_Unlock();
            return CreateNativeSocketError(ErrorCode_Initialization, u8"initialize the Windows socket library", Result);
        }

        SocketLibraryStarted = true;
    }

    SocketLibraryReferenceCount++;
    SocketLibrary_Unlock();
#endif
    return Error_CreateSuccess();
}

static void SocketLibrary_Release(void)
{
#if defined(_WIN32)
    SocketLibrary_Lock();
    if (SocketLibraryReferenceCount > 0)
    {
        SocketLibraryReferenceCount--;
        if ((SocketLibraryReferenceCount == 0) && SocketLibraryStarted)
        {
            (void)WSACleanup();
            SocketLibraryStarted = false;
        }
    }
    SocketLibrary_Unlock();
#endif
}

static int SocketFamily_ToNative(SocketFamily family)
{
    switch (family)
    {
        case SocketFamily_IPv4:
            return AF_INET;

        case SocketFamily_IPv6:
            return AF_INET6;

        default:
            return -1;
    }
}

static Error SocketFamily_FromNative(int nativeFamily, SocketFamily* outFamily)
{
    if (outFamily == NULL)
    {
        return CreateNullArgumentError(u8"outFamily");
    }

    switch (nativeFamily)
    {
        case AF_INET:
            *outFamily = SocketFamily_IPv4;
            return Error_CreateSuccess();

        case AF_INET6:
            *outFamily = SocketFamily_IPv6;
            return Error_CreateSuccess();

        default:
            return Error_Construct3(ErrorCode_IO,
                u8"Unsupported native socket family %d.",
                nativeFamily);
    }
}

static Error SocketPlatform_CloseHandle(NativeSocket handle)
{
#if defined(_WIN32)
    if (closesocket(handle) == SOCKET_ERROR)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"close the socket", SocketPlatform_GetLastErrorCode());
    }
#else
    if (close(handle) != 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"close the socket", SocketPlatform_GetLastErrorCode());
    }
#endif

    return Error_CreateSuccess();
}

static bool SocketAddress_IsByteBuffer(GenericBuffer* buffer)
{
    return (buffer != NULL) && (buffer->_elementSize == sizeof(unsigned char));
}

static Error ConvertPortToText(uint16_t port, char* buffer, size_t bufferCapacity)
{
    int WrittenCount = 0;

    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }

    WrittenCount = snprintf(buffer, bufferCapacity, "%u", (unsigned int)port);
    if ((WrittenCount < 0) || ((size_t)WrittenCount >= bufferCapacity))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Cannot format socket port %u into the provided buffer.",
            (unsigned int)port);
    }

    return Error_CreateSuccess();
}

static Error SocketAddress_CreateFromNative(const struct sockaddr* nativeAddress,
    NativeSocketLength nativeLength,
    SocketAddress** outAddress)
{
    SocketAddress* Result = NULL;
    Error FamilyResult = Error_CreateSuccess();

    if (nativeAddress == NULL)
    {
        return CreateNullArgumentError(u8"nativeAddress");
    }
    if (outAddress == NULL)
    {
        return CreateNullArgumentError(u8"outAddress");
    }

    *outAddress = NULL;
    Result = Memory_Allocate(sizeof(SocketAddress));
    if (Result == NULL)
    {
        return Error_Construct1(ErrorCode_Construct, u8"Failed to allocate a socket address.");
    }

    Memory_Zero(Result, sizeof(SocketAddress));
    FamilyResult = SocketFamily_FromNative(nativeAddress->sa_family, &Result->_family);
    if (FamilyResult.Code != ErrorCode_Success)
    {
        Memory_Free(Result);
        return FamilyResult;
    }
    if ((size_t)nativeLength > sizeof(Result->_storage))
    {
        Memory_Free(Result);
        return Error_Construct3(ErrorCode_BufferTooLarge,
            u8"Native socket address length %d exceeds the supported storage size %zu.",
            (int)nativeLength,
            sizeof(Result->_storage));
    }

    Result->_length = nativeLength;
    Memory_Copy(nativeAddress, &Result->_storage, (size_t)nativeLength);
    *outAddress = Result;
    return Error_CreateSuccess();
}

static const struct sockaddr* SocketAddress_GetNativeConst(const SocketAddress* self)
{
    return (const struct sockaddr*)&self->_storage;
}

static uint16_t SocketAddress_ReadPort(const SocketAddress* self)
{
    if (self == NULL)
    {
        return 0;
    }

    switch (self->_family)
    {
        case SocketFamily_IPv4:
            return ntohs(((const struct sockaddr_in*)&self->_storage)->sin_port);

        case SocketFamily_IPv6:
            return ntohs(((const struct sockaddr_in6*)&self->_storage)->sin6_port);

        default:
            return 0;
    }
}

static Error SocketAddress_WritePort(struct sockaddr_storage* storage, SocketFamily family, uint16_t port, NativeSocketLength* outLength)
{
    if (storage == NULL)
    {
        return CreateNullArgumentError(u8"storage");
    }
    if (outLength == NULL)
    {
        return CreateNullArgumentError(u8"outLength");
    }

    Memory_Zero(storage, sizeof(*storage));
    switch (family)
    {
        case SocketFamily_IPv4:
            ((struct sockaddr_in*)storage)->sin_family = AF_INET;
            ((struct sockaddr_in*)storage)->sin_port = htons(port);
            *outLength = (NativeSocketLength)sizeof(struct sockaddr_in);
            return Error_CreateSuccess();

        case SocketFamily_IPv6:
            ((struct sockaddr_in6*)storage)->sin6_family = AF_INET6;
            ((struct sockaddr_in6*)storage)->sin6_port = htons(port);
            *outLength = (NativeSocketLength)sizeof(struct sockaddr_in6);
            return Error_CreateSuccess();

        default:
            return CreateUnsupportedFamilyError(family);
    }
}

static Error SocketPlatform_CreateSocket(int family, int type, int protocol, NativeSocket* outSocket)
{
    NativeSocket Handle = NATIVE_SOCKET_INVALID;

    if (outSocket == NULL)
    {
        return CreateNullArgumentError(u8"outSocket");
    }

    Handle = socket(family, type, protocol);
    if (Handle == NATIVE_SOCKET_INVALID)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"create a socket", SocketPlatform_GetLastErrorCode());
    }

    *outSocket = Handle;
    return Error_CreateSuccess();
}

static Error SocketPlatform_SetReuseAddress(NativeSocket handle)
{
#if defined(_WIN32)
    char OptionValue = 1;
    const char* OptionPointer = &OptionValue;
    int OptionSize = (int)sizeof(OptionValue);
#else
    int OptionValue = 1;
    const void* OptionPointer = &OptionValue;
    socklen_t OptionSize = (socklen_t)sizeof(OptionValue);
#endif

    if (setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, OptionPointer, OptionSize) != 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"configure the socket reuse address option", SocketPlatform_GetLastErrorCode());
    }

    return Error_CreateSuccess();
}

static Error SocketPlatform_SetTimeoutMilliseconds(NativeSocket handle, int optionName, size_t milliseconds)
{
#if defined(_WIN32)
    DWORD TimeoutValue = 0;
    const char* OptionPointer = NULL;
    int OptionSize = 0;

    if (milliseconds > (size_t)UINT32_MAX)
    {
        return CreateTimeoutRangeError(milliseconds);
    }

    TimeoutValue = (DWORD)milliseconds;
    OptionPointer = (const char*)&TimeoutValue;
    OptionSize = (int)sizeof(TimeoutValue);
#else
    struct timeval TimeoutValue;
    uintmax_t Seconds = milliseconds / 1000U;
    time_t ConvertedSeconds = 0;
    suseconds_t ConvertedMicroseconds = 0;
    const void* OptionPointer = NULL;
    socklen_t OptionSize = 0;

    ConvertedSeconds = (time_t)Seconds;
    if ((uintmax_t)ConvertedSeconds != Seconds)
    {
        return CreateTimeoutRangeError(milliseconds);
    }

    ConvertedMicroseconds = (suseconds_t)((milliseconds % 1000U) * 1000U);
    TimeoutValue.tv_sec = ConvertedSeconds;
    TimeoutValue.tv_usec = ConvertedMicroseconds;
    OptionPointer = &TimeoutValue;
    OptionSize = (socklen_t)sizeof(TimeoutValue);
#endif

    if (setsockopt(handle, SOL_SOCKET, optionName, OptionPointer, OptionSize) != 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"configure the socket timeout", SocketPlatform_GetLastErrorCode());
    }

    return Error_CreateSuccess();
}

static Error SocketPlatform_GetAddressText(const SocketAddress* self, char* buffer, size_t bufferCapacity)
{
    const void* SourceAddress = NULL;
#if defined(__linux__)
    socklen_t NativeCapacity = 0;
#endif

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }

    switch (self->_family)
    {
        case SocketFamily_IPv4:
            SourceAddress = &((const struct sockaddr_in*)&self->_storage)->sin_addr;
            break;

        case SocketFamily_IPv6:
            SourceAddress = &((const struct sockaddr_in6*)&self->_storage)->sin6_addr;
            break;

        default:
            return CreateUnsupportedFamilyError(self->_family);
    }

#if defined(__linux__)
    NativeCapacity = (socklen_t)bufferCapacity;
    if ((size_t)NativeCapacity != bufferCapacity)
    {
        return Error_Construct3(ErrorCode_ArgumentOutOfRange,
            u8"Socket address text buffer capacity %zu exceeds the supported range.",
            bufferCapacity);
    }
#endif

    if (inet_ntop(SocketFamily_ToNative(self->_family),
        SourceAddress,
        buffer,
#if defined(_WIN32)
        bufferCapacity
#else
        NativeCapacity
#endif
        ) == NULL)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"convert the socket address to text", SocketPlatform_GetLastErrorCode());
    }

    return Error_CreateSuccess();
}

static Error SocketPlatform_FillAddress(NativeSocket handle, bool useLocalAddress, SocketAddress** outAddress)
{
    struct sockaddr_storage NativeAddress;
    NativeSocketLength NativeLength = (NativeSocketLength)sizeof(NativeAddress);
    int Result = 0;

    if (outAddress == NULL)
    {
        return CreateNullArgumentError(u8"outAddress");
    }

    *outAddress = NULL;
    Memory_Zero(&NativeAddress, sizeof(NativeAddress));
    if (useLocalAddress)
    {
        Result = getsockname(handle, (struct sockaddr*)&NativeAddress, &NativeLength);
    }
    else
    {
        Result = getpeername(handle, (struct sockaddr*)&NativeAddress, &NativeLength);
    }
    if (Result != 0)
    {
        return CreateNativeSocketError(ErrorCode_IO,
            useLocalAddress ? u8"query the local socket address" : u8"query the remote socket address",
            SocketPlatform_GetLastErrorCode());
    }

    return SocketAddress_CreateFromNative((const struct sockaddr*)&NativeAddress, NativeLength, outAddress);
}

static Error SocketStream_EnsureOpen(TcpSocketStream* self, const unsigned char* operationName)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (self->_owner == NULL)
    {
        return Error_Construct1(ErrorCode_InvalidState, u8"Socket stream does not have an owning TCP socket.");
    }
    if (self->_isClosed || !self->_owner->_hasSocket)
    {
        return CreateSocketClosedError(u8"socket stream", operationName);
    }

    return Error_CreateSuccess();
}

static Error TcpSocket_CloseOwnedHandle(TcpSocket* self)
{
    Error Result = Error_CreateSuccess();

    if ((self == NULL) || !self->_hasSocket)
    {
        return Error_CreateSuccess();
    }

    Result = SocketPlatform_CloseHandle(self->_handle);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    self->_hasSocket = false;
    self->_isShutDown = true;
    if (self->_stream != NULL)
    {
        self->_stream->_isClosed = true;
        self->_stream->_isEOF = true;
    }
    SocketLibrary_Release();
    return Result;
}

static Error SocketStream_Flush(void* selfVoid)
{
    TcpSocketStream* self = selfVoid;
    return SocketStream_EnsureOpen(self, u8"flush");
}

static Error SocketPlatform_ConvertChunkSize(size_t size, NativeTransferLength* outSize)
{
    if (outSize == NULL)
    {
        return CreateNullArgumentError(u8"outSize");
    }
#if defined(_WIN32)
    if (size > (size_t)INT_MAX)
    {
        return CreateSizeRangeError(u8"use the socket API with", size);
    }
#endif

    *outSize = (NativeTransferLength)size;
    return Error_CreateSuccess();
}

static Error SocketStream_Write(void* selfVoid, const unsigned char* buffer, size_t bufferSize)
{
    TcpSocketStream* self = selfVoid;
    size_t Offset = 0;
    Error Result = SocketStream_EnsureOpen(self, u8"write to");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if ((buffer == NULL) && (bufferSize > 0))
    {
        return CreateNullArgumentError(u8"buffer");
    }

    while (Offset < bufferSize)
    {
        size_t RemainingCount = bufferSize - Offset;
        size_t ChunkSize = (RemainingCount > (size_t)INT_MAX) ? (size_t)INT_MAX : RemainingCount;
        NativeTransferLength NativeChunkSize = 0;
        NativeTransferResult WrittenCount = 0;

        Result = SocketPlatform_ConvertChunkSize(ChunkSize, &NativeChunkSize);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        WrittenCount = send(self->_owner->_handle, (const char*)(buffer + Offset), NativeChunkSize, 0);
        if (WrittenCount < 0)
        {
            return CreateNativeSocketError(ErrorCode_IO, u8"write to the socket stream", SocketPlatform_GetLastErrorCode());
        }
        if (WrittenCount == 0)
        {
            return Error_Construct1(ErrorCode_IO, u8"Socket stream write returned zero bytes.");
        }

        Offset += (size_t)WrittenCount;
    }

    return Error_CreateSuccess();
}

static Error SocketStream_WriteByte(void* selfVoid, unsigned char byte)
{
    return SocketStream_Write(selfVoid, &byte, 1);
}

static Error SocketStream_Read(void* selfVoid, GenericBuffer* dest, size_t readSize)
{
    TcpSocketStream* self = selfVoid;
    size_t InitialCount = 0;
    size_t RemainingSize = readSize;
    Error Result = SocketStream_EnsureOpen(self, u8"read from");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (dest == NULL)
    {
        return CreateNullArgumentError(u8"dest");
    }
    if (!SocketAddress_IsByteBuffer(dest))
    {
        return CreateByteBufferRequiredError(u8"dest", dest->_elementSize);
    }
    if (!GenericBuffer_TryPrepareForManualMutation(dest, readSize))
    {
        return Error_Construct3(ErrorCode_BufferTooSmall,
            u8"Socket stream destination buffer is too small to receive %zu bytes.",
            readSize);
    }
    if (readSize == 0)
    {
        return Error_CreateSuccess();
    }

    InitialCount = dest->_count;
    while (RemainingSize > 0)
    {
        size_t ChunkSize = (RemainingSize > (size_t)INT_MAX) ? (size_t)INT_MAX : RemainingSize;
        NativeTransferLength NativeChunkSize = 0;
        NativeTransferResult ReadCount = 0;

        Result = SocketPlatform_ConvertChunkSize(ChunkSize, &NativeChunkSize);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        ReadCount = recv(self->_owner->_handle, (char*)(dest->_data + dest->_count), NativeChunkSize, 0);
        if (ReadCount < 0)
        {
            return CreateNativeSocketError(ErrorCode_IO, u8"read from the socket stream", SocketPlatform_GetLastErrorCode());
        }
        if (ReadCount == 0)
        {
            self->_isEOF = true;
            return Error_CreateSuccess();
        }

        dest->_count += (size_t)ReadCount;
        RemainingSize -= (size_t)ReadCount;
        if ((size_t)ReadCount < ChunkSize)
        {
            break;
        }
    }

    self->_isEOF = (dest->_count == InitialCount);
    return Error_CreateSuccess();
}

static Error SocketStream_ReadByte(void* selfVoid, unsigned char* byte)
{
    TcpSocketStream* self = selfVoid;
    NativeTransferResult ReadCount = 0;
    Error Result = SocketStream_EnsureOpen(self, u8"read from");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (byte == NULL)
    {
        return CreateNullArgumentError(u8"byte");
    }

    ReadCount = recv(self->_owner->_handle, (char*)byte, 1, 0);
    if (ReadCount < 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"read a byte from the socket stream", SocketPlatform_GetLastErrorCode());
    }
    if (ReadCount == 0)
    {
        self->_isEOF = true;
        return CreateSocketEOFError();
    }

    self->_isEOF = false;
    return Error_CreateSuccess();
}

static Error SocketStream_Close(void* selfVoid)
{
    TcpSocketStream* self = selfVoid;

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if ((self->_owner == NULL) || !self->_owner->_hasSocket || self->_isClosed)
    {
        self->_isClosed = true;
        self->_isEOF = true;
        return Error_CreateSuccess();
    }

    return TcpSocket_CloseOwnedHandle(self->_owner);
}

static bool SocketStream_IsEOF(void* selfVoid)
{
    TcpSocketStream* self = selfVoid;
    return (self == NULL) ? true : self->_isEOF;
}

static Error SocketStream_Deconstruct(void* selfVoid)
{
    TcpSocketStream* self = selfVoid;

    if (self != NULL)
    {
        self->_isClosed = true;
    }

    return Error_CreateSuccess();
}

static IOStreamVTable CreateSocketStreamVTable(TcpSocketStream* self)
{
    IOStreamVTable Result = SOCKET_STREAM_VTABLE;

    Result.Self = self;
    Result._flush = &SocketStream_Flush;
    Result._writeByte = &SocketStream_WriteByte;
    Result._write = &SocketStream_Write;
    Result._readByte = &SocketStream_ReadByte;
    Result._read = &SocketStream_Read;
    Result._close = &SocketStream_Close;
    Result._isEOF = &SocketStream_IsEOF;
    Result._deconstruct = &SocketStream_Deconstruct;
    return Result;
}

static void SocketStream_Construct(TcpSocketStream* self, TcpSocket* owner)
{
    self->_owner = owner;
    self->_isClosed = false;
    self->_isEOF = false;
    self->Base._type = IOStreamType_Socket;
    self->Base._flags = (IOStreamFlags)(IOStreamFlags_CanRead | IOStreamFlags_CanWrite);
    self->Base._vtable = CreateSocketStreamVTable(self);
}

static Error TcpSocket_CreateFromHandle(NativeSocket handle, TcpSocket** outSocket)
{
    TcpSocket* Result = NULL;
    TcpSocketStream* Stream = NULL;

    if (outSocket == NULL)
    {
        return CreateNullArgumentError(u8"outSocket");
    }

    *outSocket = NULL;
    Result = Memory_Allocate(sizeof(TcpSocket));
    if (Result == NULL)
    {
        return Error_Construct1(ErrorCode_Construct, u8"Failed to allocate a TCP socket.");
    }

    Stream = Memory_Allocate(sizeof(TcpSocketStream));
    if (Stream == NULL)
    {
        Memory_Free(Result);
        return Error_Construct1(ErrorCode_Construct, u8"Failed to allocate a TCP socket stream.");
    }

    Memory_Zero(Result, sizeof(TcpSocket));
    Memory_Zero(Stream, sizeof(TcpSocketStream));
    Result->_handle = handle;
    Result->_hasSocket = true;
    Result->_isShutDown = false;
    Result->_stream = Stream;
    SocketStream_Construct(Stream, Result);
    *outSocket = Result;
    return Error_CreateSuccess();
}

static Error TcpSocket_EnsureOpen(TcpSocket* self, const unsigned char* operationName)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_hasSocket)
    {
        return CreateSocketClosedError(u8"TCP socket", operationName);
    }

    return Error_CreateSuccess();
}

static Error TcpListener_EnsureOpen(TcpListener* self, const unsigned char* operationName)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_hasSocket)
    {
        return CreateSocketClosedError(u8"TCP listener", operationName);
    }

    return Error_CreateSuccess();
}

static Error UdpSocket_EnsureOpen(UdpSocket* self, const unsigned char* operationName)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_hasSocket)
    {
        return CreateSocketClosedError(u8"UDP socket", operationName);
    }

    return Error_CreateSuccess();
}


// Public functions.
Error SocketAddress_FromString(const unsigned char* ip, uint16_t port, SocketAddress** outAddress)
{
    struct sockaddr_storage Storage;
    NativeSocketLength AddressLength = 0;
    SocketAddress* Result = NULL;
    int ParseResult = 0;
    Error AcquireResult = Error_CreateSuccess();
    Error BuildResult = Error_CreateSuccess();

    if (ip == NULL)
    {
        return CreateNullArgumentError(u8"ip");
    }
    if (outAddress == NULL)
    {
        return CreateNullArgumentError(u8"outAddress");
    }

    *outAddress = NULL;
    AcquireResult = SocketLibrary_Acquire();
    if (AcquireResult.Code != ErrorCode_Success)
    {
        return AcquireResult;
    }

    BuildResult = SocketAddress_WritePort(&Storage, SocketFamily_IPv4, port, &AddressLength);
    if (BuildResult.Code == ErrorCode_Success)
    {
        ParseResult = inet_pton(AF_INET, (const char*)ip, &((struct sockaddr_in*)&Storage)->sin_addr);
        if (ParseResult == 1)
        {
            BuildResult = SocketAddress_CreateFromNative((const struct sockaddr*)&Storage, AddressLength, &Result);
            SocketLibrary_Release();
            return BuildResult;
        }
    }

    BuildResult = SocketAddress_WritePort(&Storage, SocketFamily_IPv6, port, &AddressLength);
    if (BuildResult.Code == ErrorCode_Success)
    {
        ParseResult = inet_pton(AF_INET6, (const char*)ip, &((struct sockaddr_in6*)&Storage)->sin6_addr);
        if (ParseResult == 1)
        {
            BuildResult = SocketAddress_CreateFromNative((const struct sockaddr*)&Storage, AddressLength, &Result);
            SocketLibrary_Release();
            return BuildResult;
        }
    }

    SocketLibrary_Release();
    return Error_Construct3(ErrorCode_IllegalArgument,
        u8"\"%s\" is not a valid IPv4 or IPv6 address string.",
        ip);
}

Error SocketAddress_FromHostname(const unsigned char* hostname, uint16_t port, SocketAddress** outAddress)
{
    struct addrinfo Hints;
    struct addrinfo* Results = NULL;
    char PortText[SOCKET_PORT_TEXT_CAPACITY];
    int ResolverResult = 0;
    Error Result = Error_CreateSuccess();

    if (hostname == NULL)
    {
        return CreateNullArgumentError(u8"hostname");
    }
    if (outAddress == NULL)
    {
        return CreateNullArgumentError(u8"outAddress");
    }

    *outAddress = NULL;
    Result = ConvertPortToText(port, PortText, sizeof(PortText));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = SocketLibrary_Acquire();
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Zero(&Hints, sizeof(Hints));
    Hints.ai_family = AF_UNSPEC;
    Hints.ai_socktype = 0;
    Hints.ai_protocol = 0;
    Hints.ai_flags = AI_NUMERICSERV;
    ResolverResult = getaddrinfo((const char*)hostname, PortText, &Hints, &Results);
    if (ResolverResult != 0)
    {
        SocketLibrary_Release();
        return CreateAddressInfoError(u8"resolve the hostname", ResolverResult);
    }
    if ((Results == NULL) || (Results->ai_addr == NULL))
    {
        freeaddrinfo(Results);
        SocketLibrary_Release();
        return Error_Construct1(ErrorCode_IO, u8"Hostname resolution did not return a usable socket address.");
    }

    Result = SocketAddress_CreateFromNative(Results->ai_addr, (NativeSocketLength)Results->ai_addrlen, outAddress);
    freeaddrinfo(Results);
    SocketLibrary_Release();
    return Result;
}

Error SocketAddress_GetIP(const SocketAddress* self, GenericBuffer* outIP)
{
    char AddressText[SOCKET_ADDRESS_TEXT_CAPACITY];
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (outIP == NULL)
    {
        return CreateNullArgumentError(u8"outIP");
    }
    if (!SocketAddress_IsByteBuffer(outIP))
    {
        return CreateByteBufferRequiredError(u8"outIP", outIP->_elementSize);
    }

    Result = SocketPlatform_GetAddressText(self, AddressText, sizeof(AddressText));
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (!GenericBuffer_AppendString(outIP, (const unsigned char*)AddressText))
    {
        return Error_Construct1(ErrorCode_BufferTooSmall, u8"Failed to append the socket IP address to the output buffer.");
    }
    if (!GenericBuffer_NullTerminate(outIP))
    {
        return Error_Construct1(ErrorCode_BufferTooSmall, u8"Failed to null-terminate the socket IP output buffer.");
    }

    return Error_CreateSuccess();
}

uint16_t SocketAddress_GetPort(const SocketAddress* self)
{
    return SocketAddress_ReadPort(self);
}

SocketFamily SocketAddress_GetFamily(const SocketAddress* self)
{
    return (self == NULL) ? SocketFamily_IPv4 : self->_family;
}

void SocketAddress_Deconstruct(SocketAddress* self)
{
    if (self != NULL)
    {
        Memory_Free(self);
    }
}

Error TcpListener_Create(uint16_t port, SocketFamily family, TcpListener** outListener)
{
    int NativeFamily = 0;
    TcpListener* Result = NULL;
    NativeSocket Handle = NATIVE_SOCKET_INVALID;
    struct sockaddr_storage BindAddress;
    NativeSocketLength BindAddressLength = 0;
    Error OperationResult = Error_CreateSuccess();

    if (outListener == NULL)
    {
        return CreateNullArgumentError(u8"outListener");
    }

    *outListener = NULL;
    NativeFamily = SocketFamily_ToNative(family);
    if (NativeFamily < 0)
    {
        return CreateUnsupportedFamilyError(family);
    }

    OperationResult = SocketLibrary_Acquire();
    if (OperationResult.Code != ErrorCode_Success)
    {
        return OperationResult;
    }

    OperationResult = SocketPlatform_CreateSocket(NativeFamily, SOCK_STREAM, IPPROTO_TCP, &Handle);
    if (OperationResult.Code != ErrorCode_Success)
    {
        SocketLibrary_Release();
        return OperationResult;
    }

    OperationResult = SocketPlatform_SetReuseAddress(Handle);
    if (OperationResult.Code != ErrorCode_Success)
    {
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return OperationResult;
    }

    OperationResult = SocketAddress_WritePort(&BindAddress, family, port, &BindAddressLength);
    if (OperationResult.Code != ErrorCode_Success)
    {
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return OperationResult;
    }

    if (bind(Handle, (const struct sockaddr*)&BindAddress, BindAddressLength) != 0)
    {
        OperationResult = CreateNativeSocketError(ErrorCode_IO, u8"bind the TCP listener", SocketPlatform_GetLastErrorCode());
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return OperationResult;
    }
    if (listen(Handle, SOMAXCONN) != 0)
    {
        OperationResult = CreateNativeSocketError(ErrorCode_IO, u8"start listening on the TCP listener", SocketPlatform_GetLastErrorCode());
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return OperationResult;
    }

    Result = Memory_Allocate(sizeof(TcpListener));
    if (Result == NULL)
    {
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return Error_Construct1(ErrorCode_Construct, u8"Failed to allocate a TCP listener.");
    }

    Memory_Zero(Result, sizeof(TcpListener));
    Result->_handle = Handle;
    Result->_hasSocket = true;
    Result->_family = family;
    *outListener = Result;
    return Error_CreateSuccess();
}

Error TcpListener_Accept(TcpListener* self, TcpSocket** outSocket)
{
    NativeSocket AcceptedHandle = NATIVE_SOCKET_INVALID;
    Error Result = TcpListener_EnsureOpen(self, u8"accept from");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outSocket == NULL)
    {
        return CreateNullArgumentError(u8"outSocket");
    }

    *outSocket = NULL;
    AcceptedHandle = accept(self->_handle, NULL, NULL);
    if (AcceptedHandle == NATIVE_SOCKET_INVALID)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"accept a TCP client", SocketPlatform_GetLastErrorCode());
    }

    Result = SocketLibrary_Acquire();
    if (Result.Code != ErrorCode_Success)
    {
        (void)SocketPlatform_CloseHandle(AcceptedHandle);
        return Result;
    }

    Result = TcpSocket_CreateFromHandle(AcceptedHandle, outSocket);
    if (Result.Code != ErrorCode_Success)
    {
        (void)SocketPlatform_CloseHandle(AcceptedHandle);
        SocketLibrary_Release();
        return Result;
    }

    return Error_CreateSuccess();
}

Error TcpListener_GetLocalAddress(const TcpListener* self, SocketAddress** outAddress)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_hasSocket)
    {
        return CreateSocketClosedError(u8"TCP listener", u8"query the local address of");
    }

    return SocketPlatform_FillAddress(self->_handle, true, outAddress);
}

Error TcpListener_Deconstruct(TcpListener* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }
    if (self->_hasSocket)
    {
        Result = SocketPlatform_CloseHandle(self->_handle);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        self->_hasSocket = false;
        SocketLibrary_Release();
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}

Error TcpSocket_Connect(const SocketAddress* address, TcpSocket** outSocket)
{
    NativeSocket Handle = NATIVE_SOCKET_INVALID;
    TcpSocket* ResultSocket = NULL;
    Error Result = Error_CreateSuccess();

    if (address == NULL)
    {
        return CreateNullArgumentError(u8"address");
    }
    if (outSocket == NULL)
    {
        return CreateNullArgumentError(u8"outSocket");
    }

    *outSocket = NULL;
    Result = SocketLibrary_Acquire();
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = SocketPlatform_CreateSocket(SocketFamily_ToNative(address->_family), SOCK_STREAM, IPPROTO_TCP, &Handle);
    if (Result.Code != ErrorCode_Success)
    {
        SocketLibrary_Release();
        return Result;
    }

    if (connect(Handle, SocketAddress_GetNativeConst(address), address->_length) != 0)
    {
        Result = CreateNativeSocketError(ErrorCode_IO, u8"connect the TCP socket", SocketPlatform_GetLastErrorCode());
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return Result;
    }

    Result = TcpSocket_CreateFromHandle(Handle, &ResultSocket);
    if (Result.Code != ErrorCode_Success)
    {
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return Result;
    }

    *outSocket = ResultSocket;
    return Error_CreateSuccess();
}

Error TcpSocket_GetIOStream(TcpSocket* self, IOStream** outStream)
{
    Error Result = TcpSocket_EnsureOpen(self, u8"get the stream of");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (outStream == NULL)
    {
        return CreateNullArgumentError(u8"outStream");
    }

    *outStream = &self->_stream->Base;
    return Error_CreateSuccess();
}

Error TcpSocket_GetLocalAddress(const TcpSocket* self, SocketAddress** outAddress)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_hasSocket)
    {
        return CreateSocketClosedError(u8"TCP socket", u8"query the local address of");
    }

    return SocketPlatform_FillAddress(self->_handle, true, outAddress);
}

Error TcpSocket_GetRemoteAddress(const TcpSocket* self, SocketAddress** outAddress)
{
    if (self == NULL)
    {
        return CreateNullArgumentError(u8"self");
    }
    if (!self->_hasSocket)
    {
        return CreateSocketClosedError(u8"TCP socket", u8"query the remote address of");
    }

    return SocketPlatform_FillAddress(self->_handle, false, outAddress);
}

Error TcpSocket_SetReceiveTimeout(TcpSocket* self, size_t milliseconds)
{
    Error Result = TcpSocket_EnsureOpen(self, u8"configure the receive timeout of");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return SocketPlatform_SetTimeoutMilliseconds(self->_handle, SO_RCVTIMEO, milliseconds);
}

Error TcpSocket_SetSendTimeout(TcpSocket* self, size_t milliseconds)
{
    Error Result = TcpSocket_EnsureOpen(self, u8"configure the send timeout of");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return SocketPlatform_SetTimeoutMilliseconds(self->_handle, SO_SNDTIMEO, milliseconds);
}

Error TcpSocket_Shutdown(TcpSocket* self)
{
    int Result = 0;
    Error EnsureResult = TcpSocket_EnsureOpen(self, u8"shut down");

    if (EnsureResult.Code != ErrorCode_Success)
    {
        return EnsureResult;
    }
    if (self->_isShutDown)
    {
        return Error_CreateSuccess();
    }

#if defined(_WIN32)
    Result = shutdown(self->_handle, SD_BOTH);
#else
    Result = shutdown(self->_handle, SHUT_RDWR);
#endif
    if (Result != 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"shut down the TCP socket", SocketPlatform_GetLastErrorCode());
    }

    self->_isShutDown = true;
    if (self->_stream != NULL)
    {
        self->_stream->_isEOF = true;
    }
    return Error_CreateSuccess();
}

Error TcpSocket_Deconstruct(TcpSocket* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }

    Result = TcpSocket_CloseOwnedHandle(self);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    if (self->_stream != NULL)
    {
        Memory_Free(self->_stream);
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}

Error UdpSocket_Create(SocketFamily family, UdpSocket** outSocket)
{
    int NativeFamily = SocketFamily_ToNative(family);
    NativeSocket Handle = NATIVE_SOCKET_INVALID;
    UdpSocket* Result = NULL;
    Error OperationResult = Error_CreateSuccess();

    if (outSocket == NULL)
    {
        return CreateNullArgumentError(u8"outSocket");
    }

    *outSocket = NULL;
    if (NativeFamily < 0)
    {
        return CreateUnsupportedFamilyError(family);
    }

    OperationResult = SocketLibrary_Acquire();
    if (OperationResult.Code != ErrorCode_Success)
    {
        return OperationResult;
    }

    OperationResult = SocketPlatform_CreateSocket(NativeFamily, SOCK_DGRAM, IPPROTO_UDP, &Handle);
    if (OperationResult.Code != ErrorCode_Success)
    {
        SocketLibrary_Release();
        return OperationResult;
    }

    Result = Memory_Allocate(sizeof(UdpSocket));
    if (Result == NULL)
    {
        (void)SocketPlatform_CloseHandle(Handle);
        SocketLibrary_Release();
        return Error_Construct1(ErrorCode_Construct, u8"Failed to allocate a UDP socket.");
    }

    Memory_Zero(Result, sizeof(UdpSocket));
    Result->_handle = Handle;
    Result->_hasSocket = true;
    Result->_family = family;
    *outSocket = Result;
    return Error_CreateSuccess();
}

Error UdpSocket_Bind(UdpSocket* self, uint16_t port)
{
    struct sockaddr_storage BindAddress;
    NativeSocketLength BindAddressLength = 0;
    Error Result = UdpSocket_EnsureOpen(self, u8"bind");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Result = SocketAddress_WritePort(&BindAddress, self->_family, port, &BindAddressLength);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (bind(self->_handle, (const struct sockaddr*)&BindAddress, BindAddressLength) != 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"bind the UDP socket", SocketPlatform_GetLastErrorCode());
    }

    return Error_CreateSuccess();
}

Error UdpSocket_Send(UdpSocket* self, const SocketAddress* destination, const void* data, size_t size)
{
    NativeTransferLength NativeSize = 0;
    NativeTransferResult SentCount = 0;
    Error Result = UdpSocket_EnsureOpen(self, u8"send from");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (destination == NULL)
    {
        return CreateNullArgumentError(u8"destination");
    }
    if ((data == NULL) && (size > 0))
    {
        return CreateNullArgumentError(u8"data");
    }
    if (destination->_family != self->_family)
    {
        return Error_Construct1(ErrorCode_IllegalArgument, u8"UDP socket family must match the destination address family.");
    }
    if (size == 0)
    {
        return Error_CreateSuccess();
    }

    Result = SocketPlatform_ConvertChunkSize(size, &NativeSize);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    SentCount = sendto(self->_handle,
        (const char*)data,
        NativeSize,
        0,
        SocketAddress_GetNativeConst(destination),
        destination->_length);
    if (SentCount < 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"send a UDP datagram", SocketPlatform_GetLastErrorCode());
    }
    if ((size_t)SentCount != size)
    {
        return Error_Construct3(ErrorCode_IO,
            u8"UDP socket send wrote %jd bytes out of %zu.",
            (intmax_t)SentCount,
            size);
    }

    return Error_CreateSuccess();
}

Error UdpSocket_Receive(UdpSocket* self, GenericBuffer* buffer, size_t* outBytesRead, SocketAddress** outSender)
{
    struct sockaddr_storage SenderStorage;
    NativeSocketLength SenderLength = (NativeSocketLength)sizeof(SenderStorage);
    NativeTransferLength NativeCapacity = 0;
    NativeTransferResult ReadCount = 0;
    Error Result = UdpSocket_EnsureOpen(self, u8"receive from");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }
    if (buffer == NULL)
    {
        return CreateNullArgumentError(u8"buffer");
    }
    if (outBytesRead == NULL)
    {
        return CreateNullArgumentError(u8"outBytesRead");
    }
    if (!SocketAddress_IsByteBuffer(buffer))
    {
        return CreateByteBufferRequiredError(u8"buffer", buffer->_elementSize);
    }
    if (outSender != NULL)
    {
        *outSender = NULL;
    }

    *outBytesRead = 0;
    if (!GenericBuffer_TryPrepareForManualMutation(buffer, GenericBuffer_GetCapacityRemaining(buffer)))
    {
        return Error_Construct1(ErrorCode_BufferTooSmall, u8"UDP receive buffer cannot accept any more bytes.");
    }
    if (buffer->_count >= buffer->_capacity)
    {
        return Error_Construct1(ErrorCode_BufferTooSmall, u8"UDP receive buffer has no remaining capacity.");
    }

    Result = SocketPlatform_ConvertChunkSize(buffer->_capacity - buffer->_count, &NativeCapacity);
    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    Memory_Zero(&SenderStorage, sizeof(SenderStorage));
    ReadCount = recvfrom(self->_handle,
        (char*)(buffer->_data + buffer->_count),
        NativeCapacity,
        0,
        (struct sockaddr*)&SenderStorage,
        &SenderLength);
    if (ReadCount < 0)
    {
        return CreateNativeSocketError(ErrorCode_IO, u8"receive a UDP datagram", SocketPlatform_GetLastErrorCode());
    }

    buffer->_count += (size_t)ReadCount;
    *outBytesRead = (size_t)ReadCount;
    if (outSender != NULL)
    {
        return SocketAddress_CreateFromNative((const struct sockaddr*)&SenderStorage, SenderLength, outSender);
    }

    return Error_CreateSuccess();
}

Error UdpSocket_SetReceiveTimeout(UdpSocket* self, size_t milliseconds)
{
    Error Result = UdpSocket_EnsureOpen(self, u8"configure the receive timeout of");

    if (Result.Code != ErrorCode_Success)
    {
        return Result;
    }

    return SocketPlatform_SetTimeoutMilliseconds(self->_handle, SO_RCVTIMEO, milliseconds);
}

Error UdpSocket_Deconstruct(UdpSocket* self)
{
    Error Result = Error_CreateSuccess();

    if (self == NULL)
    {
        return Error_CreateSuccess();
    }
    if (self->_hasSocket)
    {
        Result = SocketPlatform_CloseHandle(self->_handle);
        if (Result.Code != ErrorCode_Success)
        {
            return Result;
        }

        self->_hasSocket = false;
        SocketLibrary_Release();
    }

    Memory_Free(self);
    return Error_CreateSuccess();
}
