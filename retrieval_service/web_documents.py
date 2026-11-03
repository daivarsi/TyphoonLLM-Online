"""Public search and bounded document/JSON readers, independent of question form."""
import ipaddress,json,re,socket,ssl,threading,time,urllib.parse,urllib.request
from html.parser import HTMLParser
from service import clean
class SafeRedirect(urllib.request.HTTPRedirectHandler):
 def redirect_request(self,req,fp,code,msg,headers,newurl):
  public_url(newurl)
  return super().redirect_request(req,fp,code,msg,headers,newurl)

def public_url(url):
 p=urllib.parse.urlsplit(url)
 if p.scheme!='https' or not p.hostname or p.username or p.password or p.port not in (None,443):raise ValueError('HTTPS public source required')
 if len(url)>2048:raise ValueError('source URL too long')
 addresses=socket.getaddrinfo(p.hostname,443,type=socket.SOCK_STREAM)
 if not addresses or any(not ipaddress.ip_address(row[4][0]).is_global for row in addresses):raise ValueError('private source blocked')
 return url

def plain(parts):
 return clean(''.join(row.get('value','') for row in parts if isinstance(row,dict)))

class StructuredRecipe(HTMLParser):
 def __init__(self):super().__init__();self.active=False;self.parts=[];self.recipes=[]

 def handle_starttag(self,tag,attrs):
  if tag=='script' and dict(attrs).get('type','').lower()=='application/ld+json':self.active=True;self.parts=[]

 def handle_data(self,data):
  if self.active:self.parts.append(data)

 def handle_endtag(self,tag):
  if tag=='script' and self.active:
   self.active=False
   try:self.walk(json.loads(''.join(self.parts)))
   except (ValueError,TypeError):pass

 def walk(self,value):
  if isinstance(value,list):
   for item in value:self.walk(item)
  elif isinstance(value,dict):
   kind=value.get('@type',[]);kind=[kind] if isinstance(kind,str) else kind
   if 'Recipe' in kind:self.recipes.append(value)
   if '@graph' in value:self.walk(value['@graph'])

class WebSearch(object):
 def clear(self):
  with self.lock:self.cache.clear()

 def __init__(self,tls):
  self.opener=urllib.request.build_opener(urllib.request.HTTPSHandler(context=tls),SafeRedirect());self.local=threading.local();self.lock=threading.Lock();self.last=0;self.cache={}

 def request(self,url,limit):
  public_url(url)
  req=urllib.request.Request(url,headers={'User-Agent':'TyphoonOnline/0.2 (personal PS Vita retrieval client)','Accept':'application/json,text/html'})
  remaining=getattr(self.local,'deadline',time.monotonic()+5)-time.monotonic()
  if remaining<=0:raise TimeoutError('retrieval deadline')
  with self.opener.open(req,timeout=min(5,remaining)) as r:
   raw=r.read(limit+1)
   return raw[:limit]

 def search(self,query):
  key=' '.join(query.casefold().split())
  with self.lock:
   old=self.cache.get(key)
   if old and time.monotonic()-old[0]<3600:return old[1]
   time.sleep(max(0,1-(time.monotonic()-self.last)))
   try:data=json.loads(self.request('https://api.mwmbl.org/search?'+urllib.parse.urlencode({'s':query}),131072))
   finally:self.last=time.monotonic()
   if not isinstance(data,list):raise ValueError('bad search schema')
   self.cache[key]=(time.monotonic(),data[:20])
   if len(self.cache)>32:self.cache.pop(next(iter(self.cache)))
   return data[:20]
