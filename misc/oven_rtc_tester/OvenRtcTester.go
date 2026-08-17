package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math"
	"os"
	"os/signal"
	"sync"
	"time"
	"net/url"

	"github.com/gorilla/websocket"
	"github.com/pion/webrtc/v3"
)

const delayWarningThreshold = 1000  // ms
const lossWarningThreshold = 5		// percentage
const delayReportPeriod = 5000 		// ms

func main() {
	requestURL := flag.String("url", "undefined", "[Required] OvenMediaEngine's webrtc streaming URL")
	numberOfClient := flag.Int("n", 1, "[Optional] Number of client")
	connectionInterval := flag.Int("cint", 100, "[Optional] PeerConnection connection interval (milliseconds)")
	summaryInterval := flag.Int("sint", 5000, "[Optional] Summary information output cycle (milliseconds)")
	lifetime := flag.Int("life", 0, "[Optional] Number of times to execute the test (seconds) (default \"indefinitely\")")

	flag.Usage = func() {
		
		flag.PrintDefaults()
	}

	flag.Parse()
	_, err := url.ParseRequestURI(*requestURL)
	// URL is mandatory
	if err != nil {
		fmt.Printf("-url parameter is required and must be vaild. (input : %s)\n", *requestURL)
		flag.PrintDefaults()
		return
	}


	clientChan := make(chan *omeClient)
	quit := make(chan bool)
	go func() {
		tf := time.NewTicker(time.Duration(*connectionInterval) * time.Millisecond)
		for i := 0; i < *numberOfClient; i++ {
			select {
			case <- quit:
				return
			case <- tf.C:
				client := omeClient{}
				client.name = fmt.Sprintf("client_%d", i)
				err := client.run(*requestURL)
				if err != nil {
					fmt.Printf("%s failed to run (reason - %s)\n", client.name, err)
					return
				}

				clientChan <- &client
				fmt.Printf("%s has started\n", client.name)
			}
		}
	}()

	closed := make(chan os.Signal, 1)
	signal.Notify(closed, os.Interrupt)
	
	var clients = make([]*omeClient, 0, *numberOfClient)
	summaryTimeout := time.After(time.Millisecond * time.Duration(*summaryInterval));

	lifetimeDuration := time.Duration(0)
	if *lifetime == 0 {
		lifetimeDuration = math.MaxInt64
	}else {
		lifetimeDuration = time.Second * time.Duration(*lifetime)
	}
	lifeTimeout := time.After(lifetimeDuration)
	
	F :
	for {
		select {	
		case client := <- clientChan:
			clients = append(clients, client)

		case <- summaryTimeout:
			reportSummury(&clients)
			// Reset timer
			summaryTimeout = time.After(time.Millisecond * time.Duration(*summaryInterval));

		case <- lifeTimeout:
			fmt.Printf("Test ended (lifetime : %d seconds)\n", *lifetime);
			close(quit)
			break F

		case <-closed:
			fmt.Printf("Test stopped by user\n");
			break F
		}
	}

	fmt.Println("***************************")
	fmt.Println("Reports")
	fmt.Println("***************************")

	reportSummury(&clients)

	fmt.Println("<Details>")
	for _, client := range clients {
		if client == nil {
			continue
		}
		client.report()
		client.stop()
	}
}

type signalingClient struct {
	url    string
	socket *websocket.Conn
}

type sessionStat struct {
	startTime time.Time

	connectionState webrtc.ICEConnectionState

	maxFPS float64
	avgFPS float64
	minFPS float64

	maxBPS int64
	avgBPS int64
	minBPS int64

	totalBytes       int64
	totalVideoFrames int64
	totalVideoKeyframes int64
	totalRtpPackets  int64
	packetLoss       int64

	videoRtpTimestampElapsedMSec int64
	videoDelay float64
	audioRtpTimestampElapsedMSec int64
	audioDelay float64
}

type omeClient struct {
	name string

	sc             *signalingClient
	peerConnection *webrtc.PeerConnection

	videoAvailable bool
	videoMimeType  string
	audioAvailable bool
	audioMimeType  string

	stat sessionStat

	once  sync.Once
}

