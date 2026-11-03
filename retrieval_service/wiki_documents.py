"""Fixed-host Wikimedia document access; no question-type routing."""
import collections,json,re,ssl,threading,time,urllib.parse,urllib.request,urllib.error
from service import MAX_RAW,SourceBusy
class WikipediaDocuments:
    def __init__(self):
        try:
            import truststore
            self.tls=truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        except ImportError:self.tls=ssl.create_default_context()
        self.local=threading.local();self.http_lock=threading.Lock();self.http_cache=collections.OrderedDict();self.next_request=0;self.cooldown=0;self.cache_epoch=0

    def clear(self):
        with self.http_lock:
            self.cache_epoch+=1;self.http_cache.clear()

    def api(self,host,**params):
        # Fixed public providers only. Queries cannot supply a host or URL.
        assert host in ('en.wikipedia.org','en.wikibooks.org','www.wikidata.org')
        params.update(format='json',maxlag=5)
        key=host+'?'+urllib.parse.urlencode(params)
        with self.http_lock:
            now=time.monotonic()
            current=getattr(self.local,'cache_epoch',self.cache_epoch)==self.cache_epoch
            cached=self.http_cache.get(key) if current else None
            if cached and now-cached[0]<3600:return cached[1]
            if now<self.cooldown:raise SourceBusy('source rate limited')
            time.sleep(max(0,self.next_request-now))
            request=urllib.request.Request('https://'+host+'/w/api.php?'+urllib.parse.urlencode(params),headers={'User-Agent':'TyphoonOnline/0.2 (personal PS Vita retrieval client)','Accept':'application/json'})
            try:
                remaining=getattr(self.local,'deadline',time.monotonic()+3)-time.monotonic()
                if remaining<=0:raise TimeoutError('retrieval deadline')
                with urllib.request.urlopen(request,timeout=min(3,remaining),context=self.tls) as response:raw=response.read(MAX_RAW+1)
            except urllib.error.HTTPError as error:
                if error.code in (429,503):
                    delay=error.headers.get('Retry-After','30')
                    self.cooldown=time.monotonic()+min(300,max(5,int(delay) if delay.isdigit() else 30))
                    raise SourceBusy('source rate limited') from None
                raise
            finally:self.next_request=time.monotonic()+1.0
            if len(raw)>MAX_RAW:raise ValueError('source too large')
            data=json.loads(raw)
            if 'error' in data:
                code=data['error'].get('code','unknown')
                if code in ('maxlag','ratelimited'):
                    self.cooldown=time.monotonic()+5;raise SourceBusy('source temporarily busy')
                raise ValueError('source API error: '+code)
            if current:self.http_cache[key]=(time.monotonic(),data)
            while len(self.http_cache)>64:self.http_cache.popitem(last=False)
            return data

    def page(self,entity):
        params=dict(action='query',prop='extracts|pageprops',explaintext=1,exintro=1,redirects=1)
        data=self.api('en.wikipedia.org',titles=entity,**params)
        pages=list(data['query']['pages'].values())
        if len(pages)!=1:return None
        page=pages[0]
        if 'missing' not in page:return page
        # Resolve capitalization and aliases; never accept an unrelated top hit.
        data=self.api('en.wikipedia.org',generator='search',gsrsearch=entity,gsrnamespace=0,gsrlimit=3,**params)
        pages=list(data.get('query',{}).get('pages',{}).values())
        words=lambda value:set(re.findall(r'[a-z0-9]+',value.casefold()))
        wanted=words(entity)
        candidates=[p for p in pages if wanted and wanted<=words(p.get('title',''))]
        exact=[p for p in candidates if p.get('title','').casefold()==entity.casefold()]
        return exact[0] if len(exact)==1 else candidates[0] if len(candidates)==1 else None
