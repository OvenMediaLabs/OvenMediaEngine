---
description: Beta
---

# HLS

{% hint style="warning" %}
`MaxDuration`HLS is still in development and some features such as SignedPolicy and AdmissionWebhooks are not supported.
{% endhint %}

HLS based on MPEG-2 TS containers is still useful because it provides high compatibility, including support for older devices. Therefore, OvenMediaEngine decided to officially support HLS version 7+ based on fragmented MP4 containers, called LL-HLS, as well as HLS version 3+ based on MPEG-2 TS containers.

<table><thead><tr><th width="290">Title</th><th>Descriptions</th></tr></thead><tbody><tr><td>Container</td><td><p>MPEG-2 TS</p><p>(Only supports Audio/Video muxed)</p></td></tr><tr><td>Security</td><td>TLS (HTTPS)</td></tr><tr><td>Transport</td><td>HTTP/1.1, HTTP/2</td></tr><tr><td>Codec</td><td><p>H.264, H.265, AAC</p><ul><li>Apple Safari does not support H.265 (HEVC) in MPEG-TS format.</li></ul></td></tr><tr><td>Default URL Pattern</td><td><p><code>http[s]://{OvenMediaEngine Host}[:{HLS Port}]/{App Name}/{Stream Name}/ts:master.m3u8</code></p><p><code>http[s]://{OvenMediaEngine Host}[:{HLS Port}]/{App Name}/{Stream Name}/master.m3u8?format=ts</code></p></td></tr></tbody></table>



{% hint style="info" %}
To differentiate between LLHLS and HLS playback addresses, we created the following rule for HLS playback addresses:

`http[s]://domain[:port]/<app name>/<stream name>/ts:<playlist file name>.m3u8`

or

`http[s]://domain[:port]/<app name>/<stream name>/<playlist file name>.m3u8`<mark style="color:blue;">**`?format=ts`**</mark>
{% endhint %}

## Configuration

To use HLS, you need to add the `<HLS>` elements to the `<Publishers>` in the configuration as shown in the following example.

```xml
<Server>
    <Bind>
        <Publishers>
            <HLS>
                <Port>13333</Port>
                <TLSPort>13334</TLSPort>
                <WorkerCount>1</WorkerCount>
            </HLS>
        </Publishers>
    </Bind>
    ...
    <VirtualHosts>
        <VirtualHost>
            <Applications>
                <Application>
                    <Publishers>
                        <HLS>
                            <SegmentCount>5</SegmentCount>
                            <SegmentDuration>4</SegmentDuration>
                            <DVR>
                                <Enable>true</Enable>
                                <EventPlaylistType>false</EventPlaylistType>
                                <TempStoragePath>/tmp/ome_dvr/</TempStoragePath>
                                <MaxDuration>600</MaxDuration>
                            </DVR>
                            <CrossDomains>
                                <Url>*</Url>
                            </CrossDomains>
                        </HLS>
                    </Publishers>
                </Application>
            </Applications>
        </VirtualHost>
    </VirtualHosts>
</Server>
```

<table><thead><tr><th width="241">Element</th><th>Decscription</th></tr></thead><tbody><tr><td><code>Bind</code></td><td>Set the HTTP ports to provide HLS.</td></tr><tr><td><code>SegmentDuration</code></td><td>Set the length of the segment in seconds. Therefore, a shorter value allows the stream to start faster. However, a value that is too short will make legacy HLS players unstable. Apple recommends <strong><code>6</code></strong> seconds for this value.</td></tr><tr><td><code>SegmentCount</code></td><td>The number of segments listed in the playlist. <strong><code>5</code></strong> is recommended for HLS players. Do not set below <code>3</code>. It can only be used for experimentation.</td></tr><tr><td><code>CrossDomains</code></td><td>Control the domain in which the player works through <code>&#x3C;CrossDomain></code>. For more information, please refer to the <a href="../crossdomains.md">CrossDomains</a> section.</td></tr><tr><td><code>DVR</code></td><td><p><strong>Enable</strong> <br>You can turn DVR on or off.</p><p><strong>EventPlaylistType</strong> <br>Inserts <code>#EXT-X-PLAYLIST-TYPE: EVENT</code> into the m3u8 file.</p><p><strong>TempStoragePath</strong> <br>Specifies a temporary folder to store old segments.</p><p><strong>MaxDuration</strong> <br>Sets the maximum duration of recorded files in milliseconds.</p></td></tr></tbody></table>



