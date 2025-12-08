#!/bin/bash

if [ ! -d ".venv_meem" ]; then
    echo "Virtual environment not found, creating one..."
    python3 -m venv .venv_meem
    source .venv_meem/bin/activate
    python3 -m pip install --upgrade pip
    python3 -m pip install -r ./requirements.txt
fi 

echo "Virtual environment found, activated."
source .venv_meem/bin/activate
