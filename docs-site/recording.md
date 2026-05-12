---
title: Recording
sidebar_position: 36
---

OvenMediaEngine supports live stream recording, which can be started and stopped via the REST API. Upon completion, it generates both the recorded media file and a corresponding metadata file, enabling seamless integration with post-processing workflows.

## Configuration

### File Publisher

To enable recording, add the `<FILE>` publisher to the configuration file.

* `<FilePath>` and `<InfoPath>` are **required** and serve as **default values**:
  * `<FilePath>`: Path and filename for the recorded media.
  * `<InfoPath>`: Path and filename for the XML metadata about the recording.

If these values are not provided in the API request, the defaults from the configuration are used.\
This is useful when you want to avoid exposing internal paths for security reasons.

* `<RootPath>` is **optional** and allows the use of **relative paths** in API requests. It applies to both `<FilePath>` and `<InfoPath>`

The `FilePath` must end with `.ts` or `.mp4` to specify the container format.

```xml
Server.xml
<Publishers>
   <FILE>  
      <!-- [Optional] -->
      <RootPath>/mnt/shared_volumes</RootPath>
           
      <!-- [Optional] Specify the path where the recording will be saved.
           If not specified here, it must be provided when calling the REST API -->
      <FilePath>/${VirtualHost}/${Application}/${Stream}/
         ${StartTime:YYYYMMDDhhmmss}_${EndTime:YYYYMMDDhhmmss}.ts</FilePath>

      <!-- [Optional] Specify the path for storing recording metadata -->
      <InfoPath>/${VirtualHost}/${Application}/${Stream}.xml</InfoPath>
   </FILE>
</Publishers>
```

#### \* Supported format and codecs

<table><thead><tr><th width="290">Format</th><th>Codec</th></tr></thead><tbody><tr><td>TS</td><td>H.264, H.265, AAC</td></tr><tr><td>MP4</td><td>H.264, H.265, AAC</td></tr></tbody></table>

## Recording via REST API

For control of recording, use the REST API. Recording can be requested based on the output stream name (specified in the JSON body), and all/some tracks can be selectively recorded. And, it is possible to simultaneously record multiple files for the same stream. When recording is complete, an XML file is created at the path specified in `InfoPath`. For a sample of the recorded file information XML, refer to Appendix B.

For how to use the API, please refer to the link below.


[recording.md](rest-api/v1/virtualhost/application/recording.md)


## Split Recording

Split recording methods provide `SegmentInterval` and `SegmentSchedule`. The interval method splits files based on the accumulated recording time. The Schedule method then splits files according to scheduling options based on system time. The scheduling option is the same as the pattern used in crontab. However, only three options are used: seconds/minutes/hour.\
You can set the `SegmentRule` parameter to determine whether the start timestamp of the separated recording files will start anew from 0 (**discontinuity**)  or continue from where the previous file left off (**continuity**).


:::info

**SegmentInterval**  and **SegmentSchedule** methods cannot be used simultaneously.

:::


## Automated Recording

Provides a way to automatically start and stop recording upon input stream that matches your file-based settings. In the above settings, the XML file path is specified in `StreamMap.Path`.  You can create the XML file at the specified path and configure automatic recording as follows.

```xml
Server.xml

<Publishers>
   <FILE>  
      <!-- [Optional] -->
      <RootPath>/mnt/shared_volumes</RootPath>

      <!-- Recording settings for Automatic recording -->
      <StreamMap>
         <Enable>true</Enable>
         <Path>./record_map.xml</Path>
      </StreamMap>      
   </FILE>
</Publishers>
```


```xml
record_map.xml

<?xml version="1.0" encoding="UTF-8"?>
<RecordInfo>
  <Record>
    <!-- [Must] -->
    <Enable>true</Enable>
    <!-- [Must] -->
    <StreamName>stream1*</StreamName>
    <!-- [Optional] -->
    <VariantNames>h264_1080p,aac_128k</VariantNames>
    <!-- [Optional] -->
    <FilePath>/path/to/${VirtualHost}/${Application}/${Stream}/
              ${StartTime:YYYYMMDDhhmmss}_${EndTime:YYYYMMDDhhmmss}.mp4</FilePath>
    <!-- [Optional] -->              
    <InfoPath>/path/to/${VirtualHost}/${Application}/${Stream}/info.xml</InfoPath>
    <!-- [Optional] -->
    <Metadata>access_key_id='000000000000000',secret_access_key='000000000000000'
    ,endpoint='https://s3.aws.com'</Metadata>
  </Record>  
  <Record>
    <Enable>true</Enable>
    <StreamName>stream2*</StreamName>
    <VariantNames>h264_1080p,aac_128k</VariantNames>
    <FilePath>/path/to/${VirtualHost}/${Application}/${Stream}/
              ${EndTime:YYYYMMDDhhmmss}_${Sequence}.mp4</FilePath>
    <InfoPath>/path/to/${VirtualHost}/${Application}/${Stream}/info.xml</InfoPath>
    <!-- [Optional] -->
    <SegmentInterval>5000</SegmentInterval> 
    <!-- [Optional] Default : discontinuity -->    
    <SegmentRule>continuity</SegmentRule>
  </Record>
  <Record>
    <Enable>true</Enable>
    <StreamName>stream3*</StreamName>
    <VariantNames>aac_128k</VariantNames>
    <FilePath>/path/to/${VirtualHost}/${Application}/${Stream}/
              ${StartTime:YYYYMMDDhhmmss}_${Sequence}.mp4</FilePath>
    <InfoPath>/path/to/${VirtualHost}/${Application}/${Stream}/info.xml</InfoPath>
    <!-- [Optional] -->    
    <SegmentSchedule>*/30 * *</SegmentSchedule>
    <!-- [Optional] -->        
    <SegmentRule>discontinuity</SegmentRule>
  </Record>
</RecordInfo>
```


