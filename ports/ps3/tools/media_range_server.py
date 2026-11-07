#!/usr/bin/env python3
"""Read-only Range server exposing only the two synthetic PS3 test tracks."""
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import json
from pathlib import Path
import re
import threading
from urllib.parse import urlsplit

NAMES=('WILI_VIDEO.MP4','WILI_AUDIO.MP4')
def create_server(root,host,port,log_file=None,test_mode=False):
    root=Path(root).resolve();assets={}
    for name in NAMES:
        path=root/name;st=path.stat()
        assets[name]={'path':path,'size':st.st_size,'mtime':st.st_mtime_ns,'etag':'"'+hashlib.sha256(path.read_bytes()).hexdigest()+'"'}
    lock=threading.Lock();counts={}
    class Handler(BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def log_message(self,*args):pass
        def record(self,**fields):
            if log_file:
                with lock:
                    with Path(log_file).open('a',encoding='utf-8') as f:f.write(json.dumps(fields)+'\n')
        def empty(self,status,**headers):
            self.send_response(status)
            for key,value in headers.items():self.send_header(key.replace('_','-'),str(value))
            self.send_header('Content-Length','0');self.end_headers()
        def do_HEAD(self):self.serve(True)
        def do_GET(self):self.serve(False)
        def serve(self,head):
            path=urlsplit(self.path).path;mode='normal'
            if test_mode and path.count('/')==2:_,mode,name=path.split('/')
            else:name=path[1:] if path.startswith('/') else ''
            if path=='/health.json':
                body=json.dumps({'service':'wiliwili-ps3-fixtures','files':{n:assets[n]['size'] for n in NAMES}}).encode()
                self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers()
                if not head:self.wfile.write(body)
                return
            if name not in assets:self.empty(404);return
            asset=assets[name];st=asset['path'].stat()
            if st.st_size!=asset['size'] or st.st_mtime_ns!=asset['mtime']:self.empty(409);return
            if mode=='redirect':self.empty(302,Location='/'+name);return
            size=asset['size'];first,last=0,size-1;status=200
            value=self.headers.get('Range')
            if value and mode!='ignore':
                match=re.fullmatch(r'bytes=(\d*)-(\d*)',value)
                if not match or not any(match.groups()):self.empty(416,Content_Range=f'bytes */{size}');return
                a,b=match.groups()
                if not a:
                    suffix=int(b)
                    if suffix<=0:self.empty(416,Content_Range=f'bytes */{size}');return
                    first=max(0,size-suffix)
                else:first=int(a);last=min(size-1,int(b)) if b else size-1
                if first>=size or first>last:self.empty(416,Content_Range=f'bytes */{size}');return
                status=206
            with lock:counts[mode]=counts.get(mode,0)+1;request_number=counts[mode]
            etag=asset['etag']
            if mode=='changed':etag='"first"' if request_number==1 else '"changed"'
            if self.headers.get('If-Range') not in (None,etag) and mode!='changed':first,last,status=0,size-1,200
            count=last-first+1
            self.send_response(status);self.send_header('Content-Type','video/mp4');self.send_header('Accept-Ranges','bytes')
            self.send_header('ETag',etag);self.send_header('Cache-Control','no-transform')
            self.send_header('Content-Length',str(count))
            if status==206:self.send_header('Content-Range',f'bytes {first+1 if mode=="wrong" else first}-{last}/{size}')
            if mode=='encoded':self.send_header('Content-Encoding','gzip')
            if mode=='huge':self.send_header('X-Pad','x'*8192)
            if mode=='short':self.send_header('Connection','close');self.close_connection=True
            self.end_headers();self.record(client=self.client_address[0],file=name,mode=mode,status=status,start=first,end=last,bytes=count)
            if head:return
            left=count-17 if mode=='short' and count>17 else count
            try:
                with asset['path'].open('rb') as f:
                    f.seek(first)
                    while left:
                        data=f.read(min(left,65536))
                        if not data:self.close_connection=True;break
                        self.wfile.write(data);left-=len(data)
            except (BrokenPipeError,ConnectionResetError):self.close_connection=True
    server=ThreadingHTTPServer((host,port),Handler);server.daemon_threads=True
    return server

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',required=True);parser.add_argument('--host',default='127.0.0.1');parser.add_argument('--port',type=int,default=8765);parser.add_argument('--log')
    args=parser.parse_args();server=create_server(args.root,args.host,args.port,args.log)
    print(json.dumps({'listening':server.server_address,'files':list(NAMES)}),flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
if __name__=='__main__':main()
