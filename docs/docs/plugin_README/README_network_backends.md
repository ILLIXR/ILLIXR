# Network Backends

ILLIXR supports several network backends. Each networked topic can be assigned to any one of the available types. There 
are currently network backends for TCP and UDP protocols. We recommend UPD for traffic where occasional packet loss is
acceptable (e.g., sending head poses at 120 Hz). We recommend TCP for traffic where delivery must be ensured (e.g., images).
One caveat to consider when deciding which protocol to use for a topic is packet size. Some platforms and operating systems
will allow several small TCP packets to accrue in the buffers (sending and/or receiving) before transmitting or releasing
the data. This can lead to bottlenecks in the data flow.

## How It Works

ILLIXR's networking model is a client / server type. In general, there is no difference between the client or server, other than which one initiates the connection. The server will start and wait for connections, while the client will start and reach out to initiate the connection. Both the client and server plugins will hold the entire ILLIXR system until a connection is made. Once connected, any topic that requests a network writer (from either client or server) will also automatically register that topic on the opposite end for receiving.

When registering a topic to be sent over the network, it is only registered on the writer side. On the reader side, the network backend receives, deserializes the data, and publishes the result to the switchboard. Any plugin on the reader side that needs the data just needs to use either `get_reader` or `get_buffered_reader`. 

Topics registered with a network writer cannot also be seen by other plugins on the same end of the connection (e.g., locally running plugins).

## Configuration

### Topic Configuration

To create a networked topic, use the `get_network_writer` from the switchboard. In addition to the topic name, the `get_network_writer`
call takes a `network::topic_config` argument. The `topic_config` struct is used to set the parameters for the given topic:

  - priority (enum): the priority of this topic compared to other topics using the given backend, not supported by all 
    backends. Values are `LOWEST`, `LOW`, `MEDIUM`(default), `HIGH`, `HIGHEST`.
  - retransmit (bool): whether to retransmit lost packets, not supported by all backends; TCP will always retransmit
    while UDP will not.
  - allow_out_of_order(bool): whether out-of-order packets are allowed (not currently implemented in any backend).
  - packetization (enum): the acceptable latency for this topic using the given backend, not supported by all backends
    Values are `IMMEDIATE`, `DEFAULT`, `SUGGEST_LATENCY`.
  - serialization_method (enum): The serialization method to use for the topic. Values are `BOOST`(default) and `PROTOBUF`.
    If `BOOST` is used, ensure that the necessary serialization code for the topic's data type is in `include/illixr/data_format/serialization` ans `utils/serialization`. If `PROTOBUF` is used, ensure that the necessary `.proto` file exists and is built for the particular plugin.
  - transport_method (enum): the transport method to use, currently `TCP`(default) or `UDP`.

### Network Backend Configuration

The network backends also require configuration. Specifically, they need to be told what the IP address to connect to, what port to use, and whether they are the client or server. These are configured with environment variables.

#### IP Address

The network backends can be configured to all use the same IP address to connect to, or each can use their own.

  - `ILLIXR_SERVER_IP`: the IP address of the server machine; will bw used for all network connections if specified, overriding any backend-specific IP addresses.
  - `ILLIXR_CLIENT_IP`: the IP address of the client machine; will bw used for all network connections if specified, overriding any backend-specific IP addresses.
  - `ILLIXR_TCP_SERVER_IP`: the IP address of the server machine; will be used for TCP network connections
  - `ILLIXR_UDP_SERVER_IP`: the IP address of the server machine; will be used for UDP network connections
  - `ILLIXR_TCP_CLIENT_IP`: the IP address of the client machine; will be used for TCP network connections
  - `ILLIXR_UDP_CLIENT_IP`: the IP address of the client machine; will be used for UDP network connections

#### Ports

A port on each machine should be specified for each of the network backend types being used. It is recommended to use different ports for each type to avoid collisions.

  - `ILLIXR_TCP_SERVER_PORT`: the port to use on the server for TCP connections
  - `ILLIXR_UDP_SERVER_PORT`: the port to use on the server for UDP connections
  - `ILLIXR_TCP_CLIENT_PORT`: the port to use on the client for TCP connections
  - `ILLIXR_UDP_CLIENT_PORT`: the port to use on the client for UDP connections

#### Client Selection

The `ILLIXR_IS_CLIENT` environment variable is used to determine if the backend instance is running as the client (value = "1") or server (value = "0").

### Examples

#### TCP with Protobuf

To configure a network topic to use TCP with Protobuf serialization:

``` c++
 combined_pose_writer_{switchboard_->get_network_writer<data_format::pose::combined_pose>("combined_pose", {.serialization_method=network::topic_config::PROTOBUF, .transport_method=network::topic_config::TCP})}
```

#### UDP with Boost

To configure a network topic to use UDP with Boost serialization:

``` c++
 combined_pose_writer_{switchboard_->get_network_writer<data_format::pose::combined_pose>("combined_pose", {.serialization_method=network::topic_config::BOOST, .transport_method=network::topic_config::UDP})}
```
