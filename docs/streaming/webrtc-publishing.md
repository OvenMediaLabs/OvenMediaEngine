# WebRTC Streaming

OvenMediaEngine uses WebRTC to provide sub-second latency streaming. WebRTC uses RTP for media transmission and provides various extensions.

OvenMediaEngine provides the following features:

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>RTP / RTCP</td></tr><tr><td>Security</td><td>DTLS, SRTP</td></tr><tr><td>Transport</td><td>ICE</td></tr><tr><td>Error Correction</td><td>ULPFEC (VP8, H.264), In-band FEC (Opus)</td></tr><tr><td>Codec</td><td>VP8, H.264, H.265, Opus</td></tr><tr><td>Signaling</td><td>Self-Defined Signaling Protocol, Embedded WebSocket-based Server</td></tr><tr><td>Default URL Pattern</td><td><code>ws[s]://{OvenMediaEngine Host}[:{Signaling Port}/{App Name}/{Stream Name}/master</code></td></tr></tbody></table>

## Configuration

If you want to use the WebRTC feature, you need to add `<WebRTC>` element to the `<Publishers>` and `<Ports>` in the `Server.xml` configuration file, as shown in the example below.

```xml
<!-- /Server/Bind -->
<Publishers>
    ...
    <WebRTC>
        <Signalling>
            <Port>3333</Port>
            <TLSPort>3334</TLSPort>
            <WorkerCount>1</WorkerCount>
        </Signalling>
        <IceCandidates>
            <!-- Use a specific IP or ${PublicIP}, NOT *.                              -->
            <!-- * advertises every network interface (docker, VPN, etc.) as a         -->
            <!-- candidate, which slows down ICE negotiation on the browser side.      -->
            <!-- ${PublicIP} is auto-resolved via <StunServer> at startup.             -->
            <!-- Use a single port and raise IceWorkerCount for throughput scaling     -->
            <!-- instead of adding more ports.                                         -->
            <IceCandidate>${PublicIP}:10000/udp</IceCandidate>
            <IceCandidate>${PublicIP}:3479/tcp</IceCandidate>   <!-- Direct TCP ICE (RFC 6544) -->
            <TcpRelay>${PublicIP}:3478</TcpRelay>               <!-- TURN relay (WebRTC/TCP via TURN) -->
            <TcpRelayForce>false</TcpRelayForce>
            <IceWorkerCount>4</IceWorkerCount>           <!-- Increase for high viewer count -->
            <TcpIceWorkerCount>1</TcpIceWorkerCount>     <!-- Worker threads for Direct TCP ICE -->
            <TcpRelayWorkerCount>1</TcpRelayWorkerCount> <!-- Worker threads for TURN relay -->
            <DefaultTransport>udptcp</DefaultTransport>  <!-- udptcp (default) | udp | tcp | relay | all -->
        </IceCandidates>
    </WebRTC>
    ...
</Publishers>
```

### ICE

WebRTC uses ICE for connections and specifically NAT traversal. The web browser or player exchanges the ICE candidates with each other in the signalling phase.

OvenMediaEngine supports two types of ICE candidates:

| Type | Configuration | Description |
|---|---|---|
| UDP host | `<IceCandidate>IP:port/udp</IceCandidate>` | Standard UDP, lowest latency, preferred by browsers |
| Direct TCP ICE | `<IceCandidate>IP:port/tcp</IceCandidate>` | TCP connection direct to OME (RFC 6544, passive mode), no TURN relay needed |
| TURN relay | `<TcpRelay>IP:port</TcpRelay>` | Browser connects to embedded TURN server over TCP, works through strict firewalls |

The IP address in `<IceCandidate>` determines which addresses are advertised to browsers as ICE candidates. You can use:

- A specific IP: `<IceCandidate>203.0.113.1:10000/udp</IceCandidate>`
- `${PublicIP}` to auto-detect the public IP via the configured `<StunServer>`: `<IceCandidate>${PublicIP}:10000/udp</IceCandidate>`

{% hint style="danger" %}
**Do not use `*` for ICE candidate IP in production.**

When `*` is specified, OvenMediaEngine collects the IP address of every network interface on the host (including Docker bridge interfaces (`172.17.x.x`), VPN adapters, and other internal-only NICs) and advertises all of them as ICE candidates to the browser. The browser will attempt connectivity checks against every single candidate. This significantly increases ICE negotiation time and can cause connection delays or failures when the browser cannot reach those internal addresses.
{% endhint %}

