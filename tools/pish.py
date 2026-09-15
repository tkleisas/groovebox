"""Tiny SSH/SFTP helper for the groovebox Pi (pi@groovebox.local).

Usage:
  pish.py "shell command"          run a command, stream output
  pish.py put <local> <remote>     upload file(s)
  pish.py get <remote> <local>     download file
Password comes from GB_PI_PASSWORD env or pi_credentials.txt (gitignored).
"""
import os, sys, time, paramiko

HOST = "groovebox.local"
USER = "pi"


def password():
    pw = os.environ.get("GB_PI_PASSWORD")
    if pw:
        return pw
    here = os.path.dirname(os.path.abspath(__file__))
    creds = os.path.join(here, "pi_credentials.txt")
    with open(creds, encoding="utf-8") as f:
        return f.read().strip()


def client():
    c = paramiko.SSHClient()
    c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(HOST, username=USER, password=password(), timeout=10,
              look_for_keys=False, allow_agent=False)
    return c


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    c = client()
    if sys.argv[1] == "put":
        local, remote = sys.argv[2], sys.argv[3]
        cmd = f"cat > {remote} && chmod +x {remote}"
        stdin, stdout, stderr = c.exec_command(cmd, timeout=900)
        sent = 0
        with open(local, "rb") as f:
            while chunk := f.read(32768):
                while not stdin.channel.send_ready():
                    time.sleep(0.05)
                stdin.write(chunk)
                sent += len(chunk)
        stdin.channel.shutdown_write()
        err = stderr.read().decode(errors="replace")
        rc = stdout.channel.recv_exit_status()
        if rc != 0:
            raise SystemExit(f"put failed ({rc}): {err}")
        print(f"uploaded {local} -> {remote} ({sent} bytes)")
    elif sys.argv[1] == "sudo":
        cmd = " ".join(sys.argv[2:])
        stdin, stdout, stderr = c.exec_command(
            f"sudo -S -p '' {cmd}", timeout=600)
        stdin.write(password() + "\n")
        stdin.channel.shutdown_write()
        out = stdout.read().decode(errors="replace")
        err = stderr.read().decode(errors="replace")
        rc = stdout.channel.recv_exit_status()
        if out:
            print(out, end="")
        if err:
            print(err, end="", file=sys.stderr)
        sys.exit(rc)
    elif sys.argv[1] == "get":
        remote, local = sys.argv[2], sys.argv[3]
        with c.open_sftp() as sftp:
            sftp.get(remote, local)
        print(f"downloaded {remote} -> {local}")
    else:
        cmd = " ".join(sys.argv[1:])
        stdin, stdout, stderr = c.exec_command(cmd, timeout=600)
        out = stdout.read().decode(errors="replace")
        err = stderr.read().decode(errors="replace")
        rc = stdout.channel.recv_exit_status()
        if out:
            print(out, end="")
        if err:
            print(err, end="", file=sys.stderr)
        sys.exit(rc)
    c.close()


main()
