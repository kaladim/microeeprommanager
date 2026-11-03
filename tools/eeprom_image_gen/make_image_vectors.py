"""Regenerates the EEPROM images of test/vectors/image_vectors.json, which the inspector's tests read.
test_eeprom_image_gen.py fails while one of them is stale."""

import os
import sys
import json
import subprocess
import tempfile

IMAGE_GEN = os.path.join(os.path.dirname(__file__), "eeprom_image_gen.py")
ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
IMAGE_VECTORS_FILE = os.path.join(ROOT, "test", "vectors", "image_vectors.json")


def load_image_vectors() -> list[dict]:
    with open(IMAGE_VECTORS_FILE, mode="r", encoding="utf-8") as f:
        return json.load(f)["images"]


def generate_image(vector: dict) -> str:
    """The image of a vector, as the image generator writes it with the default base address and fill"""
    with tempfile.TemporaryDirectory() as tmp_dir:
        settings_path = os.path.join(ROOT, vector["settings"])
        if "endianness" in vector:
            with open(settings_path, mode="r", encoding="utf-8") as f:
                settings_json = json.load(f) | {"endianness": vector["endianness"]}
            settings_path = os.path.join(tmp_dir, "settings.json")
            with open(settings_path, mode="w", encoding="utf-8") as f:
                json.dump(settings_json, f)

        result = subprocess.run([sys.executable, IMAGE_GEN, os.path.join(ROOT, vector["datamodel"]), settings_path, "-o", tmp_dir], capture_output=True, text=True)
        if result.returncode != 0:
            raise Exception(f"The image generator failed on {vector['image']}: {result.stdout}{result.stderr}")

        with open(os.path.join(tmp_dir, "eeprom.hex"), mode="r", encoding="ascii") as f:
            return f.read()


if __name__ == "__main__":
    for vector in load_image_vectors():
        with open(os.path.join(ROOT, vector["image"]), mode="w", encoding="ascii", newline="\n") as f:
            f.write(generate_image(vector))
        print(f"make_image_vectors::info:: {vector['image']}")