When no `?transport` query parameter is specified, OvenMediaEngine sends **all configured candidates** (UDP, TCP, and TURN relay info if set) to the player. The browser then tries them in priority order: UDP first, then direct TCP, then TURN relay.

Each transport type has a dedicated worker-thread pool. You can tune the thread count independently:

| Configuration | Default | Applies to |
|---|---|---|
| `<IceWorkerCount>` | 1 | UDP ICE socket threads |
| `<TcpIceWorkerCount>` | 1 | Direct TCP ICE socket threads (RFC 6544) |
| `<TcpRelayWorkerCount>` | 1 | TURN relay socket threads |

For most deployments the default of `1` is fine. Increase `<IceWorkerCount>` / `<TcpIceWorkerCount>` when serving many simultaneous viewers on a multi-core server.

> **Note:** `<IceWorkerCount>` and `<TcpIceWorkerCount>` are independent. Each defaults to `1` when not set. Setting `<IceWorkerCount>` does **not** affect Direct TCP ICE sockets; set `<TcpIceWorkerCount>` separately to tune the Direct TCP ICE thread count.
>
> The worker count applies **per port**. For example, `IceWorkerCount=4` with a single UDP port creates **4** UDP ICE threads.
>
> **Prefer a single port with a higher `<IceWorkerCount>` over multiple ports.** Adding more ports multiplies the thread count (`N ports x IceWorkerCount`) and, more importantly, multiplies the number of ICE candidates advertised to clients, which slows down ICE negotiation. For throughput scaling, increase `<IceWorkerCount>` on a single port instead.

### Signalling

OvenMediaEngine has embedded a WebSocket-based signalling server and provides our defined signalling protocol. Also, OvenPlayer supports our signalling protocol. WebRTC requires signalling to exchange Offer SDP and Answer SDP, but this part isn't standardized. If you want to use SDP, you need to create your exchange protocol yourself.

If you want to change the signaling port, change the value of `<Ports><WebRTC><Signalling>`.

#### Signalling Protocol

The Signalling protocol is defined in a simple way:

![](<../.gitbook/assets/image (3) (1) (1) (1) (1).png>)

If you want to use a player other than OvenPlayer, you need to develop the signalling protocol as shown above and can integrate OvenMediaEngine.

## Streaming

### Publisher

Add `<WebRTC>`  to `<Publisher>` to provide streaming through WebRTC.

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Publishers>
    ...
    <WebRTC>
        <Timeout>30000</Timeout>
        <BandwidthEstimation>TransportCC</BandwidthEstimation> <!-- REMB | ALL -->
        <Rtx>false</Rtx>
        <Ulpfec>false</Ulpfec>
        <JitterBuffer>false</JitterBuffer>
    </WebRTC>
    ...
