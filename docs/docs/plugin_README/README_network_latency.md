# network_latency

## Summary

The `network_latency` plugin is designed to measure latencies between client and server instances of ILLIXR. It does this by sending small packets (`ping`) from the client to the server. The server then sends an immediate reply (`pong`) to the client. When a reply `pong` is received by the client it calculates the latencies based on the round-trip time and internal data from the `pong` contents. The latency results are published to the `network_latency` topic. The default is to measure the latency every 100 milliseconds, but this can be changed by setting the `NETWORK_LATENCY_INTERVAL_MS` environment variable to your desired interval (in units of milliseconds).

The `network_latency.tx` plugin should be used on the client side and the `network_latency.rx` should be used on the server side.

!!! note

    These plugins can reliably measure round-trip latencies between any combination of operating systems. The one-way latencies are only accurate between clients and servers running the same operating system, with well correlated clocks.
