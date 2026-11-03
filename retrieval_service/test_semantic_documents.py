import socket,struct,threading,time,unittest
from unittest.mock import Mock
from service import Result,Retrieval,Server,REQUEST,RESPONSE,VERSION,OK,NOT_FOUND,MALFORMED,exact,decode_result
from semantic_documents import SemanticProvider

class RecordingRanker:
 def scores(self,message,documents):return [1.0]*len(documents)
 def rank(self,message,topic,candidates):
  self.seen=(message,topic,candidates)
  return [(1,candidates[0])] if candidates else []

class SemanticDocumentsTests(unittest.TestCase):
 def provider(self):
  backend=Mock();backend.tls=None;backend.local=threading.local();backend.cache_epoch=0
  ranker=RecordingRanker();p=SemanticProvider(ranker,backend)
  p.search.search=Mock(side_effect=OSError('secondary source down'))
  return p,backend,ranker
 def test_secondary_failure_keeps_fetched_document(self):
  p,b,ranker=self.provider();b.api.return_value={'query':{'pages':{'1':{'title':'Fixture','extract':'A complete document with useful reference information.'}}}}
  r=p.fetch_context('An arbitrary current request','model proposal','Previous topic')
  self.assertEqual(r.status,OK);self.assertIn('A complete document with useful reference information.',r.evidence)
  self.assertEqual(ranker.seen[:2],('An arbitrary current request','Previous topic'))
 def test_fulltext_results_do_not_require_query_to_be_a_title(self):
  p,b,ranker=self.provider()
  def api(host,**kw):
   page={'title':'Reference article','extract':'This article provides a complete account of the requested phenomenon.'} if kw.get('generator')=='search' else {'title':'Missing','missing':''}
   return {'query':{'pages':{'1':page}}}
  b.api.side_effect=api
  self.assertEqual(p.fetch_context('Please explain the phenomenon','some search words').status,OK)
  self.assertTrue(any(c.kwargs.get('generator')=='search' for c in b.api.call_args_list))
 def test_search_snippet_is_not_evidence(self):
  p,b,ranker=self.provider();b.api.return_value={'query':{'pages':{}}}
  p.search.search=Mock(return_value=[{'url':'https://example.org','title':'Fixture','extract':'An attractive but unverified search snippet.'}]);p.search.request=Mock(side_effect=OSError('page unavailable'))
  r=p.fetch_context('Any current request','Any proposed query');self.assertEqual(r.status,NOT_FOUND);self.assertFalse(r.evidence)
 def test_context_topic_participates_in_cache_identity(self):
  p=Mock();p.fetch_context.side_effect=[Result(OK,'A',evidence='First source.',timestamp=int(time.time())),Result(OK,'B',evidence='Second source.',timestamp=int(time.time()))]
  r=Retrieval(p);self.assertEqual(r.query('Same question','Same plan','Topic A').title,'A');self.assertEqual(r.query('Same question','Same plan','Topic B').title,'B');self.assertEqual(p.fetch_context.call_count,2)

class TopicWireTests(unittest.TestCase):
 def exchange(self,payload,magic=b'TYBQ'):
  p=Mock();p.fetch_context.return_value=Result(OK,'Fixture',evidence='Complete source information.',timestamp=int(time.time()))
  with Server(('127.0.0.1',0),Retrieval(p),[]) as server:
   t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   with socket.create_connection(server.server_address,timeout=3) as s:
    s.sendall(REQUEST.pack(magic,VERSION,len(payload))+payload);header=exact(s,RESPONSE.size);f=RESPONSE.unpack(header);r=decode_result(header+exact(s,sum(f[3:6])))
   server.shutdown();t.join(2)
  return r,p
 def test_three_utf8_fields_arrive_independently(self):
  fields=[s.encode() for s in ['What changed?','current query','CafÃ© topic']];payload=struct.pack('!HHH',*map(len,fields))+b''.join(fields)
  r,p=self.exchange(payload);self.assertEqual(r.status,OK);p.fetch_context.assert_called_once_with('What changed?','current query','CafÃ© topic')
 def test_invalid_topic_cannot_clear_an_existing_conversation(self):
  fields=[b'm',b'q',b'x'*129];r,p=self.exchange(struct.pack('!HHH',*map(len,fields))+b''.join(fields),b'TYBN')
  self.assertEqual(r.status,MALFORMED);p.clear.assert_not_called();p.fetch_context.assert_not_called()

if __name__=='__main__':unittest.main()


