"""Action classification only; no generated text and no question-word rules."""
from pathlib import Path
import json,threading
import numpy as np
import onnxruntime as ort
from tokenizers import Tokenizer

class SemanticIntent:
    hypotheses=(
        'The user wants to obtain existing information, explanations, or practical instructions.',
        'The user wants to compose or invent new artistic or fictional content.',
        'The user wants to rewrite, translate, or summarize content they supplied.',
        'The user wants a social exchange rather than information or content creation.',
        'The user wants to research or verify information, including for a writing task.',
        'The user supplies numerical values or explicit premises and asks to calculate a result or make a logical deduction from them.',
        'The user is asking to edit or change provided text or a previous answer.',
        'This message is a greeting, thanks, or other social conversation.',
        'The user is expressing gratitude.',
    )
    def __init__(self,directory):
        directory=Path(directory);config=json.loads((directory/'config.json').read_text(encoding='utf-8'))
        self.entailment=config['label2id']['entailment']
        options=ort.SessionOptions();options.intra_op_num_threads=2;options.inter_op_num_threads=1
        self.model=ort.InferenceSession(str(directory/'model.onnx'),sess_options=options,providers=['CPUExecutionProvider'])
        self.tokenizer=Tokenizer.from_file(str(directory/'tokenizer.json'))
        self.tokenizer.enable_padding(pad_id=config['pad_token_id'],pad_token='[PAD]')
        self.tokenizer.enable_truncation(max_length=256)
        self.lock=threading.Lock()
    def research(self,message):
        with self.lock:
            batch=self.tokenizer.encode_batch([(message,h) for h in self.hypotheses])
            attributes={'input_ids':'ids','attention_mask':'attention_mask','token_type_ids':'type_ids'}
            inputs={i.name:np.asarray([getattr(e,attributes[i.name]) for e in batch],dtype=np.int64) for i in self.model.get_inputs()}
            logits=self.model.run(None,inputs)[0]
            scores=logits[:,self.entailment]-logits[:,1-self.entailment]
        choice=int(np.argmax(scores))
        # Reject every unsupported local label, not just deduction. A fictional
        # subject is not evidence that the user requested creative writing.
        # Compare binary entailment margins; a weak winner is not confidence.
        return bool(choice in (0,4) or scores[choice]<=0)
