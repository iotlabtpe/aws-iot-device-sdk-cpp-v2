// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "TcpForward.h"
#include <aws/crt/io/SocketOptions.h>

using namespace std;
//using namespace Aws::Iot::DeviceClient::Logging;
using namespace Aws::Crt;

namespace Aws
{
    namespace Iotsecuretunneling
    {
        TcpForward::TcpForward(    struct aws_event_loop_group *eventLoopGroup,
                                   Aws::Iotsecuretunneling::SecureTunnel *secureTunnel,
                                   Aws::Crt::Allocator *allocator,
                                   Aws::Crt::ByteCursor serviceID,
                                   uint16_t connectionID,
                                   bool isV1Protocal,
                                   uint16_t port)
            : mEventLoopGroup(eventLoopGroup),
              mSecureTunnel(secureTunnel),
              mAllocator(allocator),
              mServiceID(serviceID),
              mConnectionID(connectionID),
              mIsV1Protocal(isV1Protocal),
              mPort(port)
        {
            int result = AWS_OP_SUCCESS;

            AWS_ASSERT(eventLoopGroup);
            AWS_ASSERT(secureTunnel);
            AWS_ASSERT(allocator);

            AWS_ZERO_STRUCT(mSocket);

            Aws::Crt::Io::SocketOptions socketOptions;

            AWS_LOGF_DEBUG(AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "[TcpForward::%s]: %d, Create tcpforward and the event loop group is %p",
                            __func__, __LINE__,
                            (void *)mEventLoopGroup);

            result = aws_socket_init(&mSocket, allocator, &socketOptions.GetImpl());
            if ( AWS_OP_SUCCESS != result )
            {
                AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                "[TcpForward::%s]: %d, Socket init failed and the result is %d",
                                __func__, __LINE__, result );
            }

