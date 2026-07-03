#pragma once
#include "WRMemory.h"
#include "WRError.h"
#include "WRIO.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>


// Types.
/**
 * @brief Opaque, heap-allocated socket endpoint: an IP address (IPv4 or IPv6) together with a port.
 *
 * Produced by the SocketAddress_From* factories and by the address-query and receive operations of the
 * socket types in this module. The contained address family is fixed at creation time. An instance owns
 * its own storage and must be released with SocketAddress_Deconstruct.
 */
typedef struct SocketAddressStruct SocketAddress;

/**
 * @brief Opaque, heap-allocated connected TCP socket (stream socket).
 *
 * Represents one end of an established TCP connection, obtained either by dialing out with
 * TcpSocket_Connect or by accepting an inbound connection with TcpListener_Accept. Byte transfer is
 * performed through the IOStream returned by TcpSocket_GetIOStream. Must be released with
 * TcpSocket_Deconstruct, which also closes the underlying connection if still open.
 */
typedef struct TcpSocketStruct TcpSocket;

/**
 * @brief Opaque, heap-allocated TCP server socket that is bound to a local port and listening.
 *
 * Created by TcpListener_Create (which binds and begins listening in one step). Inbound connections are
 * pulled off the accept queue one at a time with TcpListener_Accept. Must be released with
 * TcpListener_Deconstruct.
 */
typedef struct TcpListenerStruct TcpListener;

/**
 * @brief Opaque, heap-allocated UDP socket (connectionless datagram socket).
 *
 * Created by UdpSocket_Create. May optionally be bound to a local port with UdpSocket_Bind in order to
 * receive on a known port. Sends and receives whole datagrams with UdpSocket_Send / UdpSocket_Receive.
 * Must be released with UdpSocket_Deconstruct.
 */
typedef struct UdpSocketStruct UdpSocket;

/**
 * @brief Address family (protocol version) of a socket or socket address.
 *
 * Selects between IPv4 and IPv6 when creating sockets and addresses, and reports the family of an
 * existing SocketAddress. These are the only families this module supports.
 */
typedef enum SocketFamilyEnum
{
    /** @brief IPv4 address family (maps to the native AF_INET). */
    SocketFamily_IPv4,
    /** @brief IPv6 address family (maps to the native AF_INET6). */
    SocketFamily_IPv6,
} SocketFamily;

/**
 * @brief Builds a socket address from a numeric IP-address string and a port, without doing any DNS.
 *
 * The string is parsed first as an IPv4 dotted-quad and, failing that, as an IPv6 address; the resulting
 * address takes whichever family parsed successfully. No name resolution or network access occurs. On
 * success a newly allocated SocketAddress is written to *outAddress and ownership transfers to the caller
 * (release with SocketAddress_Deconstruct). On failure *outAddress is left NULL.
 * @param ip Null-terminated UTF-8 numeric address string, e.g. "127.0.0.1" or "::1". Must not be NULL.
 *           Hostnames are NOT accepted here (use SocketAddress_FromHostname for those).
 * @param port Port number in host byte order; the full 0-65535 range is accepted (0 means "unspecified").
 * @param outAddress [out] Receives the new SocketAddress on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p ip or @p outAddress is NULL or if @p ip is neither a
 *          valid IPv4 nor a valid IPv6 address string.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error SocketAddress_FromString(const unsigned char* ip, uint16_t port, SocketAddress** outAddress);

/**
 * @brief Resolves a hostname (or numeric address) to a socket address for the given port via DNS lookup.
 *
 * Performs name resolution, which may block and may contact the network/DNS, and returns the first usable
 * result. The resulting address may be IPv4 or IPv6 depending on what resolution yields. The port is
 * combined with the resolved host. On success a newly allocated SocketAddress is written to *outAddress and
 * ownership transfers to the caller (release with SocketAddress_Deconstruct); on failure *outAddress is NULL.
 * @param hostname Null-terminated UTF-8 host name to resolve (e.g. "example.com"); a numeric address string
 *                 is also accepted. Must not be NULL.
 * @param port Port number in host byte order (0-65535).
 * @param outAddress [out] Receives the new SocketAddress on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p hostname or @p outAddress is NULL, ErrorCode_BufferTooSmall
 *          if the port cannot be formatted, or ErrorCode_IO if resolution fails or returns no usable address.
 * @note Resolution is a blocking call; do not assume it returns promptly.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error SocketAddress_FromHostname(const unsigned char* hostname, uint16_t port, SocketAddress** outAddress);

/**
 * @brief Writes the textual IP address of this endpoint into a byte buffer as a UTF-8 string.
 *
 * Formats the address (dotted-quad for IPv4, colon-hex for IPv6) and appends it to @p outIP, then writes a
 * trailing null terminator. As a string-writer this appends to whatever the buffer already holds; pass an
 * empty buffer to obtain just the address. The port is not included.
 * @param self The address to read. Must not be NULL.
 * @param outIP [out] Destination buffer whose element size must be 1 byte; receives the appended,
 *              null-terminated address text. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p self or @p outIP is NULL or if @p outIP is not a
 *          byte buffer, or ErrorCode_BufferTooSmall if the text or its null terminator does not fit.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error SocketAddress_GetIP(const SocketAddress* self, GenericBuffer* outIP);

/**
 * @brief Returns the port number of this endpoint in host byte order.
 * @param self The address to read. If NULL (or of an unsupported family), 0 is returned.
 * @returns The port in host byte order, or 0 when @p self is NULL.
 */