***

## Appendix A. Macro definition for the recording path

Various macro values are supported for file paths and names as shown below.

<table><thead><tr><th width="290">Macro</th><th>Description</th></tr></thead><tbody><tr><td>$&#x7B;TransactionId&#x7D;</td><td>Unique ID for the recording transaction. It is automatically created when recording starts. and is released when recording is stopped. In case of split recording, it is distinguished that it is the same transaction.</td></tr><tr><td>$&#x7B;Id&#x7D;</td><td>User-defined identification ID</td></tr><tr><td>$&#x7B;StartTime:YYYYMMDDhhmmss&#x7D;</td><td><p>Recording start time</p><p>YYYY - Year</p><p>MM - Month</p><p>DD - Days</p><p>hh : Hours (00&#126;23)</p><p>mm : Minutes (00&#126;59)</p><p>ss : Seconds (00&#126;59)</p></td></tr><tr><td>$&#x7B;EndTime:YYYYMMDDhhmmss&#x7D;</td><td><p>Recording end time</p><p>YYYY - Year</p><p>MM - Month</p><p>DD - Days</p><p>hh : Hours (00&#126;23)</p><p>mm : Minutes (00&#126;59)</p><p>ss : Seconds (00&#126;59)</p></td></tr><tr><td>$&#x7B;VirtualHost&#x7D;</td><td>Virtual host name</td></tr><tr><td>$&#x7B;Application&#x7D;</td><td>Application name</td></tr><tr><td>$&#x7B;SourceStream&#x7D;</td><td>Source stream name</td></tr><tr><td>$&#x7B;Stream&#x7D;</td><td>Output stream name</td></tr><tr><td>$&#x7B;Sequence&#x7D;</td><td>Sequence value that increases when splitting a file in a single transaction</td></tr></tbody></table>

## Appendix B. Recorded File Information Specification

The following is a sample of an XML file that expresses information on a recorded file.


```markup
<?xml version="1.0" encoding="utf-8"?>
<files>
  <file>
    <transactionId>bcUCyJeKuOGnsah3</transactionId>
    <id>CTS_ID001</id>
    <vhost>default</vhost>
    <app>app</app>
    <stream>stream_o</stream>
    <filePath><![CDATA[/home/dev/OvenMediaEngine/records/bcUCyJeKuOGnsah3_default_app_stream_o_20201204005351_20201204005405.ts]]></filePath>
    <recordBytes>8774737</recordBytes>
    <recordTime>60011</recordTime>
    <sequence>0</sequence>
    <interval>60000</interval>
    <lastSequence>true</lastSequence>
    <createdTime>2020-12-04T12:53:51.455+0900</createdTime>
    <startTime>2020-12-04T12:53:51.612+0900</startTime>
    <finishTime>2020-12-04T12:54:51.473+0900</finishTime>
  </file>
  <file>
    <transactionId>bcUCyJeKuOGnsah3</transactionId>
    <id>CTS_ID001</id>
    <vhost>default</vhost>
    <app>app</app>
    <stream>stream_o</stream>
    <filePath><![CDATA[/home/dev/OvenMediaEngine/records/bcUCyJeKuOGnsah3_default_app_stream_o_20201204005408_20201204005412.ts]]></filePath>
    <recordBytes>2285797</recordBytes>
    <recordTime>60012</recordTime>
    <sequence>0</sequence>
    <schedule>0 */1 *</schedule>
    <lastSequence>false</lastSequence>
    <createdTime>2020-12-04T12:53:00.000+0900</createdTime>
    <startTime>2020-12-04T12:53:00.000+0900</startTime>
    <finishTime>2020-12-04T12:54:00.000+0900</finishTime>
  </file>
  <file>
    <transactionId>bcUCyJeKuOGnsah3</transactionId>
    <id>CTS_ID001</id>
    <vhost>default</vhost>
    <app>app</app>
    <stream>stream_o</stream>
    <filePath><![CDATA[/home/dev/OvenMediaEngine/records/bcUCyJeKuOGnsah3_default_app_stream_o_20201204005415_20201204005422.ts]]></filePath>
    <recordBytes>4544626</recordBytes>
    <recordTime>60000</recordTime>
    <sequence>1</sequence>
    <schedule>0 */1 *</schedule>
    <lastSequence>true</lastSequence>
    <createdTime>2020-12-04T12:54:00.000+0900</createdTime>
    <startTime>2020-12-04T12:54:00.000+0900</startTime>
    <finishTime>2020-12-04T12:55:00.000+0900</finishTime>
  </file>
</files>
```

