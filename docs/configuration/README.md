# Configuration

OvenMediaEngine has an XML configuration file. If you start OvenMediaEngine with `systemctl start ovenmediaengine`, the config file is loaded from the following path.

```bash
/usr/share/ovenmediaengine/conf/Server.xml
```

If you run it directly from the command line, it loads the configuration file from:

```bash
/<OvenMediaEngine Binary Path>/conf/Server.xml
```

If you run it in Docker container, the path to the configuration file is:

```markup
# For Origin mode
/opt/ovenmediaengine/bin/origin_conf/Server.xml
# For Edge mode
/opt/ovenmediaengine/bin/edge_conf/Server.xml
```

## Server

The `<Server>` is the root element of the configuration file. The `version`attribute indicates the version of the configuration file. OvenMediaEngine uses this version information to check if the config file is a compatible version.

```markup
<?xml version="1.0" encoding="UTF-8"?>
<Server version="8">
    <Name>OvenMediaEngine</Name>
    <IP>*</IP>
    <PrivacyProtection>false</PrivacyProtection>
    <StunServer>stun.l.google.com:19302</StunServer>
    <Bind>...</Bind>
    <VirtualHosts>...</VirtualHosts>
</Server>
```

### IP

```xml
<Server>
    ...
    <IP>*</IP>
    ...
</Server>
```

The `IP address` is OvenMediaEngine will bind to. If you set \*, all IP addresses of the system are used. If you enter a specific IP, the Host uses that IP only.

### PrivacyProtection

PrivacyProtection is an option to comply with GDPR, PIPEDA, CCPA, LGPD, etc. by deleting the client's personal information (IP, Port) from all records. When this option is turned on, the client's IP and Port are converted to `xxx.xxx.xxx.xxx:xxx` in all logs and REST APIs.

### StunServer

OvenMediaEngine needs to know its public IP in order to connect to the player through WebRTC. The server must inform the player of the `<IceCandidates>` and TURN server addresses when signaling, and this information must be the IP the player can connect to. However, in environments such as Docker or AWS, public IP cannot be obtained through a local interface, so a method of obtaining public IP using stun server is provided (available from version 0.11.1).

If OvenMediaEngine obtains the public IP through communication with the set stun server, you can set the public IP by using \* or `${PublicIP}` in `<IceCandidate>` and `<TcpRelay>`.

```xml
<Server>
    <StunServer>stun.l.google.com:19302</StunServer>
</Server>
```

## Bind

The `<Bind>` is the configuration for the server port that will be used. Bind consists of `<Providers>` and `<Publishers>`. The Providers are the server for stream input, and the Publishers are the server for streaming.

