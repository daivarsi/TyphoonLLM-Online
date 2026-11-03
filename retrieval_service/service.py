from __future__ import annotations

import argparse, collections, dataclasses, html, ipaddress, json, re, socket, socketserver, ssl, struct, threading, time

import urllib.parse, urllib.request, urllib.error

from html.parser import HTMLParser

from pathlib import Path

VERSION=2

CACHE_POLICY=11

REQUEST=struct.Struct('!4sHH')

RESPONSE=struct.Struct('!4sHHHHIQII')

MAX_QUERY=512; MAX_RAW=65536; MAX_EVIDENCE=1024



OK=0; NOT_FOUND=1; AMBIGUOUS=2; UNAVAILABLE=3; MALFORMED=4; RATE_LIMITED=5; UNSUPPORTED=6







class PlainText(HTMLParser):
    def __init__(self): super().__init__(convert_charrefs=True); self.parts=[]; self.skip=0
    def handle_starttag(self,tag,attrs):
        if tag in ('script','style'):self.skip+=1
    def handle_endtag(self,tag):
        if tag in ('script','style') and self.skip:self.skip-=1
    def handle_data(self,data):
        if not self.skip:self.parts.append(data)

def clean(text: str) -> str:
    parser=PlainText();parser.feed(text[:MAX_RAW]);parser.close()
    text=html.unescape(' '.join(parser.parts))
    text=text.replace('<|','< |').replace('|>','| >')
    return re.sub(r'\s+',' ',''.join(c if c.isprintable() else ' ' for c in text)).strip()

@dataclasses.dataclass
class Result:
    status:int=OK; title:str=''; source:str=''; evidence:str=''; timestamp:int=0; raw_bytes:int=0; elapsed_ms:int=0; error:str=""
    def wire(self):
        if len(self.evidence.encode('utf-8'))>MAX_EVIDENCE:raise ValueError('evidence exceeds wire budget')
        def bounded(value,limit):return value.encode('utf-8')[:limit].decode('utf-8','ignore').encode('utf-8')
        title=bounded(self.title,128);source=bounded(self.source,384);evidence=bounded(self.evidence,MAX_EVIDENCE)
        return RESPONSE.pack(b'TYRS',VERSION,self.status,len(title),len(source),len(evidence),self.timestamp,self.raw_bytes,self.elapsed_ms)+title+source+evidence

def decode_result(wire:bytes) -> Result:
    if len(wire)<RESPONSE.size:raise ValueError('short response')
    magic,version,status,nt,ns,ne,stamp,raw,elapsed=RESPONSE.unpack_from(wire)
    if magic!=b'TYRS' or version!=VERSION or status>6 or nt>128 or ns>384 or ne>MAX_EVIDENCE or len(wire)!=RESPONSE.size+nt+ns+ne:raise ValueError('invalid response bounds')
    offset=RESPONSE.size
    title=wire[offset:offset+nt].decode('utf-8');offset+=nt
    source=wire[offset:offset+ns].decode('utf-8');offset+=ns
    evidence=wire[offset:].decode('utf-8')
    if '\0' in title+source+evidence or (status==OK and not evidence):raise ValueError('invalid text')
    return Result(status,title,source,evidence,stamp,raw,elapsed)

class SourceBusy(OSError):
    """Upstream asked us to stop; no automatic retry storm."""

