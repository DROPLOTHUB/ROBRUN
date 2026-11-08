# Historical patch helper — path updated for _eft_unused layout.
from pathlib import Path
path = Path(__file__).resolve().parent / "_eft_unused" / "features" / "entity_update.cpp"
print("target:", path)
print("exists:", path.exists())