uint16_t SocketAddress_GetPort(const SocketAddress* self);

/**
 * @brief Returns the address family (IPv4 or IPv6) of this endpoint.
 * @param self The address to read. May be NULL.
 * @returns The address's SocketFamily, or SocketFamily_IPv4 when @p self is NULL.
 */
SocketFamily SocketAddress_GetFamily(const SocketAddress* self);

/**
 * @brief Releases a socket address and its storage.
 *
 * Frees the address allocated by the SocketAddress_From* factories and by the address-query/receive
 * operations. Passing NULL is a safe no-op. After this call the pointer must not be used again.
 * @param self The address to release, or NULL.
 */
void SocketAddress_Deconstruct(SocketAddress* self);



/**
 * @brief Creates a TCP server socket bound to a local port and immediately listening for connections.
 *
 * In one step this creates the socket, enables address reuse (so the port can be re-bound shortly after a
 * previous listener closed), binds to the given port on all local interfaces of the chosen family, and
 * begins listening with a system-default backlog. On success a new TcpListener is written to *outListener
 * and ownership transfers to the caller (release with TcpListener_Deconstruct); on failure *outListener is NULL.
 * @param port Local port to bind, in host byte order. 0 requests an ephemeral port chosen by the OS (recover
 *             the actual port via TcpListener_GetLocalAddress).
 * @param family Address family to listen on (SocketFamily_IPv4 or SocketFamily_IPv6).
 * @param outListener [out] Receives the new listener on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p outListener is NULL or @p family is not a supported
 *          family, ErrorCode_IO if the socket cannot be created/configured/bound or listening cannot start
 *          (for example the port is already in use), or ErrorCode_Construct if the listener cannot be allocated.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpListener_Create(uint16_t port, SocketFamily family, TcpListener** outListener);

/**
 * @brief Accepts the next inbound TCP connection, blocking until one is available.
 *
 * Removes one pending connection from the listener's accept queue and returns it as a connected TcpSocket.
 * By default this blocks until a client connects (the listener has no receive-timeout setter). On success a
 * new TcpSocket is written to *outSocket and ownership transfers to the caller (release with
 * TcpSocket_Deconstruct); on failure *outSocket is NULL.
 * @param self The listening socket. Must not be NULL and must not be closed.
 * @param outSocket [out] Receives the accepted connection on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p outSocket is NULL, ErrorCode_InvalidOperation if the
 *          listener is closed, ErrorCode_IO if the native accept fails, or ErrorCode_Construct if the new
 *          socket cannot be allocated.
 * @note Blocks the calling thread until a connection arrives or the accept fails.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpListener_Accept(TcpListener* self, TcpSocket** outSocket);

/**
 * @brief Retrieves the local address the listener is bound to (useful to discover an OS-assigned port).
 *
 * Queries the bound local endpoint and returns it as a new SocketAddress. Ownership of *outAddress transfers
 * to the caller (release with SocketAddress_Deconstruct). Especially useful after binding to port 0.
 * @param self The listening socket. Must not be NULL and must not be closed.
 * @param outAddress [out] Receives the local address on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          listener is closed, or ErrorCode_IO if the address cannot be queried.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpListener_GetLocalAddress(const TcpListener* self, SocketAddress** outAddress);

/**
 * @brief Closes the listening socket and releases the listener.
 *
 * Stops accepting connections, closes the underlying socket if still open, and frees the listener. Passing
 * NULL is a safe no-op. After this call the pointer must not be used again. Connections already accepted via
 * TcpListener_Accept are independent and are not affected.
 * @param self The listener to release, or NULL.
 * @returns Success, or ErrorCode_IO if closing the underlying socket fails (in which case the listener
 *          structure is not freed and the call may be retried).
 */
