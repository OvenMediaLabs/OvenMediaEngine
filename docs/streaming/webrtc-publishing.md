# WebRTC Streaming

OvenMediaEngine uses WebRTC to provide sub-second latency streaming. WebRTC uses RTP for media transmission and provides various extensions.

OvenMediaEngine provides the following features:

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>RTP / RTCP</td></tr><tr><td>Security</td><td>DTLS, SRTP</td></tr><tr><td>Transport</td><td>ICE</td></tr><tr><td>Error Correction</td><td>ULPFEC (VP8, H.264), In-band FEC (Opus)</td></tr><tr><td>Codec</td><td>VP8, H.264, H.265, Opus</td></tr><tr><td>Signaling</td><td>Self-Defined Signaling Protocol, Embedded WebSocket-based Server</td></tr><tr><td>Additional Features</td><td>Simulcast</td></tr><tr><td>Default URL Pattern</td><td><code>ws[s]://{OvenMediaEngine Host}[:{Signaling Port}/{App Name}/{Stream Name}/master</code></td></tr></tbody></table>

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
            <IceCandidate>*:10000-10005/udp</IceCandidate>
            <TcpRelay>*:3478</TcpRelay>
            <TcpForce>true</TcpForce>
            <TcpRelayWorkerCount>1</TcpRelayWorkerCount>
        </IceCandidates>
    </WebRTC>
    ...
</Publishers>
```

### ICE

WebRTC uses ICE for connections and specifically NAT traversal. The web browser or player exchanges the Ice Candidate with each other in the Signalling phase. Therefore, OvenMediaEngine provides an ICE for WebRTC connectivity.

If you set `<IceCandidate>` to `*:10000-10005/udp`, as in the example above, OvenMediaEngine automatically gets IP from the server and generates `<IceCandidate>` using UDP ports from `10000` to `10005`. If you want to use a specific IP as IceCandidate, specify a specific IP. You can also use only one `10000` UDP Port, not a range, by setting it to `*: 10000`.

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
        <Rtx>false</Rtx>
        <Ulpfec>false</Ulpfec>
        <JitterBuffer>false</JitterBuffer>
    </WebRTC>
    ...
</Publishers>
```

| Option         | Description                                                                                                                          | Default |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------ | ------- |
| `Timeout`      | ICE (STUN request/response) timeout as milliseconds, if there is no request or response during this time, the session is terminated. | `30000` |
| `Rtx`          | WebRTC retransmission, a useful option in WebRTC/udp, but ineffective in WebRTC/tcp.                                                 | `false` |
| `Ulpfec`       | WebRTC forward error correction, a useful option in WebRTC/udp, but ineffective in WebRTC/tcp.                                       | `false` |
| `JitterBuffer` | Audio and video are interleaved and output evenly, see below for details                                                             | `false` |

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

There are environments where the network speed is fast but UDP packet loss is abnormally high. In such an environment, WebRTC may not play normally. WebRTC does not support streaming using TCP, but connections to the TURN ([https://tools.ietf.org/html/rfc8656](https://tools.ietf.org/html/rfc8656)) server support TCP. Based on these characteristics of WebRTC, OvenMediaEngine supports TCP connections from the player to OvenMediaEngine by embedding a TURN server.

### Turn on TURN server

You can turn on the TURN server by setting `<TcpRelay>` in the WebRTC Bind.

> Example : `<TcpRelay>*:3478</TcpRelay>`

OME may sometimes not be able to get the server's public IP to its local interface. (Environment like Docker or AWS) So, specify the public IP for `Relay IP`. If `*` is used, the public IP obtained from [\<StunServer>](../configuration/#stunserver) and all IPs obtained from the local interface are used. `<Port>` is the tcp port on which the TURN server is listening.

```markup
<Server version="8">
    ...
    <StunServer>stun.l.google.com:19302</StunServer>
    <Bind>
        <Publishers>
            <WebRTC>
                ...
                <IceCandidates>
                    <!-- <TcpRelay>*:3478</TcpRelay> -->
                    <TcpRelay>Relay IP:Port</TcpRelay>
                    <TcpForce>false</TcpForce>
                    <IceCandidate>*:10000-10005/udp</IceCandidate>
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

### WebRTC over TCP with OvenPlayer

WebRTC players can configure the TURN server through the [iceServers ](https://developer.mozilla.org/en-US/docs/Web/API/RTCIceServer/urls#a_single_ice_server_with_authentication)setting.

You can play the WebRTC stream over TCP by attaching the query `transport=tcp` to the existing WebRTC play URL as follows.

```markup
ws[s]://{OvenMediaEngine Host}[:{Signaling Port}]/{App Name}/{Stream Name}?transport=tcp
```

OvenPlayer automatically sets `iceServers` by obtaining TURN server information set in `<TcpRelay>` through signaling with OvenMediaEngine.

{% hint style="info" %}
If `<TcpForce>` is set to true, it will force a TCP connection even if `?transport=tcp` is not present. To use this, `<TcpRelay>` must be set.
{% endhint %}

### Custom player

If you are using custom player, set `iceServers` like this:

```markup
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

When sending `Request Offer` in the [signaling ](webrtc-publishing.md#signalling-protocol)phase with OvenMediaEngine, if you send the `transport=tcp` query string, `ice_servers` information is delivered as follows. You can use this information to set iceServers.

```markup
candidates: [{candidate: "candidate:0 1 UDP 50 192.168.0.200 10006 typ host", sdpMLineIndex: 0}]
code: 200
command: "offer"
ice_servers: [{credential: "airen", urls: ["turn:192.168.0.200:3478?transport=tcp"], user_name: "ome"}]
id: 506764844
peer_id: 0
sdp: {,…}
```
