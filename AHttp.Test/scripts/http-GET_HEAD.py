#!/usr/bin python

from socket import *
import select

HOST = "localhost"
PORT = 5555

get_content = ""
head_content = ""
part = ""

s = socket (AF_INET, SOCK_STREAM)
s.connect ((HOST, PORT))
s.send ("HEAD /server_data/python/get.py HTTP/1.1\r\nConnection: keep-alive\r\n\r\n")

content = s.recv( 16 * 1024)
print "HEAD\n%s\n--------------------------------------\n" % (content, )

        
s.send ("GET /server_data/python/get.py HTTP/1.1\r\n\r\n")

print ("GET\n")

while (len(content) > 0):
    content = s.recv( 16 * 1024)
    print (content)
    rv = select.select ([s], [], [], 3)
    if len(rv[0]) == 0: break
    

s.close()
