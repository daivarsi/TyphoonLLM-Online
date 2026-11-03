import argparse,json,dataclasses,socket
from service import REQUEST,RESPONSE,VERSION,MAX_EVIDENCE,exact,decode_result
parser=argparse.ArgumentParser();parser.add_argument('--host',default='127.0.0.1');parser.add_argument('--port',type=int,default=28500);parser.add_argument('--clear-cache',action='store_true');parser.add_argument('--fresh',action='store_true');parser.add_argument('query',nargs='?',default='');args=parser.parse_args()
with socket.create_connection((args.host,args.port),timeout=6) as s:
    s.settimeout(12)
    if args.clear_cache:
        s.sendall(REQUEST.pack(b'TYCL',VERSION,0));exact(s,RESPONSE.size);print('Retrieval cache cleared')
    else:
        query=args.query.encode('utf-8')
        if not 0<len(query)<=512:raise SystemExit('Query must be 1..512 UTF-8 bytes')
        s.sendall(REQUEST.pack(b'TYRN' if args.fresh else b'TYRQ',VERSION,len(query))+query)
        header=exact(s,RESPONSE.size);fields=RESPONSE.unpack(header)
        if fields[3]>128 or fields[4]>384 or fields[5]>MAX_EVIDENCE:raise SystemExit('Response bounds rejected')
        result=decode_result(header+exact(s,sum(fields[3:6])))
        print(json.dumps(dataclasses.asdict(result),ensure_ascii=False,indent=2))
