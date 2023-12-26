// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TCPFORWARD_H
#define TCPFORWARD_H

#include <aws/crt/Types.h>
#include <aws/io/socket.h>
#include <aws/crt/Api.h>
#include <aws/iotsecuretunneling/SecureTunnel.h>
#include <aws/crt/Types.h>
#include <string>
#include <functional>

using namespace Aws::Crt;

namespace Aws
{
    namespace Iotsecuretunneling
    {
        // Client callback
        using OnTcpForwardDataReceive = std::function<void(const Aws::Crt::ByteBuf &data)>;
        using OnDataReceive = std::function<void(const Aws::Crt::ByteBuf &data)>;

        /**
         * \brief A class that represents a local TCP socket.
         * It implements all callbacks required by using aws_socket.
         */
        class TcpForward
        {
            public:
                /**
                 * \brief Constructor
                 *
                 * @param eventLoopGroup the event loop group
                 * @param secureTunnel is the secure tunnel object
                 * @param allocator used the allocator of crt 
                 * @param port the local TCP port to connect to local TCP port
                 */
                TcpForward(    struct aws_event_loop_group *eventLoopGroup,
                               Aws::Iotsecuretunneling::SecureTunnel *secureTunnel,
                               Aws::Crt::Allocator *allocator,
                               Aws::Crt::ByteCursor serviceID,
                               uint16_t connectionID,
                               bool isV1Protocal,
                               uint16_t port);
                /**
                 * \brief Destructor
                 */
                virtual ~TcpForward();

                // Non-copyable.
                TcpForward(const TcpForward &) = delete;
                TcpForward &operator=(const TcpForward &) = delete;

                /**
                 * \brief Connect to the local TCP socket
                 */
                virtual int Connect();
                
				/**
                 * \brief Disonnect to the local TCP socket
                 */
                virtual int Disconnect();

                /**
                 * \brief Send the given payload to the TCP socket
                 *
                 * @param data the payload to send
                 */
                virtual int SendData(const Aws::Crt::ByteCursor &data);

            private:
                //
                // static callbacks for aws_socket
                //

                /**
                 * \brief Callback when connection to a socket is complete
                 */
                static void sOnConnectionResult(struct aws_socket *socket, int error_code, void *user_data);

                /**
                 * \brief Callback when writing to the socket is complete
                 */
                static void sOnWriteCompleted( struct aws_socket *socket,
                                                     int error_code,
                                                     size_t bytes_written,
                                                     void *user_data);

                /**
                 * \brief Callback when the socket has data to read
                 */
                static void sOnReadable(struct aws_socket *socket, int error_code, void *user_data);

                //
                // Corresponding member callbacks for aws_socket
                //

                /**
                 * \brief Callback when connection to a socket is complete
                 */
                void OnConnectionResult(struct aws_socket *socket, int error_code);

                /**
                 * \brief Callback when writing to the socket is complete
                 */
                void OnWriteCompleted(struct aws_socket *socket, int error_code, size_t bytes_written) const;

                /**
                 * \brief Callback when the socket has data to read
                 */
                void OnReadable(struct aws_socket *socket, int error_code);

                /**
                 * \brief Flush any buffered data (saved before the socket is ready) to the socket
                 */
                void FlushSendBuffer();

                //
                // Member data
                //

                /**
                 * \brief The event loop group.
                 */
                struct aws_event_loop_group *mEventLoopGroup;

                /**
                 * \brief The secure tunnel.
                 */
                Aws::Iotsecuretunneling::SecureTunnel *mSecureTunnel;

                /**
                 * \brief The allocator.
                 */
                struct aws_allocator *mAllocator;

                /**
                 * \brief The service id.
                 */
                Aws::Crt::ByteCursor mServiceID;

                /**
                 * \brief The connecion id.
                 */
                uint16_t mConnectionID;

                /**
                 * \brief if v1 protocal the value is true, otherwise is false.
                 */
                bool mIsV1Protocal{true};

                /**
                 * \brief The local TCP port to connect to local TCP port
                 */
                uint16_t mPort;

                /**
                 * \brief An AWS SDK socket object. It manages the connection to the local TCP port.
                 */
                aws_socket mSocket{};

                /**
                 * \brief Is the socket connected yet?
                 */
                bool mConnected{false};

                /**
                 * \brief Is data printed?
                 */
                bool mDebugPrintData{false};

                /**
                 * \brief A buffer to store data from the secure tunnel. This is only used before the socket is
                 * connected.
                 */
                Aws::Crt::ByteBuf mSendBuffer;
        };     // class TcpForward
    }         // namespace Iotsecuretunneling
} // namespace Aws

#endif // TCPFORWARD_H
