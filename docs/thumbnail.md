# Thumbnail

OvenMediaEngine can generate thumbnails from live streams. This allows you to organize a broadcast list on your website or monitor multiple streams at the same time.

## Configuration

### Bind

Thumbnails are published via HTTP(s). Set the port for thumbnails as follows. Thumbnail publisher can use the same port number as HLS and DASH.

```markup
<Bind>
    <Publishers>
      ...
        <Thumbnail>
            <Port>20080</Port>
            <!-- If you need TLS support, please uncomment below:
            <TLSPort>20081</TLSPort>
            -->
        </Thumbnail>
    </Publishers>
</Bind>
```

### Encoding

To publish thumbnails, you need to set up an encoding profile. You can choose **JPG, PNG** and **WEBP** as the format.  You can set the Framerate and Resolution. Please refer to the sample below.

```markup
<OutputProfiles>
    <OutputProfile>
        <Name>default_stream</Name>
        <OutputStreamName>${OriginStreamName}_preview</OutputStreamName>
        <Encodes>
            <Image>
                <Codec>jpeg</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>
            <Image>
                <Codec>png</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>
            <Image>
                <Codec>webp</Codec>
                <Framerate>1</Framerate>
                <Width>1280</Width>
                <Height>720</Height>
            </Image>            
        </Encodes>
    </OutputProfile>
</OutputProfiles>
```

<table><thead><tr><th width="290">Property</th><th>Description</th></tr></thead><tbody><tr><td>Codec</td><td>Specifies the image codec to use</td></tr><tr><td>Width</td><td>Width of resolution</td></tr><tr><td>Height</td><td>Height of resolution</td></tr><tr><td>Framerate</td><td>Frames per second</td></tr></tbody></table>

#### Supported image codecs

<table><thead><tr><th width="149">Encode Type</th><th width="177.33333333333331">Codec</th><th>Codec of Configuration</th></tr></thead><tbody><tr><td>Image</td><td>JPEG</td><td>jpeg</td></tr><tr><td></td><td>PNG</td><td>png</td></tr><tr><td></td><td>WEBP</td><td>webp</td></tr></tbody></table>

{% hint style="warning" %}
The image encoding profile is only used by thumbnail publishers. and, bypass option is not supported.
{% endhint %}

### Publisher

Declaring a thumbnail publisher. Cross-domain settings are available as a detailed option.

```markup
<Publishers>
    ...
    <Thumbnail>
        <CrossDomains>
            <Url>*</Url>
        </CrossDomains>	
    </Thumbnail>
</Publishers>
```

## Get thumbnails

When the setting is made for the thumbnail and the stream is input, you can view the thumbnail through the following URL.

| Method | URL Pattern                                                                                        |
| ------ | -------------------------------------------------------------------------------------------------- |
| GET    | http(s)://\<ome\_hos&#x74;_>:\<port>/\<app\_name>/\<output\_stream\_name>/thumb.\<jpg\|png\|webp>_ |

## Advanced&#x20;

### Keyframes Decoding Only

For use cases without video (re)encoding, OME can be set to only decode the keyframes of incoming streams. This is a massive performance increase when all you are using the encoder for is generating thumbnails.

{% hint style="info" %}
_Supported since OvenmediaEngine version 0.17.2_
{% endhint %}

```xml
<OutputProfiles>
<!-- Common setting for decoders. Decodes is optional. -->
	<Decodes>
	<!-- 
	By default, OME decodes all video frames. 
	With OnlyKeyframes, only keyframes are decoded,
	massively improving performance.
	Thumbnails are generated only on keyframes,
	they may not generate at your requested fps!
	-->
		<OnlyKeyframes>true</OnlyKeyframes>
	</Decodes>

    <OutputProfile>
       <Encodes>
           <Video>
                <Bypass>true</Bypass>	
  		   </Video>
           <Image>
               <Codec>jpeg</Codec>
               <Width>1280</Width>
               <Height>720</Height>
               <Framerate>1</Framerate>
          </Image>
       </Encodes>
    </OutputProfile>
</OutputProfiles>
```

### CrossDomains

For information on CrossDomains, see [CrossDomains ](crossdomains.md)chapter.
