from pathlib import Path
import argparse,json,sys
root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/'retrieval_service'))
from semantic_intent import SemanticIntent
parser=argparse.ArgumentParser();parser.add_argument('--model',type=Path,default=root/'models/intent');args=parser.parse_args()
model=SemanticIntent(args.model);cases=json.loads((root/'validation/intent_cases.json').read_text(encoding='utf-8'));failed=[]
for case in cases:
    result=model.research(case['request'])
    if result!=case['research']:failed.append(case['request'])
print(str(len(cases)-len(failed))+'/'+str(len(cases))+' intent decisions match expected actions')
for request in failed:print('FAIL: '+request)
raise SystemExit(bool(failed))