class DeadlineTests(unittest.TestCase):
 def test_slow_source_returns_without_abandoned_worker_growth(self):
  p=SemanticProvider(RecordingRanker());p.response_budget=.05
  entered=threading.Event();release=threading.Event()
  def slow(*args):
   entered.set();release.wait(2);return Result(OK,'Late',evidence='Late evidence must not be returned.')
  p.collect=slow
  started=time.monotonic();r=p.fetch_context('request','proposal')
  self.assertLess(time.monotonic()-started,.3);self.assertEqual(r.status,NOT_FOUND)
  self.assertTrue(entered.is_set())
  self.assertEqual(p.fetch_context('next request','next proposal').status,NOT_FOUND)
  release.set()
  self.assertTrue(p.flight.acquire(timeout=1));p.flight.release()
 def test_source_sentence_retains_title_and_section(self):
  p=SemanticProvider(RecordingRanker())
  rows=p.wiki_candidates({'title':'Aster','extract':'Aster is an invented subject for testing.\n== Equipment ==\nAster uses a silver compass for navigation. Another complete sentence describes its history.'})
  self.assertEqual(len(rows),3)
  self.assertEqual(rows[1].evidence,'Aster — Equipment: Aster uses a silver compass for navigation.')
 def test_document_clear_discards_inflight_cache_write(self):
  p=SemanticProvider(RecordingRanker())
  def request(*args):
   p.clear();return b'<article><p>A complete sentence of source text.</p></article>'
  p.search.request=request
  p.html_candidates('https://example.org','Fixture')
  self.assertEqual(p.documents,{})


class IntentGateTests(unittest.TestCase):
 def test_local_action_never_fetches_external_text(self):
  intent=Mock();intent.research.return_value=False
  backend=Mock();backend.tls=None
  p=SemanticProvider(RecordingRanker(),backend,intent);p.search=Mock()
  result=p.fetch_context('An arbitrary user request','A misleading proposal','A stale topic')
  self.assertEqual(result.status,6);self.assertFalse(result.evidence)
  intent.research.assert_called_once_with('An arbitrary user request')
  backend.api.assert_not_called();p.search.search.assert_not_called();p.search.request.assert_not_called()
 def test_classifier_failure_cannot_start_a_lookup(self):
  intent=Mock();intent.research.side_effect=ValueError('Unavailable classifier')
  backend=Mock();backend.tls=None
  p=SemanticProvider(RecordingRanker(),backend,intent);p.search=Mock()
  self.assertEqual(p.fetch_context('Request','proposal').status,NOT_FOUND)
  backend.api.assert_not_called();p.search.search.assert_not_called()
 def test_clear_during_collection_discards_the_result(self):
  p=SemanticProvider(RecordingRanker());entered=threading.Event();release=threading.Event();result=[]
  def slow(*args):
   entered.set();release.wait(2);return Result(OK,'Old',evidence='Old conversation source.')
  p.collect=slow;t=threading.Thread(target=lambda:result.append(p.fetch_context('request','proposal')));t.start()
  self.assertTrue(entered.wait(1));p.clear();release.set();t.join(2)
  self.assertEqual(result[0].status,NOT_FOUND)
 def test_local_decision_roundtrips_without_evidence(self):
  r=decode_result(Result(6,error='Local task').wire())
  self.assertEqual(r.status,6);self.assertEqual(r.evidence,'')


class DocumentSelectionRegressionTests(unittest.TestCase):
 def test_unfinished_web_fragment_is_not_admitted(self):
  p=SemanticProvider(RecordingRanker())
  p.search.request=Mock(return_value=b'<article><p>This fragment ends before the needed information about</p><p>This complete source sentence provides usable information.</p></article>')
  rows=p.html_candidates('https://example.org/fixture','Fixture')
  self.assertEqual(len(rows),1)
  self.assertEqual(rows[0].evidence,'This complete source sentence provides usable information.')
 def test_current_request_can_order_documents_despite_stale_context(self):
  ranker=Mock()
  ranker.scores.side_effect=[[1.0,8.0],[3.0,-2.0]]
  new=Result(OK,'New topic','https://example.org/new','A complete new-topic source sentence.')
  old=Result(OK,'Old topic','https://example.org/old','A complete old-topic source sentence.')
  ranker.rank.return_value=[(8.0,new)]
  p=SemanticProvider(ranker)
  self.assertEqual(p.select('A new subject request','Old topic',[old,new]).source,new.source)
  ranker.rank.assert_called_once_with('A new subject request','Old topic',[new])
