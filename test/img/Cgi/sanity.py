#!/usr/bin/env python3
import os
print("Content-Type: text/plain\r")
print("\r")
print("cgi-ok method=" + os.environ.get("REQUEST_METHOD","?"))
