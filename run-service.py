"""Launch the trusted-LAN public-document companion."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'retrieval_service'))
from service import main
main()
