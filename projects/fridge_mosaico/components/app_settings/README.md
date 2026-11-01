# Device preferences

`app_settings_start()` starts one application-lifetime owner of the Wi-Fi station,
its netif and handlers. Call after NVS initialization. Shared default event loop
and netif services are left alive. This component depends on `app_fridge`'s portable
settings contract and IDF Wi-Fi, netif, events, NVS and FreeRTOS. `app_board` composes it.

Read snapshots under a mutex from any task; requests copy into a bounded queue and
return false when busy/unavailable. The worker performs scans, association and NVS
writes, so UI callbacks never wait for radio operations. Credentials are instance
buffers, never returned by snapshots or RPC. Wi-Fi uses RAM storage; only a successful
IP acquisition saves the candidate in the application's `fridge_cfg` namespace.
Failed attempts preserve the previously saved network. It retries connection up to
three times within 25 seconds, then returns an error; a saved network reconnects
after a delay. The list holds up to 12 visible SSIDs; hidden and enterprise network
provisioning are not implemented. The on-screen keyboard supports printable ASCII
personal-network passwords, 8–63 bytes; open networks connect without a password.

Volume is an atomic 0–100 target, default 45, persisted by the worker. The audio task
owns codec updates and explicit mute at zero. `app_settings_volume()` is nonblocking.
Initialization failure releases radio resources and leaves volume controls usable.
The worker and context live until reboot; dynamic destruction is intentionally absent.
NVS values survive app updates. They are not exported to logs or the host.

The PC adapter uses the computer's existing network and does not provision Wi-Fi.
`FRIDGE_SIM_TEST_NETWORKS=1` enables explicitly labeled test networks only for native
interaction acceptance; it never pretends to establish a real station connection.


Device configuration routes eligible Wi-Fi/LwIP allocations to PSRAM, bounds RX
buffers and disables optional Wi-Fi IRAM speed optimizations to retain internal
DMA memory. The board initializes audio before starting Wi-Fi; its settings handle
is published atomically. SDK configuration changes must also be applied to an
existing generated `build/sdkconfig`, since IDF defaults only affect unset values.
