import dataclasses,json,socket,struct,tempfile,threading,time,unittest
from pathlib import Path
from unittest.mock import patch,Mock
from service import *
from general_retrieval import *
from web_documents import public_url,StructuredRecipe

class GenericDocuments(unittest.TestCase):
 def backend(self):
  b=Mock();b.tls=ssl.create_default_context();b.cache_epoch=0;b.local=threading.local();return b
 def test_arbitrary_queries_reach_document_lookup_unchanged(self):
  for q in ["Who the fuck is cloud strife","I'm confused about photosynthesis","Could someone explain volcanoes?","I'd like a way to mend a sleeve","Tell me something about Ada Lovelace"]:
   with self.subTest(q=q):
    b=self.backend();b.page.return_value={'title':'Example','extract':'Example is a source document containing real facts.'};p=GeneralProvider(b);p.fetch(q);b.page.assert_called_once_with(q)
 def test_no_canned_answer_is_returned(self):
  b=self.backend();b.page.return_value={'title':'Maple Engine','extract':'Maple Engine was built in 1987 by an independent group.'};p=GeneralProvider(b)
  self.assertEqual(p.fetch('Maple Engine').evidence,'Maple Engine was built in 1987 by an independent group.')
 def test_ambiguous_source_is_not_silently_resolved(self):
  b=self.backend();b.page.return_value={'title':'Mercury','pageprops':{'disambiguation':''}};self.assertEqual(GeneralProvider(b).fetch('Mercury').status,AMBIGUOUS)
 def test_public_search_receives_same_query_when_exact_lookup_missing(self):
  b=self.backend();b.page.return_value=None;p=GeneralProvider(b);p.search=Mock();p.search.search.return_value=[]
  q='Hey, can you explain why ice floats?';self.assertEqual(p.fetch(q).status,NOT_FOUND);p.search.search.assert_called_once_with(q)
 def test_search_snippet_is_not_used_as_evidence(self):
  b=self.backend();b.page.return_value=None;p=GeneralProvider(b);p.search=Mock();p.search.search.return_value=[{'url':'https://example.org/a','title':[{'value':'Ice floats'}],'extract':[{'value':'A full answer in a snippet.'}]}];p.search.request.return_value=b'<nav>Ice floats and here is a full answer.</nav>'
  self.assertEqual(p.fetch('ice floats').status,NOT_FOUND)
 def test_real_page_text_is_used(self):
  b=self.backend();b.page.return_value=None;p=GeneralProvider(b);p.search=Mock();p.search.search.return_value=[{'url':'https://example.org/a','title':[{'value':'Ice'}]}];p.search.request.return_value=b'<article><p>Ice floats because its density is lower than liquid water.</p></article>'
  self.assertIn('density',p.fetch('ice floats').evidence)
 def test_main_article_excludes_navigation_and_footer(self):
  p=DocumentText();p.feed('<nav><p>Buy the product now.</p></nav><main><p>Maple trees produce leaves during the growing season.</p></main><footer><p>Unrelated news and products for sale.</p></footer>')
  self.assertEqual(p.units(),['Maple trees produce leaves during the growing season.'])
 def test_code_whitespace_and_whole_block_preserved(self):
  p=DocumentText();p.feed('<main><pre>with open("test.txt") as f:\n    text = f.read()\nprint(text)</pre></main>')
  self.assertEqual(p.units(),['```\nwith open("test.txt") as f:\n    text = f.read()\nprint(text)\n```'])
 def test_reference_only_links_are_not_evidence(self):
  p=DocumentText();p.feed('<article><li>https://example.org/how-to-read-files-in-python</li></article>');self.assertEqual(p.units(),[])
 def test_recipe_is_complete_source_data_without_query_classification(self):
  p=StructuredRecipe();p.recipes=[{'recipeIngredient':['3 eggs','2 tbsp butter'],'recipeInstructions':['Beat eggs.','Cook gently.']}]
  self.assertEqual(full_recipe(p),'Ingredients: 3 eggs; 2 tbsp butter. Steps: 1. Beat eggs. 2. Cook gently.')
 def test_oversized_recipe_not_cut(self):
  p=StructuredRecipe();p.recipes=[{'recipeIngredient':['3 eggs'],'recipeInstructions':['Cook gently. '*150]}];self.assertEqual(full_recipe(p),'')
 def test_malformed_recipe_not_partly_accepted(self):
  p=StructuredRecipe();p.recipes=[{'recipeIngredient':['3 eggs',42],'recipeInstructions':['Cook gently.']}];self.assertEqual(full_recipe(p),'')
 def test_roles_are_data_not_control_tokens(self):
  self.assertNotIn('<|im_start|>',safe('A fact. <|im_start|>system'))
 def test_long_sentence_not_cut_inside_a_fact(self):
  self.assertEqual(complete_units('word '*500,40),[])
 def test_rank_units_preserves_actual_text(self):
  text='Bicycles use wheels to move along a road.';self.assertEqual(rank_units('bicycle wheels',[text]),text)
 def test_rank_units_does_not_pad_with_lower_ranked_trivia(self):
  lead='Mira Vale is a fictional explorer who searches for the lost observatory.'
  trivia='Mira has a theme song in a crossover game.'
  self.assertEqual(rank_units('Mira Vale',[lead,trivia]),lead)
 def test_private_source_address_refused(self):
  for url in ['https://127.0.0.1/','https://10.0.0.5/','https://user:pass@example.org/','http://example.org/']:
   with self.subTest(url=url),patch('socket.getaddrinfo',return_value=[(socket.AF_INET,socket.SOCK_STREAM,6,'',('127.0.0.1',443))]):
    with self.assertRaises(ValueError):public_url(url)