class Retrieval:
    def __init__(self,provider=None,max_entries=32,max_bytes=32768,ttl=86400,audit=None,cache_file=None):
        self.provider=provider;
        if self.provider is None:
            from general_retrieval import GeneralProvider
            self.provider=GeneralProvider()
        self.cache=collections.OrderedDict();self.lock=threading.Lock()
        self.max_entries=max_entries;self.max_bytes=max_bytes;self.ttl=ttl;self.audit=Path(audit) if audit else None;self.epoch=0
        self.cache_file=Path(cache_file) if cache_file else None
        if self.cache_file and self.cache_file.exists() and self.cache_file.stat().st_size<=max_bytes:
            try:
                stored=json.loads(self.cache_file.read_text(encoding='utf-8'))
                entries=stored.get('entries',{}) if isinstance(stored,dict) and stored.get('policy')==CACHE_POLICY else {}
                if not isinstance(entries,dict):entries={}
                for key,fields in entries.items():
                    try:
                        result=Result(**fields);decode_result(result.wire())
                        if result.status==OK and 0<=time.time()-result.timestamp<ttl and len(key.encode())<=2048:self.cache[key]=result
                    except (ValueError,TypeError,KeyError):continue
                self.trim_cache()
            except (OSError,ValueError,TypeError,KeyError):self.cache.clear()
    def trim_cache(self):
        while len(self.cache)>self.max_entries or sum(len(k.encode())+len(v.wire()) for k,v in self.cache.items())>self.max_bytes:self.cache.popitem(last=False)
    def save_cache(self):
        if self.cache_file:
            try:
                self.cache_file.parent.mkdir(parents=True,exist_ok=True)
                data=json.dumps({'policy':CACHE_POLICY,'entries':{k:dataclasses.asdict(v) for k,v in self.cache.items()}},ensure_ascii=False).encode('utf-8')
                if len(data)>self.max_bytes:return
                temporary=self.cache_file.with_suffix('.tmp');temporary.write_bytes(data);temporary.replace(self.cache_file)
            except OSError:pass
    def clear(self):
        with self.lock:
            self.epoch+=1;self.cache.clear();self.save_cache()
        clear=getattr(self.provider,'clear',None)
        if clear:clear()
        if self.audit:
            self.audit.parent.mkdir(parents=True,exist_ok=True)
            with self.audit.open('a',encoding='utf-8') as stream:stream.write(json.dumps({'event':'conversation_clear','epoch':self.epoch,'time':time.time()})+'\n')
    def query(self,query,suggestion=None,topic=""):
        if not isinstance(query,str) or not query.strip() or len(query.encode('utf-8'))>(1024 if suggestion is not None else MAX_QUERY) or any(ord(c)<32 and c not in '\n\t' for c in query):return Result(MALFORMED)
        if suggestion is None and any(ord(c)<32 for c in query):return Result(MALFORMED)
        if suggestion is not None and (not isinstance(suggestion,str) or not suggestion.strip() or len(suggestion.encode())>512 or any(ord(c)<32 for c in suggestion)):return Result(MALFORMED)
        key=' '.join(query.casefold().split()).rstrip(' ?.!');started=time.monotonic()
        if not isinstance(topic,str) or len(topic.encode())>128 or any(ord(c)<32 for c in topic):return Result(MALFORMED)
        if suggestion is not None:key=json.dumps([query,suggestion,topic],ensure_ascii=False)
        with self.lock:
            cached=self.cache.get(key)
            epoch=self.epoch
            if cached and time.time()-cached.timestamp<self.ttl:self.cache.move_to_end(key);return dataclasses.replace(cached,elapsed_ms=0)
            self.cache.pop(key,None)
        try:
            if suggestion is not None:result=self.provider.fetch_context(query,suggestion,topic) if topic else self.provider.fetch_context(query,suggestion)
            else:result=self.provider.fetch(query)
        except SourceBusy:result=Result(RATE_LIMITED,error="source rate limited")
        except (OSError,ValueError,KeyError,TypeError,UnicodeError) as error:result=Result(UNAVAILABLE,error=type(error).__name__+": "+str(error)[:180])
        result.elapsed_ms=min(0xffffffff,int((time.monotonic()-started)*1000))
        if result.status==OK:
            # Enforce schema even for future third-party provider adapters.
            if not result.evidence or len(result.evidence.encode('utf-8'))>MAX_EVIDENCE:return Result(MALFORMED)
            with self.lock:
                if epoch==self.epoch:
                    self.cache[key]=result
                    self.trim_cache();self.save_cache()
        if self.audit:
            with self.lock:
                self.audit.parent.mkdir(parents=True,exist_ok=True)
                if self.audit.exists() and self.audit.stat().st_size>2*1024*1024:self.audit.replace(self.audit.with_suffix('.previous'))
                with self.audit.open('a',encoding='utf-8') as f:f.write(json.dumps({'query':query,'suggestion':suggestion,'topic':topic,**dataclasses.asdict(result)},ensure_ascii=False)+'\n')
        return result

def exact(sock,n):
    data=bytearray()
    while len(data)<n:
        part=sock.recv(n-len(data))
        if not part:raise EOFError('short request')
        data.extend(part)
    return bytes(data)

