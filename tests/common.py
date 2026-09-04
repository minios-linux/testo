
from subprocess import Popen, PIPE
from sys import platform
import os, re, shlex

cwd = os.getcwd().replace('\\', '/')

if 'HYPERVISOR' in os.environ:
	HYPERVISOR = os.environ['HYPERVISOR']
else:
	if platform == "linux":
		HYPERVISOR = "qemu"
	elif platform == "win32":
		HYPERVISOR = "hyperv"
	else:
		raise "Please, specify hypervisor"

TESTO_BIN = os.environ.get("TESTO_BIN", "testo")
TESTO_ALLOWED_SHARING_DIRECTORY = os.environ.get("TESTO_ALLOWED_SHARING_DIRECTORY", cwd)

def prepare_cmd(cmd):
	bin_quoted = shlex.quote(TESTO_BIN)
	sharing_quoted = shlex.quote(TESTO_ALLOWED_SHARING_DIRECTORY)
	cmd = re.sub(r"\btesto run ([^\s|;&]+)", lambda m: f"{bin_quoted} run {m.group(1)} --user --allowed-sharing-directory {sharing_quoted}", cmd, count=1)
	cmd = re.sub(r"\btesto clean\b", f"{bin_quoted} clean --user", cmd, count=1)
	cmd = re.sub(r"\btesto --version\b", f"{bin_quoted} --version", cmd, count=1)
	return cmd

def must_succeed(cmd, out=None, err=None, input=None):
	cmd = prepare_cmd(cmd)
	p = Popen(cmd, stdout=PIPE, stderr=PIPE, stdin=PIPE, shell=True)
	if input is not None:
		input = input.encode('utf-8')
	stdout, stderr = p.communicate(input)
	stdout, stderr = stdout.decode('utf-8'), stderr.decode('utf-8')
	print(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ", cmd, " <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<")
	print("STDOUT:", stdout)
	print("STDERR:", stderr)
	assert p.returncode == 0, "returncode == 0"
	if out is not None:
		assert out in stdout, f"STDOUT: {out}"
	if err is not None:
		assert err in stdout, f"STDERR: {err}"
	return stdout, stderr

def must_fail(cmd, err=None, out=None, input=None):
	cmd = prepare_cmd(cmd)
	p = Popen(cmd, stdout=PIPE, stderr=PIPE, stdin=PIPE, shell=True)
	if input is not None:
		input = input.encode('utf-8')
	stdout, stderr = p.communicate(input)
	stdout, stderr = stdout.decode('utf-8'), stderr.decode('utf-8')
	print(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>> ", cmd, " <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<")
	print("STDOUT:", stdout)
	print("STDERR:", stderr)
	assert p.returncode != 0, "returncode != 0"
	if out is not None:
		assert out in stdout, f"STDOUT: {out}"
	if err is not None:
		if isinstance(err, str):
			assert err in stderr, f"STDERR: {err}"
		else:
			assert p.returncode == err, f"returncode == {err}"
	return stdout, stderr
