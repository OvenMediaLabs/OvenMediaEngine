# SRT

OvenMediaEngine supports playback of streams delivered via RTMP, WebRTC, SRT, MPEG-2 TS, and RTSP using SRT-compatible players or integration with other SRT-enabled systems.

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>MPEG-2 TS</td></tr><tr><td>Transport</td><td>SRT</td></tr><tr><td>Codec</td><td>H.264, H.265, AAC</td></tr><tr><td>Default URL Pattern</td><td><code>srt://{OvenMediaEngine Host}:{SRT Port}?streamid={Virtual Host Name}/{App Name}/{Stream Name}/master</code></td></tr></tbody></table>

Currently, OvenMediaEngine supports H.264, H.265, AAC codecs for SRT playback, ensuring the same compatibility as its [SRT provider functionality](../live-source/srt.md).

## Configuration

### Bind

To configure the port for SRT to listen on, use the following settings:

```xml
<Server>
    <Bind>
        <Publishers>
            <SRT>
                <Port>9998</Port>
                <!-- <WorkerCount>1</WorkerCount> -->
                <!--
                    To configure SRT socket options, you can use the settings shown below.
                    For more information, please refer to the details at the bottom of this document:
                    <Options>
                        <Option>...</Option>
                    </Options>
                -->
            </SRT>
            ...
        </Publishers>
    </Bind>
</Server>
```

{% hint style="warning" %}
The SRT Publisher must be configured to use a different port than the one used by the SRT Provider.
{% endhint %}

### Application

You can control whether to enable SRT playback for each application. To activate this feature, configure the settings as shown below:

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications -->
<Application>
    ...
    <Publishers>
        <SRT />
        ...
    </Publishers>
</Application>
```

## SRT client and `streamid`

As with using [SRT as a live source](../live-source/srt.md#encoders-and-streamid), multiple streams can be serviced on a single port. To distinguish each stream, you must set the `streamid` in the format `{Virtual Host Name}/{App Name}/{Stream Name}/{Playlist Name}`.

> `streamid` = `{Virtual Host Name}/{App Name}/{Stream Name}/{Playlist Name}`

SRT clients such as FFmpeg, OBS Studio, and `srt-live-transmit` allow you to specify the `streamid` as a query string appended to the SRT URL. For example, you can specify the `streamid` in the SRT URL like this to play a specific SRT stream: `srt://{OvenMediaEngine Host}:{SRT Port}?streamid={streamid}`.

## Playback

To ensure that SRT streaming works correctly, you can use tools like FFmpeg or OBS Studio to verify the functionality. Here is the guidance on how to playback the stream using the generated SRT URL.

The SRT URL to be used in the player is structured as follows:

```
srt://{OvenMediaEngine Host}:{SRT Port}?streamid={streamid}
```

SRT Publisher creates a default playlist named `master` with the first track from each of the audio tracks and video tracks, and all data tracks.

For example, to playback the `default/app/stream` stream with the default playlist from OME listening on port `9998` at `192.168.0.160`, use the following SRT URL:

> `srt://192.168.0.160:9998?streamid=default/app/stream/master`

You can input the SRT URL as shown above into your SRT client. Below, we provide instructions on how to input the SRT URL for each client.

### FFplay (FFmpeg/FFprobe)

If you want to test SRT with FFplay, FFmpeg, or FFprobe, simply enter the SRT URL next to the command. For example, with FFplay, you can use the following command:

```
$ ffplay "srt://192.168.0.160:9998?streamid=default/app/stream/master"
```

If you have multiple audio tracks, you can choose one with `-ast` parameter

```
$ ffplay "srt://192.168.0.160:9998?streamid=default/app/stream/master" -ast 1
```

<figure><img src="../.gitbook/assets/image (2) (1).png" alt=""><figcaption></figcaption></figure>

### OBS Studio

OBS Studio offers the ability to add an SRT stream as an input source. To use this feature, follow the steps below to add a Media Source:

<figure><img src="../.gitbook/assets/image (2).png" alt=""><figcaption></figcaption></figure>

Once added, you will see the SRT stream as a source, as shown below. This added source can be used just like any other media source.

<figure><img src="../.gitbook/assets/image (1) (1) (1) (1).png" alt=""><figcaption></figcaption></figure>

### VLC

You can also playback the SRT stream in VLC. Simply select `Media` > `Open Network Stream` from the menu and enter the SRT URL:

<figure><img src="../.gitbook/assets/image (1) (1) (1).png" alt=""><figcaption></figcaption></figure>

