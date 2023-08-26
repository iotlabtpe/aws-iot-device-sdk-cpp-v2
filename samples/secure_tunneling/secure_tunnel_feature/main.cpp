/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <chrono>
#include <thread>
#include <csignal>
#include <aws/iot/MqttClient.h>
#include <aws/iotsecuretunneling/IotSecureTunnelingClient.h>
#include <aws/iotsecuretunneling/SecureTunnel.h>
#include <aws/iotsecuretunneling/SecureTunnelingNotifyResponse.h>
#include <aws/iotsecuretunneling/SubscribeToTunnelsNotifyRequest.h>
#include "../../utils/CommandLineUtils.h"
#include "TcpForward.h"

using namespace std;
using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt;
using namespace Aws::Iotsecuretunneling;

std::shared_ptr<TcpForward> mTcpForward;

void createTcpForward(struct aws_event_loop_group *eventLoopGroup,
                            Aws::Iotsecuretunneling::SecureTunnel *secureTunnel,
                            Aws::Crt::Allocator *allocator,
                            Aws::Crt::ByteCursor serviceID,
                            uint16_t connectionID,
                            bool is_v1_protocal,
                            uint16_t port)
{
    mTcpForward = std::make_shared<TcpForward>(eventLoopGroup,
                                               secureTunnel,
                                               allocator,
                                               serviceID,
                                               connectionID,
                                               is_v1_protocal,
                                               port);
    mTcpForward->Connect();
}