{% code overflow="wrap" %}
```xml
<Server>
    <!-- Settings for the ports to bind -->
    <Bind>
        <!-- Enable this configuration if you want to use API Server -->
        <!--
        <Managers>
            <API>
                <Port>8081</Port>
                <WorkerCount>1</WorkerCount>
            </API>
        </Managers>
        -->

        <Providers>
            <!-- Pull providers -->
            <RTSPC>
                <WorkerCount>1</WorkerCount>
            </RTSPC>
            <OVT>
                <WorkerCount>1</WorkerCount>
            </OVT>
            <!-- Push providers -->
            <RTMP>
                <Port>1935</Port>
                <WorkerCount>1</WorkerCount>
            </RTMP>
            <SRT>
                <Port>9999</Port>
                <WorkerCount>1</WorkerCount>
            </SRT>
            <MPEGTS>
                <!--
                    Listen on port 4000~4005 (<Port>4000-4004,4005/udp</Port>)
                    This is just a demonstration to show that you can configure the port in several ways
                -->
                <Port>4000/udp</Port>
            </MPEGTS>
            <WebRTC>
                <Signalling>
                    <Port>3333</Port>
                    <TLSPort>3334</TLSPort>
                    <WorkerCount>1</WorkerCount>
                </Signalling>

                <IceCandidates>
                    <IceCandidate>*:10000/udp</IceCandidate>
                    <!-- 
                        If you want to stream WebRTC over TCP, specify IP:Port for TURN server.
                        This uses the TURN protocol, which delivers the stream from the built-in TURN server to the player's TURN client over TCP. 
                        For detailed information, refer https://airensoft.gitbook.io/ovenmediaengine/streaming/webrtc-publishing#webrtc-over-tcp
                    -->
                    <TcpRelay>*:3478</TcpRelay>
                    <!-- TcpForce is an option to force the use of TCP rather than UDP in WebRTC streaming. (You can omit ?transport=tcp accordingly.) If <TcpRelay> is not set, playback may fail. -->
                    <TcpForce>true</TcpForce>
                    <TcpRelayWorkerCount>1</TcpRelayWorkerCount>
                </IceCandidates>
            </WebRTC>
        </Providers>

        <Publishers>
            <OVT>
                <Port>9000</Port>
                <WorkerCount>1</WorkerCount>
            </OVT>
            <LLHLS>
                <!-- 
                OME only supports h2, so LLHLS works over HTTP/1.1 on non-TLS ports. 
                LLHLS works with higher performance over HTTP/2, 
                so it is recommended to use a TLS port.
                -->
                <Port>3333</Port>
                <!-- If you want to use TLS, specify the TLS port -->
                <TLSPort>3334</TLSPort>
                <WorkerCount>1</WorkerCount>
            </LLHLS>
            <WebRTC>
                <Signalling>
                    <Port>3333</Port>
                    <TLSPort>3334</TLSPort>
                    <WorkerCount>1</WorkerCount>
                </Signalling>
                <IceCandidates>
                    <IceCandidate>*:10000-10005/udp</IceCandidate>
                    <!-- 
                        If you want to stream WebRTC over TCP, specify IP:Port for TURN server.
                        This uses the TURN protocol, which delivers the stream from the built-in TURN server to the player's TURN client over TCP. 
                        For detailed information, refer https://airensoft.gitbook.io/ovenmediaengine/streaming/webrtc-publishing#webrtc-over-tcp
                    -->
                    <TcpRelay>*:3478</TcpRelay>
                    <!-- TcpForce is an option to force the use of TCP rather than UDP in WebRTC streaming. (You can omit ?transport=tcp accordingly.) If <TcpRelay> is not set, playback may fail. -->
                    <TcpForce>true</TcpForce>
                    <TcpRelayWorkerCount>1</TcpRelayWorkerCount>
                </IceCandidates>
            </WebRTC>
        </Publishers>
    </Bind>
</Server>

```
{% endcode %}

The meaning of each element is shown in the following table:

| Element        | Description                                                                                                                                                                                                                                        |
| -------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Managers/API` | REST API Server port                                                                                                                                                                                                                               |
| `RTMP`         | RTMP port for incoming RTMP stream.                                                                                                                                                                                                                |
| `SRT`          | SRT port for incoming SRT stream                                                                                                                                                                                                                   |
| `MPEGTS`       | MPEGTS ports for incoming MPEGTS/UDP stream.                                                                                                                                                                                                       |
| `WebRTC`       | Port for WebRTC. If you want more information on the WebRTC port, see the [WebRTC Ingest](../live-source/webrtc.md) and [WebRTC Streaming](../streaming/webrtc-publishing.md) chapters.                                                            |
| `OVT`          | <p>OVT port for an origin server.</p><p>OVT is a protocol defined by OvenMediaEngine for Origin-Edge communication. For more information about Origin-Edge, see the <a href="../origin-edge-clustering.md">Origin-Edge Clustering</a> chapter.</p> |
| `LLHLS`        | HTTP(s) port for LLHLS streaming.                                                                                                                                                                                                                  |

## Virtual Host

`<VirtualHosts>` are a way to run more than one streaming server on a single machine. OvenMediaEngine supports IP-based virtual host and Domain-based virtual host. "IP-based" means that you can separate streaming servers into multiples by setting different IP addresses, and "Domain-based" means that even if the streaming servers use the same IP address, you can split the streaming servers into multiples by setting different domain names.

`<VirtualHosts>` consist of `<Name>`, `<Host>`, `<Origins>`, `<SignedPolicy>`, and `<Applications>`.

```xml
<!-- /Server -->
<VirtualHosts>
    <VirtualHost>
        <Name>default</Name>
        <Host>
        ...
        </Host>

        <Origins>
        ...
        </Origins>

        <SignedPolicy>
        ...
        </SignedPolicy>

        <Applications>
        ...
        </Applications>
    </VirtualHost>