Error TcpListener_Deconstruct(TcpListener* self);



/**
 * @brief Opens a new TCP connection to a remote endpoint, blocking until the connection completes.
 *
 * Creates a socket of the address's family and connects it to @p address; this blocks until the connection
 * is established or fails. On success a connected TcpSocket is written to *outSocket and ownership transfers
 * to the caller (release with TcpSocket_Deconstruct); on failure *outSocket is NULL. Use TcpSocket_GetIOStream
 * to transfer bytes afterwards.
 * @param address The remote endpoint to connect to (its family also selects the local socket family).
 *                Must not be NULL.
 * @param outSocket [out] Receives the connected socket on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p address or @p outSocket is NULL, ErrorCode_IO if the
 *          socket cannot be created or the connect fails (host unreachable, connection refused, etc.), or
 *          ErrorCode_Construct if the socket cannot be allocated.
 * @note Blocks the calling thread for the duration of the connection attempt.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpSocket_Connect(const SocketAddress* address, TcpSocket** outSocket);

/**
 * @brief Returns the readable/writable IOStream that transfers bytes over this TCP connection.
 *
 * The returned stream is the canonical way to send and receive data on the socket. It is owned by the socket
 * (do NOT deconstruct it independently and do not use it after the socket is released) and remains valid for
 * the socket's lifetime. The stream supports reading and writing but is not seekable. Reads and writes on it
 * block subject to the socket's configured send/receive timeouts; a read that returns zero bytes indicates the
 * peer closed its side, after which the stream reports end-of-stream. Closing the stream closes the socket.
 * The same stream instance is returned on every call.
 * @param self The connected socket. Must not be NULL and must not be closed.
 * @param outStream [out] Receives a borrowed pointer to the socket's IOStream. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p outStream is NULL, or ErrorCode_InvalidOperation if
 *          the socket is closed.
 */
Error TcpSocket_GetIOStream(TcpSocket* self, IOStream** outStream);