class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        if self.client_address[0] not in self.server.allowed:return
        if not self.server.slots.acquire(blocking=False):return
        try:
            self.request.settimeout(4)
            magic,version,size=REQUEST.unpack(exact(self.request,REQUEST.size))
            if magic==b'TYHP' and version==VERSION and size==0:
                healthy=self.server.internet_health()
                self.request.sendall(Result(OK if healthy else UNAVAILABLE,evidence='ready' if healthy else '').wire());return
            if magic==b'TYCL' and version==VERSION and size==0:
                self.server.retrieval.clear();self.request.sendall(Result(NOT_FOUND).wire());return
            conversational=magic in (b'TYBQ',b'TYBN')
            anchored=conversational or magic in (b'TYAQ',b'TYAN')
            if magic not in (b'TYRQ',b'TYRN',b'TYAQ',b'TYAN',b'TYBQ',b'TYBN') or version!=VERSION or not 0<size<=(1670 if conversational else 1540 if anchored else MAX_QUERY):
                self.request.sendall(Result(MALFORMED).wire());return
            payload=exact(self.request,size)
            topic=''
            if anchored:
                prefix=6 if conversational else 4
                if size<prefix:self.request.sendall(Result(MALFORMED).wire());return
                nm,nq=struct.unpack_from('!HH',payload);nt=struct.unpack_from('!H',payload,4)[0] if conversational else 0
                if not 0<nm<=1024 or not 0<nq<=512 or nt>128 or size!=prefix+nm+nq+nt:
                    self.request.sendall(Result(MALFORMED).wire());return
                query=payload[prefix:prefix+nm].decode('utf-8');suggestion=payload[prefix+nm:prefix+nm+nq].decode('utf-8');topic=payload[prefix+nm+nq:].decode('utf-8')
            else:query=payload.decode('utf-8');suggestion=None
            if magic in (b'TYRN',b'TYAN',b'TYBN'):self.server.retrieval.clear()
            result=self.server.retrieval.query(query,suggestion,topic)
            self.request.sendall(result.wire())
        except (OSError,EOFError,ValueError,UnicodeError):return
        finally:self.server.slots.release()

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address=not hasattr(socket,'SO_EXCLUSIVEADDRUSE');daemon_threads=True
    def internet_health(self):
        with self.health_lock:
            if time.monotonic()-self.health_at<30:return self.health_result
            try:
                context=getattr(getattr(self.retrieval.provider,'backend',self.retrieval.provider),'tls',ssl.create_default_context())
                request=urllib.request.Request('https://www.wikipedia.org/',method='HEAD',headers={'User-Agent':'TyphoonOnline/0.14 connectivity'})
                with urllib.request.urlopen(request,timeout=2,context=context) as response:self.health_result=True
            except urllib.error.HTTPError:self.health_result=True # TLS/HTTP response confirms Internet reachability.
            except (OSError,ValueError):self.health_result=False
            self.health_at=time.monotonic();return self.health_result
    def server_bind(self):
        if hasattr(socket,'SO_EXCLUSIVEADDRUSE'):
            self.socket.setsockopt(socket.SOL_SOCKET,socket.SO_EXCLUSIVEADDRUSE,1)
        super().server_bind()
    def __init__(self,address,retrieval,allowed):
        self.health_lock=threading.Lock();self.health_at=0;self.health_result=False
        self.retrieval=retrieval;self.allowed=set(allowed)|{'127.0.0.1'};self.slots=threading.BoundedSemaphore(4)
        super().__init__(address,Handler)

def main():
    from semantic_ranker import SemanticRanker
    from semantic_intent import SemanticIntent
    from semantic_documents import SemanticProvider
    parser=argparse.ArgumentParser();parser.add_argument('--bind',default='127.0.0.1');parser.add_argument('--port',type=int,default=28500);parser.add_argument('--allow-ip',action='append',default=[]);parser.add_argument('--audit');parser.add_argument('--cache');parser.add_argument('--public-search',action='store_true',help='Compatibility flag; general public search is the default.');parser.add_argument('--ranker-model',type=Path,default=Path(__file__).resolve().parent.parent/'models/relevance');parser.add_argument('--intent-model',type=Path,default=Path(__file__).resolve().parent.parent/'models/intent');args=parser.parse_args()
    if not ipaddress.ip_address(args.bind).is_private:parser.error('trusted LAN bind only')
    with Server((args.bind,args.port),Retrieval(provider=SemanticProvider(SemanticRanker(args.ranker_model),intent=SemanticIntent(args.intent_model)),audit=args.audit,cache_file=args.cache),args.allow_ip) as server:
        print(f'retrieval listening {args.bind}:{args.port}; allowed clients={sorted(server.allowed)}',flush=True)
        server.serve_forever()
if __name__=='__main__':
    import service
    service.main()
