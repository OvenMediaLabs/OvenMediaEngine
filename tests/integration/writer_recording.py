#!/usr/bin/env python3
"""Opt-in local regression for discontinuous MP4 recording."""
import argparse
import base64
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from xml.sax.saxutils import escape


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def stop(process):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGINT)
            process.wait(timeout=6)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        except ProcessLookupError:
            pass
    process.wait()


def interrupted(signum, _frame):
    raise RuntimeError(f"integration test interrupted by signal {signum}")


def run(args):
    root = Path(tempfile.mkdtemp(prefix="ome-recording-integration-", dir=args.artifact_dir))
    processes, logs = [], []
    success = False
    with socket.socket() as api_socket, socket.socket() as input_socket:
        api_socket.bind(("127.0.0.1", 0))
        input_socket.bind(("127.0.0.1", 0))
        api_port = api_socket.getsockname()[1]
        input_port = input_socket.getsockname()[1]

    def launch(name, command):
        log = (root / (name + ".log")).open("w")
        logs.append(log)
        process = subprocess.Popen(command, stdout=log, stderr=log, start_new_session=True)
        processes.append(process)
        return process

    def capture(command):
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   text=True, start_new_session=True)
        processes.append(process)
        output, errors = process.communicate(timeout=20)
        processes.remove(process)
        check(process.returncode == 0, f"media command failed: {errors}")
        check(not errors, f"media command reported errors: {errors}")
        return output

    def frames(path):
        output = capture([args.ffmpeg, "-v", "error", "-xerror", "-i", str(path),
                          "-map", "0:v:0", "-map", "0:a:0", "-vsync", "0", "-f", "framemd5", "-"])
        rows = [line.split(",") for line in output.splitlines() if line and not line.startswith("#")]
        check(any(row[0].strip() == "1" for row in rows), f"no decoded audio: {path.name}")
        return [row[-1].strip() for row in rows if row[0].strip() == "0"]

    try:
        source = root / "source.flv"
        # End at an IDR so this checks segment boundaries without relying on
        # the input provider flushing reordered frames at end of stream.
        generate = launch("generate", [args.ffmpeg, "-v", "error", "-f", "lavfi", "-i",
            "testsrc2=size=320x240:rate=30", "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
            "-t", "17", "-frames:v", "481", "-c:v", "libx264", "-preset", "veryfast", "-g", "60", "-keyint_min", "60",
            "-sc_threshold", "0", "-bf", "2", "-x264-params", "open-gop=0", "-c:a", "aac",
            "-ar", "48000", "-ac", "2", "-f", "flv", str(source)])
        check(generate.wait(timeout=20) == 0, "fixture generation failed (requires libx264 and AAC encoders)")
        source_frames = frames(source)
        source_positions = {}
        for index, value in enumerate(source_frames):
            source_positions.setdefault(value, []).append(index)
        directory = escape(str(root))
        (root / "Server.xml").write_text(f"""<Server version="8"><Name>recording-test</Name><Type>origin</Type><IP>127.0.0.1</IP>
<Bind><Providers><RTMP><Port>{input_port}</Port><WorkerCount>1</WorkerCount></RTMP></Providers><Managers><API><Port>{api_port}</Port></API></Managers></Bind>
<Managers><API><AccessToken>local:test</AccessToken></API></Managers>
<VirtualHosts><VirtualHost><Name>default</Name><Host><Names><Name>*</Name></Names></Host>
<Applications><Application><Name>app</Name><Type>live</Type><OutputProfiles>
<OutputProfile><Name>bypass</Name><OutputStreamName>${{OriginStreamName}}</OutputStreamName><Encodes><Video><Name>video</Name><Bypass>true</Bypass></Video><Audio><Name>audio</Name><Bypass>true</Bypass></Audio></Encodes></OutputProfile>
</OutputProfiles><Providers><RTMP/></Providers><Publishers><FILE><Record><Enable>true</Enable><RecordInfo>RecordingsMap.xml</RecordInfo></Record></FILE></Publishers></Application></Applications>
</VirtualHost></VirtualHosts></Server>\n""")
        (root / "RecordingsMap.xml").write_text(f"""<RecordInfo><Record><Enable>true</Enable><StreamName>test</StreamName>
<VariantNames>video,audio</VariantNames><SegmentInterval>5000</SegmentInterval><SegmentRule>discontinuity</SegmentRule>
<FilePath>{directory}/mp4/${{Sequence}}.mp4</FilePath><InfoPath>{directory}/mp4/${{Sequence}}.xml</InfoPath>
</Record></RecordInfo>\n""")
        (root / "Logger.xml").write_text(f'<Logger version="2"><Path>{directory}/logs</Path><Tag name=".*" level="info"/></Logger>\n')
        engine = launch("ome", [args.ome, "-c", str(root)])
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        request = urllib.request.Request(f"http://127.0.0.1:{api_port}/v1/vhosts/default/apps/app/streams",
            headers={"Authorization": "Basic " + base64.b64encode(b"local:test").decode()})
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            check(engine.poll() is None, "OME exited during startup")
            try:
                with opener.open(request, timeout=3) as response:
                    if json.load(response)["statusCode"] == 200:
                        break
            except (urllib.error.URLError, TimeoutError):
                pass
            time.sleep(0.1)
        else:
            raise RuntimeError("OME API did not become ready")

        publisher = launch("publish", [args.ffmpeg, "-v", "error", "-re", "-i", str(source),
            "-map", "0:v:0", "-map", "0:a:0", "-c", "copy", "-f", "flv", f"rtmp://127.0.0.1:{input_port}/app/test"])
        check(publisher.wait(timeout=30) == 0, "input publisher failed")
        stop(engine)  # Finalize the last file before probing any segments.
        check(engine.returncode == 0, f"OME did not shut down normally: {engine.returncode}")

        segments = sorted((root / "mp4").glob("*.mp4"))
        check(len(segments) >= 3, "FILE publisher did not produce three MP4 segments")
        for path in segments:
            packets = json.loads(capture([args.ffprobe, "-v", "error", "-show_packets", "-of", "json", str(path)]))["packets"]
            video = [p for p in packets if p["codec_type"] == "video"]
            check(video and "K" in video[0]["flags"], f"MP4 segment has no opening keyframe: {path.name}")
            check(any(p["codec_type"] == "audio" for p in packets), f"MP4 segment has no audio: {path.name}")
            if path != segments[-1]:
                check(len(video) >= 150, f"MP4 segment is shorter than five seconds: {path.name}")
            decoded = frames(path)
            check(len(decoded) == len(video), f"MP4 video packet/frame count differs: {path.name}")
            first = source_positions.get(decoded[0], [])
            check(len(first) == 1 and decoded == source_frames[first[0]:first[0] + len(decoded)],
                  f"MP4 decoded frames differ from source: {path.name}")
        print(f"{len(segments)} H.264/AAC MP4 segments retain opening keyframes and decode to contiguous source frames")
        success = True
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        for process in reversed(processes):
            stop(process)
        for log in logs:
            log.close()
        if success and not args.artifact_dir:
            shutil.rmtree(root)
        else:
            print(f"Integration artifacts: {root}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ome", required=True)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--artifact-dir", type=Path)
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGALRM):
        signal.signal(sig, interrupted)
    signal.alarm(90)
    run(parser.parse_args())
