import http.client,socket,threading,unittest
from unittest.mock import Mock,MagicMock,patch
import urllib.request
from web_documents import normalized_url,public_url,SafeRedirect,WebSearch
from semantic_documents import SemanticProvider
from service import OK,NOT_FOUND

class URLHandlingTests(unittest.TestCase):
 def test_spaces_unicode_and_existing_escapes(self):
  self.assertEqual(normalized_url('https://example.org/wiki/Alice Smith?q=café%20tea#A B'),'https://example.org/wiki/Alice%20Smith?q=caf%C3%A9%20tea#A%20B')
 def test_reserved_delimiters_and_idempotence(self):
  url='https://example.org/a%2Fb?q=x+y&next=/c?d=e'
  self.assertEqual(normalized_url(url),url)
  self.assertEqual(normalized_url(normalized_url(url)),url)
 def test_unicode_host_uses_idna(self):
  self.assertEqual(normalized_url('https://bücher.example/a'),'https://xn--bcher-kva.example/a')
 def test_invalid_authority_controls_and_expansion_bounds(self):
  for url in ['http://example.org/a','https://user:pass@example.org/a','https://example.org:8443/a','https://exa mple.org/a','https://example.org/a\nb','https://example.org/'+'é'*1000]:
   with self.subTest(url=url[:50]):
    with self.assertRaises(ValueError):normalized_url(url)
 @patch('web_documents.socket.getaddrinfo')
 def test_private_resolution_remains_blocked(self,dns):
  dns.return_value=[(socket.AF_INET,socket.SOCK_STREAM,6,'',('127.0.0.1',443))]
  with self.assertRaises(ValueError):public_url('https://example.org/A B')
 @patch('web_documents.socket.getaddrinfo')
 def test_encoded_url_reaches_transport(self,dns):
  dns.return_value=[(socket.AF_INET,socket.SOCK_STREAM,6,'',('93.184.216.34',443))]
  web=WebSearch(None);web.opener=Mock();response=MagicMock();response.__enter__.return_value.read.return_value=b'ok';web.opener.open.return_value=response
  self.assertEqual(web.request('https://example.org/A B',20),b'ok')
  self.assertEqual(web.opener.open.call_args.args[0].full_url,'https://example.org/A%20B')
 @patch('web_documents.socket.getaddrinfo')
 def test_redirect_is_normalized_and_validated(self,dns):
  dns.return_value=[(socket.AF_INET,socket.SOCK_STREAM,6,'',('93.184.216.34',443))]
  req=urllib.request.Request('https://example.org/start')
  redirected=SafeRedirect().redirect_request(req,None,302,'Found',{},'https://example.org/A B')
  self.assertEqual(redirected.full_url,'https://example.org/A%20B')

class SourceFailureIsolationTests(unittest.TestCase):
 def test_bad_secondary_response_keeps_prior_valid_evidence(self):
  backend=Mock();backend.tls=None;backend.local=threading.local();backend.cache_epoch=0
  backend.api.return_value={'query':{'pages':{'1':{'title':'Fixture','extract':'This complete factual sentence is valid fetched evidence.'}}}}
  ranker=Mock();ranker.scores.side_effect=lambda q,docs:[1.0]*len(docs);ranker.rank.side_effect=lambda q,t,rows:[(1,rows[0])] if rows else []
  provider=SemanticProvider(ranker,backend)
  provider.search.search=Mock(return_value=[{'url':'https://example.org/A B','title':[],'extract':[]}])
  provider.search.request=Mock(side_effect=http.client.InvalidURL('malformed upstream URL'))
  result=provider.fetch_context('An arbitrary factual request','query')
  self.assertEqual(result.status,OK);self.assertIn('valid fetched evidence',result.evidence)
 def test_unexpected_http_failure_returns_error_and_releases_worker(self):
  p=SemanticProvider(Mock());p.collect=Mock(side_effect=http.client.BadStatusLine('bad response'))
  r=p.fetch_context('request','query');self.assertEqual(r.status,NOT_FOUND);self.assertEqual(r.error,'BadStatusLine')
  self.assertTrue(p.flight.acquire(blocking=False));p.flight.release()
