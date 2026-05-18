---
title: Push Publishing
description: "Push OvenMediaEngine streams to external destinations over RTMP, SRT, or MPEG-2 TS with the Push publisher."
sidebar_position: 37
---

OvenMediaEngine supports Push Publishing function that can restreaming live streams to other systems. The protocol supports widely used protocols such as [SRT](live-source/srt.md), [RTMP](live-source/rtmp.md), and MPEG-2 TS.

The `StreamMap` feature has been added, and it now automatically re-streaming based on predefined conditions. You can also use the Rest API to control and monitor it.

## Configuration

### Push Publisher

To use Push Publishing, you need to declare the **`<Push>`** publisher in the configuration. `<StreamMap>` is optional. It is used when automatic push is needed.

```xml
<Applications>
  <Application>
     ...
    <Publishers>
      ... 
      <Push>
         <!-- [Optional] -->
         <StreamMap>
           <Enable>false</Enable>
           <Path>path/to/map.xml</Path>
         </StreamMap>
      </Push>
      ...
    </Publishers>
  </Application>
</Applications>
```


:::info

The RTMP protocol only supports H264 and AAC codecs.

:::


### StreamMap

`<StreamMap>` is used to automatically push content based on user-defined conditions. The XML file path must be specified relative to `<ApplicationPath>/conf`.

`<StreamName>` is used to match output stream names and supports wildcard characters.

`<VariantNames>` can be used to select specific tracks. Multiple variants can be specified by separating them with commas (,). \
If multiple tracks with the same `VariantName` exist in the output stream, a specific track can be selected by appending a `:[Index]` suffix.

`<Protocol>` supports `rtmp`, `mpegts`, and `srt`. The destination address is specified in the `<Url>` and `<StreamKey>` fields, and macros can be used.


```xml
<?xml version="1.0" encoding="UTF-8"?>
<PushInfo>
  <!-- RTMP -->
  <Push>
    <!-- [Must] -->
    <Enable>true</Enable>

    <!-- [Must] -->
    <StreamName>stream_a_*</StreamName>
    
    <!-- [Optional] -->
    <VariantNames>video_h264,audio_aac</VariantNames>
    <!-- Select a specific track among tracks with the same VariantName -->
    <!-- <VariantNames>video_h264:0,audio_aac:1</VariantNames> -->
    
    <!-- [Must] -->
    <Protocol>rtmp</Protocol>
    
    <!-- [Must] -->
    <Url>rtmp://1.2.3.4:1935/app/${SourceStream}</Url>
    <!-- <Url>rtmp://1.2.3.4:1935/app/${<a data-footnote-ref="" href="#user-content-fn-1">Stream</a>}</Url> --> 
    
    <!-- [Optional] -->
    <!-- <StreamKey>some-stream-key</StreamKey> -->
<strong>  </Push>  
</strong>
  <!-- SRT -->
  <Push>
    <!-- [Must] -->
    <Enable>true</Enable>

    <!-- [Must] -->
    <StreamName>stream_b_*</StreamName>

    <!-- [Optional] -->
    <VariantNames></VariantNames>

    <!-- [Must] -->
    <Protocol>srt</Protocol>

    <!-- [Must] -->
    <Url>srt://1.2.3.4:9999?streamid=srt%3A%2F%2F1.2.3.4%3A9999%2Fapp%2Fstream</Url>
  </Push>

  <!-- MPEG-TS -->
  <Push>
    <!-- [Must] -->
    <Enable>false</Enable>

    <!-- [Must] -->
    <StreamName>stream_c_*</StreamName>

    <!-- [Must] -->
    <Protocol>mpegts</Protocol>

    <!-- [Must] -->
    <Url>udp://1.2.3.4:2400</Url>
  </Push>    
</PushInfo>
```


| Macro           | Description        |
| --------------- | ------------------ |
| $&#x7B;Application&#x7D;  | Application name   |
| $&#x7B;SourceStream&#x7D; | Source stream name |
| $&#x7B;Stream&#x7D;       | Output stream name |

## REST API

Push can be controlled using the REST API. Please refer to the documentation below for more details.


[push.md](rest-api/v1/virtualhost/application/push.md)


[^1]: 
