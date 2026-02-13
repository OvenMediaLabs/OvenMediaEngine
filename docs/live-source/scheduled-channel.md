# Scheduled Channel

Scheduled Channel that allows you to create a live channel by scheduling pre-recorded files has been added to OvenMediaEngine. Other services or software call this Pre-recorded Live or File Live, but OvenMediaEngine plans to expand the function to organize live channels as a source, so we named it Scheduled Channel.

## Getting Started

To use this feature, activate Schedule Provider as follows.

```xml
<!-- /Server/VirtualHosts/VirtualHost/Applications/Application -->
<Providers>
    ...
    <Schedule>
        <MediaRootDir>/opt/ovenmediaengine/media</MediaRootDir>
        <ScheduleFilesDir>/opt/ovenmediaengine/media</ScheduleFilesDir>
    </Schedule>
    ...
</Providers>
```

`<MediaRootDir>`\
Root path where media files are located. If you specify a relative path, the directory where the config file is located is root.

`<ScheduleFileDir>`\
Root path where the schedule file is located. If you specify a relative path, the directory where the config file is located is root.

## Schedule Files

Scheduled Channel creates/updates/deletes streams by creating/editing/deleting files with the .sch extension in the ScheduleFileDir path. Schedule files (`.sch`) use the following XML format. When a `{Stream Name}.sch` file is created in ScheduleFileDir, OvenMediaEngine analyzes the file and creates a Schedule Channel with `{Stream Name}`. If the contents of `{Stream Name}.sch` are changed, the Schedule Channel is updated, and if the file is deleted, the stream is deleted.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Schedule>
    <Stream>
        <Name>tv1</Name> <!-- optional, using filename without ext -->
        <BypassTranscoder>false</BypassTranscoder>
        <VideoTrack>true</VideoTrack>
        <AudioTrack>true</AudioTrack>
        <AudioMap> <!-- optional, only needed if you want to enable multilingual audio -->
            <Item>
                <Name>English</Name>
                <Language>en</Language>
            </Item>
            <Item>
                <Name>Korean</Name>
                <Language>ko</Language>
            </Item>
            <Item>
                <Name>Japanese</Name>
                <Language>ja</Language>
            </Item>
        </AudioMap>
    </Stream>

    <FallbackProgram> <!-- Not yet supported -->
        <Item url="file://sample.mp4" start="0" duration="60000" />
    </FallbackProgram>

    <Program name="1" scheduled="2023-09-27T13:21:15.123+09:00" repeat="true">
        <Item url="stream://default/app/stream1" duration="60000" />
    </Program>
    <Program name="2" scheduled="2022-03-14T15:10:0.0+09:00" repeat="true">
        <Item url="file://sample.mp4" start="0" duration="60000" />
        <Item url="stream://default/app/stream1" duration="60000" /> <!-- Not yet supported -->
        <Item url="file://sample.mp4" start="60000" duration="120000" />
    </Program>
</Schedule>
```

`<Stream> (required)`\
This is the stream information that the Channel needs to create.

`<Stream>/<Name> (optional)`\
It's the stream's name. This is a reference value extracted from the file name for usage. It's recommended to set it same for consistency, although it's for reference purposes.

`<Stream>/<BypassTranscoder> (optional, default: false)`\
Set to true if transcoding is not desired.

`<Stream>/<VideoTrack> (optional, default: true)`\
Determines whether to use the video track. If `VideoTrack` is set to true and there's no video track in the Item, an error will occur.

`<Stream>/<AudioTrack> (optional, default: true)`\
Determines whether to use the audio track. If `AudioTrack` is set to true and there's no audio track in the Item, an error will occur.

`<Stream>/<AudioMap> (optional, default: false)`\
To enable multiple audio tracks (multilingual audio) in Scheduled Channel, enable `AudioMap`. It is important that all scheduled live sources and file sources provide audio tracks equal to or greater than the number of audio tracks defined in `AudioMap`. If you define 3 `AudioMaps`, but the file source or live source provides less than 3 audio tracks, the Program will generate an error. If you provide more audio tracks than the defined `AudioMaps`, they will be mapped in order and the rest will be ignored.

`<FallbackProgram> (optional)`\
It is a program that switches automatically when there is no program scheduled at the current time or an error occurs in an item. If the program is updated at the current time or the item returns to normal, it will fail back to the original program. Both files and live can be used for items in FallbackProgram. However, it is recommended to use a stable file.

`<Program> (optional)`\
Schedules a program. The `name` is an optional reference value. If not set, a random name will be assigned. Set the start time in ISO8601 format in the `scheduled` attribute. Decide whether to repeat the `Items` when its playback ends.

`<Program>/<Item> (optional)`\
Configures the media source to broadcast.

The `url` points to the location of the media source. If it starts with `file://`, it refers to a file within the `<MediaRootDir>` directory. If it starts with `stream://`, it refers to another stream within the same OvenMediaEngine. stream:// has the following format: `stream://{VHost Name}/{App Name}/{Stream Name}`

