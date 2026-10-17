# XYoxa — Universal RS-232/422/485 ↔ USB ↔ Ethernet Bridge

A serial interface bridge built on STM32F401 and W5500, with a separate ESP-07 Wi-Fi module for web-based configuration. Depending on the selected device pair, the adapter operates as a USB ↔ RS converter, an Ethernet ↔ RS converter, or a fully transparent RS ↔ Ethernet bridge.

---

## Capabilities

- **Four physical interfaces**: RS-232, RS-422, RS-485 (2-wire), RS-485 (4-wire).
- **Analog multiplexer switching**: exactly one of four RS channels is selected at a time (UART TTL / RS-232 / RS-422 / RS-485); only one transceiver is active at once.
- **USB mode**: the device enumerates as a virtual COM port (CDC ACM); data flows in both directions without loss and with byte order preserved.
- **Ethernet mode**: built-in TCP server (W5500), configurable port (default 5000), static IP or DHCP, configurable MAC/IP/mask/gateway/DNS.
- **Transparent RS ↔ Ethernet bridge mode**: bytes received from RS are sent to the TCP socket, and vice versa; in parallel, the byte stream is mirrored to the ESP module for the web terminal.
- **Flexible UART configuration**: baud rate (300…3,000,000), data bits (5–9), parity (None / Even / Odd), stop bits (1/2), hardware flow control.
- **RS-485/RS-422 direction control**: DE/RE switching driven by DMA transfer-complete interrupt, so the last byte is never truncated.
- **Wi-Fi web interface** (ESP-07, access point mode): interface and network configuration, theme selection, built-in terminal with color-coded line sources, Wi-Fi password and AP IP address change.
- **Two command entry points**: a line arriving over USB-CDC or the ESP UART with a magic prefix is interpreted as a command; data without the prefix is treated as payload and forwarded to the selected interface. The reply is returned to the same channel the command came from.
- **Status indication**: two LEDs — "process" (toggles in the main loop) and "fault" (lit when W5500 initialization fails, with automatic restart after 5 s).
- **Error resilience**: DMA+IDLE reception restart on UART error, line length limit, transmitter-busy timeouts, safe buffer overflow handling.

---

## How It Works

### Overall structure

The device consists of three functional blocks connected by a common core:

1. **STM32F401 core** — manages the RS interfaces, USB-CDC and the W5500 TCP socket, and decides how to route byte streams.
2. **W5500 network module** — provides a TCP connection to a remote client; communicates with the STM32 over SPI.
3. **ESP-07 Wi-Fi module** — brings up the access point and web server, exchanges data with the STM32 over UART at 921,600 baud, displays passing traffic and forwards configuration commands.

### Mode selection

The operating mode is determined by the device pair specified in the `CMD=SET` command:

- **COM (0) ↔ UART (2)** — USB-CDC ↔ direct TTL UART;
- **COM (0) ↔ RS-232 (3)** — USB-CDC ↔ RS-232;
- **COM (0) ↔ RS-485 2W (4)** — USB-CDC ↔ RS-485 (two-wire);
- **COM (0) ↔ RS-422 (5)** — USB-CDC ↔ RS-422;
- **COM (0) ↔ RS-485 4W (6)** — USB-CDC ↔ RS-485 (four-wire);
- **Ethernet (1) ↔ UART / RS-232 / RS-485 / RS-422** — TCP socket ↔ selected RS interface.

When an Ethernet pair is selected, the TCP server is started; when a COM pair is selected, the server is stopped. Intermediate states (socket close, UART deinit, transceiver shutdown) are performed before the new mode starts, eliminating conflicts.

### Data routing

Depending on the active mode, the byte stream is routed as follows:

- **USB ↔ RS mode**: a line received over USB-CDC is appended with `\n` and sent to the selected RS interface. Bytes received from RS are sent to USB-CDC and mirrored to the ESP with the `RX:` prefix.
- **Ethernet ↔ RS mode**: bytes from the TCP socket are forwarded to RS, bytes from RS are forwarded to the TCP socket; in parallel, a copy of each direction goes to the ESP with `TCP:` and `RX:` prefixes respectively.
- **USB ↔ Ethernet mode**: USB-CDC data is sent to the TCP socket, socket data is sent to USB-CDC. Each direction is mirrored to the ESP with `COM:` and `TCP:` prefixes.

