"""Install pinned public relevance-model data. No remote Python is executed."""
from pathlib import Path
import argparse,hashlib,json,ssl,urllib.request
import truststore

REPO='cross-encoder/ms-marco-MiniLM-L6-v2'
REVISION='233902d25c440f23af6f7d6e94d2946bac0bee0a'
MODEL_SHA256='c80a8b34256ea453093d612e3ac48d3d965a0c0a48c7906709af8b8e28461bf9'

EXPECTED_SHA256={'onnx/model_quint8_avx2.onnx': 'c80a8b34256ea453093d612e3ac48d3d965a0c0a48c7906709af8b8e28461bf9', 'tokenizer.json': 'd241a60d5e8f04cc1b2b3e9ef7a4921b27bf526d9f6050ab90f9267a1f9e5c66', 'config.json': '380e02c93f431831be65d99a4e7e5f67c133985bf2e77d9d4eba46847190bacc', 'README.md': '7c0ec39941d0d1d766ccde671f592971d82fe45b7eea550a80d1ea864ebc3baa'}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--directory',type=Path,default=Path(__file__).resolve().parent/'models/relevance');args=parser.parse_args()
    args.directory.mkdir(parents=True,exist_ok=True);context=truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT);files=[]
    for remote in ['onnx/model_quint8_avx2.onnx','tokenizer.json','config.json','README.md']:
        url='https://huggingface.co/'+REPO+'/resolve/'+REVISION+'/'+remote
        with urllib.request.urlopen(url,timeout=90,context=context) as response:data=response.read(100*1024*1024+1)
        if len(data)>100*1024*1024:raise ValueError('Unexpected model file size')
        digest=hashlib.sha256(data).hexdigest()
        if digest!=EXPECTED_SHA256[remote]:raise ValueError('Pinned file checksum mismatch: '+remote)
        name=Path(remote).name;(args.directory/name).write_bytes(data);files.append(dict(file=name,sha256=digest,size=len(data)))
        print(name,len(data),'bytes',flush=True)
    (args.directory/'manifest.json').write_text(json.dumps(dict(repo=REPO,revision=REVISION,files=files),indent=2),encoding='utf-8')
    print('Relevance model ready. Answer generation remains on the Vita.')

if __name__=='__main__':main()
