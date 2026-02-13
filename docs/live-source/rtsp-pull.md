# RTSP Pull

OvenMediaEngine can pull RTSP Stream in two ways. The first way is to use the Stream creation API, and the second way is to use `OriginMap` or `OriginMapStore`. The supported codecs are H.264, AAC (ADTS). Supported codecs will continue to be added.&#x20;

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>RTP</td></tr><tr><td>Transport</td><td>UDP / TCP</td></tr><tr><td>Codec</td><td>H.264, H.265, Opus, AAC</td></tr></tbody></table>

## Pulling streams using the Stream Creation API

You can create a stream by pulling an RTSP stream using the [Stream Creation API](../rest-api/v1/virtualhost/application/stream/#create-stream-pull). For more information on using the [REST API](../rest-api/), check out that chapter.

## Pulling streams using the OriginMapStore

If OriginMapStore is configured and Redis Server provides an rtsp URL, OvenMediaEngine pulls the RTSP URL when a playback request comes in. Check out [OriginMapStore ](../origin-edge-clustering.md#originmapstore)for more details.

## Pulling streams using the OriginMap

### Configuration

RTSP Pull is provided through OriginMap configuration. OriginMap is the rule that the Edge server pulls the stream of the Origin server. Edge server can pull a stream of origin with RTSP and OVT (protocol defined by OvenMediaEngine for Origin-Edge) protocol. See the [Clustering](../origin-edge-clustering.md) section for more information about OVT.

```markup
<!-- /Server/VirtualHosts -->
<VirtualHost>
    ...
    <Name>default</Name>

    <Origins>
        <Origin>
            <Location>/app_name/rtsp_stream_name</Location>
            <Pass>
                <Scheme>rtsp</Scheme>
                <Urls><Url>192.168.0.200:554/</Url></Urls>
            </Pass>
        </Origin>
    </Origins>
    ...
</VirtualHost>
```

For example, in the above setup, when a player requests `ws://{OvenMediaEngine Host}[:{Signaling Port}]/}App Name}/{RTSP Stream Name}` to stream WebRTC, it pulls the stream from `rtsp://192.168.0.200:554` and publishes it to WebRTC.

{% hint style="warning" %}
If the app name set in Location isn't created, OvenMediaEngine creates the app with default settings. The default generated app doesn't have an OPUS encoding profile, so to use WebRTC streaming, you need to add the app to your configuration.
{% endhint %}

### Event to trigger pulling

Pulling type providers are activated by streaming requests from publishers. And by default, the provider is automatically disabled after 30 seconds of no client playback. If you want to change this setting, check out the [Clustering ](../origin-edge-clustering.md#less-than-properties-greater-than)chapter.

When a playback request comes in from the following URL, RTSP pull starts working according to Origins settings.

<table data-header-hidden><thead><tr><th width="147">Protocol</th><th>URL</th></tr></thead><tbody><tr><td>Protocol</td><td>URL</td></tr><tr><td>WebRTC</td><td><code>ws[s]:://{OvenMediaEngine Host}[:{Signaling Port}]/{App Name}/{RTSP Stream Name}</code></td></tr><tr><td>LLHLS</td><td><code>http[s]://{OvenMediaEngine Host}[:{LLHLS Port}]/{App Name}/{RTSP Stream Name}/llhls.m3u8</code></td></tr></tbody></table>

