"""Question-form independent retrieval; returns documents, never final answers."""
import collections,html,math,re,time,urllib.parse
from html.parser import HTMLParser
from service import Result,OK,NOT_FOUND,AMBIGUOUS,UNAVAILABLE,UNSUPPORTED,MAX_EVIDENCE,clean
from wiki_documents import WikipediaDocuments
from web_documents import WebSearch,StructuredRecipe,plain

def words(text):return re.findall(r'[^\W_]+',text.casefold())
def safe(text):return ''.join(c for c in html.unescape(text) if c.isprintable() or c in '\n\t').replace('<|','< |').replace('|>','| >').strip()

def complete_units(text,limit=850):
    units=[]
    for para in re.split(r'\n\s*\n|\n',text):
        para=safe(para)
        if not para:continue
        if len(para.encode())<=limit:units.append(para);continue
        group=''
        for sentence in re.split(r'(?<=[.!?])\s+',para):
            if len(sentence.encode())>limit:continue
            if len((group+' '+sentence).encode())>limit:
                if group:units.append(group)
                group=sentence
            else:group=(group+' '+sentence).strip()
        if group:units.append(group)
    return units

class DocumentText(HTMLParser):
    def __init__(self):super().__init__(convert_charrefs=True);self.skip=0;self.main=0;self.capture=None;self.parts=[];self.blocks=[];self.scope=False
    def handle_starttag(self,tag,attrs):
        if tag in ('script','style','nav','footer','header','aside','form','button'):self.skip+=1
        if tag in ('main','article'):self.main+=1
        if not self.skip and self.capture is None and tag in ('p','li','pre'):
            self.capture=tag;self.parts=[];self.scope=self.main>0
    def handle_endtag(self,tag):
        if self.capture==tag:
            text=safe(''.join(self.parts));kind=self.capture
            if text:self.blocks.append((kind,text,self.scope))
            self.capture=None;self.parts=[]
        if tag in ('script','style','nav','footer','header','aside','form','button') and self.skip:self.skip-=1
        if tag in ('main','article') and self.main:self.main-=1
    def handle_data(self,text):
        if self.capture and not self.skip:self.parts.append(text)
    def units(self):
        main=any(scope for _,_,scope in self.blocks);out=[]
        for kind,text,scope in self.blocks:
            if main and not scope:continue
            if kind=='pre':
                code='```\n'+text+'\n```'
                if len(code.encode())<=MAX_EVIDENCE:out.append(code)
                continue
            for unit in complete_units(text):
                prose=re.sub(r'https?://\S+|www\.\S+','',unit)
                if len(words(prose))<4:continue
                if re.search(r'(?i)\b(subscribe|newsletter|cookie policy|read on|users recommend|did this .*help|was this helpful|check out what)\b',prose):continue
                if re.search(r'(?i)ignore .*instruct|system prompt|api.?key|password',prose):continue
                out.append(unit)
        return out

def full_recipe(parser):
    for recipe in parser.recipes:
        ingredients=recipe.get('recipeIngredient');steps=[]
        def visit(value):
            if isinstance(value,str):steps.append(clean(value))
            elif isinstance(value,list):
                for v in value:visit(v)
            elif isinstance(value,dict):
                if value.get('text'):steps.append(clean(value['text']))
                elif value.get('itemListElement'):visit(value['itemListElement'])
        visit(recipe.get('recipeInstructions',[]))
        if not isinstance(ingredients,list) or not ingredients or not steps or not all(isinstance(v,str) and v.strip() for v in ingredients):continue
        value='Ingredients: '+'; '.join(clean(v) for v in ingredients)+'. Steps: '+' '.join(str(i+1)+'. '+v for i,v in enumerate(steps))
        if len(value.encode())<=MAX_EVIDENCE:return value
    return ''