</VirtualHosts>
```

### Host

The Domain has `<Names>` and `<TLS>`. `<Names>` can be either a domain or an IP address. Setting `*` means it allows all domains and IP addresses.

```xml
<!-- /Server/VirtualHosts -->
<VirtualHost>
    <Host>
        <Names>
            <!--
                You can specify domain names/IP addresses

                <Name>stream1.airensoft.com</Name>
                <Name>stream2.airensoft.com</Name>
                <Name>*.sub.airensoft.com</Name>
                <Name>192.168.0.160</Name>
            -->
            <Name>*</Name>
        </Names>
        <TLS>
            <CertPath>path/to/file.crt</CertPath>
            <KeyPath>path/to/file.key</KeyPath>
            <ChainCertPath>path/to/file.crt</ChainCertPath>
        </TLS>
    </Host>
</VirtualHost>
```

### SignedPolicy

`SignedPolicy` is a module that limits the user's privileges and time. For example, operators can distribute RTMP URLs that can be accessed for 60 seconds to authorized users, and limit RTMP transmission to 1 hour. The provided URL will be destroyed after 60 seconds, and transmission will automatically stop after 1 hour. Users who are provided with a SingedPolicy URL cannot access resources other than the provided URL. This is because the SignedPolicy URL is authenticated. See the [SignedPolicy](../access-control/signedpolicy.md) chapter for more information.

### Origins

Origins (also we called `OriginMap`) are a feature to pull streams from external servers. It now supports OVT and RTSP for the pulling protocols. OVT is a protocol defined by OvenMediaEngine for Origin-Edge communication. It allows OvenMediaEngine to relay a stream from other OvenMediaEngines that have OVP Publisher turned on. Using RTSP, OvenMediaEngine pulls a stream from an RTSP server and creates a stream. RTSP stream from external servers can stream by WebRTC, HLS, and MPEG-DASH.

The Origin has `<Location>` and `<Pass>` elements. Location is a URI pattern for incoming requests. If the incoming URL request matches Location, OvenMediaEngine pulls the stream according to a Pass element. In the Pass element, you can set the origin stream's protocol and URLs.

To run the Edge server, Origin creates application and stream if there isn't those when user request. For more learn about Origin-Edge, see the [Live Source](../live-source/) chapter.

```xml
<!-- /Server/VirtualHosts -->
<VirtualHost>
    <Origins>
        <Origin>
            <Location>/app/stream</Location>
            <Pass>
                <Scheme>ovt</Scheme>
                <Urls><Url>origin.com:9000/app/stream_720p</Url></Urls>
            </Pass>
        </Origin>
        <Origin>
            <Location>/app/</Location>
            <Pass>
                <Scheme>ovt</Scheme>
                <Urls><Url>origin.com:9000/app/</Url></Urls>
            </Pass>
        </Origin>
        <Origin>
            <Location>/rtsp/stream</Location>
            <Pass>
                <Scheme>rtsp</Scheme>
                <Urls><Url>rtsp-server.com:554/</Url></Urls>
            </Pass>
        </Origin>
        <Origin>
            <Location>/</Location>
            <Pass>
                <Scheme>ovt</Scheme>
                <Urls><Url>origin2.com:9000/</Url></Urls>
            </Pass>
        </Origin>
    </Origins>