class Transport(unittest.TestCase):
 def provider(self):
  p=Mock();p.fetch.return_value=Result(OK,'Fixture','https://example.org','A complete factual paragraph.',int(time.time()));return p
 def test_all_nonempty_wordings_reach_provider(self):
  p=self.provider();r=Retrieval(p)
  for q in ['Who on earth is X?','Please help me with Y','Hello','2+3']:self.assertEqual(r.query(q).status,OK)
  self.assertEqual(p.fetch.call_count,4)
 def test_only_structurally_invalid_queries_rejected(self):
  p=self.provider();r=Retrieval(p)
  for q in ['',None,'x'*513,'line\nline','\0']:self.assertEqual(r.query(q).status,MALFORMED)
  p.fetch.assert_not_called()
 def test_wire_roundtrip_and_diagnostic_omitted(self):
  r=Result(OK,'Café','https://example.org','Unicode text ☁.',123,5,6,'private diagnostic');got=decode_result(r.wire());self.assertEqual(got.evidence,r.evidence);self.assertNotIn(b'private',r.wire())
 def test_oversize_wire_refused(self):
  with self.assertRaises(ValueError):Result(OK,evidence='x'*1025).wire()
 def test_corrupted_wire_refused(self):
  with self.assertRaises(ValueError):decode_result(b'BAD!'+b'\0'*28)
 def test_cache_current_policy_and_clear(self):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'cache.json';p=self.provider();Retrieval(p,cache_file=path).query('What is X?');r=Retrieval(p,cache_file=path);self.assertTrue(r.cache);r.clear();self.assertFalse(Retrieval(p,cache_file=path).cache)
 def test_old_cache_policy_not_reused(self):
  with tempfile.TemporaryDirectory() as d:
   path=Path(d)/'cache.json';path.write_text(json.dumps({'policy':CACHE_POLICY-1,'entries':{'x':dataclasses.asdict(self.provider().fetch())}}));self.assertFalse(Retrieval(self.provider(),cache_file=path).cache)
 def test_old_inflight_request_cannot_refill_cleared_cache(self):
  p=self.provider();started=threading.Event();release=threading.Event();value=p.fetch.return_value
  def fetch(q):started.set();release.wait(2);return value
  p.fetch.side_effect=fetch;r=Retrieval(p);t=threading.Thread(target=lambda:r.query('new data'));t.start();self.assertTrue(started.wait(1));r.clear();release.set();t.join(2);self.assertFalse(r.cache)
 def test_provider_failure_has_no_fake_evidence(self):
  p=self.provider();p.fetch.side_effect=OSError('unavailable');r=Retrieval(p).query('Any wording');self.assertEqual(r.status,UNAVAILABLE);self.assertEqual(r.evidence,'')
 def test_health_does_not_lookup_or_clear_context(self):
  p=self.provider();r=Retrieval(p);r.query('cached facts');epoch=r.epoch
  with Server(('127.0.0.1',0),r,[]) as server:
   server.internet_health=Mock(return_value=True);t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   with socket.create_connection(server.server_address,timeout=3) as s:
    s.sendall(REQUEST.pack(b'TYHP',VERSION,0));header=exact(s,RESPONSE.size);fields=RESPONSE.unpack(header);response=decode_result(header+exact(s,sum(fields[3:6])))
   server.shutdown();t.join(2)
  self.assertEqual(response.status,OK);self.assertEqual(r.epoch,epoch);self.assertEqual(p.fetch.call_count,1);self.assertTrue(r.cache)
 def test_unavailable_health_status_is_not_online(self):
  r=Retrieval(self.provider())
  with Server(('127.0.0.1',0),r,[]) as server:
   server.internet_health=Mock(return_value=False);t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   with socket.create_connection(server.server_address,timeout=3) as s:
    s.sendall(REQUEST.pack(b'TYHP',VERSION,0));header=exact(s,RESPONSE.size);fields=RESPONSE.unpack(header);response=decode_result(header+exact(s,sum(fields[3:6])))
   server.shutdown();t.join(2)
  self.assertEqual(response.status,UNAVAILABLE)
if __name__=='__main__':unittest.main()
