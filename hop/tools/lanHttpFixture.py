"""Ограниченный HTTP-стенд на loopback хоста для проверки LAN из NAT-сети VM."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import threading

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(204 if self.path == '/hop-lan-ok' else 404)
        self.send_header('X-Hop-Lan-Test', 'yes')
        self.send_header('Content-Length', '0')
        self.end_headers()

    def log_message(self, *args):
        pass

server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
timer = threading.Timer(180, server.shutdown)
timer.daemon = True
timer.start()
print(json.dumps({'port': server.server_address[1]}), flush=True)
try:
    server.serve_forever()
finally:
    timer.cancel()
    server.server_close()
