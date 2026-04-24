# WebRTC / WHIP

Users can send video and audio from a web browser to OvenMediaEngine via WebRTC without requiring any plug-ins. In addition to browsers, any encoder that supports WebRTC transmission can also be used as a media source.

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>RTP / RTCP</td></tr><tr><td>Security</td><td>DTLS, SRTP</td></tr><tr><td>Transport</td><td>ICE</td></tr><tr><td>Error Correction</td><td>ULPFEC (VP8, H.264), In-band FEC (Opus)</td></tr><tr><td>Codec</td><td>VP8, H.264, H.265, Opus</td></tr><tr><td>Signaling</td><td>Self-Defined Signaling Protocol, Embedded WebSocket-based Server / WHIP</td></tr><tr><td>Additional Features</td><td>Simulcast</td></tr></tbody></table>

## Configuration

### Bind

OvenMediaEngine supports self-defined signaling protocol and [WHIP ](https://datatracker.ietf.org/doc/draft-ietf-wish-whip/)for WebRTC ingest.&#x20;

```xml
<!-- /Server/Bind -->
<Providers>
    ...
    <WebRTC>
        ...
        <Signalling>
            <Port>3333</Port>
            <TLSPort>3334</TLSPort>
        </Signalling>
        <IceCandidates>
            <!-- Use a specific IP or ${PublicIP}, NOT *.                              -->
            <!-- * advertises every network interface (docker, VPN, etc.) as a         -->
            <!-- candidate, which slows down ICE negotiation on the encoder side.      -->
            <!-- ${PublicIP} is auto-resolved via <StunServer> at startup.             -->
            <!-- Use a single port and raise IceWorkerCount for throughput scaling     -->
            <!-- instead of adding more ports.                                         -->
            <IceCandidate>${PublicIP}:10000/udp</IceCandidate>
            <IceCandidate>${PublicIP}:3479/tcp</IceCandidate>  <!-- Direct TCP ICE (RFC 6544) -->
            <TcpRelay>${PublicIP}:3478</TcpRelay>              <!-- TURN relay -->
            <TcpRelayForce>false</TcpRelayForce>
            <IceWorkerCount>4</IceWorkerCount>           <!-- Increase for high publisher count -->
            <TcpIceWorkerCount>1</TcpIceWorkerCount>     <!-- Worker threads for Direct TCP ICE -->
            <TcpRelayWorkerCount>1</TcpRelayWorkerCount> <!-- Worker threads for TURN relay -->
            <DefaultTransport>udptcp</DefaultTransport>  <!-- udptcp (default) | udp | tcp | relay | all -->
        </IceCandidates>
        ...
    </WebRTC>
    ...
</Providers>
```

You can set the port to use for signaling in `/<Server>/<Bind>/<Provider>/<WebRTC>/<Signalling>/<Port>` is for setting an unsecured HTTP port, and `<TLSPort>` is for setting a secured HTTP port that is encrypted with TLS.

For WebRTC ingest, you must set the ICE candidates of the OvenMediaEngine server to `<IceCandidates>`. The candidates set in `<IceCandidate>` are delivered to the WebRTC peer, and the peer requests communication with this candidate. Therefore, you must set the IP that the peer can access. If the IP is specified as `*`, OvenMediaEngine gathers all IPs of the server and delivers them to the peer.

{% hint style="danger" %}
**Do not use `*` for ICE candidate IP in production.**

When `*` is specified, OvenMediaEngine collects the IP address of every network interface on the host (including Docker bridge interfaces (`172.17.x.x`), VPN adapters, and other internal-only NICs) and advertises all of them as ICE candidates to the connecting encoder. The encoder will attempt connectivity checks against every single candidate. This significantly increases ICE negotiation time and can cause connection delays or failures when the encoder cannot reach those internal addresses.

**Always specify the exact IP address the encoder can reach:**

- Specific public IP: `<IceCandidate>203.0.113.1:10000/udp</IceCandidate>`
- Auto-detected public IP via STUN: `<IceCandidate>${PublicIP}:10000/udp</IceCandidate>` (requires `<StunServer>` to be configured in `Server.xml`)
{% endhint %}

OvenMediaEngine supports two types of ICE candidates:

| Type | Configuration | Description |
|---|---|---|
| UDP host | `<IceCandidate>IP:port/udp</IceCandidate>` | Standard UDP, lowest latency, preferred by browsers |
| Direct TCP ICE | `<IceCandidate>IP:port/tcp</IceCandidate>` | TCP connection direct to OME (RFC 6544, passive mode), no TURN relay needed |
| TURN relay | `<TcpRelay>IP:port</TcpRelay>` | Encoder connects to embedded TURN server over TCP, works through strict firewalls |

`<TcpRelay>` means OvenMediaEngine's built-in TURN Server. When this is enabled, the address of this turn server is passed to the peer via self-defined signaling protocol or WHIP, and the peer communicates with this turn server over TCP. This allows OvenMediaEngine to support WebRTC/TCP itself. For more information on URL settings, check out [WebRTC over TCP](webrtc.md#webrtc-over-tcp).

Each transport type has a dedicated worker-thread pool. You can tune the thread count independently:

| Configuration | Default | Applies to |
|---|---|---|
| `<IceWorkerCount>` | 1 | UDP ICE socket threads |
| `<TcpIceWorkerCount>` | 1 | Direct TCP ICE socket threads (RFC 6544) |
| `<TcpRelayWorkerCount>` | 1 | TURN relay socket threads |

For most deployments the default of `1` is fine. Increase `<IceWorkerCount>` / `<TcpIceWorkerCount>` when serving a large number of simultaneous WebRTC publishers on a multi-core server.

> **Note:** `<IceWorkerCount>` and `<TcpIceWorkerCount>` are independent. Each defaults to `1` when not set. Setting `<IceWorkerCount>` does **not** affect Direct TCP ICE sockets; set `<TcpIceWorkerCount>` separately to tune the Direct TCP ICE thread count.
>
> The worker count applies **per port**. For example, `IceWorkerCount=4` with a single UDP port creates **4** UDP ICE threads.
>
> **Prefer a single port with a higher `<IceWorkerCount>` over multiple ports.** Adding more ports multiplies the thread count (`N ports x IceWorkerCount`) and, more importantly, multiplies the number of ICE candidates advertised to clients, which slows down ICE negotiation. For throughput scaling, increase `<IceWorkerCount>` on a single port instead.

### Application

WebRTC input can be turned on/off for each application. As follows Setting enables the WebRTC input function of the application. The `<CrossDomains>` setting is used in WebRTC signaling.

```markup
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Providers>
    ...
    <WebRTC>
        <Timeout>30000</Timeout>
        <FIRInterval>3000</FIRInterval>
        <RtcpBasedTimestamp>false</RtcpBasedTimestamp>
        <CrossDomains>
            <Url>*</Url>
        </CrossDomains>
    </WebRTC>
    ...
</Providers>
```

<table data-header-hidden><thead><tr><th width="167"></th><th></th></tr></thead><tbody><tr><td>Timeout</td><td>The maximum duration (ms) to wait for an ICE Binding request/response before terminating the session due to connection loss.</td></tr><tr><td>FIRInterval</td><td>The interval (ms) for sending a Full Intra Request (FIR) to the sender to force the generation of an IDR Frame (setting this to 0 disables the request).</td></tr><tr><td>RtcpBasedTimestamp</td><td>When set to <code>false</code> (default), each track's RTP timestamp is counted independently from zero, with no waiting for RTCP Sender Reports. When set to <code>true</code>, RTCP Sender Reports are used to synchronize A/V timestamps on a common clock. Use <code>true</code> only if the sender is known to reliably send RTCP SR; otherwise the stream start may be delayed up to 5 seconds while waiting for the first SR.</td></tr><tr><td>CrossDomain</td><td>Specifies the allowed domains for signaling requests in compliance with Cross-Origin Resource Sharing (CORS) policies.</td></tr></tbody></table>



## URL Pattern

OvenMediaEnigne supports self-defined signaling protocol and WHIP for WebRTC ingest.

### Self-defined Signaling URL

The signaling URL for WebRTC ingest uses the query string `?direction=send` as follows to distinguish it from the url for WebRTC playback. Since the self-defined WebRTC signaling protocol is based on WebSocket, you must specify `ws`  or `wss` as the scheme.

> `ws[s]://<OME Host>[:Signaling Port]/<App Name>/<Stream Name>`**`?direction=send`**

### WHIP URL

For ingest from the WHIP client, put `?direction=whip` in the query string in the signaling URL as in the example below. Since WHIP is based on HTTP, you must specify `http` or `https` as the scheme.

> `http[s]://<OME Host>[:Signaling Port]/<App Name>/<Stream Name>`**`?direction=whip`**

### WebRTC over TCP

WebRTC transmission is sensitive to packet loss because it affects all encoders who access the stream. OvenMediaEngine supports two independent mechanisms for WebRTC/TCP ingest:

| Mode | How it works | Configuration |
|---|---|---|
| **Direct TCP ICE** (RFC 6544) | Encoder connects directly to OME over TCP, no relay, lower overhead | `<IceCandidate>IP:port/tcp</IceCandidate>` |
| **TURN relay** (RFC 8656) | Encoder connects to OME's embedded TURN server over TCP. Works through strict firewalls that block direct connections | `<TcpRelay>IP:port</TcpRelay>` |

Both modes can be enabled simultaneously. When no `?transport` parameter is specified, the behavior is determined by `<DefaultTransport>` (default: `udptcp`).

#### `?transport` query parameter

You can control which ICE candidates and TURN relay info (`iceServers`) are sent via the `?transport` query parameter:

| Value | Direct ICE candidates sent | TURN relay info (`iceServers`) sent | Encoder behavior |
|---|---|---|---|
| (none) / `udptcp` | All configured (UDP + TCP) | No | UDP → Direct TCP (no relay) |
| `udp` | UDP only | No | UDP only |
| `tcp` | Direct TCP ICE only (RFC 6544) | No | Direct TCP only |
| `relay` | None | Yes | TURN relay only |
| `all` | All configured (UDP + TCP) | Yes | UDP → Direct TCP → TURN relay fallback |

{% hint style="warning" %}
**Behavior change from previous versions**

In previous versions, `?transport=tcp` sent TURN relay info (`iceServers`) to the encoder and routed WebRTC/TCP traffic through the embedded TURN server. This behavior has changed:

- `?transport=tcp` now means **Direct TCP ICE** (RFC 6544) — a direct TCP connection to OvenMediaEngine, no relay.
- To use TURN relay over TCP (the previous `tcp` behavior), use **`?transport=relay`** instead.
{% endhint %}

{% hint style="info" %}
**Direct TCP ICE vs. TURN relay**

`?transport=tcp` sends **Direct TCP ICE candidates only** (RFC 6544) — the encoder connects directly to OvenMediaEngine over TCP without any relay server. Most modern browsers and encoders support RFC 6544 Direct TCP ICE well.

To force TURN relay over TCP, use `?transport=relay`. The encoder then connects to OvenMediaEngine's embedded TURN server over TCP, which works even through strict firewalls that block direct connections.

To enable the full fallback chain (UDP → Direct TCP → TURN relay), use `?transport=all`. This sends both direct candidates and `iceServers`. Note that if the encoder has `iceTransportPolicy` pre-set to `"relay"`, it will only use TURN relay even when direct candidates are provided — `?transport=all` ensures both paths are available for the encoder to choose from.
{% endhint %}

#### `<DefaultTransport>` configuration

The `<DefaultTransport>` element sets the transport policy applied when no `?transport` query parameter is present. Valid values: `udptcp` (default), `udp`, `tcp`, `relay`, `all`.

```xml
<IceCandidates>
    <IceCandidate>${PublicIP}:10000/udp</IceCandidate>
    <IceCandidate>${PublicIP}:3479/tcp</IceCandidate>
    <TcpRelay>${PublicIP}:3478</TcpRelay>
    <DefaultTransport>udptcp</DefaultTransport>  <!-- udptcp (default) | udp | tcp | relay | all -->
</IceCandidates>
```

> `ws[s]://{OME Host}[:{Signaling Port}]/{App Name}/{Stream Name}`**`?direction=send&transport=tcp`**
>
> `http[s]://{OME Host}[:{Signaling Port}]/{App Name}/{Stream Name}`**`?direction=whip&transport=tcp`**

{% hint style="warning" %}
To use TURN relay (`transport=relay`), `<TcpRelay>` must be configured in `<Bind>`.

`<TcpForce>` has been renamed to `<TcpRelayForce>`. The old name is still accepted for backward compatibility. When set to `true`, TURN relay info is always included in the response regardless of the `?transport` parameter.
{% endhint %}

## Simulcast

Simulcast is a feature that allows the sender to deliver multiple layers of quality to the end viewer without relying on a server encoder. This is a useful feature that allows for high-quality streaming to be delivered to viewers while significantly reducing costs in environments with limited server resources.

OvenMediaEngine supports WebRTC simulcast since 0.18.0. OvenMediaEngine only supports simulcast with WHIP signaling, and not with OvenMediaEngine's own signaling. Simulcast is only supported with WHIP signaling, and is not supported with OvenMediaEngine's own defined signaling.

You can test this using an encoder that supports WHIP and simulcast, such as OvenLiveKit or OBS. You can usually set the number of layers as below, and if you use the OvenLiveKit API directly, you can also configure the resolution and bitrate per layer.

<figure><img src="../.gitbook/assets/image (45).png" alt=""><figcaption></figcaption></figure>

<figure><img src="../.gitbook/assets/image (46).png" alt=""><figcaption></figcaption></figure>

<figure><img src="../.gitbook/assets/image (47).png" alt=""><figcaption></figcaption></figure>

### Playlist Template for Simulcast

When multiple input video Tracks exist, it means that several Tracks with the same Variant Name are present. For example, consider the following basic `<OutputProfile>` and assume there are three input video Tracks. In this case, three Tracks with the Variant Name `video_bypass` will be created:

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<OutputProfiles>
    ...
    <OutputProfile>
        <Name>stream</Name>
        <OutputStreamName>${OriginStreamName}</OutputStreamName>
        <Encodes>
            <Video>
                <Name>video_bypass</Name>
                <Bypass>true</Bypass>
            </Video>
        </Encodes>
    </OutputProfile>
    ...
</OutputProfiles>
```

How can we structure Playlists with multiple Tracks? A simple method introduces an `Index` concept in Playlists:

<pre class="language-xml"><code class="lang-xml">&#x3C;!-- /Server/VirtualHosts/VirtualHost/Applications/Application/OutputProfiles -->
<strong>&#x3C;OutputProfile>
</strong><strong>    ...
</strong>    &#x3C;Playlist>
        &#x3C;Name>simulcast&#x3C;/Name>
        &#x3C;FileName>template&#x3C;/FileName>
        &#x3C;Options>
            &#x3C;WebRtcAutoAbr>true&#x3C;/WebRtcAutoAbr>
            &#x3C;HLSChunklistPathDepth>0&#x3C;/HLSChunklistPathDepth>
            &#x3C;EnableTsPackaging>true&#x3C;/EnableTsPackaging>
        &#x3C;/Options>
        &#x3C;Rendition>
            &#x3C;Name>first&#x3C;/Name>
            &#x3C;Video>video_bypass&#x3C;/Video>
            &#x3C;VideoIndexHint>0&#x3C;/VideoIndexHint> &#x3C;!-- Optional, default : 0 -->
            &#x3C;Audio>aac_audio&#x3C;/Audio>
        &#x3C;/Rendition>
        &#x3C;Rendition>
            &#x3C;Name>second&#x3C;/Name>
            &#x3C;Video>video_bypass&#x3C;/Video>
            &#x3C;VideoIndexHint>1&#x3C;/VideoIndexHint> &#x3C;!-- Optional, default : 0 -->
            &#x3C;Audio>aac_audio&#x3C;/Audio>
            &#x3C;AudioIndexHint>0&#x3C;/AudioIndexHint> &#x3C;!-- Optional, default : 0 -->
        &#x3C;/Rendition>
    &#x3C;/Playlist>
    ...
&#x3C;/OutputProfile>
</code></pre>

`<VideoIndexHint>` and `<AudioIndexHint>` specify the Index of input video and audio Tracks, respectively.

However, when using the above configuration, if the encoder broadcasts 3 video tracks with Simulcast, it is inconvenient to change the configuration and restart the server to provide HLS/WebRTC streaming with 3 ABR layers. So I implemented a dynamic Rendition tool called RenditionTemplate.



### RenditionTemplate

The `<RenditionTemplate>` feature automatically generates Renditions based on specified conditions, eliminating the need to define each one manually. Here’s an example:

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application/OutputProfiles -->
<OutputProfile>
    ...
    <Playlist>
        <Name>template</Name>
        <FileName>template</FileName>
        <Options>
            <WebRtcAutoAbr>true</WebRtcAutoAbr>
            <HLSChunklistPathDepth>0</HLSChunklistPathDepth>
            <EnableTsPackaging>true</EnableTsPackaging>
        </Options>
        <RenditionTemplate>
            <Name>hls_${Height}p</Name>
            <VideoTemplate>
                <EncodingType>bypassed</EncodingType>
            </VideoTemplate>
            <AudioTemplate>
                <VariantName>aac_audio</VariantName>
            </AudioTemplate>
        </RenditionTemplate>
    </Playlist>
    ...
</OutputProfile>
```

This configuration creates Renditions for all bypassed videos and uses audio Tracks with the `aac_audio` Variant Name.\
The following macros can be used in the Name of a `RenditionTemplate`:\
`${Width}` | `${Height}` | `${Bitrate}` | `${Framerate}` | `${Samplerate}` | `${Channel}`

You can specify conditions to control Rendition creation. For example, to include only videos with a minimum resolution of 280p and bitrate above 500kbps, or to exclude videos exceeding 1080p or 2Mbps:

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application/OutputProfiles/OutputProfile/Playlist -->
<RenditionTemplate>
    <VideoTemplate>
        <EncodingType>bypassed</EncodingType> <!-- all, bypassed, encoded -->
        <VariantName>bypass_video</VariantName>
        <VideoIndexHint>0</VideoIndexHint>
        <MaxWidth>1080</MaxWidth>
        <MinWidth>240</MinWidth>
        <MaxHeight>720</MaxHeight>
        <MinHeight>240</MinHeight>
        <MaxFPS>30</MaxFPS>
        <MinFPS>30</MinFPS>
        <MaxBitrate>2000000</MaxBitrate>
        <MinBitrate>500000</MinBitrate>
    </VideoTemplate>
    <AudioTemplate>
        <EncodingType>encoded</EncodingType> <!-- all, bypassed, encoded -->
        <VariantName>aac_audio</VariantName>
        <MaxBitrate>128000</MaxBitrate>
        <MinBitrate>128000</MinBitrate>
        <MaxSamplerate>48000</MaxSamplerate>
        <MinSamplerate>48000</MinSamplerate>
        <MaxChannel>2</MaxChannel>
        <MinChannel>2</MinChannel>
        <AudioIndexHint>0</AudioIndexHint>
    </AudioTemplate>
    ...
</RenditionTemplate>
```

## WebRTC Producer

We provide a demo page so you can easily test your WebRTC input. You can access the demo page at the URL below.

{% embed url="https://demo.ovenplayer.com/demo_input.html" %}

![](<../.gitbook/assets/image (4) (1) (1).png>)

{% hint style="warning" %}
The getUserMedia API to access the local device only works in a [secure context](https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia#privacy_and_security). So, the WebRTC Input demo page can only work on the https site \*\*\*\* [**https**://demo.ovenplayer.com/demo\_input.html](https://demo.ovenplayer.com/demo_input.html). This means that due to [mixed content](https://developer.mozilla.org/en-US/docs/Web/Security/Mixed_content) you have to install the certificate in OvenMediaEngine and use the signaling URL as wss to test this. If you can't install the certificate in OvenMediaEngine, you can temporarily test it by allowing the insecure content of the demo.ovenplayer.com URL in your browser.
{% endhint %}

### Self-defined WebRTC Ingest Signaling Protocol

To create a custom WebRTC Producer, you need to implement OvenMediaEngine's Self-defined Signaling Protocol or WHIP. Self-defined protocol is structured in a simple format and uses the[ same method as WebRTC Streaming](../streaming/webrtc-publishing.md#signalling-protocol).

![](<../.gitbook/assets/image (10).png>)

When the player connects to ws\[s]://host:port/app/stream?**direction=send** through a WebSocket and sends a request offer command, the server responds with the offer SDP. The response includes all configured ICE candidates (UDP, direct TCP if configured) and, if `<TcpRelay>` is set, the `iceServers` field with TURN server information. You should pass this `iceServers` value to `RTCPeerConnection` to enable TURN relay. The player then `setsRemoteDescription` and `addIceCandidate` offer SDP, generates an answer SDP, and responds to the server.
