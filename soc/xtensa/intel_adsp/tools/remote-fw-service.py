#!/usr/bin/env python3
# Copyright(c) 2022 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
import os
import sys
import struct
import logging
import time
import subprocess
import argparse
import socketserver
import threading
import hashlib
import queue
from urllib.parse import urlparse

# Global variable used to sync between log and request services
runner = None

# INADDR_ANY as default
HOST = ''
PORT_LOG = 9999
PORT_REQ = PORT_LOG + 1
BUF_SIZE = 4096

# Define the command and the max size
CMD_LOG_START = "start_log"
CMD_DOWNLOAD = "download"
MAX_CMD_SZ = 16

# Define the return value in handle function
ERR_FAIL = 1

# Define the header format and size for
# transmitting the firmware
PACKET_HEADER_FORMAT_FW = 'I 42s 32s'
HEADER_SZ = 78

logging.basicConfig(level=logging.INFO,
    format='%(levelname)s: %(name)s: %(message)s')
log = logging.getLogger("remote-fw")


class adsp_request_handler(socketserver.BaseRequestHandler):
    """
    Request handler for server
    """

    def receive_fw(self):
        # Receive the header first
        d = self.request.recv(HEADER_SZ)

        # Unpack the header data
        # Include size(4), filename(42) and MD5(32)
        header = d[:HEADER_SZ]
        total = d[HEADER_SZ:]
        s = struct.Struct(PACKET_HEADER_FORMAT_FW)
        fsize, fname, md5_tx = s.unpack(header)
        md5_tx = md5_tx.decode('utf-8')
        fname = fname.decode('utf-8')

        log.info(f"Received firmware header: {{ size: {fsize} bytes, filename: {fname}, MD5: {md5_tx} }}")

        # Receive the firmware. We only receive the specified amount of bytes.
        while len(total) < fsize:
            data = self.request.recv(min(BUF_SIZE, fsize - len(total)))
            if not data:
                raise EOFError("Truncated firmware file")
            total += data

        log.debug(f"Received all {len(total)} bytes expected")

        try:
            with open(fname,'wb') as f:
                f.write(total)
        except Exception as e:
            log.error(f"Exception occurred {e} during firmware download!")
            return None

        # Check the MD5 of the firmware
        md5_rx = hashlib.md5(total).hexdigest()

        if md5_tx != md5_rx:
            log.error(f'MD5 mismatch: {md5_tx} VS {md5_rx}')
            return None

        return fname

    def do_download(self):
        recv_file = self.receive_fw()

        if recv_file and os.path.exists(recv_file):
            runner.set_fw_ready(recv_file)
            return 0

        log.error("Cannot find firmware file!")
        return ERR_FAIL

    def handle(self):
        cmd = self.request.recv(MAX_CMD_SZ)
        log.info(f"Client {self.client_address[0]} wrote: {cmd}")

        ret = ERR_FAIL

        action = cmd.decode('utf-8')
        if action == CMD_DOWNLOAD:
            self.request.sendall(cmd)
            ret = self.do_download()
        else:
            log.error("Incorrect communication from client to firmware loader!")
            return

        if not ret:
            self.request.sendall("success".encode('utf-8'))
            log.info("Received firmware")
        else:
            self.request.sendall("failed".encode('utf-8'))
            log.error("Firmware receive failed!")

class adsp_log_handler(socketserver.BaseRequestHandler):
    """
    Log handler class
    """

    def handle(self):
        cmd = self.request.recv(MAX_CMD_SZ)
        action = cmd.decode("utf-8")

        log.info(f"Client {self.client_address[0]} wrote: {cmd}")

        if action == CMD_LOG_START:
            self.request.sendall(cmd)
        else:
            log.error("Incorrect communication from client to logger!")

        log.debug("Waiting for loader to finish downloading firmware...")
        while not runner.is_fw_ready():
            if not self.is_connection_alive():
                return

            time.sleep(1)

        with subprocess.Popen(runner.get_script(), stdout=subprocess.PIPE) as proc:
            log.info(f"Attaching log monitor to firmware load process {proc.pid}")
            # Thread for monitoring the connection
            t = threading.Thread(target=self.check_connection, args=(proc,))
            t.start()

            while True:
                try:
                    out = proc.stdout.readline()
                    self.request.sendall(out)
                    ret = proc.poll()
                    if ret:
                        log.info(f"return code: {ret}")
                        break

                except (BrokenPipeError, ConnectionResetError):
                    log.debug("Log monitor disconnected")
                    break

            t.join()

        log.info("Firmware load and run with log capture complete")

    def finish(self):
        runner.cleanup()
        log.info("Waiting for next request...")

    def is_connection_alive(self):
        try:
            self.request.sendall(b'\x00')
        except (BrokenPipeError, ConnectionResetError):
            log.debug("Client disconnected")
            return False

        return True

    def check_connection(self, proc):
        # Not to check connection alive for
        # the first 10 secs.
        time.sleep(10)

        log.info("Checking connection with client...")
        while True:
            if not self.is_connection_alive():
                log.info(f"Client dropped connection, killing log monitor process {proc.pid}")

                try:
                    proc.kill()
                except PermissionError:
                    log.info("Cannot kill process started with sudo, killing as root")
                    os.system(f"sudo kill -9 {proc.pid} ")
                return

            time.sleep(1)