Lines beginning with the magic sequence `69EF13A,` are recognized as commands and do **not** enter the payload. Everything else is treated as data.

### RS transceiver control

A 74HC4052D multiplexer selects one of four channels (UART, RS-232, RS-422, RS-485). In parallel with channel selection, the core drives the `DE`/`RE` lines of the corresponding transceivers:

- **RS-485 and RS-422**: before transmission the transceiver is set to TX mode (`DE=1`, `RE=1`); after the DMA transfer completes it returns to RX mode (`DE=0`, `RE=0`). Switching happens in the transfer-complete interrupt, so the last byte is not "cut off".
- **RS-232 and direct UART**: the 485/422 transceivers are held disabled, and only the corresponding multiplexer channel is active.

### Commands

A command has the form:
69EF13A,CMD=<command>[,<param>=<value>...]


Supported commands:

- `CMD=CONNECT` — liveness check (replies `OK`).
- `CMD=SET,DEV1=...,DEV2=...,...` — switch mode and apply parameters. Replies `OK` or `ERROR`.

`CONNECT` is used by the web interface and external clients to check that the bridge is alive; `SET` is used to apply new settings.

### Web interface and terminal

The ESP-07 brings up an access point (default SSID `XYoxa`) and a web server on port 80. The browser provides:

- **Home** — STM32 link status, navigation to sections;
- **COM** — RS interface selection and UART parameters;
- **Ethernet** — the same settings plus TCP server parameters (port, MAC, IP, mask, gateway, DNS);
- **XYrminal** — color terminal showing traffic in real time: violet for TCP, blue for RS, yellow for COM;
- **Settings** — theme, Wi-Fi password, AP IP address.

Every change in the web interface forms a `CMD=SET` command and sends it to the STM32; the reply (`OK`/`ERROR`) is shown to the user. The terminal receives data from the STM32 as a stream with `1:`/`2:`/`3:` prefixes (TCP/RX/COM) and renders it line by line with the corresponding color label; the buffer is bounded, with older lines displaced by newer ones.

### Network stack

The W5500 is initialized at startup: reset via the `RST` line, chip version check (`0x04`), network parameters applied, DHCP started if requested. The TCP server listens for connections; when a client connects, data is received in chunks of up to 1 KB and forwarded to the RS interface. On disconnect the socket is closed cleanly and restarted.

### Fault tolerance

- **UART**: on error, DMA+IDLE reception is restarted and the line is discarded; line length is limited to 256 bytes; on overflow the line is dropped and reception continues.
- **DMA transmitters**: only one transfer is active at a time; if the transmitter is busy, the new call waits up to 100–200 ms and is then dropped so as not to block the main loop.
- **Socket**: if the TCP socket closes unexpectedly, the server is restarted automatically; on client-side disconnect a clean `disconnect` is performed and the socket returns to listening.
- **Initialization**: if the W5500 does not respond or the chip version does not match, the device enters `Error_Handler`, indicates the fault, and restarts after 5 seconds.

---

## Supported Interfaces

| Interface | Electrical level     | Direction control       | Max speed      |
|-----------|----------------------|-------------------------|----------------|
| UART TTL  | 3.3 V                | —                       | up to 3 Mbaud  |
| RS-232    | ±5…±12 V (MAX232)    | —                       | up to 1 Mbaud  |
| RS-485 2W | Differential         | Automatic (DE/RE)       | up to 1 Mbaud  |
| RS-485 4W | Differential         | Automatic (DE/RE)       | up to 1 Mbaud  |
| RS-422    | Differential         | Automatic (DE/RE)       | up to 1 Mbaud  |
| USB CDC   | USB 2.0 Full Speed   | —                       | up to 12 Mbit/s |
| Ethernet  | 10/100 Mbit/s (W5500)| —                       | 100 Mbit/s     |