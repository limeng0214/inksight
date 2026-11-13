"""Board PCM ↔ Xiaozhi v1 WebSocket/Opus. Run only on your trusted home LAN."""
import json
import os
import time
import uuid
from http.server import BaseHTTPRequestHandler, HTTPServer

import opuslib
from websockets.sync.client import connect

RATE = 16000
FRAME = 960  # 60 ms
BACKEND = os.getenv("XIAOZHI_WS", "ws://127.0.0.1:8000/xiaozhi/v1/")
DEVICE = os.getenv("XIAOZHI_DEVICE", "02:00:00:00:39:70")
CLIENT = str(uuid.uuid5(uuid.NAMESPACE_DNS, DEVICE))

def exchange(pcm):
    headers = {"Device-Id": DEVICE, "Client-Id": CLIENT, "Protocol-Version": "1"}
    if os.getenv("XIAOZHI_TOKEN"):
        headers["Authorization"] = "Bearer " + os.environ["XIAOZHI_TOKEN"]
    encoder = opuslib.Encoder(RATE, 1, opuslib.APPLICATION_VOIP)
    decoder = opuslib.Decoder(RATE, 1)
    with connect(BACKEND, additional_headers=headers, open_timeout=5, close_timeout=1) as sock:
        def send(**message):
            sock.send(json.dumps(message))
        send(type="hello", version=1, transport="websocket",
             audio_params=dict(format="opus", sample_rate=RATE, channels=1, frame_duration=60))
        hello = json.loads(sock.recv(timeout=5))
        if hello.get("type") != "hello":
            raise ValueError("Expected Xiaozhi hello")
        sid = hello["session_id"]
        send(type="listen", session_id=sid, state="start", mode="manual")
        for offset in range(0, len(pcm), FRAME * 2):
            frame = pcm[offset:offset+FRAME*2].ljust(FRAME*2, b"\0")
            sock.send(encoder.encode(frame, FRAME))
            time.sleep(.06)
        send(type="listen", session_id=sid, state="stop")
        deadline = time.monotonic()+32
        reply = bytearray()
        while time.monotonic()<deadline:
            event = sock.recv(timeout=max(.01,deadline-time.monotonic()))
            if isinstance(event, bytes):
                reply.extend(decoder.decode(event, RATE*120//1000))
                if len(reply)>RATE*2*120:
                    raise ValueError("Reply too long")
            else:
                msg=json.loads(event)
                if msg.get("type")=="tts" and msg.get("state")=="stop" and reply:
                    return bytes(reply)
        raise TimeoutError("No voice reply")

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path!="/turn":
            return self.send_error(404)
        try:
            n=int(self.headers.get("Content-Length","0"))
            if not 0<n<=RATE*2*8 or n%2:
                return self.send_error(400)
            self.connection.settimeout(50)
            pcm=self.rfile.read(n)
            if len(pcm)!=n:
                return self.send_error(400)
            reply=exchange(pcm)
        except Exception as error:
            print("Xiaozhi:",type(error).__name__,flush=True)
            return self.send_error(502,"Speech backend unavailable")
        self.send_response(200)
        self.send_header("Content-Type","application/octet-stream")
        self.send_header("Content-Length",str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

if __name__=="__main__":
    HTTPServer((os.getenv("GATEWAY_BIND","0.0.0.0"),8765),Handler).serve_forever()