</Publishers>
```

<table><thead><tr><th width="189">Option</th><th width="433.33333333333326">Description</th><th>Default</th></tr></thead><tbody><tr><td><code>Timeout</code></td><td>ICE (STUN request/response) timeout as milliseconds, if there is no request or response during this time, the session is terminated.</td><td><code>30000</code></td></tr><tr><td><code>Rtx</code></td><td>WebRTC retransmission, a useful option in WebRTC/udp, but ineffective in WebRTC/tcp.</td><td><code>false</code></td></tr><tr><td><code>Ulpfec</code></td><td>WebRTC forward error correction, a useful option in WebRTC/udp, but ineffective in WebRTC/tcp.</td><td><code>false</code></td></tr><tr><td><code>JitterBuffer</code></td><td>Audio and video are interleaved and output evenly, see below for details</td><td><code>false</code></td></tr><tr><td><code>BanswidthEstimation</code></td><td><p>Determines which method OvenMediaEngine uses to estimate the bandwidth of the connected player. This bandwidth estimation is required for WebRTC ABR when OME selects and sends an appropriate rendition to the player.</p><p>If <strong>TransportCC</strong> or <strong>REMB</strong> is set, only one method is used. If the default value <strong>All</strong> is set, both methods are included in the SDP offer, and the player operates according to its preference. Most modern browsers use Transport-cc by default in this case. Transport-cc provides more accurate bandwidth estimation.</p></td><td><code>All</code></td></tr></tbody></table>

{% hint style="info" %}
WebRTC Publisher's `<JitterBuffer>` is a function that evenly outputs A/V (interleave) and is useful when A/V synchronization is no longer possible in the browser (player) as follows.

* If the A/V sync is excessively out of sync, some browsers may not be able to handle this or it may take several seconds to synchronize.
* Players that do not support RTCP also cannot A/V sync.
{% endhint %}

### Encoding

WebRTC Streaming starts when a live source is inputted and a stream is created. Viewers can stream using OvenPlayer or players that have developed or applied the OvenMediaEngine Signalling protocol.

Also, the codecs supported by each browser are different, so you need to set the Transcoding profile according to the browser you want to support. For example, Safari for iOS supports H.264 but not VP8. If you want to support all browsers, please set up VP8, H.264, and Opus codecs in all transcoders.

WebRTC doesn't support AAC, so when trying to bypass transcoding RTMP input, audio must be encoded as opus. See the settings below.

```markup
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application/OutputProfiles -->
<OutputProfile>
    <Name>bypass_stream</Name>
    <OutputStreamName>${OriginStreamName}</OutputStreamName>
    <Encodes>
        <Audio>
            <Bypass>true</Bypass>
        </Audio>
        <Video>
            <Bypass>true</Bypass>
        </Video>
        <Video>
            <!-- vp8, h264 -->
            <Codec>vp8</Codec>
            <Width>1280</Width>
            <Height>720</Height>
            <Bitrate>2000000</Bitrate>
            <Framerate>30.0</Framerate>
        </Video>
        <Audio>
            <Codec>opus</Codec>
            <Bitrate>128000</Bitrate>
            <Samplerate>48000</Samplerate>
            <Channel>2</Channel>
        </Audio>
    </Encodes>
</OutputProfile>
```

{% hint style="info" %}
Some browsers support both H.264 and VP8 to send Answer SDP to OvenMediaEngine, but sometimes H.264 can't be played. In this situation, if you write the VP8 above the H.264 code line in the Transcoding profile setting, you can increase the priority of the VP8.

Using this manner so that some browsers, support H.264 but can't be played, can stream smoothly using VP8. This means that you can solve most problems with this method.
{% endhint %}

### Playback

If you created a stream as shown in the table above, you can play WebRTC on OvenPlayer via the following URL:

| Protocol                 | URL format                                                                                  |
| ------------------------ | ------------------------------------------------------------------------------------------- |
| WebRTC Signalling        | `ws://{OvenMediaEngine Host}[:{Signaling Port}/{App Name}/{Stream Name}[/{Playlist Name}]`  |
| Secure WebRTC Signalling | `wss://{OvenMediaEngine Host}[:{Signaling Port}/{App Name}/{Stream Name}[/{Playlist Name}]` |

If you use the default configuration, you can stream to the following URL:

* `ws://{OvenMediaEngine Host}:3333/app/stream`
* `wss://{OvenMediaEngine Host}:3333/app/stream`

We have prepared a test player to make it easy to check if OvenMediaEngine is working. Please see the [Test Player](../quick-start/test-player.md) chapter for more information.

## Adaptive Bitrates Streaming (ABR)

OvenMediaEnigne provides adaptive bitrates streaming over WebRTC. OvenPlayer can also play and display OvenMediaEngine's WebRTC ABR URL.

![](<../.gitbook/assets/image (39).png>)

### Create Playlist for WebRTC ABR

You can provide ABR by creating a `playlist` in `<OutputProfile>` as shown below. The URL to play the playlist is `ws[s]://{OvenMediaEngine Host}[:{Signaling Port}]/{App Name}/{Stream Name}/master`.

`<Playlist>/<Rendition>/<Video>` and `<Playlist>/<Rendition>/<Audio>` can connected using `<Encodes>/<Video>/<Name>` or `<Encodes>/<Audio>/<Name>`.

{% hint style="warning" %}
It is not recommended to use a \<Bypass>true\</Bypass> encode item if you want a seamless transition between renditions because there is a time difference between the transcoded track and bypassed track.
{% endhint %}

If `<Options>/<WebRtcAutoAbr>` is set to true, OvenMediaEngine will measure the bandwidth of the player session and automatically switch to the appropriate rendition.

Here is an example play URL for ABR in the playlist settings below. `wss://domain:13334/app/stream/master`

