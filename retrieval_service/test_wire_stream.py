import socket,struct,threading,time,unittest
from unittest.mock import Mock
from service import Server,Retrieval,Result,OK,REQUEST,RESPONSE,VERSION,exact,decode_result

class StreamTests(unittest.TestCase):
 def test_fragmented_request_preserves_all_three_fields(self):
  provider=Mock();provider.fetch_context.return_value=Result(OK,'Fixture',evidence='Complete reference text.',timestamp=int(time.time()))
  with Server(('127.0.0.1',0),Retrieval(provider),[]) as server:
   t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   fields=[v.encode() for v in ['Where did she go?','Aster voyage','Aster']]
   payload=struct.pack('!HHH',*map(len,fields))+b''.join(fields)
   with socket.create_connection(server.server_address,timeout=2) as s:
    for byte in REQUEST.pack(b'TYBQ',VERSION,len(payload))+payload:s.sendall(bytes([byte]))
    h=exact(s,RESPONSE.size);r=decode_result(h+exact(s,sum(RESPONSE.unpack(h)[3:6])))
   server.shutdown();t.join(2)
  self.assertEqual(r.status,OK);provider.fetch_context.assert_called_once_with('Where did she go?','Aster voyage','Aster')
 def test_peer_outside_allowlist_cannot_query_or_clear(self):
  provider=Mock()
  with Server(('127.0.0.1',0),Retrieval(provider),[]) as server:
   server.allowed=set();t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   with socket.create_connection(server.server_address,timeout=2) as s:
    s.sendall(REQUEST.pack(b'TYCL',VERSION,0))
    try:self.assertEqual(s.recv(32),b'')
    except (ConnectionResetError,ConnectionAbortedError):pass
   server.shutdown();t.join(2)
  provider.clear.assert_not_called();provider.fetch_context.assert_not_called()
 def test_truncated_request_cannot_reach_provider(self):
  provider=Mock()
  with Server(('127.0.0.1',0),Retrieval(provider),[]) as server:
   t=threading.Thread(target=server.serve_forever,daemon=True);t.start()
   with socket.create_connection(server.server_address,timeout=2) as s:
    s.sendall(REQUEST.pack(b'TYBQ',VERSION,20)+b'partial');s.shutdown(socket.SHUT_WR)
    self.assertEqual(s.recv(32),b'')
   server.shutdown();t.join(2)
  provider.fetch_context.assert_not_called()