{% hint style="warning" %}
Safari Native Player only provides the Seek UI if `#EXT-X-PLAYLIST-TYPE: EVENT` is present. Since it is specified that nothing can be removed from the playlist when it is of type EVENT, you must call the [concludeHlsLive API](../rest-api/v1/virtualhost/application/stream/conclude-hls-live.md) to switch to VoD or terminate the stream before `<MaxDuration>` is exceeded if you use this option. Otherwise, unexpected behavior may occur in the Safari Player.
{% endhint %}

## Playback

HLS is ready when a live source is inputted and a stream is created. Viewers can stream using OvenPlayer or other players.

If your input stream is already h.264/aac, you can use the input stream as is like below. If not, or if you want to change the encoding quality, you can do [Transcoding](../transcoding/).

```xml
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
    </Encodes>
    ...
</OutputProfile>
```

HLS Publisher basically creates a `master.m3u8` Playlist using the first video track and the first audio track. When you create a stream, as shown above, you can play HLS with the following URL:

> `http[s]://{OvenMediaEngine Host}[:{HLS Port}]/{App Name}/{Stream Name}/ts:mster.m3u8`
>
> `http[s]://{OvenMediaEngine Host}[:{HLS Port}]/{App Name}/{Stream Name}/master.m3u8?format=ts`

If you use the default configuration, you can start streaming with the following URL:

`http://{OvenMediaEngine Host}:3333/{App Name}/{Stream Name}/ts:master.m3u8`

`http://{OvenMediaEngine Host}:3333/{App Name}/{Stream Name}/master.m3u8?format=ts`

We have prepared a test player that you can quickly see if OvenMediaEngine is working. Please refer to the [Test Player](../quick-start/test-player.md) for more information.

## Adaptive Bitrates Streaming (ABR)

HLS can deliver adaptive bitrate streaming. OME encodes the same source with multiple renditions and delivers it to the players. And HLS Player, including OvenPlayer, selects the best quality rendition according to its network environment. Of course, these players also provide option for users to manually select rendition.