For 'file' cases, the `start` attribute can be set in milliseconds to indicate where in the file playback should start.\
`duration` indicates the playback time of that item in milliseconds. After the duration ends, it moves to the next item.\
Both 'start' and 'duration' are optional. If not set, `start` defaults to 0, and `duration` defaults to the file's duration; if not specified, the media file will be played until its full duration.

#### Supported Formats for File Live

<table><thead><tr><th width="290">Title</th><th>Description</th></tr></thead><tbody><tr><td>Formats</td><td><p>MP4, TS, MP3, and more.</p><ul><li>All formats supported by FFmpeg are supported.</li></ul></td></tr></tbody></table>

## Multiple Audio Track

The Scheduled Channel supports multiple audio tracks. This is automatically applied to the LLHLS Publisher. You can configure the `<AudioMap>` settings as follows to prepare multiple audio tracks in a Scheduled Channel.

```xml
<?xml version="1.0"?>
<Schedule>
    <Stream>
        <Name>today</Name>
        <BypassTranscoder>false</BypassTranscoder>
        <VideoTrack>true</VideoTrack>
        <AudioTrack>true</AudioTrack>
        <AudioMap>
            <Item>
                <Name>English</Name>
                <Language>en</Language> <!-- Optioanl, RFC 5646 -->
                <Characteristics>public.accessibility.describes-video</Characteristics> <!-- Optional -->
            </Item>
            <Item>
                <Name>Korean</Name>
                <Language>ko</Language> <!-- Optioanl, RFC 5646 -->
                <Characteristics>public.alternate</Characteristics> <!-- Optional -->
            </Item>
            <Item>
                <Name>Japanese</Name>
                <Language>ja</Language> <!-- Optioanl, RFC 5646 -->
                <Characteristics>public.alternate</Characteristics> <!-- Optional -->
            </Item>
        </AudioMap>
    </Stream>
</Schedule>
```

{% hint style="warning" %}
A Scheduled Channel creates streams in advance and copies tracks from files or other streams. Therefore, all source content used in a Scheduled Channel with multiple audio tracks must provide at least the same number of audio tracks. Otherwise, the content will not be scheduled.
{% endhint %}

## Application : Persistent Live Channel

This function is a scheduling channel, but it can be used for applications such as creating a permanent stream as follows.

```xml
<?xml version="1.0"?>
<Schedule>
    <Stream>
        <Name>stream</Name>
        <BypassTranscoder>false</BypassTranscoder>
        <VideoTrack>true</VideoTrack>
        <AudioTrack>true</AudioTrack>
    </Stream>
    <FallbackProgram>
        <Item url="file://hevc.mov" />
        <Item url="file://avc.mov" />
    </FallbackProgram>

    <Program name="origin" scheduled="2000-01-01T20:57:00.000+09" repeat="true">
        <Item url="stream://default/app/input" duration="-1" />
    </Program>
</Schedule>
```

This channel normally plays `default/app/input`, but when live input is stopped, it plays the file in `<FallbackProgram>`. This will last forever until the .sch file is deleted. One trick was to set the origin program's schedule time to year 2000 so that this stream would play unconditionally.

{% hint style="warning" %}
You may experience some buffering when going from file to live. This is unavoidable due to the nature of the function and low latency. If this is inconvenient, buffering issues can disappear if you add a little delay in advance by setting PartHoldBack in LLHLS to 5 or more. It is a choice between delay and buffering.
{% endhint %}

## REST API

ScheduledChannel can also be controlled via API. Please refer to the page below.

{% content-ref url="../rest-api/v1/virtualhost/application/scheduledchannel-api.md" %}
[scheduledchannel-api.md](../rest-api/v1/virtualhost/application/scheduledchannel-api.md)
{% endcontent-ref %}
