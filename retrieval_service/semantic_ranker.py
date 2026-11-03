"""CPU passage ranking only. This model never writes chatbot answers."""
from pathlib import Path
import threading,time
import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer

class SemanticRanker:
    def __init__(self,directory):
        directory=Path(directory)
        options=ort.SessionOptions();options.intra_op_num_threads=2;options.inter_op_num_threads=1
        self.model=ort.InferenceSession(str(directory/'model_quint8_avx2.onnx'),sess_options=options,providers=['CPUExecutionProvider'])
        self.tokenizer=Tokenizer.from_file(str(directory/'tokenizer.json'))
        self.tokenizer.enable_truncation(max_length=256)
        self.tokenizer.enable_padding(pad_id=0,pad_token='[PAD]')
        self.inputs={v.name for v in self.model.get_inputs()};self.lock=threading.Lock()

    def scores(self,query,documents):
        if not documents:return []
        # Length grouping bounds padding overhead without discarding passages.
        order=sorted(range(len(documents)),key=lambda i:len(documents[i]))
        result=[0.0]*len(documents)
        for offset in range(0,len(order),16):
            indices=order[offset:offset+16]
            with self.lock:batch=self.tokenizer.encode_batch([(query,documents[i]) for i in indices])
            inputs={name:np.asarray([getattr(e,attr) for e in batch],dtype=np.int64)
                    for name,attr in [('input_ids','ids'),('attention_mask','attention_mask'),('token_type_ids','type_ids')] if name in self.inputs}
            scores=self.model.run(None,inputs)[0].reshape(-1).tolist()
            for i,score in zip(indices,scores):result[i]=score
        return result

    def rank(self,message,topic,candidates):
        if not candidates:return []
        documents=[c.title+'\n'+c.evidence for c in candidates]+['']
        raw=self.scores(message,documents)
        contextual=self.scores(topic+': '+message,documents) if topic else raw
        rows=[]
        for i,candidate in enumerate(candidates):
            # Context can resolve an ambiguous request, but it cannot veto a
            # source independently relevant to a clear new request.
            current=raw[i]>max(0,raw[-1]+2)
            contextual_match=contextual[i]>max(0,contextual[-1]+2)
            if raw[i]>raw[-1]+2 and (current or contextual_match):
                rows.append((max(raw[i],contextual[i]),candidate))
        return sorted(rows,key=lambda row:row[0],reverse=True)
