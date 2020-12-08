import os, datetime, sys, urllib.parse
import http.server
from http.server import BaseHTTPRequestHandler, HTTPServer
import wave
import logging
import socket
import os.path
import time
import os

PORT = 8000
HOST = '0.0.0.0'
file_stream = ""
class Handler(BaseHTTPRequestHandler):
    def _set_response(self):
        self.send_response(200)
        self.send_header('Content-type', 'text/html')
        self.end_headers()

    def _set_headers(self, length):
        self.send_response(200)
        if length > 0:
            self.send_header('Content-length', str(length))
        self.end_headers()

    def _get_chunk_size(self):
        data = self.rfile.read(2)
        while data[-2:] != b"\r\n":
            data += self.rfile.read(1)
        return int(data[:-2], 16)

    def _get_chunk_data(self, chunk_size):
        data = self.rfile.read(chunk_size)
        self.rfile.read(2)
        return data

    def _write_wav(self, data, rates, bits, ch):
        t = datetime.datetime.utcnow()
        time = t.strftime('%Y%m%dT%H%M%SZ')
        filename = str.format('{}_{}_{}_{}.wav', time, rates, bits, ch)

        wavfile = wave.open(filename, 'wb')
        wavfile.setparams((ch, bits/8, rates, 0, 'NONE', 'NONE'))
        wavfile.writeframes(bytearray(data))
        wavfile.close()
        return filename

    # def stream_response(request):
    #     return StreamingHttpResponse(stream_response_generator())

    # def stream_response_generator():
    #     for x in range(1,11):
    #         yield "%s\n" % x  # Returns a chunk of the response to the browser
    #         time.sleep(1)

    def do_POST(self):
        urlparts = urllib.parse.urlparse(self.path)
        request_file_path = urlparts.path.strip('/')
        total_bytes = 0
        sample_rates = 0
        bits = 0
        channel = 0
        if (request_file_path == 'upload'
            and self.headers.get('Transfer-Encoding', '').lower() == 'chunked'):
            data = []
            sample_rates = self.headers.get('x-audio-sample-rates', '').lower()
            bits = self.headers.get('x-audio-bits', '').lower()
            channel = self.headers.get('x-audio-channel', '').lower()
            sample_rates = self.headers.get('x-audio-sample-rates', '').lower()

            print("Audio information, sample rates: {}, bits: {}, channel(s): {}".format(sample_rates, bits, channel))
            # https://stackoverflow.com/questions/24500752/how-can-i-read-exactly-one-response-chunk-with-pythons-http-client
            while True:
                chunk_size = self._get_chunk_size()
                total_bytes += chunk_size
                print("Total bytes received: {}".format(total_bytes))
                sys.stdout.write("\033[F")
                if (chunk_size == 0):
                    break
                else:
                    chunk_data = self._get_chunk_data(chunk_size)
                    data += chunk_data
 
            filename = self._write_wav(data, int(sample_rates), int(bits), int(channel))
            body = 'File {} was written, size {}'.format(filename, total_bytes)
            self._set_headers(len(body))
            self.wfile.write(body)
            self.wfile.close()
        else:
            return http.server.SimpleHTTPRequestHandler.do_GET(self)
            
    def do_GET(self):
        logging.info("GET request,\nPath: %s\nHeaders:\n%s\n", str(self.path), str(self.headers))
        if False:
            self._set_response()
            self.wfile.write("GET request for {}".format(self.path).encode('utf-8'))
        else:
            self.send_response(200)
            self.send_header('Transfer-Encoding', 'chunked')
            self.send_header('Content-type', 'application/octet-stream')
            self.end_headers()

            if False:       # Test only
                # Write trunk
                data = "123"
                tosend = '%X\r\n%s\r\n'%(len(data), data)
                self.wfile.write(bytes(tosend, "utf-8"))
                # print("Send end msg")
                self.wfile.write(bytes('0\r\n\r\n', "utf-8"))
                self.wfile.flush()
            else:
                file_size = os.path.getsize(file_stream)
                print("File size", file_size)
                file = open(file_stream, "rb")
                file.seek(0, 0)
                print("File opened")
                size = 0
                while True:
                    file_data = file.read(115200) # use an appropriate chunk size
                    if file_data is None or len(file_data) == 0:
                        break
                    else:
                        size += len(file_data)
                        print("Written size", size)
                        # yield file_data
                        tmp_chunk_size = "%X\r\n" % len(file_data)
                        # print(tmp_chunk_size)
                        self.wfile.write(bytes(tmp_chunk_size, "utf-8"))
                        self.wfile.write(file_data)
                        self.wfile.write(bytes('\r\n', "utf-8"))
                        # break
                        # # self.wfile.write("%s\r\n" % hex(len(file_data))[2:])
                        # self.wfile.flush()

                print("Close file")
                file.close()
                self.wfile.write(bytes('0\r\n\r\n', "utf-8"))
                self.wfile.flush()
                
                print("Test completed.")


def run(server_class=HTTPServer, handler_class=Handler, port=80):
    logging.basicConfig(level=logging.INFO)
    # hostname = socket.gethostname()
    local_ip = "192.168.1.7"
    server_address = (local_ip, port)
    httpd = server_class(server_address, handler_class)
    logging.info('Starting httpd on address %s, port %d\n', local_ip, port)
    try:
        httpd.serve_forever()
        time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    httpd.server_close()
    logging.info('Stopping httpd...\n')

if __name__ == '__main__':
    from sys import argv
    print("Len argv", len(argv))
    if len(argv) == 2:
        file_stream = argv[1]
        print("Stream file", file_stream)
        if os.path.isfile(file_stream) == 0:
            print("File not exit")
            file_stream = ""
        run()
    else:
        run()