<figure><img src="../.gitbook/assets/image (3) (1).png" alt=""><figcaption></figcaption></figure>

## Using Playlist

{% hint style="info" %}
OvenMediaEngine automatically generates a default playlist regardless of whether a playlist is specified, so this step is optional.
{% endhint %}

When playing back stream via SRT, you can use a playlist configured for [Adaptive Bitrate Streaming (ABR)](../transcoding/abr.md#adaptive-bitrate-streaming-abr) to ensure that only specific audio/video renditions are delivered.

By utilizing this feature, you can provide services with different codecs, profiles, or other variations to meet diverse streaming requirements.

### **Configuration for playlists**

{% hint style="warning" %}
Since SRT does not support ABR, it uses only the first rendition when there are multiple renditions.
{% endhint %}

{% hint style="info" %}
Since SRT is packaged in the MPEG-TS, the `EnableTsPackaging` option must be set to `true` to use the playlist.
{% endhint %}

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application/OutputProfiles -->
<OutputProfile>
    <Name>stream_pt</Name>
    <OutputStreamName>${OriginStreamName}</OutputStreamName>

    <Encodes>
        <!-- Audio/Video passthrough -->
        <Audio>
            <Name>audio_pt</Name>
            <Bypass>true</Bypass>
        </Audio>
        <Video>
            <Name>video_pt</Name>
            <Bypass>true</Bypass>
        </Video>

        <!-- Encode Video -->
        <Video>
            <Name>video_360p</Name>
            <Codec>h264</Codec>
            <Height>360</Height>
            <Bitrate>200000</Bitrate>
        </Video>
        <Video>
            <Name>video_1080p</Name>
            <Codec>h264</Codec>
            <Height>1080</Height>
            <Bitrate>7000000</Bitrate>
        </Video>
    </Encodes>

    <!-- SRT URL: srt://<host>:<port>?streamid=host/app/stream/360p -->
    <Playlist>
        <Name>Low</Name>
        <FileName>360p</FileName>
        <Options>
            <EnableTsPackaging>true</EnableTsPackaging>
        </Options>
        <Rendition>
            <Name>360p</Name>
            <Video>video_360p</Video>
            <Audio>audio_pt</Audio>
        </Rendition>

        <!--
            This is an example to show how it behaves when using multiple renditions in SRT.
            Since SRT only uses the first rendition, this rendition is ignored.
        -->
        <Rendition>
            <Name>passthrough</Name>
            <Video>video_pt</Video>
            <Audio>audio_pt</Audio>
        </Rendition>
    </Playlist>

    <!-- SRT URL: srt://<host>:<port>?streamid=default/app/stream/1080p -->
    <Playlist>
        <Name>High</Name>
        <FileName>1080p</FileName>
        <Options>
            <EnableTsPackaging>true</EnableTsPackaging>
        </Options>
        <Rendition>
            <Name>1080p</Name>
            <Video>video_1080p</Video>
            <Audio>audio_pt</Audio>
        </Rendition>
    </Playlist>
</OutputProfile>
```

### **Playback using the playlists**

To play a stream using a particular playlist, specify the `Playlist.FileName` to the playlist name in the SRT playback URL, as shown below:

**SRT playback URL using default playlist**

```
srt://192.168.0.160:9998?streamid=host/app/stream/master
```

**SRT playback URL using `360p` playlist**

```
srt://192.168.0.160:9998?streamid=host/app/stream/360p
```

**SRT playback URL using `1080p` playlist**

<pre><code><strong>srt://192.168.0.160:9998?streamid=host/app/stream/1080p
</strong></code></pre>

## SRT Socket Options

You can configure SRT's socket options of the OvenMediaEngine server using `<Options>`. This is particularly useful when setting the encryption for SRT, and you can specify a passphrase by configuring as follows:

```xml
<Server>
    <Bind>
        <Publishers>
            <SRT>
                ...
                <Options>
                    <Option>
                        <Key>SRTO_PBKEYLEN</Key>
                        <Value>16</Value>
                    </Option>
                    <Option>
                        <Key>SRTO_PASSPHRASE</Key>
                        <Value>thisismypassphrase</Value>
                    </Option>
                </Options>
            </SRT>
...
```

For more information on SRT socket options, please refer to [https://github.com/Haivision/srt/blob/v1.5.2/docs/API/API-socket-options.md#list-of-options](https://github.com/Haivision/srt/blob/v1.5.2/docs/API/API-socket-options.md#list-of-options).
