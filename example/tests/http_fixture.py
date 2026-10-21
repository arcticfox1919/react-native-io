import http.server, pathlib, sys, time
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args): pass
    def do_GET(self):
        if self.path == '/redirect':
            self.send_response(302); self.send_header('Location','/full'); self.end_headers(); return
        if self.path == '/error':
            self.send_response(404); self.end_headers(); return
        if self.path == '/slow': time.sleep(0.5)
        data=b'complete'
        self.send_response(200); self.send_header('Content-Length',str(len(data))); self.end_headers()
        try: self.wfile.write(data)
        except BrokenPipeError: pass
    def do_POST(self):
        data=self.rfile.read(int(self.headers['Content-Length']))
        ok=b'name="caption"' in data and b'hello' in data and b'filedata' in data
        self.send_response(200 if ok else 400); self.send_header('Content-Length','0'); self.end_headers()
server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
pathlib.Path(sys.argv[1]).write_text(str(server.server_port))
server.serve_forever()
