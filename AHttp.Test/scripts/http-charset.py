#!/usr/bin python

from socket import *
from select import *

HOST = "localhost"
PORT = 5555

s = socket (AF_INET, SOCK_STREAM)
s.connect ((HOST, PORT))
s.send ("GET / HTTP/1.1\r\nAccept-Charset: Windows-1251, iso-8859-5, unicode-1-1;q=0.8\r\n\r\n")


res = "Retrieved:\n"

while len(res) > 0:
    res = s.recv (1024*1024)
    print res
        
    rv = select ([s], [], [], 1)
    if len(rv[0]) == 0: break

s.close ()