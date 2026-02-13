# MPEG-2 TS

From version 0.10.4, MPEG-2 TS input is supported. The supported codecs are H.264, AAC (ADTS). Supported codecs will continue to be added. And the current version only supports basic MPEG-2 TS with 188 bytes packet size. Since the information about the input stream is obtained using `PAT` and `PMT`, the client must send this table information as required.

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>MPEG-2 TS</td></tr><tr><td>Transport</td><td>UDT / TCP</td></tr><tr><td>Codec</td><td>H.264, H.265, AAC</td></tr></tbody></table>

## Configuration

To enable MPEG-2 TS, you must bind the ports fist and map the bound ports and streams.

### Bind

To use multiple streams, it is necessary to bind multiple ports, so we provide a way to bind multiple ports as in the example below. You can use the dash to specify the port as a range, such as `Start port-End port`, and multiple ports using commas.

### Stream mapping

First, name the stream and map the port bound above. The macro `${Port}`is provided to map multiple streams at once. Check out the example below.

{% code overflow="wrap" %}
```xml
<!-- /Server/Bind -->
<Providers>
    ...
    <MPEGTS>
        <!--
            Listen on port 4000,4001,4004,4005
            This is just a demonstration to show that
            you can configure the port in several ways
        -->
        <Port>4000-4001,4004,4005/udp</Port>
        ...
    </MPEGTS>
</providers>

<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Providers>
    ...
    <MPEGTS>
        <StreamMap>
            <!--
                Set the stream name of the client connected to the
                port to "stream_${Port}"
                For example, if a client connets to port 4000,
                OME creates a "stream_4000" stream
            -->
            <Stream>
                <Name>stream_${Port}</Name>
                <Port>4000-4001,4004</Port>
            </Stream>
            <Stream>
                <Name>stream_name_for_4005_port</Name>
                <Port>4005</Port>
            </Stream>
            ...
        </StreamMap>
    </MPEGTS>
</Providers>
```
{% endcode %}

## Publish

This is an example of publishing using FFMPEG.

{% code overflow="wrap" %}
```markup
# Video / Audio
ffmpeg.exe -re -stream_loop -1 -i <file.ext> -c:v libx264 -bf 0 -x264-params keyint=30:scenecut=0  -acodec aac -pes_payload_size 0 -f mpegts udp://<IP>:4000?pkt_size=1316

# Video only
ffmpeg.exe -re -stream_loop -1 -i <file.ext> -c:v libx264 -bf 0 -x264-params keyint=30:scenecut=0  -an -f mpegts udp://<IP>:4000?pkt_size=1316

# Audio only
ffmpeg.exe -re -stream_loop -1 -i <file.ext> -vn  -acodec aac -pes_payload_size 0 -f mpegts udp://<IP>:4000?pkt_size=1316
```
{% endcode %}

{% hint style="info" %}
Giving the -pes\_payload\_size 0 option to the AAC codec is very important for AV synchronization and low latency. If this option is not given, FFMPEG bundles several ADTSs and is transmitted at once, which may cause high latency and AV synchronization errors.
{% endhint %}
