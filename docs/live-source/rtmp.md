# RTMP

RTMP is one of the most widely used protocols in live streaming.

<table><thead><tr><th width="290">Title</th><th>Functions</th></tr></thead><tbody><tr><td>Container</td><td>FLV</td></tr><tr><td>Transport</td><td>TCP</td></tr><tr><td>Codec</td><td>H.264, AAC / H.265 (E-RTMP only)</td></tr></tbody></table>

## Configuration

`<Providers>` ingests streams that come from a media source. OvenMediaEngine supports RTMP protocol. You can set it in the configuration as follows:

### Bind

```markup
<!-- /Server/Bind/Providers -->
<Providers>
    ...
    <RTMP>
        <Port>1935</Port>
        <WorkerCount>1</WorkerCount>
        <ThreadPerSocket>false</ThreadPerSocket>
    </RTMP>
    ...
</Providers>
```

<table><thead><tr><th width="212">Property</th><th>Description</th></tr></thead><tbody><tr><td>Port</td><td>Specifies the TCP port number that listens for incoming RTMP connections.</td></tr><tr><td>WorkerCount (default : 1)</td><td>Defines the number of worker threads for handling RTMP sockets. If the number of incoming RTMP sessions increases, you can raise this value to distribute traffic more efficiently.</td></tr><tr><td>ThreadPerSocket (default : false)</td><td>Determines whether each socket gets its own dedicated thread. If it is set to <strong>true</strong>, <code>WorkerCount</code> is ignored, and a new thread is created for every session when it connects, then terminated when the session ends. </td></tr></tbody></table>

{% hint style="warning" %}
Using `ThreadPerSocket` can prevent a session thread from blocking while waiting for the Control Server to respond during AdmissionWebhooks, which would otherwise stop other sessions from proceeding. However, if too many sessions are connected, the overhead from thread context switching can become very high. On a 16-core system, practical cases have shown that around 100 sessions can run without issues.
{% endhint %}

### Application

RTMP input can be turned on/off for each application. As follows Setting enables the RTMP input function of the application.

```
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Providers>
    ...
    <RTMP />
    ...
</Providers>
```

## RTMP live stream

If you set up a live stream using an RTMP-based encoder, you need to set the following in `Server.xml`:

```markup
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Providers>
    <RTMP>
        <BlockDuplicateStreamName>true</BlockDuplicateStreamName>
        ...
    </RTMP>
</Providers>
```

* `<BlockDuplicateStreamName>` is a policy for streams that are inputted as overlaps.

`<BlockDuplicateStreamName>` works with the following rules:

| Value   | Description                                                                             |
| ------- | --------------------------------------------------------------------------------------- |
| `true`  | `Default` Rejects the new stream inputted as overlap and maintains the existing stream. |
| `false` | Accepts a new stream inputted as overlap and disconnects the existing stream.           |

{% hint style="info" %}
To allow the duplicated stream name feature can cause several problems. When a new stream is an input the player may be disconnected. Most encoders have the ability to automatically reconnect when it is disconnected from the server. As a result, two encoders compete and disconnect each other, which can cause serious problems in playback.
{% endhint %}

## Publish

If you want to publish the source stream, you need to set the following in the Encoder:

* URL: `rtmp://{OME Host}[:{RTMP Port}]/{App Name}`
* Stream Key: `{Stream Name}`

If you use the default configuration, the `{RTMP Port}` is `1935`, which is the default port for RTMP. So it can be omitted. Also, since the Application named `app` is created by default in the default configuration, you can enter `app` in the `{App Name}`. You can define a Stream Key and use it in the Encoder, and the Streaming URL will change according to the Stream Key.

Moreover, some encoders can include a stream key in the URL, and if you use these encoders, you need to set it as follows:

* URL: `rtmp://{OME Host}[:{RTMP Port}]/{App Name}/{Stream Name}`

### Example with OvenLiveKit (OvenStreamEncoder)

If you are using the default configuration, press the URL button in the top right corner of OvenStreamEnoder, and enter the URL as shown below:

![](../.gitbook/assets/03.png)

Also, `{App Name}` and `{Stream Name}` can be changed and used as desired in the configuration.

### Example with OBS

If you use the default configuration, set the OBS as follows:

<figure><img src="../.gitbook/assets/image (63).png" alt=""><figcaption></figcaption></figure>

You can set the Stream Key to any name you like at any time.

## E-RTMP

Enhanced RTMP (E-RTMP) is an experimental streaming feature that extends the capabilities of the traditional RTMP protocol. One of its key advantages is support for modern video codecs such as H.265 (HEVC), which are not available in standard RTMP. This allows for better video quality and lower bitrates, making it ideal for high-efficiency streaming workflows. The list of supported codecs will continue to grow as development progresses.

| Title               | Functions             |
| ------------------- | --------------------- |
| Container           | FLV                   |
| Transport           | TCP                   |
| Codec               | H.264, H.265, AAC     |
| Additional Features | Simulcast, Multitrack |

Since E-RTMP is still experimental, **it is disabled by default** and must be manually enabled in the server settings.

### How to Enable E-RTMP

To enable E-RTMP, you need to update the `Server.xml` configuration file. Add the following configuration:

```xml
<Server>
    ...
    <Modules>
        ...
        <ERTMP>
            <Enable>true</Enable>
        </ERTMP>
        ...
    </Modules>
    ...
</Server>
```

### Publish with OBS

To stream with E-RTMP using OBS, select an encoder that supports HEVC in the `Video Encoder` section of the `Output` settings as shown below:

<figure><img src="../.gitbook/assets/image (64).png" alt=""><figcaption></figcaption></figure>