See the [Adaptive Bitrates Streaming](../transcoding/#adaptive-bitrates-streaming-abr) section for how to configure renditions.

HLS Publisher basically creates the `master.m3u8` Playlist using the first video track and the first audio track. If you want to create a new playlist for ABR, you can add it to Server.xml as follows:

{% hint style="info" %}
Since TS files used in HLS must have A/V pre-muxed, the Playlist must have the <mark style="color:orange;">`EnableTsPackaging`</mark> option set.
{% endhint %}

```xml
<?xml version="1.0" encoding="UTF-8"?>
<OutputProfile>
    <Name>abr_stream</Name>
    <OutputStreamName>${OriginStreamName}</OutputStreamName>
    <Playlist>
        <Name>abr</Name>
        <FileName>abr</FileName>
        <Options>
            <HLSChunklistPathDepth>0</HLSChunklistPathDepth>
            <EnableTsPackaging>true</EnableTsPackaging>
        </Options>
        <Rendition>
            <Name>SD</Name>
            <Video>video_360</Video>
            <Audio>aac_audio</Audio>
        </Rendition>
        <Rendition>
            <Name>HD</Name>
            <Video>video_720</Video>
            <Audio>aac_audio</Audio>
        </Rendition>
        <Rendition>
            <Name>FHD</Name>
            <Video>video_1080</Video>
            <Audio>aac_audio</Audio>
        </Rendition>
    </Playlist>
    <Encodes>
        <Audio>
            <Name>aac_audio</Name>
            <Codec>aac</Codec>
            <Bitrate>128000</Bitrate>
            <Samplerate>48000</Samplerate>
            <Channel>2</Channel>
            <BypassIfMatch>
                <Codec>eq</Codec>
            </BypassIfMatch>
        </Audio>
        <Video>
            <Name>video_360</Name>
            <Codec>h264</Codec>
            <Bitrate>365000</Bitrate>
            <Framerate>30</Framerate>
            <Width>640</Width>
            <Height>360</Height>
            <KeyFrameInterval>30</KeyFrameInterval>
            <ThreadCount>2</ThreadCount>
            <Preset>medium</Preset>
            <BFrames>0</BFrames>
            <ThreadCount>1</ThreadCount>
        </Video>
        <Video>
            <Name>video_720</Name>
            <Codec>h264</Codec>
            <Profile>high</Profile>
            <Bitrate>1500000</Bitrate>
            <Framerate>30</Framerate>
            <Width>1280</Width>
            <Height>720</Height>
            <KeyFrameInterval>30</KeyFrameInterval>
            <Preset>medium</Preset>
            <BFrames>2</BFrames>
            <ThreadCount>4</ThreadCount>
        </Video>
        <Video>
            <Name>video_1080</Name>
            <Codec>h264</Codec>
            <Bitrate>6000000</Bitrate>
            <Framerate>30</Framerate>
            <Width>1920</Width>
            <Height>1080</Height>
            <KeyFrameInterval>30</KeyFrameInterval>
            <ThreadCount>8</ThreadCount>
            <Preset>medium</Preset>
            <BFrames>0</BFrames>
        </Video>
    </Encodes>
</OutputProfile>
```

## CrossDomain

For information on `CrossDomains`, see [CrossDomains ](../crossdomains.md)chapter.

## Live Rewind

You can create as long a playlist as you want by setting `<DVR>` to the HLS publisher as shown below. This allows the player to rewind the live stream and play older segments. OvenMediaEngine stores and uses old segments in a file in `<DVR>/<TempStoragePath>` to prevent excessive memory usage. It stores as much as `<DVR>/<MaxDuration>` and the unit is seconds.

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application/Publishers -->
<HLS>
    ...
    <DVR>
        <Enable>true</Enable>
        <TempStoragePath>/tmp/ome_dvr/</TempStoragePath>
        <MaxDuration>3600</MaxDuration>
    </DVR>
    ...
</HLS>
```

## Dump

You can dump the HLS stream for VoD. You can enable it by setting the following in `<Application><Publishers><LLHLS>`. Unlike LLHLS, dump function cannot be controlled by REST API at this time.

{% code overflow="wrap" %}
```xml
<LLHLS>
    <Dumps>
        <Dump>
            <Enable>true</Enable>
            <TargetStreamName>stream*</TargetStreamName>
            
            <Playlists>
                <Playlist>playlist.m3u8</Playlist>
            </Playlists>
    
            <OutputPath>/service/www/ome-dev.airensoft.com/html/${VHostName}_${AppName}_${StreamName}/${YYYY}_${MM}_${DD}_${hh}_${mm}_${ss}</OutputPath>
        </Dump>
    </Dumps>
    ...
</LLHLS>
```
{% endcode %}

**TargetStreamName**

The name of the stream to dump to. You can use \* and ? to filter stream names.

**Playlists**

The name of the master playlist file to be dumped together.

**OutputPath**

The folder to output to. In the OutputPath you can use the macros shown in the table below. You must have write permission on the specified folder.

| Macro         | Description                    |
| ------------- | ------------------------------ |
| ${VHostName}  | Virtual Host Name              |
| ${AppName}    | Application Name               |
| ${StreamName} | Stream Name                    |
| ${YYYY}       | Year                           |
| ${MM}         | Month                          |
| ${DD}         | Day                            |
| ${hh}         | Hour                           |
| ${mm}         | Minute                         |
| ${ss}         | Second                         |
| ${S}          | Timezone                       |
| ${z}          | UTC offset (ex: +0900)         |
| ${ISO8601}    | Current time in ISO8601 format |