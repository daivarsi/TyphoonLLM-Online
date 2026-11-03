import unittest,struct,socket,threading,time
from unittest.mock import Mock
from service import Result,OK,NOT_FOUND,MALFORMED,Retrieval,Server,REQUEST,RESPONSE,VERSION,exact,decode_result
from general_retrieval import GeneralProvider,relevant_to_message

class AnchorTests(unittest.TestCase):
 def test_stale_topic_rejected_and_original_question_tried(self):
  p=GeneralProvider.__new__(GeneralProvider)
  stale=Result(OK,'Raven Quest','https://example.org/game','A fantasy game about an explorer.')
  useful=Result(OK,'Fresh berries','https://example.org/food','Store berries dry and refrigerated.')
  p.fetch=Mock(side_effect=[stale,useful])
  r=p.fetch_context('How do I store fresh berries?','Raven Quest')
  self.assertEqual(r,useful);self.assertEqual(p.fetch.call_args_list[0].args[0],'raven quest');self.assertIn('berries',p.fetch.call_args_list[1].args[0])
 def test_followup_can_use_query_words_from_history(self):
  p=GeneralProvider.__new__(GeneralProvider);p.fetch=Mock(return_value=Result(OK,'Raven Quest','https://example.org','The character appears in the video game Raven Quest.'))
  self.assertEqual(p.fetch_context('What game is she from?','Raven Quest').status,OK)
 def test_all_irrelevant_results_produce_empty_context(self):
  p=GeneralProvider.__new__(GeneralProvider);p.fetch=Mock(return_value=Result(OK,'An explorer','https://example.org','An explorer appears in a fantasy game.'))
  r=p.fetch_context('How should I refrigerate berries?','An explorer');self.assertEqual(r.status,NOT_FOUND);self.assertFalse(r.evidence);self.assertFalse(r.source)
 def test_exact_document_still_requires_original_relevance(self):
  self.assertFalse(relevant_to_message('Explain how photosynthesis works.',Result(OK,'Video game','https://example.org','A fantasy role-playing game.')))
 def test_queries_without_content_terms_are_conservative(self):
  self.assertFalse(relevant_to_message('Tell me more.',Result(OK,'Anything',evidence='An unrelated subject.')))
 def test_generic_shared_action_does_not_admit_wrong_object(self):
  self.assertFalse(relevant_to_message('How do I store fresh pears?',Result(OK,'Store fresh cabbage',evidence='Store fresh cabbage in a cool place.')))
 def test_inflected_object_matches_source(self):
  self.assertTrue(relevant_to_message('How should I store berries?',Result(OK,'Berry storage',evidence='A berry stays fresh when kept dry.')))
 def test_vague_followup_does_not_accept_unrelated_game(self):
  p=GeneralProvider.__new__(GeneralProvider);p.fetch=Mock(return_value=Result(OK,'A different game',evidence='A video game about unrelated characters.'))
  self.assertEqual(p.fetch_context('What game is he from?','Raven Quest').status,NOT_FOUND)
 def test_cache_does_not_mix_different_messages_with_same_plan(self):
  p=Mock();p.fetch_context.side_effect=[Result(OK,'A',evidence='First facts.',timestamp=int(time.time())),Result(NOT_FOUND)]
  r=Retrieval(p);r.query('First request','same plan');self.assertEqual(r.query('Different request','same plan').status,NOT_FOUND);self.assertEqual(p.fetch_context.call_count,2)
 def test_source_loop_continues_after_wrong_object_page(self):
  backend=Mock();backend.tls=None;backend.local=threading.local();backend.cache_epoch=0;backend.page.return_value=None
  p=GeneralProvider(backend);p.search.search=Mock(return_value=[{'url':'https://example.org/dill','title':'Store fresh dill'},{'url':'https://example.org/berries','title':'Berries'}])
  p.search.request=Mock(side_effect=[b'<article><p>Store fresh dill in a cool place.</p></article>',b'<article><p>Fresh berries should be stored dry and refrigerated.</p></article>'])
  r=p.fetch('store fresh berries',accept=lambda x:relevant_to_message('How do I store fresh berries?',x))
  self.assertEqual(r.source,'https://example.org/berries');self.assertEqual(p.search.request.call_count,2)
 def wire(self,payload):
  p=Mock();p.fetch_context.return_value=Result(OK,'Fixture',evidence='Complete source information.',timestamp=int(time.time()))
  with Server(('127.0.0.1',0),Retrieval(p),[]) as server:
   t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   with socket.create_connection(server.server_address,timeout=3) as s:
    s.sendall(REQUEST.pack(b'TYAQ',VERSION,len(payload))+payload);header=exact(s,RESPONSE.size);f=RESPONSE.unpack(header);r=decode_result(header+exact(s,sum(f[3:6])))
   server.shutdown();t.join(2)
  return r,p
 def test_wire_preserves_original_and_suggestion_as_separate_fields(self):
  m='Explain café brewing ☕'.encode();q=b'coffee';r,p=self.wire(struct.pack('!HH',len(m),len(q))+m+q)
  self.assertEqual(r.status,OK);p.fetch_context.assert_called_once_with(m.decode(),'coffee')
 def test_wire_rejects_inconsistent_lengths(self):
  r,p=self.wire(struct.pack('!HH',100,2)+b'ab');self.assertEqual(r.status,MALFORMED);p.fetch_context.assert_not_called()
 def test_wire_rejects_oversized_original(self):
  r,p=self.wire(struct.pack('!HH',1025,1)+b'x'*1025+b'y');self.assertEqual(r.status,MALFORMED);p.fetch_context.assert_not_called()

if __name__=='__main__':unittest.main()