def rank_units(query,units,title='',limit=900):
    terms=set(words(query));docs=[set(words(u)) for u in units]
    if not terms or not docs:return ''
    idf={t:math.log(1+(len(docs)+.5)/(1+sum(t in d for d in docs))) for t in terms}
    scored=[]
    for i,(unit,doc) in enumerate(zip(units,docs)):
        overlap=terms&doc
        score=sum(idf[t] for t in overlap)/max(1,len(words(unit)))**.25
        if score>0:scored.append((score,-i,unit))
    chosen=[]
    for _,_,unit in sorted(scored,reverse=True):
        if any(unit in old or old in unit for old in chosen):continue
        if len(('\n\n'.join(chosen+[unit])).encode())<=limit:chosen.append(unit)
        # One best complete passage limits distraction in the 512-token model.
        # This changes supplied context only; the model still writes the answer.
        if chosen:break
    return '\n\n'.join(chosen)

class GeneralProvider:
    name='general-documents'
    def __init__(self,backend=None):
        self.backend=backend or WikipediaDocuments();self.search=WebSearch(self.backend.tls)
    def clear(self):self.backend.clear();self.search.clear()
    def wiki_document(self,title,query):
        data=self.backend.api('en.wikipedia.org',action='query',prop='extracts|pageprops',titles=title,redirects=1,explaintext=1,exintro=1)
        pages=list(data.get('query',{}).get('pages',{}).values())
        if len(pages)!=1 or 'missing' in pages[0]:return None
        return self.from_wiki(pages[0],query)
    def from_wiki(self,page,query):
        if 'disambiguation' in page.get('pageprops',{}):return Result(AMBIGUOUS)
        title=clean(page.get('title',''));text=page.get('extract','');units=complete_units(text)
        # The complete lead binds identity; further whole units are optional.
        evidence=units[0] if units else ''
        for unit in units[1:]:
            if len((evidence+'\n\n'+unit).encode())<=850:evidence+='\n\n'+unit
            if len(evidence.encode())>=600:break
        return Result(OK if evidence else NOT_FOUND,title,'https://en.wikipedia.org/wiki/'+urllib.parse.quote(title.replace(' ','_')),evidence,int(time.time()),len(text.encode()))
    def fetch(self,query):
        started=time.monotonic();deadline=started+10
        self.backend.local.cache_epoch=self.backend.cache_epoch;self.backend.local.deadline=deadline
        # Exact document resolution is an optimization, not a question whitelist.
        try:
            page=self.backend.page(query)
            if page:
                result=self.from_wiki(page,query)
                if result.status in (OK,AMBIGUOUS):return result
        except (OSError,ValueError,KeyError,TypeError):pass
        self.search.local.deadline=deadline
        rows=self.search.search(query)
        terms=set(words(query))
        rows=sorted(rows,key=lambda r:len(terms&set(words(plain(r.get('title',[]))+' '+plain(r.get('extract',[]))))),reverse=True)
        failures=[]
        for row in rows[:3]:
            if time.monotonic()>deadline:break
            url=row.get('url','');url='https://'+url[7:] if url.startswith('http://') else url;title=plain(row.get('title',[]))[:100]
            if not url.startswith('https://'):continue
            try:
                parsed=urllib.parse.urlsplit(url)
                if parsed.hostname=='en.wikipedia.org' and parsed.path.startswith('/wiki/'):
                    result=self.wiki_document(urllib.parse.unquote(parsed.path[6:]).replace('_',' '),query)
                    if result and result.status==OK:return result
                    continue
                raw=self.search.request(url,524288);text=raw.decode('utf-8',errors='replace')
                recipe=StructuredRecipe();recipe.feed(text)
                if recipe.recipes:
                    evidence=full_recipe(recipe)
                    if not evidence:continue
                else:
                    parser=DocumentText();parser.feed(text);evidence=rank_units(query,parser.units(),title)
                if evidence:return Result(OK,title,url,evidence,int(time.time()),len(raw))
            except (OSError,ValueError,TypeError,KeyError) as e:failures.append(type(e).__name__)
        return Result(NOT_FOUND,error=','.join(failures)[:180])