{% hint style="info" %}
Streaming starts from the top rendition of Playlist, and when Auto ABR is true, the server finds the best rendition and switches to it. Alternatively, the user can switch manually by selecting a rendition in the player.
{% endhint %}

```xml
<OutputProfiles>
<OutputProfile>
    <Name>default</Name>
    <OutputStreamName>${OriginStreamName}</OutputStreamName>

    <Playlist>
        <Name>for Webrtc</Name>
        <FileName>master</FileName>
        <Options>
            <WebRtcAutoAbr>false</WebRtcAutoAbr> 
        </Options>
        <Rendition>
            <Name>1080p</Name>
            <Video>1080p</Video>
            <Audio>opus</Audio>
        </Rendition>
        <Rendition>
            <Name>480p</Name>
            <Video>480p</Video>
            <Audio>opus</Audio>
        </Rendition>
        <Rendition>
            <Name>720p</Name>
            <Video>720p</Video>
            <Audio>opus</Audio>
        </Rendition>
    </Playlist>

    <Playlist>
        <Name>for llhls</Name>
        <FileName>master</FileName>
        <Rendition>
            <Name>480p</Name>
            <Video>480p</Video>
            <Audio>bypass_audio</Audio>
        </Rendition>
        <Rendition>
            <Name>720p</Name>
            <Video>720p</Video>
            <Audio>bypass_audio</Audio>
        </Rendition>
    </Playlist>
    
    <Encodes>
        <Video>
            <Name>bypass_video</Name>
            <Bypass>true</Bypass>
        </Video>
        <Video>
            <Name>480p</Name>
            <Codec>h264</Codec>
            <Width>640</Width>
            <Height>480</Height>
            <Bitrate>500000</Bitrate>
            <Framerate>30</Framerate>
        </Video>
        <Video>
            <Name>720p</Name>
            <Codec>h264</Codec>
            <Width>1280</Width>
            <Height>720</Height>
            <Bitrate>2000000</Bitrate>
            <Framerate>30</Framerate>
        </Video>
        <Video>
            <Name>1080p</Name>
            <Codec>h264</Codec>
            <Width>1920</Width>
            <Height>1080</Height>
            <Bitrate>5000000</Bitrate>
            <Framerate>30</Framerate>
        </Video>
        <Audio>
            <Name>bypass_audio</Name>
            <Bypass>True</Bypass>
        </Audio>
        <Audio>
            <Name>opus</Name>
            <Codec>opus</Codec>
            <Bitrate>128000</Bitrate>
            <Samplerate>48000</Samplerate>
            <Channel>2</Channel>
        </Audio>
    </Encodes>
</OutputProfile>
</OutputProfiles>
```

