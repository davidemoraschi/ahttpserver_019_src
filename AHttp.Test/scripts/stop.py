#!/usr/bin python

from socket import *
import threading, thread, time

HOST = "localhost"
PORT = 5555
END_MARK = "\r\n\r\n"

s = socket (AF_INET, SOCK_STREAM)
s.connect ((HOST, PORT))
s.send ("STOP" + END_MARK)

res = s.recv( 1024) 
print "Retrieved: %s" % (res,)
s.close()