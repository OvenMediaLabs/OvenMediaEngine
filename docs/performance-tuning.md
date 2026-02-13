# Performance Tuning

## Performance Test

OvenMediaEngine provides a tester for measuring WebRTC performance called OvenRtcTester. It is developed in Go language and uses the `pion/webrtc/v3` and `gorilla/websocket` modules. Many thanks to the [pion/webrtc](https://github.com/pion/webrtc/) and [gorilla/websocket](https://github.com/gorilla/websocket) teams for contributing this wonderful project.

### Getting Started OvenRtcTester

#### Install GO&#x20;

Since OvenRtcTester is developed in Go language, Go must be installed on your system. Install Go from the following URL: [https://golang.org/doc/install](https://golang.org/doc/install)

OvenRtcTester was tested with the latest version of go 1.17.

#### Run&#x20;

You can simply run it like this: `-url` is required. If the `-life` option is not used, it will run indefinitely until the user presses `ctrl+c`.

```bash
$ cd OvenMediaEngine/misc/oven_rtc_tester
$ go run OvenRtcTester.go
-url parameter is required and must be vaild. (input : undefined)
  -cint int
        [Optional] PeerConnection connection interval (milliseconds) (default 100)
  -life int
        [Optional] Number of times to execute the test (seconds)
  -n int
        [Optional] Number of client (default 1)
  -sint int
        [Optional] Summary information output cycle (milliseconds) (default 5000)
  -url string
        [Required] OvenMediaEngine's webrtc streaming URL (default "undefined")

```

You can also use `go build` or `go install` depending on your preference.

{% hint style="warning" %}
OvenRtcTester must test OvenMediaEngine 0.12.4 or higher as the target system. OvenMediaEngine versions below 0.12.4 have a problem with incorrectly calculating the RTP timestamp, so OvenRtcTester calculates the `Video Delay` value incorrectly.
{% endhint %}

```bash
$ go run OvenRtcTester.go -url ws://192.168.0.160:13333/app/stream -n 5
client_0 connection state has changed checking 
client_0 has started
client_1 connection state has changed checking 
client_1 has started
client_0 connection state has changed connected 
client_1 connection state has changed connected 
client_1 track has started, of type 100: video/H264 
client_0 track has started, of type 100: video/H264 
client_1 track has started, of type 101: audio/OPUS 
client_0 track has started, of type 101: audio/OPUS 
client_2 connection state has changed checking 
client_2 has started
client_2 connection state has changed connected 
client_2 track has started, of type 100: video/H264 
client_2 track has started, of type 101: audio/OPUS 
client_3 connection state has changed checking 
client_3 has started
client_3 connection state has changed connected 
client_3 track has started, of type 100: video/H264 
client_3 track has started, of type 101: audio/OPUS 
client_4 connection state has changed checking 
client_4 has started
client_4 connection state has changed connected 
client_4 track has started, of type 100: video/H264 
client_4 track has started, of type 101: audio/OPUS 
<Summary>
Running time : 5s
Number of clients : 5
ICE Connection State : New(0), Checking(0) Connected(5) Completed(0) Disconnected(0) Failed(0) Closed(0)
Avg Video Delay(54.20 ms) Max Video Delay(55.00 ms) Min Video Delay(53.00 ms)
Avg Audio Delay(37.00 ms) Max Audio Delay(55.00 ms) Min Audio Delay(26.00 ms)
Avg FPS(30.15) Max FPS(30.25) Min FPS(30.00)
Avg BPS(4.1 Mbps) Max BPS(4.1 Mbps) Min BPS(4.0 Mbps)
Total Bytes(11.6 MBytes) Avg Bytes(2.3 MBytes)
Total Packets(13897) Avg Packets(2779)
Total Packet Losses(0) Avg Packet Losses(0)

<Summary>
Running time : 10s
Number of clients : 5
ICE Connection State : New(0), Checking(0) Connected(5) Completed(0) Disconnected(0) Failed(0) Closed(0)
Avg Video Delay(43.60 ms) Max Video Delay(45.00 ms) Min Video Delay(42.00 ms)
Avg Audio Delay(36.60 ms) Max Audio Delay(55.00 ms) Min Audio Delay(25.00 ms)
Avg FPS(30.04) Max FPS(30.11) Min FPS(30.00)
Avg BPS(4.0 Mbps) Max BPS(4.0 Mbps) Min BPS(4.0 Mbps)
Total Bytes(24.3 MBytes) Avg Bytes(4.9 MBytes)
Total Packets(28832) Avg Packets(5766)
Total Packet Losses(0) Avg Packet Losses(0)

<Summary>
Running time : 15s
Number of clients : 5
ICE Connection State : New(0), Checking(0) Connected(5) Completed(0) Disconnected(0) Failed(0) Closed(0)
Avg Video Delay(36.60 ms) Max Video Delay(38.00 ms) Min Video Delay(35.00 ms)
Avg Audio Delay(49.20 ms) Max Audio Delay(68.00 ms) Min Audio Delay(38.00 ms)
Avg FPS(30.07) Max FPS(30.07) Min FPS(30.07)
Avg BPS(4.0 Mbps) Max BPS(4.0 Mbps) Min BPS(4.0 Mbps)
Total Bytes(36.8 MBytes) Avg Bytes(7.4 MBytes)
Total Packets(43717) Avg Packets(8743)
Total Packet Losses(0) Avg Packet Losses(0)

^CTest stopped by user
***************************
Reports
***************************
<Summary>
Running time : 15s
Number of clients : 5
ICE Connection State : New(0), Checking(0) Connected(5) Completed(0) Disconnected(0) Failed(0) Closed(0)
Avg Video Delay(23.60 ms) Max Video Delay(25.00 ms) Min Video Delay(22.00 ms)
Avg Audio Delay(11.20 ms) Max Audio Delay(18.00 ms) Min Audio Delay(5.00 ms)
Avg FPS(30.07) Max FPS(30.07) Min FPS(30.07)
Avg BPS(4.0 Mbps) Max BPS(4.0 Mbps) Min BPS(4.0 Mbps)
Total Bytes(38.6 MBytes) Avg Bytes(7.7 MBytes)
Total Packets(45662) Avg Packets(9132)
Total Packet Losses(0) Avg Packet Losses(0)

<Details>
[client_0]
        running_time(15s) connection_state(connected) total_packets(9210) packet_loss(0)
        last_video_delay (22.0 ms) last_audio_delay (52.0 ms)
        total_bytes(7.8 Mbytes) avg_bps(4.0 Mbps) min_bps(3.6 Mbps) max_bps(4.3 Mbps)
        total_video_frames(463) avg_fps(30.07) min_fps(28.98) max_fps(31.00)

client_0 connection state has changed closed 
client_0 has stopped
[client_1]
        running_time(15s) connection_state(connected) total_packets(9210) packet_loss(0)
        last_video_delay (22.0 ms) last_audio_delay (52.0 ms)
        total_bytes(7.8 Mbytes) avg_bps(4.0 Mbps) min_bps(3.6 Mbps) max_bps(4.3 Mbps)
        total_video_frames(463) avg_fps(30.07) min_fps(28.98) max_fps(31.00)

client_1 has stopped
[client_2]
        running_time(15s) connection_state(connected) total_packets(9145) packet_loss(0)
        last_video_delay (23.0 ms) last_audio_delay (63.0 ms)
        total_bytes(7.7 Mbytes) avg_bps(4.0 Mbps) min_bps(3.6 Mbps) max_bps(4.5 Mbps)
        total_video_frames(460) avg_fps(30.07) min_fps(28.97) max_fps(31.02)

client_1 connection state has changed closed 
client_2 has stopped
[client_3]
        running_time(15s) connection_state(connected) total_packets(9081) packet_loss(0)
        last_video_delay (25.0 ms) last_audio_delay (65.0 ms)
        total_bytes(7.7 Mbytes) avg_bps(4.0 Mbps) min_bps(3.6 Mbps) max_bps(4.3 Mbps)
        total_video_frames(457) avg_fps(30.07) min_fps(29.00) max_fps(31.03)

client_2 connection state has changed closed 
client_3 has stopped
client_3 connection state has changed closed 
[client_4]
        running_time(15s) connection_state(connected) total_packets(9016) packet_loss(0)
        last_video_delay (26.0 ms) last_audio_delay (36.0 ms)
        total_bytes(7.6 Mbytes) avg_bps(4.0 Mbps) min_bps(3.6 Mbps) max_bps(4.3 Mbps)
        total_video_frames(454) avg_fps(30.07) min_fps(28.99) max_fps(31.02)

client_4 has stopped
```

## Performance Tuning

### Monitoring the usage of threads

Linux has various tools to monitor CPU usage per thread. We will check the simplest with the top command. If you issue the `top -H -p` \[pid] command, you will see the following screen.

![](<.gitbook/assets/image (34).png>)

You can use OvenRtcTester to test the capacity of the server as shown below. When testing the maximum performance, OvenRtcTester also uses a lot of system resources, so test it separately from the system where OvenMediaEngine is running. Also, it is recommended to test OvenRtcTester with multiple servers. For example, simulate 500 players with -n 500 on one OvenRtcTester, and simulate 2000 players with four servers.

{% hint style="warning" %}
Building and running OvenMediaEngine in debug mode results in very poor performance. Be sure to test the maximum performance using the binary generated by make release && make install .
{% endhint %}

```
$ go run OvenRtcTester.go -url ws://192.168.0.160:13333/app/stream -n 100
client_0 connection state has changed checking 
client_0 has started
client_0 connection state has changed connected 
client_0 track has started, of type 100: video/H264 
client_0 track has started, of type 101: audio/OPUS 
client_1 connection state has changed checking 
client_1 has started
client_1 connection state has changed connected 
client_1 track has started, of type 100: video/H264 
client_1 track has started, of type 101: audio/OPUS 
client_2 connection state has changed checking 
client_2 has started
client_2 connection state has changed connected 
client_2 track has started, of type 100: video/H264 
client_2 track has started, of type 101: audio/OPUS
....
client_94 connection state has changed checking 
client_94 has started
client_94 connection state has changed connected 
client_94 track has started, of type 100: video/H264 
client_94 track has started, of type 101: audio/OPUS 
client_95 connection state has changed checking 
client_95 has started
client_95 connection state has changed connected 
client_95 track has started, of type 100: video/H264 
client_95 track has started, of type 101: audio/OPUS 
client_96 connection state has changed checking 
client_96 has started
<Summary>
Running time : 10s
Number of clients : 97
ICE Connection State : New(0), Checking(1) Connected(96) Completed(0) Disconnected(0) Failed(0) Closed(0)
Avg Video Delay(13.51 ms) Max Video Delay(47.00 ms) Min Video Delay(0.00 ms)
Avg Audio Delay(22.42 ms) Max Audio Delay(67.00 ms) Min Audio Delay(0.00 ms)
Avg FPS(27.20) Max FPS(32.51) Min FPS(0.00)
Avg BPS(3.7 Mbps) Max BPS(4.6 Mbps) Min BPS(0bps)
Total Bytes(238.7 MBytes) Avg Bytes(2.5 MBytes)
Total Packets(285013) Avg Packets(2938)
Total Packet Losses(306) Avg Packet Losses(3)
```

If the OvenMediaEngine's capacity is exceeded, you will notice it in OvenRtcTester's Summary report with `Avg Video Delay` and `Avg Audio Delay` or `Packet loss`.&#x20;

![](<.gitbook/assets/image (36).png>)

On the right side of the above capture screen, we simulate 400 players with OvenRtcTester.  `<Summary>` of OvenRtcTester shows that `Avg Video Delay` and `Avg Audio Delay` are very high, and `Avg FPS` is low.

And on the left, you can check the CPU usage by thread with the `top -H -p` command. This confirms that the `StreamWorker` threads are being used at 100%, and now you can scale the server by increasing the number of `StreamWorker` threads. If OvenMediaEngine is not using 100% of all cores of the server, you can improve performance by [tuning the number of threads](performance-tuning.md#tuning-the-number-of-threads).

![](<.gitbook/assets/image (37).png>)

This is the result of tuning the number of `StreamWorkerCount` to 8 in config. This time, we simulated 1000 players with OvenRtcTester, and you can see that it works stably.

### Tuning the number of threads

The `WorkerCount` in `<Bind>` can set the thread responsible for sending and receiving over the socket. Publisher's `AppWorkerCount` allows you to set the number of threads used for per-stream processing such as RTP packaging, and `StreamWorkerCount` allows you to set the number of threads for per-session processing such as SRTP encryption.

```xml

<Bind>
    <Providers>
        <RTMP>
            <Port>1935</Port>
            <WorkerCount>1</WorkerCount>
        </RTMP>
        ...
    </Providers>
    ...
    <Publishers>
        <WebRTC>
            <Signalling>
                <Port>3333</Port>
                <WorkerCount>1</WorkerCount>
            </Signalling>
            <IceCandidates>
                <TcpRelay>*:3478</TcpRelay>
                <IceCandidate>*:10000/udp</IceCandidate>
                <TcpRelayWorkerCount>1</TcpRelayWorkerCount>
            </IceCandidates>
    ...
</Bind>
        
<Application>
<Publishers>
<AppWorkerCount>1</AppWorkerCount>
<StreamWorkerCount>8</StreamWorkerCount>
</Publishers>
</Application>
```

#### Scalable Threads and Configuration

<table data-header-hidden><thead><tr><th width="289">Thread name</th><th>Element in the configuration</th></tr></thead><tbody><tr><td>Thread name</td><td>Element in the configuration</td></tr><tr><td>AW-XXX</td><td>&#x3C;Application>&#x3C;Publishers>&#x3C;AppWorkerCount></td></tr><tr><td>StreamWorker</td><td>&#x3C;Application>&#x3C;Publishers>&#x3C;StreamWorkerCount></td></tr><tr><td>SPICE-XXX</td><td><p>&#x3C;Bind>&#x3C;Provider>&#x3C;WebRTC>&#x3C;IceCandidates>&#x3C;TcpRelayWorkerCount></p><p>&#x3C;Bind>&#x3C;Pubishers>&#x3C;WebRTC>&#x3C;IceCandidates>&#x3C;TcpRelayWorkerCount></p></td></tr><tr><td>SPRtcSignalling</td><td><p>&#x3C;Bind>&#x3C;Provider>&#x3C;WebRTC>&#x3C;Signalling>&#x3C;WorkerCount></p><p>&#x3C;Bind>&#x3C;Pubishers>&#x3C;WebRTC>&#x3C;Signalling>&#x3C;WorkerCount></p></td></tr><tr><td>SPSegPub</td><td><p>&#x3C;Bind>&#x3C;Pubishers>&#x3C;HLS>&#x3C;WorkerCount></p><p>&#x3C;Bind>&#x3C;Pubishers>&#x3C;DASH>&#x3C;WorkerCount></p></td></tr><tr><td>SPRTMP-XXX</td><td>&#x3C;Bind>&#x3C;Providers>&#x3C;RTMP>&#x3C;WorkerCount></td></tr><tr><td>SPMPEGTS</td><td>&#x3C;Bind>&#x3C;Providers>&#x3C;MPEGTS>&#x3C;WorkerCount></td></tr><tr><td>SPOvtPub</td><td>&#x3C;Bind>&#x3C;Pubishers>&#x3C;OVT>&#x3C;WorkerCount></td></tr><tr><td>SPSRT</td><td>&#x3C;Bind>&#x3C;Providers>&#x3C;SRT>&#x3C;WorkerCount></td></tr></tbody></table>

#### AppWorkerCount

| Type    | Value |
| ------- | ----- |
| Default | 1     |
| Minimum | 1     |
| Maximum | 72    |

With `AppWorkerCount`, you can set the number of threads for distributed processing of streams when hundreds of streams are created in one application. When an application is requested to create a stream, the stream is evenly attached to one of created threads. The main role of Stream is to packetize raw media packets into the media format of the protocol to be transmitted. When there are thousands of streams, it is difficult to process them in one thread. Also, if `StreamWorkerCount` is set to 0, `AppWorkerCount` is responsible for sending media packets to the session.

It is recommended that this value does not exceed the number of CPU cores.

#### StreamWorkerCount

| Type    | Value |
| ------- | ----- |
| Default | 8     |
| Minimum | 0     |
| Maximum | 72    |

It may be impossible to send data to thousands of viewers in one thread. `StreamWorkerCount` allows sessions to be distributed across multiple threads and transmitted simultaneously. This means that resources required for SRTP encryption of WebRTC or TLS encryption of HLS/DASH can be distributed and processed by multiple threads. It is recommended that this value not exceed the number of CPU cores.

### Use-Case

If a large number of streams are created and very few viewers connect to each stream, increase `AppWorkerCount` and lower `StreamWorkerCount` as follows.

```
<Publishers>
  <AppWorkerCount>32</AppWorkerCount>
  <StreamWorkerCount>0</StreamWorkerCount>
</Publishers>
```

If a small number of streams are created and a very large number of viewers are connected to each stream, lower `AppWorkerCount` and increase `StreamWorkerCount` as follows.

```
<Publishers>
  <AppWorkerCount>1</AppWorkerCount>
  <StreamWorkerCount>32</StreamWorkerCount>
</Publishers>
```
