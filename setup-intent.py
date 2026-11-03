"""Install pinned public intent-classifier data. No remote Python is executed."""
from pathlib import Path
import argparse,hashlib,json,ssl,urllib.request
import truststore

REPO='MoritzLaurer/deberta-v3-base-zeroshot-v2.0'
REVISION='8e7e5af5983a0ddb1a5b45a38b129ab69e2258e8'
EXPECTED_SHA256={'onnx/model.onnx': 'd46fa6c1addc598abd34af8651120291f51417ddfce45b9d5f70bc32d3efa81c', 'onnx/tokenizer.json': 'bfead76cd475f67849a6710fdd16497f75732773e4d90e40d3b5bb5d3f031df9', 'onnx/config.json': 'a68162c0b02cd39759732b33d26ee2868cb64701b0b9255fee4f55fd9d5609f4', 'README.md': 'bd974d885c91d90f96183e86803e80b080ad365cb65307f2ebd9169d2646f08f'}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--directory',type=Path,default=Path(__file__).resolve().parent/'models/intent');args=parser.parse_args()
    args.directory.mkdir(parents=True,exist_ok=True);context=truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT);files=[]
    for remote in ['onnx/model.onnx', 'onnx/tokenizer.json', 'onnx/config.json', 'README.md']:
        url='https://huggingface.co/'+REPO+'/resolve/'+REVISION+'/'+remote
        with urllib.request.urlopen(url,timeout=90,context=context) as response:data=response.read(800*1024*1024+1)
        if len(data)>800*1024*1024:raise ValueError('Unexpected model file size')
        digest=hashlib.sha256(data).hexdigest()
        if digest!=EXPECTED_SHA256[remote]:raise ValueError('Pinned file checksum mismatch: '+remote)
        name=Path(remote).name;(args.directory/name).write_bytes(data);files.append(dict(file=name,sha256=digest,size=len(data)))
        print(name,len(data),'bytes',flush=True)
    (args.directory/'manifest.json').write_text(json.dumps(dict(repo=REPO,revision=REVISION,files=files),indent=2),encoding='utf-8')
    print('Intent classifier ready. Answer generation remains on the Vita.')

if __name__=='__main__':main()
