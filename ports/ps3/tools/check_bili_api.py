#!/usr/bin/env python3
"""Offline Bili schema/signing and strict HTTPS transport regression checks."""
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
import shlex
import ssl
import subprocess
import threading
PORT=Path(__file__).resolve().parents[1]
def run(args):subprocess.run(list(map(str,args)),check=True)
def main():
    build=PORT/'build/api';build.mkdir(parents=True,exist_ok=True)
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','libcurl','json-c'],text=True))
    common=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-deprecated-declarations','-fsanitize=address,undefined','-I'+str(PORT/'include')]
    api=build/'bili_api_test';run([*common,PORT/'src/bili_api.c',PORT/'tests/bili_api_tests.c',*flags,'-lcrypto','-o',api]);run([api])
    test=build/'https_request_test';run([*common,PORT/'src/https_request.c',PORT/'src/danmaku.c',PORT/'src/search_text.c',PORT/'tests/https_request_tests.c',*flags,'-o',test])
    cert=build/'host-test-cert.pem';key=build/'host-test-key.pem'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost','-keyout',str(key),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*args):pass
        def do_GET(self):
            if self.path=='/downgrade':
                self.send_response(302);self.send_header('Location','http://127.0.0.1:1/blocked');self.send_header('Content-Length','0');self.end_headers();return
            body=b'{"code":0}\n' if self.path=='/ok' else b'x'*4096
            if self.path=='/danmaku':
                def vi(v):
                    b=bytearray()
                    while v>127:b.append((v&127)|128);v>>=7
                    b.append(v);return bytes(b)
                text='真实传输弹幕'.encode('utf-8')
                event=b'\x10'+vi(1000)+b'\x18\x01\x3a'+vi(len(text))+text
                body=b'\x0a'+vi(len(event))+event+b'\x12'+vi(3*1024*1024)+bytes(3*1024*1024)
            status=200
            if self.path=='/range':body=b'x'*32;status=206
            self.send_response(status)
            if status==206:self.send_header('Content-Range','bytes 0-31/64')
            self.send_header('Content-Length',str(len(body)));self.end_headers()
            try:self.wfile.write(body)
            except (BrokenPipeError,ConnectionResetError,ssl.SSLError):pass
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version=context.maximum_version=ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(str(cert),str(key));server.socket=context.wrap_socket(server.socket,server_side=True)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:run([test,f'https://localhost:{server.server_port}',cert,f'https://127.0.0.1:{server.server_port}'])
    finally:server.shutdown();server.server_close();thread.join()
if __name__=='__main__':main()