</VirtualHost>
```

### Application

`<Application>` consists of various elements that can define the operation of the stream, including Stream input, Encoding, and Stream output. In other words, you can create as many `<Application>` as you like and build various streaming environments.

```markup
<!-- /Server/VirtualHosts/VirtualHost -->
<Applications>
    <Application>
        ...
    </Application>
    <Application>
        ...
    </Application>
    ...
</Applications>
```

`<Application>` needs to set `<Name>` and `<Type>` as follows:

```markup
<!-- /Server/VirtualHosts/VirtualHost/Applications -->
<Application>
    <Name>app</Name>
    <Type>live</Type>
    <OutputProfiles>...</OutputProfiles>
    <Providers>...</Providers>
    <Publishers>...</Publishers>
</Application>
```

* `<Name>` is used to configure the Streaming URL.
* `<Type>` defines the operation of `<Application>`. Currently, there is only a `live` type.

#### OutputProfiles

`<OutputProfile>` is a configuration that creates an output stream. Output stream name can be set with `<OutputStreamName>`, and transcoding properties can be set through `<Encodes>`. If you want to stream one input to multiple output streams, you can set multiple `<OutputProfile>`.

```markup
<!-- /Server/VirtualHosts/VirtualHost/Applications -->
<Application>
    <OutputProfiles>
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
                <Audio>
                    <Codec>opus</Codec>
                    <Bitrate>128000</Bitrate>
                    <Samplerate>48000</Samplerate>
                    <Channel>2</Channel>
                </Audio>
                <!--
                <Video>
                    <Codec>vp8</Codec>
                    <Bitrate>1024000</Bitrate>
                    <Framerate>30</Framerate>
                    <Width>1280</Width>
                    <Height>720</Height>
                </Video>
                -->
                ...
            </Encodes>
        </OutputProfile>
    </OutputProfiles>
</Application>
```

For more information about the `<OutputProfiles>`, please see the [Transcoding](../transcoding/) chapter.

#### Providers

`<Providers>` ingest streams that come from a media source.

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications -->
<Application>
    <Providers>
        <RTMP />
        <WebRTC />
        <SRT />
        <RTSPPull />
        <OVT />
        <MPEGTS>
            <StreamMap>
                ...
            </StreamMap>
        </MPEGTS>
    </Providers>
</Application>
```

If you want to get more information about the `<Providers>`, please refer to the [Live Source](../live-source/) chapter.

#### Publishers

You can configure the Output Stream operation in `<Publishers>``/<ThreadCount>` is the number of threads used by each component responsible for the `<Publishers>` protocol.

{% hint style="info" %}
You need many threads to transmit streams to a large number of users at the same time. So it's better to use a higher core CPU and set `<ThreadCount>` equal to the number of CPU cores.
{% endhint %}

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications -->
<Application>
    <Publishers>
        <OVT />
        <LLHLS />
        <WebRTC />
    </Publishers>
</Application>
```

​OvenMediaEngine currently supports WebRTC, Low-Latency DASH, MEPG-DASH, and HLS. If you don't want to use any protocol then you can delete that protocol setting, the component for that protocol isn't initialized. As a result, you can save system resources by deleting the settings of unused protocol components.

If you want to learn more about WebRTC, visit the [WebRTC Streaming](../streaming/webrtc-publishing.md) chapter. And if you want to get more information on Low-Latency DASH, MPEG-DASH, and HLS, refer to the chapter on [HLS & MPEG-DASH Streaming](broken-reference).

## Configuration Example

Finally, `Server.xml` is configured as follows:

```xml
<?xml version="1.0" encoding="UTF-8"?>