/**
 * @brief Retrieves the local endpoint (address and port) of this connected socket.
 *
 * Returns the local side of the connection as a new SocketAddress; ownership of *outAddress transfers to the
 * caller (release with SocketAddress_Deconstruct).
 * @param self The connected socket. Must not be NULL and must not be closed.
 * @param outAddress [out] Receives the local address on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          socket is closed, or ErrorCode_IO if the address cannot be queried.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpSocket_GetLocalAddress(const TcpSocket* self, SocketAddress** outAddress);

/**
 * @brief Retrieves the remote (peer) endpoint this socket is connected to.
 *
 * Returns the peer's address as a new SocketAddress; ownership of *outAddress transfers to the caller
 * (release with SocketAddress_Deconstruct).
 * @param self The connected socket. Must not be NULL and must not be closed.
 * @param outAddress [out] Receives the remote address on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          socket is closed, or ErrorCode_IO if the address cannot be queried.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpSocket_GetRemoteAddress(const TcpSocket* self, SocketAddress** outAddress);

/**
 * @brief Sets a timeout for blocking receive (read) operations on this socket.
 *
 * Applies to subsequent reads performed through the socket's IOStream. A value of 0 means "no timeout"
 * (block indefinitely). Once set, a read that exceeds the timeout fails with an I/O error rather than
 * blocking forever.
 * @param self The connected socket. Must not be NULL and must not be closed.
 * @param milliseconds Timeout in milliseconds; 0 disables the timeout. Very large values that exceed the
 *                     platform's representable range are rejected.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          socket is closed, ErrorCode_ArgumentOutOfRange if the value exceeds the supported range, or
 *          ErrorCode_IO if the option cannot be applied.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpSocket_SetReceiveTimeout(TcpSocket* self, size_t milliseconds);

/**
 * @brief Sets a timeout for blocking send (write) operations on this socket.
 *
 * Applies to subsequent writes performed through the socket's IOStream. A value of 0 means "no timeout"
 * (block indefinitely). Once set, a write that exceeds the timeout fails with an I/O error.
 * @param self The connected socket. Must not be NULL and must not be closed.
 * @param milliseconds Timeout in milliseconds; 0 disables the timeout. Very large values that exceed the
 *                     platform's representable range are rejected.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          socket is closed, ErrorCode_ArgumentOutOfRange if the value exceeds the supported range, or
 *          ErrorCode_IO if the option cannot be applied.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpSocket_SetSendTimeout(TcpSocket* self, size_t milliseconds);

/**
 * @brief Gracefully shuts down both directions of the connection without freeing the socket.
 *
 * Disables further sends and receives on the connection, signaling the peer that no more data will follow,
 * and marks the socket's stream as at end-of-stream. The socket object itself is not released and must still
 * be passed to TcpSocket_Deconstruct. Calling this when the socket has already been shut down is a safe no-op
 * that returns success.
 * @param self The connected socket. Must not be NULL and must not be closed.
 * @returns Success (including when already shut down), or ErrorCode_IllegalArgument if @p self is NULL,
 *          ErrorCode_InvalidOperation if the socket is closed, or ErrorCode_IO if the native shutdown fails.
 */
Error TcpSocket_Shutdown(TcpSocket* self);

