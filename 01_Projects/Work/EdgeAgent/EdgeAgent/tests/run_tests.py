#!/usr/bin/env python3
"""Build the gateway and run unit/component and MQTT integration tests on Linux/WSL."""
from pathlib import Path
import os
import subprocess
import sys

project = Path(__file__).resolve().parent.parent
build = project / "build-proxy"


def compile_project(pro_file, directory):
    directory.mkdir(parents=True, exist_ok=True)
    log_path = directory / "build.log"
    with log_path.open("w") as log:
        for command in (["qmake", str(pro_file)], ["make", "-j", str(min(4, os.cpu_count() or 2))]):
            result = subprocess.run(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                log.flush()
                print(log_path.read_text(errors="replace")[-12000:], file=sys.stderr)
                raise SystemExit(result.returncode)
    print("BUILD PASS", pro_file.name, flush=True)


compile_project(project / "EdgeAgent.pro", build)
compile_project(project / "tests" / "TransportTests.pro", build / "tests")
subprocess.run([str(build / "tests" / "transport-tests"), str(project)], check=True)
subprocess.run([sys.executable, str(project / "tests" / "integration.py"), str(build / "edgeagent")], check=True)
subprocess.run([sys.executable, str(project / "tests" / "test_dynamic_features.py"), str(build / "edgeagent")], check=True)
print("ALL TESTS PASSED", flush=True)