func (sc *signalingClient) connect(url string) error {

	sc.url = url

	var err error
	sc.socket, _, err = websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		return fmt.Errorf("could not connect signaling server : %s (reason : %s)", url, err)
	}

	return nil
}

func (sc *signalingClient) close() {
	if sc.socket != nil {
		sc.socket.Close()
	}
}

func (sc *signalingClient) getOffer() (*signalMessage, error) {
	if sc.socket == nil {
		return nil, fmt.Errorf("not connected")
	}

	err := sc.socket.WriteMessage(websocket.TextMessage, signalMessage{Command: "request_offer"}.marshal())
	if err != nil {
		return nil, fmt.Errorf("could not request offer : %s (reason : %s)", sc.url, err)
	}

	_, offerMsg, err := sc.socket.ReadMessage()
	if err != nil {
		return nil, fmt.Errorf("could not recevied offer message : %s (reason : %s)", sc.url, err)
	}

	var offer signalMessage
	err = offer.unmarshal(offerMsg)
	if err != nil {
		return nil, fmt.Errorf("could not unmarshal offer message : %s (reason : %s)", sc.url, err)
	}

	if offer.Sdp.Type == 0 {
		return nil, fmt.Errorf("invalid offer message received : %s (%s)", sc.url, string(offerMsg))
	}

	return &offer, nil
}

func (sc *signalingClient) sendAnswer(answer signalMessage) error {
	byteAnswer, _ := json.Marshal(answer)
	return sc.socket.WriteMessage(websocket.TextMessage, byteAnswer)
}

func (sc *signalingClient) readPump() error {
	for {
		_, data, err := sc.socket.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				return fmt.Errorf("error: %v", err)
			}
			break
		}
		
		// Not used
		_ = data
	}

	return nil
}

// Old version of OME gives username as user_name
type ICEServer struct {
	URLs       []string `json:"urls"`
	User_name   string   `json:"user_name,omitempty"`
	Credential string   `json:"credential,omitempty"`
}

type signalMessage struct {
	Command    string                    `json:"command"`
	Id         int64                     `json:"id,omitempty"`
	PeerId     int64                     `json:"peer_id,omitempty"`
	Sdp        webrtc.SessionDescription `json:"sdp,omitempty"`
	Candidates []webrtc.ICECandidateInit `json:"candidates"`
	// Format given by the old version of OME, it will be deprecated
	ICE_Servers []ICEServer              `json:"ice_servers,omitempty"`
	// Format given by the latest version of OME
	ICEServers []webrtc.ICEServer		 `json:"iceServers,omitempty"`
}

func (msg signalMessage) marshal() []byte {
	jsonMsg, _ := json.Marshal(msg)
	return jsonMsg
}

func (jsonMsg *signalMessage) unmarshal(msg []byte) error {
	return json.Unmarshal(msg, jsonMsg)
}