class device_runner():
    def __init__(self, args):
        self.fw_file = None
        self.lock = threading.Lock()
        self.fw_queue = queue.Queue()

        # Board specific config
        self.board = board_config(args)
        self.load_cmd = self.board.get_cmd()

    def set_fw_ready(self, fw_recv):
        if fw_recv:
            self.fw_queue.put(fw_recv)

    def is_fw_ready(self):
        self.fw_file = self.fw_queue.get()
        log.debug(f"Current firmware is {self.fw_file}")

        return bool(self.fw_file)

    def cleanup(self):
        self.lock.acquire()
        self.script = None
        if self.fw_file:
            os.remove(self.fw_file)
        self.fw_file = None
        self.lock.release()

    def get_script(self):
        if os.geteuid() != 0:
            self.script = ([f'sudo', f'{self.load_cmd}'])
        else:
            self.script = ([f'{self.load_cmd}'])

        self.script.append(f'{self.fw_file}')

        if self.board.params:
            for param in self.board.params:
                self.script.append(param)

        log.info(f'Now running firmware load script {self.script}')
        return self.script

class board_config():
    def __init__(self, args):

        self.load_cmd = args.load_cmd    # cmd for loading
        self.params = []            # params of loading cmd

        if not self.load_cmd:
            self.load_cmd = "./cavstool.py"

        if not os.path.exists(self.load_cmd):
            log.error(f'Could not set script to load firmware as path {self.load_cmd} is invalid.')
            sys.exit(1)

    def get_cmd(self):
        return self.load_cmd

    def get_params(self):
        return self.params


ap = argparse.ArgumentParser(description="Remote HW service tool")
ap.add_argument("-q", "--quiet", action="store_true",
                help="No loader output, just DSP logging")
ap.add_argument("-v", "--verbose", action="store_true",
                help="More loader output, DEBUG logging level")
ap.add_argument("-s", "--server-addr",
                help="Specify the only IP address the log server will LISTEN on")
ap.add_argument("-p", "--log-port",
                help="Specify the PORT that the log server will be active on")
ap.add_argument("-r", "--req-port",
                help="Specify the PORT that the request server will be active on")
ap.add_argument("-c", "--load-cmd",
                help="Specify command to load firmware onto the board")

args = ap.parse_args()

if args.quiet:
    log.setLevel(logging.WARN)
elif args.verbose:
    log.setLevel(logging.DEBUG)

if args.server_addr:
    url = urlparse("//" + args.server_addr)

    if url.hostname:
        HOST = url.hostname

    if url.port:
        PORT_LOG = int(url.port)

if args.log_port:
    PORT_LOG = int(args.log_port)

if args.req_port:
    PORT_REQ = int(args.req_port)

if __name__ == "__main__":

    # Do board configuration setup
    runner = device_runner(args)

    # Launch the command request service
    socketserver.TCPServer.allow_reuse_address = True
    req_server = socketserver.TCPServer((HOST, PORT_REQ), adsp_request_handler)
    req_t = threading.Thread(target=req_server.serve_forever, daemon=True)

    # Activate the log service that outputs the board's execution result
    log_server = socketserver.TCPServer((HOST, PORT_LOG), adsp_log_handler)
    log_t = threading.Thread(target=log_server.serve_forever, daemon=True)

    try:
        log.info(f"Firmware load server starting on port {PORT_REQ}...")
        req_t.start()
        log.info(f"Log monitor server starting on port {PORT_LOG}...")
        log_t.start()
        req_t.join()
        log_t.join()
    except KeyboardInterrupt:
        log_server.shutdown()
        req_server.shutdown()
