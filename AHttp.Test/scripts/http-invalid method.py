#!/usr/bin python

from socket import *
import threading, thread, time

HOST = "localhost"
PORT = 5555

s = socket (AF_INET, SOCK_STREAM)
s.connect ((HOST, PORT))
s.send ("RETRIEVE /test HTTP/1.1\r\n\r\n")

res = s.recv( 1024) 
print "Retrieved: %s" % (res,)
s.close()