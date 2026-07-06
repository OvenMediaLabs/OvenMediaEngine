#!/usr/bin/env bash
#
# test_av1_avif.sh
#
# Reproduces the AV1-decode + AVIF-thumbnail tests for the OvenMediaEngine
# branch  feature/av1-decode-avif-thumbnails.
#
# It is deliberately linear and verbose. Read it top to bottom: each "STAGE"
# is one self-contained step that prints its own raw output. The output is
# ugly on purpose -- the point is that anyone can run this and see what happens.
#
# Needs:
#   - docker
#   - the built image  ome-av1-avif:dev   (a USE_LOCAL docker build of the branch:
#         docker build -t ome-av1-avif:dev --build-arg USE_LOCAL=true . )
#   - jrottenberg/ffmpeg:8.0-ubuntu  (pulled on first use) to push test streams
#   - host  file  and  ffprobe  for verification
#
# Run:    bash test_av1_avif.sh
# Clean:  docker rm -f ome-test pusher pusherav1 2>/dev/null

set -u

# ---- knobs ----------------------------------------------------------------
OME_IMAGE=ome-av1-avif:dev
FF_IMAGE=jrottenberg/ffmpeg:8.0-ubuntu
OME=ome-test
RTMP=21935        # host:21935 -> container 1935   (1935 is often busy)
HTTP=23333        # host:23333 -> container 3333
DIR=$(mktemp -d)
cd "$DIR"
echo "work dir: $DIR"


###############################################################################
echo; echo "### STAGE 0: write a tiny OME config (jpeg + png + webp + avif thumbs) ###"
###############################################################################
cat > Server.xml <<'XML'
<?xml version="1.0" encoding="UTF-8" ?>
<Server version="8">
  <Name>OvenMediaEngine</Name>
  <Type>origin</Type>
  <IP>*</IP>
  <PrivacyProtection>false</PrivacyProtection>
  <Modules>
    <!-- Enhanced-RTMP: required to ingest AV1 (and H.265 / VP9) over RTMP. -->
    <ERTMP><Enable>true</Enable></ERTMP>
  </Modules>
  <Bind>
    <Providers>
      <RTMP><Port>1935</Port><WorkerCount>1</WorkerCount></RTMP>
    </Providers>
    <Publishers>
      <Thumbnail><Port>3333</Port><WorkerCount>1</WorkerCount></Thumbnail>
    </Publishers>
  </Bind>
  <Defaults><CrossDomains><Url>*</Url></CrossDomains></Defaults>
  <VirtualHosts>
    <VirtualHost>
      <Name>default</Name>
      <Host><Names><Name>*</Name></Names></Host>
      <CrossDomains><Url>*</Url></CrossDomains>
      <Applications>
        <Application>
          <Name>app</Name>
          <Type>live</Type>
          <OutputProfiles>
            <HWAccels><Decoder><Enable>false</Enable></Decoder><Encoder><Enable>false</Enable></Encoder></HWAccels>
            <OutputProfile>
              <Name>thumb</Name>
              <OutputStreamName>${OriginStreamName}</OutputStreamName>
              <Encodes>
                <Image><Codec>jpeg</Codec><Framerate>1</Framerate><Width>1280</Width><Height>720</Height></Image>
                <Image><Codec>png</Codec><Framerate>1</Framerate><Width>1280</Width><Height>720</Height></Image>
                <Image><Codec>webp</Codec><Framerate>1</Framerate><Width>1280</Width><Height>720</Height></Image>
                <Image><Codec>avif</Codec><Framerate>1</Framerate><Width>1280</Width><Height>720</Height></Image>
              </Encodes>
            </OutputProfile>
          </OutputProfiles>
          <Providers><RTMP /></Providers>
          <Publishers><Thumbnail><CrossDomains><Url>*</Url></CrossDomains></Thumbnail></Publishers>
        </Application>
      </Applications>
    </VirtualHost>
  </VirtualHosts>
</Server>
XML
echo "wrote $DIR/Server.xml"


###############################################################################
echo; echo "### STAGE 1: start OME and show the bundled FFmpeg build flags ###"
###############################################################################
docker rm -f "$OME" >/dev/null 2>&1
docker run -d --name "$OME" \
  -p $RTMP:1935 -p $HTTP:3333 \
  -v "$DIR/Server.xml:/opt/ovenmediaengine/bin/origin_conf/Server.xml:ro" \
  "$OME_IMAGE" >/dev/null
sleep 6
echo "--- expect libaom_av1 in encoder+decoder, 'avif' in muxer, 'colorspace' in filter ---"
docker logs "$OME" 2>&1 | grep -o -- '--enable-encoder=[^ ]*' | head -1
docker logs "$OME" 2>&1 | grep -o -- '--enable-decoder=[^ ]*' | head -1
docker logs "$OME" 2>&1 | grep -o -- '--enable-muxer=[^ ]*'   | head -1
docker logs "$OME" 2>&1 | grep -o -- '--enable-filter=[^ ]*'  | head -1


