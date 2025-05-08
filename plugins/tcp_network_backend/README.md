# TCP_network_backend
## Summary
`TCP_network_backend` manages the transmission and reception of data published on networked topics (created by `get_network_writer` in Switchboard) through a TCP backend.
The users need to specify the IP address and port number of the server and client through setting some environment variables.
```
ILLIXR_TCP_SERVER_IP: IP address of the server.
ILLIXR_TCP_SERVER_PORT: Port number of the server.
ILLIXR_IS_CLIENT: Set to `1` if this process is acting as a client; otherwise, `0`.
```
The client will initiate connection to the server by specifying the IP and port number, and the server will accept the connection request.

Optionally, the client can set its IP address and port number.
```
ILLIXR_TCP_CLIENT_IP: IP address of the client.
ILLIXR_TCP_CLIENT_PORT: Port number of the client.
```