int main(int argc, char *argv[])
{
    /************************ Setup ****************************/
    // Do the global initialization for the API.
    ApiHandle apiHandle;
    struct aws_allocator *allocator = aws_default_allocator();
    struct aws_event_loop_group *event_loop_group;

    aws_iotdevice_library_init(allocator);

    std::shared_ptr<SecureTunnel> secureTunnel;
    std::promise<bool> clientStoppedPromise;
    std::unique_ptr<Aws::Crt::Io::EventLoopGroup> mEventLoopGroup;
    std::unique_ptr<Aws::Crt::Io::DefaultHostResolver> mDefaultHostResolver;
    std::unique_ptr<Aws::Crt::Io::ClientBootstrap> mClientBootstrap;
    Aws::Crt::ByteBuf m_serviceIdStorage;

    // service id storage for use in sample
    AWS_ZERO_STRUCT(m_serviceIdStorage);
    Aws::Crt::Optional<Aws::Crt::ByteCursor> m_serviceId;

    /* Connection Id is used for Simultaneous HTTP Connections (Protocl V3) */
    uint32_t connectionId = 1;

    //  if the service id is null , used V1 protocal, otherwise is false
    bool isV1Protocal = true;

    /**
     * cmdData is the arguments/input from the command line placed into a single struct for
     * use in this sample. This handles all of the command line parsing, validating, etc.
     * See the Utils/CommandLineUtils for more information.
    */
    Utils::cmdData cmdData = Utils::parseSampleInputSecureTunnelFeature(argc, argv, &apiHandle);

    // Create the MQTT builder and populate it with data from cmdData.
    auto clientConfigBuilder = Aws::Iot::MqttClientConnectionConfigBuilder(cmdData.input_cert.c_str(), cmdData.input_key.c_str());

    clientConfigBuilder.WithEndpoint(cmdData.input_endpoint);

    if ( cmdData.input_ca != "" )
    {
        clientConfigBuilder.WithCertificateAuthority(cmdData.input_ca.c_str());
    }

    // Create the MQTT connection from the MQTT builder
    auto clientConfig = clientConfigBuilder.Build();
    if (!clientConfig)
    {
        fprintf( stderr, "Client Configuration initialization failed with error %s\n", Aws::Crt::ErrorDebugString(clientConfig.LastError()));
        exit(-1);
    }

    Aws::Iot::MqttClient client = Aws::Iot::MqttClient();
    auto connection = client.NewConnection(clientConfig);
    if (!*connection)
    {
        fprintf( stderr, "MQTT Connection Creation failed with error %s\n", Aws::Crt::ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    /**
     * In a real world application you probably don't want to enforce synchronous behavior
     * but this is a sample console application, so we'll just do that with a condition variable.
    */
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;

    // Invoked when a MQTT connect has completed or failed
    auto onConnectionCompleted = [&](Aws::Crt::Mqtt::MqttConnection &, int errorCode, Aws::Crt::Mqtt::ReturnCode returnCode, bool)
    {
        if (errorCode)
        {
            fprintf(stdout, "Connection failed with error %s\n", Aws::Crt::ErrorDebugString(errorCode));
            connectionCompletedPromise.set_value(false);
        }
        else
        {
            fprintf(stdout, "Connection completed with return code %d\n", returnCode);
            connectionCompletedPromise.set_value(true);
        }
    };

    // Invoked when a disconnect message has completed.
    auto onDisconnect = [&](Aws::Crt::Mqtt::MqttConnection &)
    {
        fprintf(stdout, "Disconnect completed\n");
        connectionClosedPromise.set_value();
    };

    // Assign callbacks
    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);

    /************************ Run the sample ****************************/

    // Connect
    fprintf(stdout, "Connecting...\n");
    if (!connection->Connect(cmdData.input_clientId.c_str(), true, 0))
    {
        fprintf(stderr, "MQTT Connection failed with error %s\n", ErrorDebugString(connection->LastError()));
        exit(-1);
    }

    auto onSubscribeToTunnelsNotifyResponse = [&](Aws::Iotsecuretunneling::SecureTunnelingNotifyResponse *response, int ioErr) -> void
    {        
        // The number of threads used depends on your use-case. IF you have a maximum of less than
        // a few hundred connections 1 thread is the ideal threadCount.
        mEventLoopGroup = unique_ptr<Aws::Crt::Io::EventLoopGroup>(new Aws::Crt::Io::EventLoopGroup(1, allocator));
        if (!mEventLoopGroup)
        {
            fprintf(stderr, "[ERROR] Create event loop group failed with error %s\n", ErrorDebugString(mEventLoopGroup->LastError()));
            exit(-1);
        }

        mDefaultHostResolver = unique_ptr<Aws::Crt::Io::DefaultHostResolver>(new Aws::Crt::Io::DefaultHostResolver(*mEventLoopGroup, 2, 30));
        if (!mDefaultHostResolver)
        {
            fprintf(stderr, "[ERROR] Create default host resolver failed with error %s\n", ErrorDebugString(mDefaultHostResolver->LastError()));
            exit(-1);
        }

        mClientBootstrap = unique_ptr<Aws::Crt::Io::ClientBootstrap>(new Aws::Crt::Io::ClientBootstrap(*mEventLoopGroup, *mDefaultHostResolver));
        if (!mClientBootstrap)
        {
            fprintf(stderr, "[ERROR] Create client bootstrap failed with error %s\n", ErrorDebugString(mClientBootstrap->LastError()));
            exit(-1);
        }

        if (ioErr == 0)
        {
            fprintf( stdout, "Received MQTT Tunnel Notification\n");
            fprintf( stdout,
                     "Recv:\n\tToken:%s\n\tMode:%s\n\tRegion:%s\n",
                     response->ClientAccessToken->c_str(),
                     response->ClientMode->c_str(),
                     response->Region->c_str());

            Aws::Crt::String region = response->Region->c_str();
            Aws::Crt::String endpoint = "data.tunneling.iot." + region + ".amazonaws.com";

            size_t nServices = response->Services->size();
            if (nServices <= 0)
            {
                fprintf(stdout, "\tNo Service Ids requested\n");
            }
            else
            {
                for (size_t i = 0; i < nServices; ++i)
                {
                    std::string service = response->Services->at(i).c_str();
                    fprintf(stdout, "\tService Id %zu=%s\n", (i + 1), service.c_str());
                }
            }

            SecureTunnelBuilder builder = SecureTunnelBuilder(  allocator,
                                                                *mClientBootstrap,
                                                                Aws::Crt::Io::SocketOptions(),
                                                                response->ClientAccessToken->c_str(),
                                                                AWS_SECURE_TUNNELING_DESTINATION_MODE,
                                                                endpoint.c_str());
            builder.WithOnConnectionSuccess([&](SecureTunnel *secureTunnel, const ConnectionSuccessEventData &eventData)
            {
                if (eventData.connectionData->getServiceId1().has_value())
                {
                    /* Store the service id for future use */
                    aws_byte_buf_clean_up(&m_serviceIdStorage);
                    AWS_ZERO_STRUCT(m_serviceIdStorage);
                    aws_byte_buf_init_copy_from_cursor(&m_serviceIdStorage, allocator,
                                                       eventData.connectionData->getServiceId1().value());
                    m_serviceId = aws_byte_cursor_from_buf(&m_serviceIdStorage);

                    fprintf( stdout, 
                             "Sending Stream Start request on Service Id:'" PRInSTR "' with Connection Id: (%d)\n",
                             AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId1().value()),
                             connectionId);

                    fprintf(stdout, "Secure Tunnel connected with Service IDs '" PRInSTR "'", AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId1().value()));

                    if (eventData.connectionData->getServiceId2().has_value())
                    {
                        fprintf(stdout, ", '" PRInSTR "'", AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId2().value()));
                        if (eventData.connectionData->getServiceId3().has_value())
                        {
                            fprintf( stdout, ", '" PRInSTR "'", AWS_BYTE_CURSOR_PRI(eventData.connectionData->getServiceId3().value()));
                        }
                    }
                    fprintf(stdout, "\n");
                }
                else
                {
                    fprintf(stdout, "Secure Tunnel connected with no Service IDs available\n");
                }
            });

            builder.WithOnStreamStarted( [&](SecureTunnel *secureTunnel, int errorCode, const StreamStartedEventData &eventData)
            {
                (void)secureTunnel;

                event_loop_group = mEventLoopGroup->GetUnderlyingHandle();

                if ( event_loop_group == nullptr)
                {
                    fprintf(stdout, "[ERROR] Event loop group is null and exit ...");
                    fprintf(stdout, "\n");
                    exit(-1);
                }

                if (!errorCode)
                {
                    fprintf(stdout, "Stream started and create tcpforward...\n");
                    if (eventData.streamStartedData->getServiceId().has_value())
                    {
                        fprintf(stdout, "Stream started on service id: '" PRInSTR, AWS_BYTE_CURSOR_PRI(eventData.streamStartedData->getServiceId().value()));
                        isV1Protocal = false;
                    }
                    else
                    {
                        isV1Protocal = true;
                    }

                    if (eventData.streamStartedData->getConnectionId() > 0)
                    {
                        fprintf(stdout, " with Connection Id: (%d)", eventData.streamStartedData->getConnectionId());
                    }
                    fprintf(stdout, "\n");
                    fprintf(stdout, "Create tcpforward, the port is %d and isV1Protocal is %d...\n",  cmdData.input_local_port, isV1Protocal);

                    createTcpForward( event_loop_group,
                                      secureTunnel,
                                      allocator,
                                      m_serviceId.value(),
                                      connectionId,
                                      isV1Protocal,
                                      cmdData.input_local_port);
                }
                else
                {
                    fprintf(stdout, "[ERROR] Stream Start failed with error code %d(%s)\n",
                                    errorCode, ErrorDebugString(errorCode));
                }
            });

            /* Add callbacks using the builder */
            builder.WithOnMessageReceived([&](SecureTunnel *secureTunnel, const MessageReceivedEventData &eventData)
            {
                std::shared_ptr<Message> message = eventData.message;

                /* Send an echo message back to the Source Device */
                std::shared_ptr<Message> echoMessage = std::make_shared<Message>(message->getPayload().value());

                fprintf(stdout, "Message received");
                if (message->getServiceId().has_value())
                {
                    fprintf(stdout, " on service id:'" PRInSTR "'", AWS_BYTE_CURSOR_PRI(message->getServiceId().value()));
                }
                
                if (message->getConnectionId() > 0)
                {
                    fprintf(stdout, " with connection id: (%d)", message->getConnectionId());
                }
                
                if (message->getPayload().has_value())
                {
                    fprintf(stdout, " with payload: '" PRInSTR "'", AWS_BYTE_CURSOR_PRI(message->getPayload().value()));
                }
                
                fprintf(stdout, "\n\n");

                /* Echo message on same service id received message arrived on */
                if (message->getServiceId().has_value())
                {
                    echoMessage->WithServiceId(message->getServiceId().value());
                }

                /* Echo message on the same connection id received message arrived on */
                if (message->getConnectionId() > 0)
                {
                    echoMessage->WithConnectionId(message->getConnectionId());
                }

                fprintf(stdout, "Sending Message data to tcpforward...\n");

                mTcpForward->SendData(message->getPayload().value());
            });

            builder.WithOnSendMessageComplete([&](SecureTunnel *secureTunnel, int errorCode, const SendMessageCompleteEventData &eventData)
            {
                (void)secureTunnel;
                (void)eventData;

                if (!errorCode)
                {
                    fprintf( stdout, "Message of type '" PRInSTR "' sent successfully\n", AWS_BYTE_CURSOR_PRI(eventData.sendMessageCompleteData->getMessageType()));
                }
                else
                {
                    fprintf(stdout, "Send Message failed with error code %d(%s)\n", errorCode, ErrorDebugString(errorCode));
                }
            });

            builder.WithOnConnectionStarted([&](SecureTunnel *secureTunnel, int errorCode, const ConnectionStartedEventData &eventData)
            {
                (void)secureTunnel;
                fprintf(stdout, "WithOnConnectionStarted....\n");

                if (!errorCode)
                {
                    fprintf(stdout, "Connection started...\n");

                    if (eventData.connectionStartedData->getServiceId().has_value())
                    {
                        fprintf( stdout, "Connection started on service id: '" PRInSTR, AWS_BYTE_CURSOR_PRI(eventData.connectionStartedData->getServiceId().value()));
                    }

                    if (eventData.connectionStartedData->getConnectionId() > 0)
                    {
                        fprintf(stdout, " with Connection Id: (%d)", eventData.connectionStartedData->getConnectionId());
                    }

                    fprintf(stdout, "\n");
                }
                else
                {
                    fprintf(stdout, "Connection Start failed with error code %d(%s)\n", errorCode, ErrorDebugString(errorCode));
                }
            });

            builder.WithOnStreamStopped([&](SecureTunnel *secureTunnel, const StreamStoppedEventData &eventData)
            {
                (void)secureTunnel;

                fprintf(stdout, "Stream Stoped...\n");
                if (eventData.streamStoppedData->getServiceId().has_value())
                {
                    fprintf(stdout, "Stream stoped on service id: '" PRInSTR, AWS_BYTE_CURSOR_PRI(eventData.streamStoppedData->getServiceId().value()));
                }
                fprintf(stdout, " stopped\n");
            });

            builder.WithOnConnectionShutdown([&]()
            { 
                fprintf(stdout, "Connection Shutdown\n");

                if ( mEventLoopGroup )
                {
                    mEventLoopGroup.reset();
                }
                
                if ( mDefaultHostResolver )
                {
                    mDefaultHostResolver.reset();
                }
                
                if ( mClientBootstrap )
                {
                    mClientBootstrap.reset();
                }
                
                if ( mTcpForward )
                {
                    mTcpForward->Disconnect();
                    mTcpForward.reset();
                }

                // Disconnect
                if (connection->Disconnect())
                {
                    connection.reset();
                    connectionClosedPromise.get_future().wait();
                }
                clientStoppedPromise.set_value(true);
            });

            builder.WithOnStopped([&](SecureTunnel *secureTunnel)
            {
                (void)secureTunnel;
                fprintf(stdout, "Secure Tunnel has entered Stopped State\n");
            });

            secureTunnel = builder.Build();

            if (!secureTunnel)
            {
                fprintf(stderr, "Secure Tunnel Creation failed: %s\n", ErrorDebugString(LastError()));
                exit(-1);
            }
            secureTunnel->Start();
        }
        else
        {
            fprintf(stderr, "MQTT Connection failed with error %d\n", ioErr);
        }
    };

    auto OnSubscribeComplete = [&](int ioErr) -> void
    {
        if (ioErr)
        {
            fprintf(stderr, "MQTT Connection failed with error %d\n", ioErr);
        }
    };

    if (connectionCompletedPromise.get_future().get())
    {
        SubscribeToTunnelsNotifyRequest request;
        request.ThingName = cmdData.input_thingName;

        IotSecureTunnelingClient secureClient(connection);
        secureClient.SubscribeToTunnelsNotify( request,
                                               AWS_MQTT_QOS_AT_LEAST_ONCE,
                                               onSubscribeToTunnelsNotifyResponse,
                                               OnSubscribeComplete);
    }

    if (clientStoppedPromise.get_future().get())
    {
        if ( secureTunnel )
        {
            fprintf(stderr, "secureTunnel will stop.\n");
            secureTunnel->Stop();
            std::this_thread::sleep_for(3000ms);
            secureTunnel.reset();
            exit(0);
        }
    }
}
