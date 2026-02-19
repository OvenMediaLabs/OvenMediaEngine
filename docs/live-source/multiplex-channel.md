# Multiplex Channel

Now with Multiplex Channel, you can configure ABR by combining multiple input streams into one, or duplicate external streams and send them to other applications.

Multiplex Channel takes tracks from other local streams and organizes them into its own tracks. This will pull in tracks that have already been encoded, which can be useful if you want to change codecs or adjust the quality once again. And the Multiplex Channel is sent to the publisher, unconditionally bypassing the encoder.

<figure><img src="../.gitbook/assets/image (32).png" alt=""><figcaption></figcaption></figure>

## Configuration

To use this feature, enable Multiplex Provider in `Server.xml`.

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Providers>
    ...
    <Multiplex>
        <MuxFilesDir>mux_files</MuxFilesDir>
    </Multiplex>
    ...
</Providers>
```

Multiplex Channels are created through `.mux` files or API. `<MuxFilesDir>` is the path where the .mux files are located and can be set to an absolute system path or relative to the path where the `Server.xml` configuration is located.

The Multiplex Provider monitors the `<MuxFilesDir>` path, and when a mux file is created, it parses the file and creates a multiplex channel. When the mux file is modified, the channel is deleted and created again, and when the mux file is deleted, the channel is deleted.

## Mux file format

mux files can be created or deleted while the system is running. This works dynamically. The mux file has the format below.&#x20;

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Multiplex>
    <OutputStream>
        <Name>stream</Name>
    </OutputStream>

    <SourceStreams>
        <SourceStream>
            <Name>tv1</Name>
            <Url>stream://default/app/tv1</Url>
            <TrackMap>
                <Track>
                    <SourceTrackName>bypass_video</SourceTrackName>
                    <NewTrackName>tv1_video</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>bypass_audio</SourceTrackName>
                    <NewTrackName>tv1_audio</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>opus</SourceTrackName>
                    <NewTrackName>tv1_opus</NewTrackName>
                </Track>
            </TrackMap>
        </SourceStream>
        <SourceStream>
            <Name>tv2</Name>
            <Url>stream://default/app/tv2</Url>

            <TrackMap>
                <Track>
                    <SourceTrackName>bypass_video</SourceTrackName>
                    <NewTrackName>tv2_video</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>bypass_audio</SourceTrackName>
                    <NewTrackName>tv2_audio</NewTrackName>
                </Track>
                <Track>
                    <SourceTrackName>opus</SourceTrackName>
                    <NewTrackName>tv2_opus</NewTrackName>
                </Track>
            </TrackMap>

        </SourceStream>
    </SourceStreams>
    
    <Playlists>
        <Playlist>
            <Name>LLHLS ABR</Name>
            <FileName>abr</FileName>
            
            <Rendition>
                <Name>1080p</Name>
                <Video>tv1_video</Video>
                <Audio>tv1_audio</Audio>
            </Rendition>
            <Rendition>
                <Name>720p</Name>
                <Video>tv2_video</Video>
                <Audio>tv2_audio</Audio>
            </Rendition>
        </Playlist>
    </Playlists>
    
</Multiplex>
```

**OutputStream**\
This is information about the stream to be newly created. It must be the same as the file name. `{Stream Name}.mux`

**SourceStreams**\
Specifies the internal stream to be muxed. You can also load streams from other VHosts or applications in the format `stream://{VHost Name}/{App Name}/{Stream Name}`. Because multiple streams are muxed into one stream, track names may be duplicated. Therefore, it is necessary to change the Track name for each `<SourceStream>` through `<TrackMap>/<SourceTrackName>` is either `<OutputProfile>/<Encodes>/<Video>/<Name>` or `<OutputProfile>/<Encodes>/<Audio>/<Name>`.

**Playlist**\
The same format as `<OutputProfile>` must be used, and the Playlist must be constructed using the newly mapped Track name in `<TrackMap>`  of `<SourceStreams>`. The Playlist configured here exists only in this stream. The `<Playlist>` 's `<FileName>` must be unique throughout the application.

## REST API

MultiplexChannel can also be controlled via API. Please refer to the page below.&#x20;

{% content-ref url="../rest-api/v1/virtualhost/application/scheduledchannel-api-1.md" %}
[scheduledchannel-api-1.md](../rest-api/v1/virtualhost/application/scheduledchannel-api-1.md)
{% endcontent-ref %}
