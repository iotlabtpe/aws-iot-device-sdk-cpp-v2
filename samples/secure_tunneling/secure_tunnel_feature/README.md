# Secure Tunnel Feature

[**Return to main sample list**](../../README.md)

This sample uses AWS IoT [Secure Tunneling](https://docs.aws.amazon.com/iot/latest/developerguide/secure-tunneling.html) Service to connect a destination Secure Tunnel Client to an AWS Secure Tunnel endpoint using access tokens using the [V3WebSocketProtocol](https://github.com/aws-samples/aws-iot-securetunneling-localproxy/blob/main/V3WebSocketProtocolGuide.md). For more information, see the [Secure Tunnel Userguide](../../../documents/Secure_Tunnel_Userguide.md)

## How to run

Create a new secure tunnel in the AWS IoT console (https://console.aws.amazon.com/iot/) (AWS IoT/Manage/Tunnels/Create tunnel). (https://docs.aws.amazon.com/iot/latest/developerguide/secure-tunneling-tutorial-open-tunnel.html). Once you have these tokens, you are ready to open a secure tunnel.

To run the sample, you can use the following command:

``` sh
./secure-tunnel-feature --endpoint <endpoint> --cert <your device certification> --key <your device key> --thing_name <thing name> --local_port <local port>  --verbosity <Trace or Debug or Info or Warn or Error or Fatal or None> --log_file <your log file, eg: log.txt>
```