func reportSummury(clients *[]*omeClient) {
	fmt.Println("<Summary>")

	clientCount := int64(len(*clients))
	var firstSessionStartTime time.Time 
	if clientCount > 0 {
		firstSessionStartTime = ((*clients)[0]).stat.startTime
		fmt.Printf("Running time : %s\n", time.Since(firstSessionStartTime).Round(time.Second))
	}

	fmt.Printf("Number of clients : %d\n", clientCount)

	if clientCount == 0 {
		return
	}

	connectionStateCount := struct {
						ICEConnectionStateNew int
						ICEConnectionStateChecking int
						ICEConnectionStateConnected int
						ICEConnectionStateCompleted int
						ICEConnectionStateDisconnected int
						ICEConnectionStateFailed int
						ICEConnectionStateClosed int
					} {0,0,0,0,0,0,0}
	
	

	totalStat := sessionStat{}

	var minVideoDelay, maxVideoDelay, minAudioDelay, maxAudioDelay float64
	var minAvgFPS, maxAvgFPS float64
	var minAvgBPS, maxAvgBPS int64
	var minGOP, maxGOP float64

	for _, client := range *clients {
		stat := client.stat

		switch stat.connectionState {
		case webrtc.ICEConnectionStateNew:
			connectionStateCount.ICEConnectionStateNew ++
		case webrtc.ICEConnectionStateChecking:
			connectionStateCount.ICEConnectionStateChecking ++
		case webrtc.ICEConnectionStateConnected:
			connectionStateCount.ICEConnectionStateConnected ++
		case webrtc.ICEConnectionStateCompleted:
			connectionStateCount.ICEConnectionStateCompleted ++
		case webrtc.ICEConnectionStateDisconnected:
			connectionStateCount.ICEConnectionStateDisconnected ++
		case webrtc.ICEConnectionStateFailed:
			connectionStateCount.ICEConnectionStateFailed ++
		case webrtc.ICEConnectionStateClosed:
			connectionStateCount.ICEConnectionStateClosed ++
		}

		if stat.connectionState != webrtc.ICEConnectionStateConnected {
			continue
		}

		gop := float64(stat.totalVideoFrames) / float64(stat.totalVideoKeyframes)

		if totalStat.startTime.IsZero() {
			totalStat = stat

			maxAvgFPS = totalStat.avgFPS
			minAvgFPS = totalStat.avgFPS
			maxAvgBPS = totalStat.avgBPS
			minAvgBPS = totalStat.avgBPS

			minVideoDelay = totalStat.videoDelay
			maxVideoDelay = totalStat.videoDelay
			minAudioDelay = totalStat.audioDelay
			maxAudioDelay = totalStat.audioDelay

			minGOP = gop
			maxGOP = gop

			continue
		}
		
		minAvgFPS = math.Min(minAvgFPS, stat.avgFPS)
		maxAvgFPS = math.Max(maxAvgFPS, stat.avgFPS)
		
		minAvgBPS = int64(math.Min(float64(minAvgBPS), float64(stat.avgBPS)))
		maxAvgBPS = int64(math.Max(float64(maxAvgBPS), float64(stat.avgBPS)))

		minVideoDelay = math.Min(minVideoDelay, stat.videoDelay)
		maxVideoDelay = math.Max(maxVideoDelay, stat.videoDelay)

		minAudioDelay = math.Min(minAudioDelay, stat.audioDelay)
		maxAudioDelay = math.Max(maxAudioDelay, stat.audioDelay)

		minGOP = math.Min(minGOP, gop)
		maxGOP = math.Max(maxGOP, gop)

		// This will output the average value.
		totalStat.avgBPS += stat.avgBPS
		totalStat.avgFPS += stat.avgFPS
		totalStat.totalBytes += stat.totalBytes
		totalStat.totalVideoFrames += stat.totalVideoFrames
		totalStat.totalVideoKeyframes += stat.totalVideoKeyframes
		totalStat.totalRtpPackets += stat.totalRtpPackets
		totalStat.packetLoss += stat.packetLoss
		totalStat.videoDelay += stat.videoDelay
		totalStat.audioDelay += stat.audioDelay
	}

	fmt.Printf("ICE Connection State : New(%d), Checking(%d) Connected(%d) Completed(%d) Disconnected(%d) Failed(%d) Closed(%d)\n", 
	connectionStateCount.ICEConnectionStateNew, connectionStateCount.ICEConnectionStateChecking, connectionStateCount.ICEConnectionStateConnected, connectionStateCount.ICEConnectionStateCompleted, connectionStateCount.ICEConnectionStateDisconnected, connectionStateCount.ICEConnectionStateFailed, connectionStateCount.ICEConnectionStateClosed)

	connected := int64(connectionStateCount.ICEConnectionStateConnected)
	if connected == 0 {
		return
	}

	fmt.Printf("Avg Video Delay(%.2f ms) Max Video Delay(%.2f ms) Min Video Delay(%.2f ms)\nAvg Audio Delay(%.2f ms) Max Audio Delay(%.2f ms) Min Audio Delay(%.2f ms)\n", totalStat.videoDelay/float64(connected), maxVideoDelay, minVideoDelay, totalStat.audioDelay/float64(connected), maxAudioDelay, minAudioDelay)

	fmt.Printf("Avg GOP(%.2f) Max GOP(%.2f) Min GOP(%2.f)\n", float64(totalStat.totalVideoFrames) / float64(totalStat.totalVideoKeyframes), maxGOP, minGOP)
	
	fmt.Printf("Avg FPS(%.2f) Max FPS(%.2f) Min FPS(%.2f)\nAvg BPS(%sbps) Max BPS(%sbps) Min BPS(%sbps)\n", totalStat.avgFPS/float64(connected), maxAvgFPS, minAvgFPS, CountDecimal(totalStat.avgBPS/connected), CountDecimal(maxAvgBPS), CountDecimal(minAvgBPS))
	
	fmt.Printf("Total Bytes(%sBytes) Avg Bytes(%sBytes)\nTotal Packets(%d) Avg Packets(%d)\nTotal Packet Losses(%d) Avg Packet Losses(%d)\n", CountDecimal(totalStat.totalBytes), CountDecimal(totalStat.totalBytes/connected), totalStat.totalRtpPackets, totalStat.totalRtpPackets/connected, totalStat.packetLoss, totalStat.packetLoss/connected)

	fmt.Printf("\n")
}