See the [Adaptive Bitrates Streaming](../transcoding/#adaptive-bitrates-streaming-abr) section for more details on how to configure renditions.

### Multiple codec support in Playlist

WebRTC can negotiate codecs with SDP to support more devices. Playlist can set rendition with different kinds of codec. And OvenMediaEngine includes only renditions corresponding to the negotiated codec in the playlist and provides it to the player.

{% hint style="warning" %}
If an unsupported codec is included in the Rendition, the Rendition is not used. For example, if the Rendition's Audio contains aac, WebRTC ignores the Rendition.
{% endhint %}

In the example below, it consists of renditions with H.264 and Opus codecs set and renditions with VP8 and Opus codecs set. If the player selects VP8 in the answer SDP, OvenMediaEngine creates a playlist with only renditions containing VP8 and Opus and passes it to the player.

```xml
<Playlist>
    <Name>for Webrtc</Name>
    <FileName>abr</FileName>
    <Options>
        <WebRtcAutoAbr>false</WebRtcAutoAbr> 
    </Options>
    <Rendition>
        <Name>1080p</Name>
        <Video>1080p</Video>
        <Audio>opus</Audio>
    </Rendition>
    <Rendition>
        <Name>480p</Name>
        <Video>480p</Video>
        <Audio>opus</Audio>
    </Rendition>
    <Rendition>
        <Name>720p</Name>
        <Video>720p</Video>
        <Audio>opus</Audio>
    </Rendition>
    
    <Rendition>
        <Name>1080pVp8</Name>
        <Video>1080pVp8</Video>
        <Audio>opus</Audio>
    </Rendition>
    <Rendition>
        <Name>480pVp8</Name>
        <Video>480pVp8</Video>
        <Audio>opus</Audio>
    </Rendition>
    <Rendition>
        <Name>720pVp8</Name>
        <Video>720pVp8</Video>
        <Audio>opus</Audio>
    </Rendition>
</Playlist>
```

## WebRTC over TCP

There are environments where the network speed is fast but UDP packet loss is abnormally high. In such an environment, WebRTC may not play normally. OvenMediaEngine supports two independent mechanisms for WebRTC over TCP:

| Mode | How it works | Configuration |
|---|---|---|
| **Direct TCP ICE** (RFC 6544) | Browser connects directly to OME over TCP, no relay, lower overhead | `<IceCandidate>IP:port/tcp</IceCandidate>` |
| **TURN relay** (RFC 8656) | Browser connects to OME's embedded TURN server over TCP. Useful for browsers or players that do not support Direct TCP ICE (RFC 6544) | `<TcpRelay>IP:port</TcpRelay>` |

Both modes can be enabled simultaneously. When the `?transport` query parameter is omitted, all configured candidates are sent to the player and the browser picks the best one automatically (UDP → Direct TCP → TURN relay, in priority order).

### Turn on TURN relay server

You can enable the embedded TURN server by setting `<TcpRelay>` in the WebRTC Bind.

> Example : `<TcpRelay>*:3478</TcpRelay>`

OME may sometimes not be able to get the server's public IP on its local interface (e.g. Docker or AWS). Specify the public IP for `Relay IP`. If `*` is used, the public IP obtained from [<StunServer>](../configuration/#stunserver) and all IPs from local interfaces are used. `<Port>` is the TCP port on which the TURN server listens.

```xml
<Server version="8">
    ...
    <StunServer>stun.l.google.com:19302</StunServer>
    <Bind>
        <Publishers>
            <WebRTC>
                ...
            <IceCandidates>
                <IceCandidate>*:10000-10005/udp</IceCandidate>
                <IceCandidate>*:3479/tcp</IceCandidate>  <!-- Direct TCP ICE (RFC 6544) -->
                <TcpRelay>Relay IP:3478</TcpRelay>       <!-- TURN relay -->
                <TcpRelayForce>false</TcpRelayForce>
                <IceWorkerCount>1</IceWorkerCount>         <!-- Worker threads for UDP ICE -->
                <TcpIceWorkerCount>1</TcpIceWorkerCount>   <!-- Worker threads for Direct TCP ICE -->
                <TcpRelayWorkerCount>1</TcpRelayWorkerCount> <!-- Worker threads for TURN relay -->
            </IceCandidates>
            </WebRTC>
        </Publishers>
    </Bind>
    ...
</Server>
```

{% hint style="info" %}
If `*` is used as the IP of `<TcpRelay>` and `<IceCandidate>`, all available candidates are generated and sent to the player, so the player tries to connect to all candidates until a connection is established. This can cause delay in initial playback. Therefore, specifying the `${PublicIP}` macro or IP directly may be more beneficial to quality.
{% endhint %}

{% hint style="info" %}
`<TcpForce>` has been renamed to `<TcpRelayForce>`. The old name is still accepted for backward compatibility. When set to `true`, TURN relay info is always sent to the player even without `?transport=relay`.
{% endhint %}

### `?transport` query parameter

The `?transport` query parameter controls which ICE candidates and TURN relay info (`iceServers`) are sent to the player. The browser/player uses `iceServers` to set up TURN relay; if `iceServers` is not sent, the browser cannot use TURN relay.

| Value | Direct ICE candidates sent | TURN relay info (`iceServers`) sent | Player behavior |
|---|---|---|---|
| (none) / `udptcp` | All configured (UDP + TCP) | No | UDP → Direct TCP (no relay) |
| `udp` | UDP only | No | UDP only |
| `tcp` | Direct TCP ICE only (RFC 6544) | No | Direct TCP only |
| `relay` | None | Yes | TURN relay only |
| `all` | All configured (UDP + TCP) | Yes | UDP → Direct TCP → TURN relay fallback |

When `?transport` is omitted, the behavior follows `<DefaultTransport>` (default: `udptcp`).

{% hint style="warning" %}
**Behavior change from previous versions**

In previous versions, `?transport=tcp` sent TURN relay info (`iceServers`) to the player and routed WebRTC/TCP traffic through the embedded TURN server. This behavior has changed:

- `?transport=tcp` now means **Direct TCP ICE** (RFC 6544) — a direct TCP connection to OvenMediaEngine, no relay.
- To use TURN relay over TCP (the previous `tcp` behavior), use **`?transport=relay`** instead.
{% endhint %}

{% hint style="info" %}
**Why is `iceServers` not sent by default?**

When the browser receives `iceServers` (TURN server information), it is configured as `iceTransportPolicy: "relay"` internally if no direct candidates are provided, or it will try relay alongside direct candidates if direct candidates are also present. However, some players or environments may have `iceTransportPolicy` pre-set to `"relay"`, which means the browser will **only** use TURN relay even if direct UDP/TCP candidates were provided. To enable the full fallback chain (UDP → Direct TCP → TURN relay), use `?transport=all`, which sends both direct candidates and `iceServers`.
{% endhint %}

{% hint style="info" %}
**Direct TCP ICE vs. TURN relay**

`?transport=tcp` now sends **Direct TCP ICE candidates only** (RFC 6544), not TURN relay. Direct TCP ICE connects the browser directly to OvenMediaEngine over TCP without any relay server. Most modern browsers support RFC 6544 Direct TCP ICE. To force TURN relay over TCP, use `?transport=relay`.
{% endhint %}

### `<DefaultTransport>` configuration

The `<DefaultTransport>` element sets the transport policy applied when no `?transport` query parameter is present in the URL. Valid values are `udptcp` (default), `udp`, `tcp`, `relay`, and `all`.

```xml
<IceCandidates>
    <IceCandidate>${PublicIP}:10000/udp</IceCandidate>
    <IceCandidate>${PublicIP}:3479/tcp</IceCandidate>
    <TcpRelay>${PublicIP}:3478</TcpRelay>
    <DefaultTransport>udptcp</DefaultTransport>  <!-- udptcp (default) | udp | tcp | relay | all -->
</IceCandidates>
```

| Value | Equivalent to |
|---|---|
| `udptcp` (default) | No `?transport` with `udptcp` — UDP + TCP direct, no relay info |
| `udp` | `?transport=udp` |
| `tcp` | `?transport=tcp` |
| `relay` | `?transport=relay` — relay only |
| `all` | `?transport=all` — UDP + TCP + relay fallback |

### WebRTC over TCP with OvenPlayer

You can restrict playback to Direct TCP ICE by attaching `?transport=tcp` to the play URL:

```
ws[s]://{OvenMediaEngine Host}[:{Signaling Port}]/{App Name}/{Stream Name}?transport=tcp
```

To use TURN relay (useful for environments where direct connections are blocked):

```
ws[s]://{OvenMediaEngine Host}[:{Signaling Port}]/{App Name}/{Stream Name}?transport=relay
```

To enable full fallback (UDP → Direct TCP → TURN relay):

```
ws[s]://{OvenMediaEngine Host}[:{Signaling Port}]/{App Name}/{Stream Name}?transport=all
```

OvenPlayer automatically sets `iceServers` using the TURN server information received via signaling.

### Custom player

If you are using a custom player, set `iceServers` like this:

```javascript
myPeerConnection = new RTCPeerConnection({
  iceServers: [
    {
      urls: "turn:Relay IP:Port?transport=tcp",
      username: "ome",
      credential: "airen"
    }
  ]
});
```

When sending `Request Offer` in the [signaling](webrtc-publishing.md#signalling-protocol) phase, if `<TcpRelay>` is configured, OvenMediaEngine includes `ice_servers` in the offer response by default. You can use this information to set `iceServers` in your `RTCPeerConnection`.

```json
{
  "command": "offer",
  "id": 506764844,
  "peer_id": 0,
  "sdp": { "..." },
  "candidates": [
    {"candidate": "candidate:0 1 UDP 2130706431 192.168.0.200 10000 typ host", "sdpMLineIndex": 0},
    {"candidate": "candidate:1 1 TCP 2124414975 192.168.0.200 3479 typ host tcptype passive", "sdpMLineIndex": 0}
  ],
  "ice_servers": [{"credential": "airen", "urls": ["turn:192.168.0.200:3478?transport=tcp"], "user_name": "ome"}],
  "iceServers":  [{"credential": "airen", "urls": ["turn:192.168.0.200:3478?transport=tcp"], "username": "ome"}],
  "code": 200
}
```