<Server version="8">
    <Name>OvenMediaEngine</Name>
    <!-- Host type (origin/edge) -->
    <Type>origin</Type>
    <!-- Specify IP address to bind (* means all IPs) -->
    <IP>*</IP>
    <PrivacyProtection>false</PrivacyProtection>

    <!--
        To get the public IP address(mapped address of stun) of the local server.
        This is useful when OME cannot obtain a public IP from an interface, such as AWS or docker environment.
        If this is successful, you can use ${PublicIP} in your settings.
    -->
    <StunServer>stun.l.google.com:19302</StunServer>

    <Modules>
        <!--
            Currently OME only supports h2 like all browsers do. Therefore, HTTP/2 only works on TLS ports.
        -->
        <HTTP2>
            <Enable>true</Enable>
        </HTTP2>

        <LLHLS>
            <Enable>true</Enable>
        </LLHLS>

        <!-- P2P works only in WebRTC and is experiment feature -->
        <P2P>
            <!-- disabled by default -->
            <Enable>false</Enable>
            <MaxClientPeersPerHostPeer>2</MaxClientPeersPerHostPeer>
        </P2P>
    </Modules>

    <!-- Settings for the ports to bind -->
    <Bind>
        <!-- Enable this configuration if you want to use API Server -->
        <!--
        <Managers>
            <API>
                <Port>8081</Port>
                <TLSPort>8082</TLSPort>
                <WorkerCount>1</WorkerCount>
            </API>
        </Managers>
        -->

        <Providers>
            <!-- Pull providers -->
            <RTSPC>
                <WorkerCount>1</WorkerCount>
            </RTSPC>
            <OVT>
                <WorkerCount>1</WorkerCount>
            </OVT>
            <!-- Push providers -->
            <RTMP>
                <Port>1935</Port>
                <WorkerCount>1</WorkerCount>
            </RTMP>
            <SRT>
                <Port>9999</Port>
                <WorkerCount>1</WorkerCount>
            </SRT>
            <MPEGTS>
                <!--
                    Listen on port 4000~4005 (<Port>4000-4004,4005/udp</Port>)
                    This is just a demonstration to show that you can configure the port in several ways
                -->
                <Port>4000/udp</Port>
            </MPEGTS>
            <WebRTC>
                <Signalling>
                    <Port>3333</Port>
                    <TLSPort>3334</TLSPort>
                    <WorkerCount>1</WorkerCount>
                </Signalling>

                <IceCandidates>
                    <IceCandidate>*:10000/udp</IceCandidate>
                    <!--
                        If you want to stream WebRTC over TCP, specify IP:Port for TURN server.
                        This uses the TURN protocol, which delivers the stream from the built-in TURN server to the player's TURN client over TCP.
                        For detailed information, refer https://airensoft.gitbook.io/ovenmediaengine/streaming/webrtc-publishing#webrtc-over-tcp
                    -->
                    <TcpRelay>*:3478</TcpRelay>
                    <!--
                        TcpForce is an option to force the use of TCP rather than UDP in WebRTC streaming.
                        (You can omit ?transport=tcp accordingly.) If <TcpRelay> is not set, playback may fail.
                    -->
                    <TcpForce>true</TcpForce>
                    <TcpRelayWorkerCount>1</TcpRelayWorkerCount>
                </IceCandidates>
            </WebRTC>
        </Providers>

        <Publishers>
            <OVT>
                <Port>9000</Port>
                <WorkerCount>1</WorkerCount>
            </OVT>
            <LLHLS>
                <!--
                    OME only supports h2, so LLHLS works over HTTP/1.1 on non-TLS ports.
                    LLHLS works with higher performance over HTTP/2,
                    so it is recommended to use a TLS port.
                -->
                <Port>3333</Port>
                <!-- If you want to use TLS, specify the TLS port -->
                <TLSPort>3334</TLSPort>
                <WorkerCount>1</WorkerCount>
            </LLHLS>
            <WebRTC>
                <Signalling>
                    <Port>3333</Port>
                    <TLSPort>3334</TLSPort>
                    <WorkerCount>1</WorkerCount>
                </Signalling>
                <IceCandidates>
                    <IceCandidate>*:10000-10005/udp</IceCandidate>
                    <!--
                        If you want to stream WebRTC over TCP, specify IP:Port for TURN server.
                        This uses the TURN protocol, which delivers the stream from the built-in TURN server to the player's TURN client over TCP.
                        For detailed information, refer https://airensoft.gitbook.io/ovenmediaengine/streaming/webrtc-publishing#webrtc-over-tcp
                    -->
                    <TcpRelay>*:3478</TcpRelay>
                    <!--
                        TcpForce is an option to force the use of TCP rather than UDP in WebRTC streaming.
                        (You can omit ?transport=tcp accordingly.) If <TcpRelay> is not set, playback may fail.
                    -->
                    <TcpForce>true</TcpForce>
                    <TcpRelayWorkerCount>1</TcpRelayWorkerCount>
                </IceCandidates>
            </WebRTC>
        </Publishers>
    </Bind>

    <!--
        Enable this configuration if you want to use API Server

        <AccessToken> is a token for authentication, and when you invoke the API, you must put "Basic base64encode(<AccessToken>)" in the "Authorization" header of HTTP request.
        For example, if you set <AccessToken> to "ome-access-token", you must set "Basic b21lLWFjY2Vzcy10b2tlbg==" in the "Authorization" header.
    -->
    <!--
    <Managers>
        <Host>
            <Names>
                <Name>*</Name>
            </Names>
            <TLS>
                <CertPath>path/to/file.crt</CertPath>
                <KeyPath>path/to/file.key</KeyPath>
                <ChainCertPath>path/to/file.crt</ChainCertPath>
            </TLS>
        </Host>
        <API>
            <AccessToken>ome-access-token</AccessToken>

            <CrossDomains>
                <Url>*.airensoft.com</Url>
                <Url>http://*.sub-domain.airensoft.com</Url>
                <Url>http?://airensoft.*</Url>
            </CrossDomains>
        </API>
    </Managers>
    -->

    <VirtualHosts>
        <!-- You can use wildcard like this to include multiple XMLs -->
        <VirtualHost include="VHost*.xml" />
        <VirtualHost>
            <Name>default</Name>
            <!--
                Distribution is a value that can be used when grouping the same vhost distributed across multiple servers.
                This value is output to the events log, so you can use it to aggregate statistics.
            -->
            <Distribution>ovenmediaengine.com</Distribution>

            <!-- Settings for multi ip/domain and TLS -->
            <Host>
                <Names>
                    <!-- Host names
                        <Name>stream1.airensoft.com</Name>
                        <Name>stream2.airensoft.com</Name>
                        <Name>*.sub.airensoft.com</Name>
                        <Name>192.168.0.1</Name>
                    -->
                    <Name>*</Name>
                </Names>
                <!--
                <TLS>
                    <CertPath>path/to/file.crt</CertPath>
                    <KeyPath>path/to/file.key</KeyPath>
                    <ChainCertPath>path/to/file.crt</ChainCertPath>
                </TLS>
                -->
            </Host>

            <!--
                Refer to https://airensoft.gitbook.io/ovenmediaengine/signedpolicy
            -->
            <!--
            <SignedPolicy>
                <PolicyQueryKeyName>policy</PolicyQueryKeyName>
                <SignatureQueryKeyName>signature</SignatureQueryKeyName>
                <SecretKey>aKq#1kj</SecretKey>

                <Enables>
                    <Providers>rtmp,webrtc,srt</Providers>
                    <Publishers>webrtc,llhls</Publishers>
                </Enables>
            </SignedPolicy>
            -->

            <!--
            <AdmissionWebhooks>
                <ControlServerUrl></ControlServerUrl>
                <SecretKey></SecretKey>
                <Timeout>3000</Timeout>
                <Enables>
                    <Providers>rtmp,webrtc,srt</Providers>
                    <Publishers>webrtc,llhls</Publishers>
                </Enables>
            </AdmissionWebhooks>
            -->

            <!--
            <Origins>
                <Properties>
                    <NoInputFailoverTimeout>3000</NoInputFailoverTimeout>
                    <UnusedStreamDeletionTimeout>60000</UnusedStreamDeletionTimeout>
                </Properties>
                <Origin>
                    <Location>/app/stream</Location>
                    <Pass>
                        <Scheme>ovt</Scheme>
                        <Urls><Url>origin.com:9000/app/stream_720p</Url></Urls>
                    </Pass>
                    <ForwardQueryParams>false</ForwardQueryParams>
                </Origin>
                <Origin>
                    <Location>/app/</Location>
                    <Pass>
                        <Scheme>ovt</Scheme>
                        <Urls><Url>origin.com:9000/app/</Url></Urls>
                    </Pass>
                </Origin>
                <Origin>
                    <Location>/edge/</Location>
                    <Pass>
                        <Scheme>ovt</Scheme>
                        <Urls><Url>origin.com:9000/app/</Url></Urls>
                    </Pass>
                </Origin>
            </Origins>
            -->

            <!-- Settings for applications -->
            <Applications>
                <Application>
                    <Name>app</Name>
                    <!-- Application type (live/vod) -->
                    <Type>live</Type>
                    <OutputProfiles>
                        <!-- Enable this configuration if you want to hardware acceleration using GPU -->
                        <HardwareAcceleration>false</HardwareAcceleration>
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
                                <Audio>
                                    <Codec>opus</Codec>
                                    <Bitrate>128000</Bitrate>
                                    <Samplerate>48000</Samplerate>
                                    <Channel>2</Channel>
                                </Audio>
                                <!--
                                <Video>
                                    <Codec>vp8</Codec>
                                    <Bitrate>1024000</Bitrate>
                                    <Framerate>30</Framerate>
                                    <Width>1280</Width>
                                    <Height>720</Height>
                                    <Preset>faster</Preset>
                                </Video>
                                -->
                            </Encodes>
                        </OutputProfile>
                    </OutputProfiles>
                    <Providers>
                        <OVT />
                        <WebRTC />
                        <RTMP />
                        <SRT />
                        <MPEGTS>
                            <StreamMap>
                                <!--
                                    Set the stream name of the client connected to the port to "stream_${Port}"
                                    For example, if a client connects to port 4000, OME creates a "stream_4000" stream
                                -->
                                <!--
                                <Stream>
                                    <Name>stream_${Port}</Name>
                                    <Port>4000,4001-4004</Port>
                                </Stream>
                                <Stream>
                                    <Name>stream_4005</Name>
                                    <Port>4005</Port>
                                </Stream>
                                -->
                                <Stream>
                                    <Name>stream_${Port}</Name>
                                    <Port>4000</Port>
                                </Stream>
                            </StreamMap>
                        </MPEGTS>
                        <RTSPPull />
                        <WebRTC>
                            <Timeout>30000</Timeout>
                        </WebRTC>
                    </Providers>
                    <Publishers>
                        <AppWorkerCount>1</AppWorkerCount>
                        <StreamWorkerCount>8</StreamWorkerCount>
                        <OVT />
                        <WebRTC>
                            <Timeout>30000</Timeout>
                            <Rtx>false</Rtx>
                            <Ulpfec>false</Ulpfec>
                            <JitterBuffer>false</JitterBuffer>
                        </WebRTC>
                        <LLHLS>
                            <ChunkDuration>0.2</ChunkDuration>
                            <SegmentDuration>6</SegmentDuration>
                            <SegmentCount>10</SegmentCount>
                            <CrossDomains>
                                <Url>*</Url>
                            </CrossDomains>
                        </LLHLS>
                    </Publishers>
                </Application>
            </Applications>
        </VirtualHost>
    </VirtualHosts>
</Server>
```