func (c *omeClient) report() {

	//c.mutex.Lock()
	// Copy and Unlock
	stat := c.stat
	//c.mutex.Unlock()

	videoDelay := float64(0)
	audioDelay := float64(0)

	if stat.videoRtpTimestampElapsedMSec != 0 {
		videoDelay = math.Abs(float64(time.Since(stat.startTime).Milliseconds() - stat.videoRtpTimestampElapsedMSec))
	}

	if stat.audioRtpTimestampElapsedMSec != 0 {
		audioDelay = math.Abs(float64(time.Since(stat.startTime).Milliseconds() - stat.audioRtpTimestampElapsedMSec))
	}

	fmt.Printf("%c[32m[%s]%c[0m\n\trunning_time(%s) connection_state(%s) total_packets(%d) packet_loss(%d)\n\tlast_video_delay (%.1f ms) last_audio_delay (%.1f ms)\n\ttotal_bytes(%sbytes) avg_bps(%sbps) min_bps(%sbps) max_bps(%sbps)\n\ttotal_video_frames(%d) total_video_keyframes(%d) avg_gop(%2.f) avg_fps(%.2f) min_fps(%.2f) max_fps(%.2f)\n\n",
		27, c.name, 27, time.Since(stat.startTime).Round(time.Second), stat.connectionState.String(), stat.totalRtpPackets, stat.packetLoss, videoDelay, audioDelay, CountDecimal(stat.totalBytes), CountDecimal(stat.avgBPS), CountDecimal(stat.minBPS), CountDecimal(stat.maxBPS), stat.totalVideoFrames, stat.totalVideoKeyframes, float64(stat.totalVideoFrames) / float64(stat.totalVideoKeyframes), stat.avgFPS, stat.minFPS, stat.maxFPS)
}

