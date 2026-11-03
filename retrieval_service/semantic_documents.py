"""Question-independent conversational document retrieval and passage ranking."""
import dataclasses,re,time,urllib.parse,threading,http.client
from service import Result,OK,NOT_FOUND,AMBIGUOUS,UNSUPPORTED
from general_retrieval import GeneralProvider,DocumentText,complete_units,full_recipe,words,compact_query
from web_documents import StructuredRecipe,plain,normalized_url

class SemanticProvider(GeneralProvider):
    def __init__(self,ranker,backend=None,intent=None):
        super().__init__(backend);self.ranker=ranker;self.intent=intent;self.documents={};self.document_lock=threading.Lock();self.document_epoch=0;self.document_local=threading.local();self.flight=threading.Lock();self.response_budget=35.0

    def clear(self):
        with self.document_lock:self.document_epoch+=1;self.documents.clear()
        super().clear()

    def fetch(self,query,deadline=None,accept=None):
        return self.fetch_context(query,query)

    def wiki_pages(self,query,search=False,full=False):
        params=dict(action='query',prop='extracts|pageprops',explaintext=1,exintro=1,redirects=1)
        if full:params.pop('exintro');params.update(exlimit=1,_byte_limit=262144)
        if search:params.update(generator='search',gsrsearch=query,gsrnamespace=0,gsrlimit=3)
        else:params['titles']=query
        data=self.backend.api('en.wikipedia.org',**params)
        return sorted(data.get('query',{}).get('pages',{}).values(),key=lambda p:p.get('index',0))

    def wiki_candidates(self,page):
        if 'missing' in page or 'disambiguation' in page.get('pageprops',{}):return []
        title=page.get('title','');text=page.get('extract','')
        if text.rstrip().endswith(('Ã¢â‚¬Â¦','...')):text=text.rsplit('\n\n',1)[0]
        source='https://en.wikipedia.org/wiki/'+urllib.parse.quote(title.replace(' ','_'))
        results=[];section=''
        for paragraph in text.splitlines():
            if paragraph.strip().startswith('=='):
                section=paragraph.strip('= ');continue
            # Keep source section context, and rank complete sentences rather
            # than paragraphs containing several different claims.
            for unit in re.split(r'(?<=[.!?])\s+',paragraph):
                if len(words(unit))<5:continue
                evidence=title+(' — '+section if section else '')+': '+unit.strip()
                if len(evidence.encode())<=850:
                    results.append(Result(OK,title,source,evidence,int(time.time()),len(text.encode())))
        return results

    def web_candidates(self,row):
        url=row.get('url','');url='https://'+url[7:] if url.startswith('http://') else url
        if not url.startswith('https://'):return []
        url=normalized_url(url)
        parsed=urllib.parse.urlsplit(url)
        if parsed.hostname=='en.wikipedia.org' and parsed.path.startswith('/wiki/'):
            title=urllib.parse.unquote(parsed.path[6:]).replace('_',' ')
            return self.html_candidates(url,title)
        return self.html_candidates(url,plain(row.get('title',[]))[:100])

    def html_candidates(self,url,title):
        with self.document_lock:
            epoch=getattr(self.document_local,'epoch',self.document_epoch);cached=self.documents.get(url) if epoch==self.document_epoch else None
            if cached and time.monotonic()-cached[0]<3600:return cached[1]
        raw=self.search.request(url,524288);html=raw.decode('utf-8',errors='replace')
        recipe=StructuredRecipe();recipe.feed(html)
        if recipe.recipes:
            content=full_recipe(recipe);units=[content] if content else []
        else:
            parser=DocumentText();parser.feed(html)
            units=[u for u in parser.units() if u.endswith('```') or u.rstrip(' \t\r\n"\'”’)]').endswith(('.','!','?'))]
        results=[Result(OK,title,url,u,int(time.time()),len(raw)) for u in units]
        with self.document_lock:
            if len(self.documents)>=16:self.documents.pop(next(iter(self.documents)))
            if epoch==self.document_epoch:self.documents[url]=(time.monotonic(),results)
        return results

    def fetch_context(self,message,suggestion,topic=''):
        # DNS, TLS and response reads can outlive individual socket timeouts.
        # One bounded worker prevents those calls from blocking the Vita or
        # accumulating abandoned workers. Late results are never returned.
        if not self.flight.acquire(blocking=False):
            return Result(NOT_FOUND,error='Previous source request still finishing')
        done=threading.Event();result=[];epoch=self.document_epoch
        def run():
            try:
                self.document_local.epoch=epoch
                value=self.collect(message,suggestion,topic)
                result.append(value if epoch==self.document_epoch else Result(NOT_FOUND,error='Conversation changed'))
            except (OSError,ValueError,KeyError,TypeError,http.client.HTTPException) as e:
                result.append(Result(NOT_FOUND,error=type(e).__name__))
            finally:self.flight.release();done.set()
        threading.Thread(target=run,daemon=True).start()
        if not done.wait(self.response_budget):return Result(NOT_FOUND,error='Source collection deadline')
        return result[0] if result else Result(NOT_FOUND,error='Source worker failed')

    def select(self,message,topic,candidates):
        if getattr(self.document_local,'epoch',self.document_epoch)!=self.document_epoch:return None
        groups={}
        for candidate in candidates:groups.setdefault(candidate.source,[]).append(candidate)
        if not groups:return None
        documents=list(groups.values())
        # Rank the document's actual lead before its individual passages.
        # This prevents a keyword-heavy fragment of a general article from
        # displacing an otherwise better-matched complete document.
        leads=[group[0].title+'\n'+group[0].evidence for group in documents]
        raw=self.ranker.scores(message,leads)
        contextual=self.ranker.scores(topic+': '+message,leads) if topic else raw
        order=sorted(range(len(documents)),key=lambda i:max(raw[i],contextual[i]),reverse=True)
        for i in order:
            ranked=self.ranker.rank(message,topic,documents[i][:128])
            if ranked:
                score,result=ranked[0]
                # Preserve the selected document's identity/context alongside a
                # highly relevant detail. One isolated sentence can answer only
                # half a request or leave a pronoun/attribute without its subject.
                # Both units remain verbatim; never splice or clip source prose.
                lead=documents[i][0].evidence
                evidence=result.evidence
                combined=lead+'\n\n'+evidence
                if lead!=evidence and len(combined.encode('utf-8'))<=850:
                    evidence=combined
                return dataclasses.replace(result,evidence=evidence,error='semantic relevance='+str(round(score,3)))
        return None

    def collect(self,message,suggestion,topic=''):
        if self.intent is not None and not self.intent.research(message):
            return Result(UNSUPPORTED,error='Action classified as local; no external sources fetched')
        if getattr(self.document_local,'epoch',self.document_epoch)!=self.document_epoch:return Result(NOT_FOUND,error='Conversation changed')
        deadline=time.monotonic()+25.0;candidates=[];errors=[];seen=set();expanded=set()
        self.backend.local.cache_epoch=self.backend.cache_epoch;self.backend.local.deadline=deadline;self.search.local.deadline=deadline
        def add(rows):
            for r in rows:
                key=(r.source,r.evidence)
                if key not in seen:seen.add(key);candidates.append(r)
        # Collect the prior source for follow-ups, but rank all documents once
        # after collecting current-request sources. Eager prior-source ranking
        # repeated expensive irrelevant work before every topic change.
        if topic:
            with self.document_lock:
                previous=[r for fetched,rows in self.documents.values() if time.monotonic()-fetched<3600 for r in rows if r.title==topic]
            add(previous)
            try:
                add([r for p in self.wiki_pages(topic,full=True) for r in self.wiki_candidates(p)])
                expanded.add(topic)
            except (OSError,ValueError,KeyError,TypeError,http.client.HTTPException) as e:errors.append(type(e).__name__)
        # Original request has priority over a potentially stale model rewrite.
        tasks=list(dict.fromkeys([(compact_query(message),True),(compact_query(suggestion),True)]))
        for query,search in tasks:
            if time.monotonic()>=deadline or getattr(self.document_local,'epoch',self.document_epoch)!=self.document_epoch:break
            try:
                pages=self.wiki_pages(query,search)
                add([r for p in pages for r in self.wiki_candidates(p)])
                first=next((p for p in pages if 'missing' not in p and 'disambiguation' not in p.get('pageprops',{})),None)
                if first and first['title'] not in expanded and time.monotonic()<deadline:
                    expanded.add(first['title'])
                    add([r for p in self.wiki_pages(first['title'],full=True) for r in self.wiki_candidates(p)])
            except (OSError,ValueError,KeyError,TypeError,http.client.HTTPException) as e:errors.append(type(e).__name__)
        if time.monotonic()<deadline:
            try:
                rows=self.search.search(compact_query(message))
                # Metadata only orders which pages to fetch; it is never evidence.
                scores=self.ranker.scores(message,[plain(row.get('title',[]))+' '+plain(row.get('extract',[])) for row in rows])
                rows=[row for _,row in sorted(zip(scores,rows),key=lambda item:item[0],reverse=True)]
                for row in rows[:3]:
                    if time.monotonic()>=deadline or getattr(self.document_local,'epoch',self.document_epoch)!=self.document_epoch:break
                    try:add(self.web_candidates(row))
                    except (OSError,ValueError,KeyError,TypeError,http.client.HTTPException) as e:errors.append(type(e).__name__)
            except (OSError,ValueError,KeyError,TypeError,http.client.HTTPException) as e:errors.append(type(e).__name__)
        selected=self.select(message,topic,candidates)
        return selected or Result(NOT_FOUND,error='No relevant document; source errors='+repr(errors)[:120])
