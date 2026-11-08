#!/bin/bash
cd "$(dirname "$0")" || exit 1
python3 PTX_to_AAF_GUI.py
if [ $? -ne 0 ]; then
  echo ""
  echo "The GUI could not start. Make sure Python 3 with Tkinter is installed."
  echo "Press Enter to close this window."
  read -r
fi
