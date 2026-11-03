"""Launch the companion, reusing task-local dependencies when present."""
import sys,runpy
from pathlib import Path
root=Path(__file__).resolve().parent
local_dependencies=root.parent.parent/'work/retrieval-python-deps'
if local_dependencies.is_dir():sys.path.insert(0,str(local_dependencies))
sys.path.insert(0,str(root/'retrieval_service'))
from service import main
main()