            result = aws_byte_buf_init(&mSendBuffer, allocator, 1);
            if ( AWS_OP_SUCCESS != result )
            {
                AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                "[TcpForward::%s]: %d, Byte buffer init failed and the result is %d",
                                __func__, __LINE__, result );
            }
        }

        TcpForward::~TcpForward()
        {
            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "[TcpForward::%s]: %d is called, connect status is %d..",
                            __func__, __LINE__, mConnected);

            if (mConnected)
            {
                aws_socket_close(&mSocket);
                aws_socket_clean_up(&mSocket);
                aws_byte_buf_clean_up(&mSendBuffer);
            }
        }

        int TcpForward::Connect()
        {
            int result = AWS_OP_SUCCESS;
            aws_socket_endpoint endpoint{};
            String localhost = "127.0.0.1";

            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING, "[TcpForward::%s]: %d is called..", __func__, __LINE__);

            snprintf(endpoint.address, AWS_ADDRESS_MAX_LEN, "%s", localhost.c_str());

            endpoint.port = mPort;

            aws_event_loop *eventLoop = aws_event_loop_group_get_next_loop( mEventLoopGroup );

            result = aws_socket_connect(&mSocket, &endpoint, eventLoop, sOnConnectionResult, this);
            if ( AWS_OP_SUCCESS != result )
            {
                AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                "[TcpForward::%s]: %d, Socket connect failed and the result is %d",
                                __func__, __LINE__, result );
            }

            return AWS_OP_SUCCESS;
        }

        int TcpForward::Disconnect()
        {
            int result = AWS_OP_SUCCESS;
            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "[TcpForward::%s]: %d is called, connect status is %d..",
                            __func__, __LINE__, mConnected);

            if (mConnected)
            {
                result = aws_socket_close(&mSocket);
                if ( AWS_OP_SUCCESS != result )
                {
                    AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                    "[TcpForward::%s]: %d, Socket close failed and the result is %d",
                                    __func__, __LINE__, result );
                }
                aws_socket_clean_up(&mSocket);
                aws_byte_buf_clean_up(&mSendBuffer);

                mConnected = false;
            }

            return AWS_OP_SUCCESS;
        }

        int TcpForward::SendData(const Aws::Crt::ByteCursor &data)
        {    
            int result = AWS_OP_SUCCESS;

            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING, "[TcpForward::%s]: %d is called..", __func__, __LINE__);

            if (!mConnected)
            {
                AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING, "Not connected yet. Saving the data to send");
                aws_byte_buf_append_dynamic(&mSendBuffer, &data);
                return 0;
            }

            AWS_LOGF_DEBUG(AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "--->[zzz]: %d, TcpForward::%s, Send data to socket and the data len = %d ",
                            __LINE__, __func__, (int)data.len);

            if (mDebugPrintData)
            {
                int i = 0;

                fprintf(stdout, "[TcpForward], Send data to socket:  \n");
                for(; i < (int)data.len; i++)
                {
                    fprintf(stdout, "0x%02x ", data.ptr[i]);
                }
                fprintf(stdout, "\n");
            }

            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "--->[zzz][TcpForward::%s]: %d, Send data to socket: id = %p fd = %d eventLoop = %p: write lendth = %d",
                            __func__, __LINE__,
                            (void *)&mSocket, mSocket.io_handle.data.fd, (void *)mSocket.event_loop, (int)data.len);

            result = aws_socket_write(&mSocket, &data, sOnWriteCompleted, this);
            if ( AWS_OP_SUCCESS != result )
            {
                AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                "[TcpForward::%s]: %d, Socket write failed and the result is %d",
                                __func__, __LINE__, result );
            }

            return AWS_OP_SUCCESS;
        }

        void TcpForward::sOnConnectionResult(struct aws_socket *socket, int error_code, void *user_data)
        {
            auto *self = static_cast<TcpForward *>(user_data);
            self->OnConnectionResult(socket, error_code);
        }
        
        void TcpForward::sOnWriteCompleted( struct aws_socket *socket, int error_code, size_t bytes_written, void *user_data)
        {
            auto *self = static_cast<TcpForward *>(user_data);
            self->OnWriteCompleted(socket, error_code, bytes_written);
        }
        
        void TcpForward::sOnReadable(struct aws_socket *socket, int error_code, void *user_data)
        {
            auto *self = static_cast<TcpForward *>(user_data);
            self->OnReadable(socket, error_code);
        }
        
        void TcpForward::OnConnectionResult(struct aws_socket *, int error_code)
        {
            int result = AWS_OP_SUCCESS;

            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "[TcpForward::%s]: %d is called with error code %d..",
                            __func__, __LINE__, error_code);

            if (!error_code)
            {
                result = aws_socket_subscribe_to_readable_events(&mSocket, sOnReadable, this);
                if ( AWS_OP_SUCCESS != result )
                {
                    AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                    "[TcpForward::%s]: %d, Socket subscribe to readable events failed and the result is %d",
                                    __func__, __LINE__, result );
                }

                AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                "--->[zzz][TcpForward::%s]: %d, subscribe to readable events: id = %p fd = %d eventLoop = %p",
                                __func__, __LINE__,
                                (void *)&mSocket, mSocket.io_handle.data.fd, (void *)mSocket.event_loop);

                mConnected = true;
                FlushSendBuffer();
            }
        }

        void TcpForward::OnWriteCompleted(struct aws_socket *, int error_code, size_t bytes_written) const
        {
            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "[TcpForward::%s]: %d is called with error code (%d), the written bytes is %d..",
                            __func__, __LINE__, error_code, (int) bytes_written);

            if (error_code)
            {
                AWS_LOGF_ERROR( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                                "TcpForward::OnWriteCompleted error_code = %d, bytes_written = %d",
                                error_code, bytes_written);
            }
        }

        void TcpForward::OnReadable(struct aws_socket *, int error_code)
        {
            Aws::Crt::ByteBuf everything; // For cumulating everything available
            Aws::Crt::ByteBuf chunk;
            constexpr size_t chunkCapacity = 2048;
            int result = AWS_OP_SUCCESS;

            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "[TcpForward::%s]: %d is called and the error code is %d..",
                            __func__, __LINE__, error_code);

            aws_byte_buf_init(&everything, mAllocator, 0);
            aws_byte_buf_init(&chunk, mAllocator, chunkCapacity);

            size_t amountRead = 0;
            do
            {
                aws_byte_buf_reset(&chunk, false);
                amountRead = 0;
                
                result = aws_socket_read(&mSocket, &chunk, &amountRead);
                if ( result == AWS_OP_SUCCESS && amountRead > 0)
                {
                    aws_byte_cursor chunkCursor = aws_byte_cursor_from_buf(&chunk);
                    aws_byte_buf_append_dynamic(&everything, &chunkCursor);
                }
            } while (amountRead > 0);

            AWS_LOGF_DEBUG( AWS_LS_IOTDEVICE_SECURE_TUNNELING,
                            "--->[zzz]: %d, TcpForward::%s Read data from socket and the data length is %d, isV1Protocal = %d",
                            __LINE__, __func__, everything.len, mIsV1Protocal);

            if (mDebugPrintData)
            {
                int i = 0;

                fprintf(stdout, "[TcpForward], Read data from socket:  \n");
                for(; i < (int)everything.len; i++)
                {
                    fprintf(stdout, "0x%02x ", everything.buffer[i]);
                }
                fprintf(stdout, "\n");
            }

            fprintf(stdout, "[TcpForward], Send data to secure tunnel...\n\n");

            std::shared_ptr<Message> message = std::make_shared<Message>(aws_byte_cursor_from_buf(&everything));

            if ( mIsV1Protocal )
            {
                // Send everything
                mSecureTunnel->SendData(aws_byte_cursor_from_buf(&everything)); 
            }
            else
            {
                message->WithServiceId(mServiceID);
                message->WithConnectionId(mConnectionID);

                // Send everything
                mSecureTunnel->SendMessage(message); 
            }

            aws_byte_buf_clean_up(&chunk);
            aws_byte_buf_clean_up(&everything);
        }

        void TcpForward::FlushSendBuffer()
        {
            if (mConnected && mSendBuffer.len > 0)
            {
                aws_byte_cursor c = aws_byte_cursor_from_buf(&mSendBuffer);
                aws_socket_write(&mSocket, &c, sOnWriteCompleted, this);
                aws_byte_buf_reset(&mSendBuffer, false);
            }
        }
    }
}