/**
 * @brief Closes the connection (if still open) and releases the socket and its stream.
 *
 * Closes the underlying connection if it has not already been closed, frees the socket's owned IOStream, and
 * frees the socket itself. Passing NULL is a safe no-op. After this call the socket pointer and any IOStream
 * obtained from it must not be used again.
 * @param self The socket to release, or NULL.
 * @returns Success, or ErrorCode_IO if closing the underlying socket fails (in which case the socket and its
 *          stream are not freed and the call may be retried).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error TcpSocket_Deconstruct(TcpSocket* self);



/**
 * @brief Creates an unbound UDP (datagram) socket of the given address family.
 *
 * The socket can immediately send datagrams; to receive on a known port, bind it first with UdpSocket_Bind.
 * On success a new UdpSocket is written to *outSocket and ownership transfers to the caller (release with
 * UdpSocket_Deconstruct); on failure *outSocket is NULL.
 * @param family Address family for the socket (SocketFamily_IPv4 or SocketFamily_IPv6); destinations and
 *               senders must match this family.
 * @param outSocket [out] Receives the new socket on success, or NULL on failure. Must not be NULL.
 * @returns Success, or ErrorCode_IllegalArgument if @p outSocket is NULL or @p family is not supported,
 *          ErrorCode_IO if the native socket cannot be created, or ErrorCode_Construct if the socket cannot
 *          be allocated.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UdpSocket_Create(SocketFamily family, UdpSocket** outSocket);

/**
 * @brief Binds the UDP socket to a local port so it can receive datagrams addressed to that port.
 *
 * Binds on all local interfaces of the socket's family. Typically called once before UdpSocket_Receive.
 * @param self The UDP socket. Must not be NULL and must not be closed.
 * @param port Local port to bind, in host byte order. 0 requests an OS-chosen ephemeral port.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          socket is closed, or ErrorCode_IO if the bind fails (for example the port is already in use).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UdpSocket_Bind(UdpSocket* self, uint16_t port);

/**
 * @brief Sends a single UDP datagram to a destination endpoint.
 *
 * Transmits the @p data buffer as one datagram. The destination's family must match the socket's family.
 * The send must place all @p size bytes in a single datagram; a partial send is reported as an error.
 * Sending @p size == 0 succeeds without transmitting anything.
 * @param self The UDP socket. Must not be NULL and must not be closed.
 * @param destination Endpoint to send to; its family must equal the socket's family. Must not be NULL.
 * @param data Pointer to the bytes to send. May be NULL only when @p size is 0. The buffer is read, not
 *             retained.
 * @param size Number of bytes to send. May exceed practical datagram limits, in which case the underlying
 *             send fails. On platforms where the value exceeds the native transfer-size limit it is rejected.
 * @returns Success, or ErrorCode_IllegalArgument if @p self or @p destination is NULL, if @p data is NULL
 *          while @p size > 0, or if the destination family does not match the socket; ErrorCode_InvalidOperation
 *          if the socket is closed; ErrorCode_ArgumentOutOfRange if @p size exceeds the native size limit; or
 *          ErrorCode_IO if the send fails or fewer than @p size bytes are transmitted.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UdpSocket_Send(UdpSocket* self, const SocketAddress* destination, const void* data, size_t size);

/**
 * @brief Receives a single UDP datagram into a buffer, blocking until one arrives, and reports the sender.
 *
 * Reads at most one datagram, appending its bytes to @p buffer's remaining capacity and advancing the
 * buffer's element count. The number of bytes received is written to *outBytesRead (0 is valid for an empty
 * datagram). If @p outSender is non-NULL it receives a newly allocated SocketAddress identifying the sender
 * (ownership transfers to the caller; release with SocketAddress_Deconstruct). Blocks until a datagram is
 * available unless a receive timeout has been set with UdpSocket_SetReceiveTimeout. Note: a datagram larger
 * than the buffer's remaining capacity is truncated to fit (excess bytes for that datagram are discarded).
 * @param self The UDP socket. Must not be NULL and must not be closed.
 * @param buffer [out] Destination byte buffer (element size must be 1); received bytes are appended to its
 *               existing contents using its remaining capacity, so reserve capacity beforehand. Must not be
 *               NULL and must have at least one byte of free capacity.
 * @param outBytesRead [out] Receives the number of bytes placed into the buffer. Must not be NULL.
 * @param outSender [out] Optional; if non-NULL, receives a new SocketAddress for the datagram's sender (set
 *                  to NULL before the receive). Pass NULL if the sender is not needed.
 * @returns Success, or ErrorCode_IllegalArgument if @p self, @p buffer, or @p outBytesRead is NULL or
 *          @p buffer is not a byte buffer; ErrorCode_InvalidOperation if the socket is closed;
 *          ErrorCode_BufferTooSmall if @p buffer has no remaining capacity; or ErrorCode_IO if the receive
 *          fails (including when the configured receive timeout elapses).
 * @note Blocks the calling thread until a datagram arrives, the receive fails, or the timeout elapses.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UdpSocket_Receive(UdpSocket* self, GenericBuffer* buffer, size_t* outBytesRead, SocketAddress** outSender);

/**
 * @brief Sets a timeout for blocking UdpSocket_Receive operations on this socket.
 *
 * A value of 0 means "no timeout" (block indefinitely). Once set, a receive that exceeds the timeout fails
 * with an I/O error instead of blocking forever.
 * @param self The UDP socket. Must not be NULL and must not be closed.
 * @param milliseconds Timeout in milliseconds; 0 disables the timeout. Very large values that exceed the
 *                     platform's representable range are rejected.
 * @returns Success, or ErrorCode_IllegalArgument if @p self is NULL, ErrorCode_InvalidOperation if the
 *          socket is closed, ErrorCode_ArgumentOutOfRange if the value exceeds the supported range, or
 *          ErrorCode_IO if the option cannot be applied.
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UdpSocket_SetReceiveTimeout(UdpSocket* self, size_t milliseconds);

/**
 * @brief Closes the socket (if still open) and releases it.
 *
 * Closes the underlying datagram socket if it has not already been closed and frees the socket. Passing NULL
 * is a safe no-op. After this call the pointer must not be used again.
 * @param self The socket to release, or NULL.
 * @returns Success, or ErrorCode_IO if closing the underlying socket fails (in which case the socket is not
 *          freed and the call may be retried).
 * @note May propagate errors from internal calls; consult the documentation of called functions for the full set.
 */
Error UdpSocket_Deconstruct(UdpSocket* self);