###############################################################################
echo; echo "### STAGE 2: push H.264 (libx264, CPU) over RTMP for 60s ###"
###############################################################################
docker rm -f pusher >/dev/null 2>&1
docker run -d --rm --name pusher --network host "$FF_IMAGE" \
  -re -f lavfi -i "testsrc2=size=1280x720:rate=30,format=yuv420p" \
  -c:v libx264 -preset veryfast -g 30 -pix_fmt yuv420p \
  -color_primaries bt709 -color_trc bt709 -colorspace bt709 \
  -t 60 -f flv "rtmp://localhost:$RTMP/app/h264" >/dev/null
echo "pushing H.264 ... waiting 30s for the stream + 1fps thumbnails to appear"
sleep 30


###############################################################################
echo; echo "### STAGE 3: fetch thumbnails -- jpg/png/webp = REGRESSION, avif = NEW ###"
echo "###          (any thumbnail here also proves H.264 was decoded)            ###"
###############################################################################
curl -s -o h264.jpg  "http://localhost:$HTTP/app/h264/thumb.jpg"
curl -s -o h264.png  "http://localhost:$HTTP/app/h264/thumb.png"
curl -s -o h264.webp "http://localhost:$HTTP/app/h264/thumb.webp"
curl -s -o h264.avif "http://localhost:$HTTP/app/h264/thumb.avif"
ls -l h264.jpg h264.png h264.webp h264.avif
echo "--- file types: jpg/png/webp must be valid images; avif must be 'ISO Media, AVIF' ---"
file h264.jpg h264.png h264.webp h264.avif


###############################################################################
echo; echo "### STAGE 4: AVIF colour tags -- expect av1 / yuv420p / bt709 / range=pc ###"
###############################################################################
ffprobe -hide_banner -loglevel error \
  -show_entries stream=codec_name,pix_fmt,color_space,color_primaries,color_transfer,color_range \
  -of default=noprint_wrappers=1 h264.avif


###############################################################################
echo; echo "### STAGE 5: AV1 ingest -> AV1 DECODE -> thumbnail (NEW feature) ###"
echo "###          relies on <Modules><ERTMP> from STAGE 0 so AV1 ingests over RTMP ###"
###############################################################################
docker rm -f pusherav1 >/dev/null 2>&1
docker run -d --rm --name pusherav1 --network host "$FF_IMAGE" \
  -re -f lavfi -i "testsrc2=size=640x360:rate=15,format=yuv420p" \
  -c:v libaom-av1 -cpu-used 8 -b:v 800k -g 30 -pix_fmt yuv420p \
  -color_primaries bt709 -color_trc bt709 -colorspace bt709 \
  -t 40 -f flv "rtmp://localhost:$RTMP/app/av1" >/dev/null
echo "pushing AV1 ... waiting 25s"
sleep 25
echo "--- did OME ingest the stream as AV1? expect a track logged as BSF(AV1_OBU) ---"
docker logs "$OME" 2>&1 | grep -iE 'AV1_OBU' | grep -ivE 'enable-|Configuration' | tail -5
curl -s -o av1.jpg  "http://localhost:$HTTP/app/av1/thumb.jpg"
curl -s -o av1.avif "http://localhost:$HTTP/app/av1/thumb.avif"
ls -l av1.jpg av1.avif
echo "--- valid thumbnails here prove the AV1 stream was decoded and re-encoded ---"
file av1.jpg av1.avif


###############################################################################
echo; echo "### STAGE 6: protocols / codecs this script does NOT auto-drive (run by hand) ###"
###############################################################################
echo "WebRTC/WHIP : browser or OBS WHIP -- needed for VP8 ingest (and an alt AV1/H.265 path)"
echo "SRT         : ffmpeg ... -f mpegts 'srt://HOST:PORT?streamid=...'   (needs <SRT> bind)"
echo "MPEG-TS     : ffmpeg ... -f mpegts 'udp://HOST:PORT'                 (needs <MPEGTS> bind)"
echo "RTSP        : OME <RTSPPull> SourceStream from an rtsp:// server (e.g. mediamtx)"
echo "OVT         : a second OME instance pulling this one as an edge"
echo "Scheduled / Multiplex : config-driven providers"


###############################################################################
echo; echo "### DONE.  OME container '$OME' is left running for poking at. ###"
echo "clean up:  docker rm -f $OME pusher pusherav1 2>/dev/null"
echo "artifacts: $DIR"
###############################################################################
