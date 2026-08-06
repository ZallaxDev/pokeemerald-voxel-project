import os
from pathlib import Path

BASE_DIR = Path(__file__).resolve().parent.parent
TMP_DIR = BASE_DIR / ".tmp"
EXAMPLES_DIR = BASE_DIR / "examples"
STATIC_DIR = Path(__file__).resolve().parent / "static"

TMP_DIR.mkdir(parents=True, exist_ok=True)
EXAMPLES_DIR.mkdir(parents=True, exist_ok=True)

HOST = "127.0.0.1"
PORT = 8765

MAX_IMAGE_DIMENSION = 512
MAX_UPLOAD_SIZE = 10 * 1024 * 1024  # 10MB
