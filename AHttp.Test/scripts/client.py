#!/usr/bin/python

from socket import *
import threading, thread, time

HOST = "localhost"
PORT = 5555
END_MARK = "\r\n\r\n"


errLock = thread.allocate_lock()  
err_count = 0
threads = []

def increaseErrorsCount ():
    if errLock.acquire(1) == 1:
        increaseErrorsCount.func_globals["err_count"] += 1
        errLock.release()

def interact ():
    try:
        s = socket (AF_INET, SOCK_STREAM)
        s.connect ((HOST, PORT))
        s.send ("GET /dhtml/atree/drag_and_drop.html HTTP/1.1\r\n" \
                + "User-Agent: Mozilla" \
                + "Host: localhost:5555" + END_MARK)
        
        res = s.recv( 1024) 
        #print "Retrieved: %s" % (res,)
        s.close()
    except Exception, ex:
        increaseErrorsCount()


def portion ():   
    portion.func_globals["err_count"] = 0
    
    for i in range(1, 300):
        th = threading.Thread (None, interact)
        time.sleep (0.1)
        threads.append (th)
        th.start ()
        # print "started: %d" % i
        
    print "started all"

    for th in threads:
        th.join (1)

    print "joined all"
    print "err_count: " + str(err_count)
    
ndx = 0
while (ndx < 10):
    portion ()
    ndx += 1

    
