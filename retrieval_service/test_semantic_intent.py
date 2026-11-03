import threading,unittest
from types import SimpleNamespace
from unittest.mock import Mock
import numpy as np
from semantic_intent import SemanticIntent
class DeductionConfidenceTests(unittest.TestCase):
 def classifier(self,logits):
  m=SemanticIntent.__new__(SemanticIntent);m.entailment=0;m.lock=threading.Lock();m.tokenizer=Mock();m.tokenizer.encode_batch.return_value=[SimpleNamespace(ids=[1],attention_mask=[1])]*6;m.model=Mock();m.model.get_inputs.return_value=[SimpleNamespace(name='input_ids')];m.model.run.return_value=[np.asarray(logits)];return m
 def test_positive_deduction_skips_research(self):
  m=self.classifier([[0,0],[-3,3],[-3,3],[-3,3],[-1,1],[2,-2]])
  self.assertFalse(m.research('A self-contained task'))
 def test_least_negative_deduction_does_not_hide_research(self):
  m=self.classifier([[-2,2],[-3,3],[-3,3],[-3,3],[-3,3],[-1,1]])
  self.assertTrue(m.research('An underspecified factual follow-up'))

 def test_unsupported_creative_label_cannot_suppress_lookup(self):
  m=self.classifier([[-2,2],[-1,1],[-4,4],[-3,3],[-2,2],[-4,4]])
  self.assertTrue(m.research('An unknown subject'))
 def test_supported_original_content_remains_local(self):
  m=self.classifier([[-2,2],[3,-3],[-4,4],[-3,3],[-2,2],[-4,4]])
  self.assertFalse(m.research('An imaginative task'))
 def test_binary_margin_ignores_common_logit_offsets(self):
  base=[[-2,2],[3,-3],[-4,4],[-3,3],[-2,2],[-4,4]]
  shifted=[row[:] for row in base];shifted[0]=[98,102]
  self.assertEqual(self.classifier(base).research('Task'),self.classifier(shifted).research('Task'))
