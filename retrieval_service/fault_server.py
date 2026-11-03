"""Explicit physical-client fault fixtures. Never use as the normal companion."""
import argparse,socketserver,time
from service import REQUEST,RESPONSE,VERSION,Server,Retrieval,Handler,exact,Result,NOT_FOUND
class FaultHandler(socketserver.BaseRequestHandler):
    def handle(self):
        if self.client_address[0] not in self.server.allowed:return
        self.request.settimeout(3)
        try:
            magic,version,size=REQUEST.unpack(exact(self.request,8))
            if magic!=b'TYRQ' or version!=VERSION or not 0<size<=512:return
            query=exact(self.request,size).decode('utf-8')
            if 'TimeoutFixture' in query or 'CancelFixture' in query:time.sleep(14);return
            if 'MalformedFixture' in query:
                self.request.sendall(RESPONSE.pack(b'BAD!',VERSION,0,0,0,0,0,0,0));return
            if 'OversizeFixture' in query:
                self.request.sendall(RESPONSE.pack(b'TYRS',VERSION,0,0,0,0xffffffff,0,0,0));return
            self.request.sendall(Result(NOT_FOUND).wire())
        except (OSError,ValueError,EOFError):return
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--bind',default='127.0.0.1');parser.add_argument('--port',type=int,default=28500);parser.add_argument('--allow-ip',action='append',default=[]);args=parser.parse_args()
    with Server((args.bind,args.port),Retrieval(),args.allow_ip) as server:
        server.RequestHandlerClass=FaultHandler
        print('TEST FAULT SERVER only; no real evidence returned',flush=True);server.serve_forever()