func (c *omeClient) run(url string) error {

	// Initialize
	c.sc = &signalingClient{}
	c.peerConnection = &webrtc.PeerConnection{}

	// Connect to OME signaling server
	err := c.sc.connect(url)
	if err != nil {
		return err
	}

	// Request offer sdp
	offer, err := c.sc.getOffer()
	if err != nil {
		return err
	}

	// Everything below is the pion-WebRTC API! Thanks for making it ❤️.

	// webrtc config
	config := webrtc.Configuration{
		ICEServers: []webrtc.ICEServer{
			// When playing OME's stream, there is no need to obtain a candidate with stun server
			// {
			// 	URLs: []string{"stun:stun.l.google.com:19302"},
			// },
		},
	}

	// The latest version of OME delivers information in two formats: iceServers and ice_servers. ice_servers will be deprecated. 
	// Use iceServers first, then use ice_servers if iceServers is not exist for backward compatibility.	
	if len(offer.ICEServers) > 0 {
		config.ICEServers = append(config.ICEServers, offer.ICEServers...)
	} else {
		for _, iceServer := range offer.ICE_Servers {
			// The ICEServer information provided by old OME has a different format. (user_name => username)
			config.ICEServers = append(config.ICEServers, webrtc.ICEServer{URLs: iceServer.URLs, Username: iceServer.User_name, Credential: iceServer.Credential})
		}
	}

	if len(offer.ICE_Servers) > 0 || len(offer.ICEServers) > 0 {
		config.ICETransportPolicy = webrtc.ICETransportPolicyRelay
	}

	// Create a MediaEngine object to configure the supported codec
	engine := &webrtc.MediaEngine{}

	if err := engine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{MimeType: "video/H264", ClockRate: 90000, Channels: 0, SDPFmtpLine: "", RTCPFeedback: nil},
	}, webrtc.RTPCodecTypeVideo); err != nil {
		panic(err)
	}
	if err := engine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{MimeType: "video/VP8", ClockRate: 90000, Channels: 0, SDPFmtpLine: "", RTCPFeedback: nil},
	}, webrtc.RTPCodecTypeVideo); err != nil {
		panic(err)
	}
	if err := engine.RegisterCodec(webrtc.RTPCodecParameters{
		RTPCodecCapability: webrtc.RTPCodecCapability{MimeType: "audio/opus", ClockRate: 48000, Channels: 0, SDPFmtpLine: "", RTCPFeedback: nil},
	}, webrtc.RTPCodecTypeAudio); err != nil {
		panic(err)
	}

	setting := webrtc.SettingEngine{}
	setting.SetICETimeouts(30 * time.Second, 30 * time.Second, 1 * time.Second)
	webrtcAPI := webrtc.NewAPI(webrtc.WithMediaEngine(engine), webrtc.WithSettingEngine(setting))
	c.peerConnection, err = webrtcAPI.NewPeerConnection(config)
	if err != nil {
		return err
	}

	// RTP start
	c.peerConnection.OnTrack(func(track *webrtc.TrackRemote, receiver *webrtc.RTPReceiver) {

		fmt.Printf("%s track has started, of type %d: %s \n", c.name, track.PayloadType(), track.Codec().RTPCodecCapability.MimeType)
		c.stat.startTime = time.Now()

		switch track.Kind() {
		case webrtc.RTPCodecTypeVideo:
			c.videoAvailable = true
			c.videoMimeType = track.Codec().MimeType
		case webrtc.RTPCodecTypeAudio:
			c.audioAvailable = true
			c.audioMimeType = track.Codec().MimeType
		}

		go func() {

			lastTime := time.Now()
			lastFrames := int64(0)
			lastBytes := int64(0)

			ticker := time.NewTicker(time.Second * 1)
			// fps/bps calculator, One ticker is sufficient.
			c.once.Do(func() {
				for range ticker.C {

					stat := c.stat

					currVideoFrames := stat.totalVideoFrames
					currTotalBytes := stat.totalBytes

					fps := float64(float64(currVideoFrames-lastFrames) / float64(time.Since(lastTime).Seconds()))
					bps := int64((float64(currTotalBytes-lastBytes) / float64(time.Since(lastTime).Seconds())) * 8)

					//c.mutex.Lock()
					if c.stat.maxFPS == 0 || c.stat.maxFPS < fps {
						c.stat.maxFPS = fps
					}

					if c.stat.minFPS == 0 || c.stat.minFPS > fps {
						c.stat.minFPS = fps
					}

					if c.stat.maxBPS == 0 || c.stat.maxBPS < bps {
						c.stat.maxBPS = bps
					}

					if c.stat.minBPS == 0 || c.stat.minBPS > bps {
						c.stat.minBPS = bps
					}

					c.stat.avgBPS = int64(float32(stat.totalBytes) / float32(time.Since(stat.startTime).Seconds()) * 8)
					c.stat.avgFPS = float64(stat.totalVideoFrames) / float64(time.Since(stat.startTime).Seconds())


					lastTime = time.Now()
					lastFrames = currVideoFrames
					lastBytes = currTotalBytes
				}
			})
		}()

		prev_seq := uint16(0)
		startTimestamp := uint32(0)
		last_report_time := time.Now().AddDate(0, 0, -1)

		for {
			// Read RTP packets being sent to Pion
			rtp, _, readErr := track.ReadRTP()
			if readErr != nil {
				if readErr == io.EOF {
					return
				}
				panic(readErr)
			}

			if startTimestamp == 0 {
				startTimestamp = rtp.Timestamp
			}

			// total packet count

			c.stat.totalRtpPackets++
			c.stat.totalBytes += int64(rtp.PayloadOffset) + int64(len(rtp.Payload))

			// packet loss metric
			if prev_seq != 0 && rtp.SequenceNumber != prev_seq+1 {
				c.stat.packetLoss++
			}
			prev_seq = rtp.SequenceNumber

			need_delay_warn := false

			switch track.Kind() {
			case webrtc.RTPCodecTypeVideo:
				if rtp.Marker {
					c.stat.totalVideoFrames++

					framemarking := rtp.GetExtension(1)
					if len(framemarking) > 0 {
						// Start && Keyframe
						if uint8(framemarking[0]) & 0x20 != 0 {
							c.stat.totalVideoKeyframes++
						}
					}

					c.stat.videoRtpTimestampElapsedMSec = int64((float64(rtp.Timestamp-startTimestamp) / float64(track.Codec().ClockRate) * 1000))

					c.stat.videoDelay = math.Abs(float64(time.Since(c.stat.startTime).Milliseconds() - c.stat.videoRtpTimestampElapsedMSec))
					if c.stat.videoDelay > delayWarningThreshold {
						need_delay_warn = true
					}
				}
			case webrtc.RTPCodecTypeAudio:
				c.stat.audioRtpTimestampElapsedMSec = int64((float64(rtp.Timestamp-startTimestamp) / float64(track.Codec().ClockRate) * 1000))

				c.stat.audioDelay = math.Abs(float64(time.Since(c.stat.startTime).Milliseconds() - c.stat.audioRtpTimestampElapsedMSec))
				if c.stat.audioDelay > delayWarningThreshold {
					need_delay_warn = true
				}
			}

			// packet loss rate
			packetLossRate := (float32(c.stat.packetLoss)/float32(c.stat.totalRtpPackets)) * 100
			// 5%
			if(packetLossRate > lossWarningThreshold) {
				need_delay_warn = true
			}

			
			// report() must be called after mutex.Unlock
			if need_delay_warn && time.Since(last_report_time).Milliseconds() > delayReportPeriod {
				fmt.Printf("%c[31m[warning]%c[0m delay or packet loss is too high!\n", 27, 27)
				c.report()

				last_report_time = time.Now()
			}
		}
	})

	c.peerConnection.OnICEConnectionStateChange(func(connectionState webrtc.ICEConnectionState) {
		fmt.Printf("%s connection state has changed %s \n", c.name, connectionState.String())
		c.stat.connectionState = connectionState
	})

	c.peerConnection.OnICECandidate(func(candidate *webrtc.ICECandidate) {
		if candidate == nil {
			return
		}
		fmt.Printf("%s candidate found\n", candidate.String())
	})

	// Set remote SessionDescription
	err = c.peerConnection.SetRemoteDescription(offer.Sdp)
	if err != nil {
		return err
	}

	// Set remote IceCandidates
	for _, candidate := range offer.Candidates {
		c.peerConnection.AddICECandidate(candidate)
	}

	// Create an answer
	answerSdp, err := c.peerConnection.CreateAnswer(nil)
	if err != nil {
		return err
	}

	err = c.sc.sendAnswer(signalMessage{"answer", offer.Id, 0, answerSdp, nil, nil, nil})
	if err != nil {
		return err
	}
	
	err = c.peerConnection.SetLocalDescription(answerSdp)
	if err != nil {
		return err
	}

	// Read signaling messages 
	go c.sc.readPump()

	return nil
}

func (c *omeClient) stop() error {

	if c.sc != nil {
		c.sc.close()
	}

	if c.peerConnection != nil {
		c.peerConnection.Close()
	}

	fmt.Printf("%s has stopped\n", c.name)

	return nil
}

// utilities
func CountDecimal(b int64) string {
	const unit = 1000
	if b < unit {
		return fmt.Sprintf("%d", b)
	}
	div, exp := int64(unit), 0
	for n := b / unit; n >= unit; n /= unit {
		div *= unit
		exp++
	}
	return fmt.Sprintf("%.1f %c", float64(b)/float64(div), "kMGTPE"[exp])
}